#include <stdbool.h>
#include <string.h>

#include <rtthread.h>

#include "ui.h"
#include "ui_components.h"
#include "ui_helpers.h"
#include "ui_i18n.h"
#include "ui_runtime_adapter.h"
#include "network/net_manager.h"
#include "petgame.h"
#include "xiaozhi/weather/weather.h"
#include "xiaozhi/xiaozhi_client_public.h"
#include "xiaozhi/xiaozhi_service.h"

lv_obj_t *ui_Home = NULL;

static xiaozhi_home_screen_refs_t s_home_refs;
static lv_timer_t *s_home_status_timer = NULL;
static lv_obj_t *s_home_time_label = NULL;
static lv_obj_t *s_home_date_label = NULL;

static lv_obj_t *s_home_ai_face_img = NULL;
static lv_obj_t *s_home_ai_mouth_label = NULL;
static lv_obj_t *s_home_ai_copy_label = NULL;
static lv_obj_t *s_home_ai_button_label = NULL;
static lv_obj_t *s_home_ai_network_label = NULL;
static lv_obj_t *s_home_ai_talk_card = NULL;
static lv_timer_t *s_home_deferred_start_timer = NULL;
static lv_timer_t *s_home_ai_sync_timer = NULL;
static lv_timer_t *s_home_ai_boot_timer = NULL;
static lv_timer_t *s_home_ai_greeting_timer = NULL;
static rt_mutex_t s_home_ai_pending_mutex = RT_NULL;
static bool s_home_ai_stop_pending = false;
static bool s_home_ai_press_active = false;
static rt_tick_t s_home_ai_last_tts_tick = 0;
static net_manager_service_state_t s_home_ai_last_network_state = (net_manager_service_state_t)-1;
static xz_service_state_t s_home_ai_last_service_state = (xz_service_state_t)-1;
static xz_service_state_t s_home_ai_prev_service_state = (xz_service_state_t)-1;
static char s_home_ai_mouth_cache[48] = {0};
static char s_home_ai_copy_cache[200] = {0};
static char s_home_ai_button_cache[32] = {0};
static char s_home_ai_network_cache[96] = {0};
static const lv_image_dsc_t *s_home_ai_face_cache = NULL;

#define HOME_STATUS_REFRESH_MS      1000
#define HOME_DEFERRED_START_MS      80
#define HOME_AI_UI_SYNC_INTERVAL_MS 200
#define HOME_AI_SERVICE_BOOT_MS     500
#define HOME_AI_GREETING_DELAY_MS   1200
#define HOME_AI_TTS_UPDATE_MS       1200

static void home_ai_runtime_start(void);

extern const lv_image_dsc_t aihome_face_calm;
extern const lv_image_dsc_t aihome_face_happy;
extern const lv_image_dsc_t aihome_face_tired;
extern const lv_image_dsc_t aihome_bubble;
extern const lv_image_dsc_t aihome_talk_button;

typedef struct
{
    bool state_valid;
    xz_service_state_t state;
    bool copy_valid;
    char copy_text[200];
    bool emoji_valid;
    char emoji[32];
    bool network_dirty;
} home_ai_pending_state_t;

static home_ai_pending_state_t s_home_ai_pending = {0};

static void home_ai_update_network_status(void);
static void home_ai_update_button_text(xz_service_state_t state);
static void home_ai_sync_timer_cb(lv_timer_t *timer);
static void home_ai_service_boot_timer_cb(lv_timer_t *timer);
static void home_ai_greeting_timer_cb(lv_timer_t *timer);
static void home_deferred_start_timer_cb(lv_timer_t *timer);
static void home_ai_restore_runtime_state(void);
static void home_ai_on_service_state_change(xz_service_state_t state);
static void home_ai_on_chat_output(const char *text);
static void home_ai_on_tts_output(const char *text);
static void home_ai_on_emoji_change(const char *emoji);
static void home_ai_on_error(const char *error_msg);

static xz_service_ui_callbacks_t s_home_ai_cbs = {
    .on_state_change = home_ai_on_service_state_change,
    .on_chat_output = home_ai_on_chat_output,
    .on_tts_output = home_ai_on_tts_output,
    .on_emoji_change = home_ai_on_emoji_change,
    .on_error = home_ai_on_error,
};

static const char *home_weekday_from_index(int weekday)
{
    switch (weekday)
    {
    case 1:
        return ui_i18n_pick("周一", "Mon");
    case 2:
        return ui_i18n_pick("周二", "Tue");
    case 3:
        return ui_i18n_pick("周三", "Wed");
    case 4:
        return ui_i18n_pick("周四", "Thu");
    case 5:
        return ui_i18n_pick("周五", "Fri");
    case 6:
        return ui_i18n_pick("周六", "Sat");
    case 7:
    case 0:
    default:
        return ui_i18n_pick("周日", "Sun");
    }
}

static void home_status_refresh(void)
{
    date_time_t current_time;
    bool fallback = false;
    char time_text[16];
    char date_text[32];

    if (s_home_time_label == NULL || s_home_date_label == NULL)
    {
        return;
    }

    memset(&current_time, 0, sizeof(current_time));
    if (xiaozhi_time_get_current(&current_time) != RT_EOK)
    {
        fallback = true;
    }

    if (!fallback)
    {
        if (current_time.month < 1 || current_time.month > 12 ||
            current_time.day < 1 || current_time.day > 31 ||
            current_time.hour < 0 || current_time.hour > 23 ||
            current_time.minute < 0 || current_time.minute > 59)
        {
            fallback = true;
        }
    }

    if (fallback)
    {
        current_time.month = 4;
        current_time.day = 28;
        current_time.hour = 21;
        current_time.minute = 32;
        current_time.weekday = 2;
    }

    rt_snprintf(time_text, sizeof(time_text), "%02d:%02d", current_time.hour, current_time.minute);
    rt_snprintf(date_text,
                sizeof(date_text),
                "%02d/%02d  %s",
                current_time.month,
                current_time.day,
                home_weekday_from_index(current_time.weekday));
    lv_label_set_text(s_home_time_label, time_text);
    lv_label_set_text(s_home_date_label, date_text);
}

static void home_status_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    home_status_refresh();
}

static lv_obj_t *home_plain_obj(lv_obj_t *parent,
                                int x,
                                int y,
                                int w,
                                int h,
                                int radius,
                                lv_opa_t opa,
                                uint32_t bg,
                                int border_w)
{
    lv_obj_t *obj = lv_obj_create(parent);

    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, ui_px_x(x), ui_px_y(y));
    lv_obj_set_size(obj, ui_px_w(w), ui_px_h(h));
    lv_obj_set_style_radius(obj, ui_px_x(radius), 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(obj, opa, 0);
    lv_obj_set_style_border_color(obj, lv_color_black(), 0);
    lv_obj_set_style_border_width(obj, border_w, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    return obj;
}

static const char *home_ai_text_idle(void)
{
    return ui_i18n_pick("按住说话", "Hold to talk");
}

static const char *home_ai_text_prompt(void)
{
    return ui_i18n_pick("我在，有什么想问小豆的吗？", "I am here. What would you like to ask?");
}

static const char *home_ai_network_state_text(net_manager_service_state_t state)
{
    switch (state)
    {
    case NET_MANAGER_SERVICE_INTERNET_READY:
        return ui_i18n_pick("网络已连接", "Network connected");
    case NET_MANAGER_SERVICE_DNS_READY:
        return ui_i18n_pick("DNS已就绪", "DNS ready");
    case NET_MANAGER_SERVICE_LINK_READY:
        return ui_i18n_pick("链路已连接", "Link connected");
    case NET_MANAGER_SERVICE_RADIO_READY:
        return ui_i18n_pick("网络准备中", "Network readying");
    case NET_MANAGER_SERVICE_OFFLINE:
    default:
        return ui_i18n_pick("网络未连接", "Network offline");
    }
}

static const char *home_ai_network_status_text(net_manager_service_state_t net_state,
                                               xz_service_state_t svc_state)
{
    LV_UNUSED(svc_state);
    return home_ai_network_state_text(net_state);
}

static void home_ai_format_bubble_text(char *dst, size_t dst_size, const char *src)
{
    const unsigned char *p = (const unsigned char *)(src != NULL ? src : "");
    size_t out = 0U;
    int line_units = 0;
    int lines = 1;

    if (dst == NULL || dst_size == 0U)
    {
        return;
    }

    while (*p != '\0' && out + 5U < dst_size && lines <= 3)
    {
        size_t char_len = 1U;
        int units = 1;

        if ((*p & 0x80U) != 0U)
        {
            if ((*p & 0xE0U) == 0xC0U)
            {
                char_len = 2U;
            }
            else if ((*p & 0xF0U) == 0xE0U)
            {
                char_len = 3U;
            }
            else if ((*p & 0xF8U) == 0xF0U)
            {
                char_len = 4U;
            }
            units = 2;
        }

        if (*p == '\n')
        {
            if (lines >= 3)
            {
                break;
            }
            dst[out++] = '\n';
            p++;
            line_units = 0;
            lines++;
            continue;
        }

        if (line_units > 0 && line_units + units > 10)
        {
            if (lines >= 3)
            {
                break;
            }
            dst[out++] = '\n';
            line_units = 0;
            lines++;
        }

        if (lines > 3 || out + char_len >= dst_size)
        {
            break;
        }

        for (size_t i = 0U; i < char_len && p[i] != '\0'; ++i)
        {
            dst[out++] = (char)p[i];
        }
        p += char_len;
        line_units += units;
    }

    dst[out] = '\0';
}

static void home_ai_set_label_if_changed(lv_obj_t *label,
                                         char *cache,
                                         size_t cache_size,
                                         const char *text)
{
    const char *final_text = text != NULL ? text : "";
    char bubble_text[200];

    if (label == NULL || cache == NULL || cache_size == 0U)
    {
        return;
    }

    if (label == s_home_ai_copy_label)
    {
        home_ai_format_bubble_text(bubble_text, sizeof(bubble_text), final_text);
        final_text = bubble_text;
    }

    if (strncmp(cache, final_text, cache_size) == 0)
    {
        return;
    }

    rt_snprintf(cache, cache_size, "%s", final_text);
    lv_label_set_text(label, final_text);
}

static void home_ai_set_face_if_changed(const lv_image_dsc_t *img)
{
    if (s_home_ai_face_img == NULL || img == NULL || s_home_ai_face_cache == img)
    {
        return;
    }

    s_home_ai_face_cache = img;
    ui_img_set_src(s_home_ai_face_img, img);
}

static void home_ai_pending_lock(void)
{
    if (s_home_ai_pending_mutex != RT_NULL)
    {
        rt_mutex_take(s_home_ai_pending_mutex, RT_WAITING_FOREVER);
    }
}

static void home_ai_pending_unlock(void)
{
    if (s_home_ai_pending_mutex != RT_NULL)
    {
        rt_mutex_release(s_home_ai_pending_mutex);
    }
}

static void home_ai_update_button_text(xz_service_state_t state)
{
    const char *text = home_ai_text_idle();

    if (s_home_ai_button_label == NULL)
    {
        return;
    }

    switch (state)
    {
    case XZ_SERVICE_LISTENING:
        text = ui_i18n_pick("松手发送", "Release to send");
        break;
    case XZ_SERVICE_SPEAKING:
        text = ui_i18n_pick("正在回答", "Responding");
        break;
    case XZ_SERVICE_INITING:
        text = ui_i18n_pick("连接中...", "Connecting...");
        break;
    case XZ_SERVICE_CLOSING:
        text = ui_i18n_pick("关闭中...", "Closing...");
        break;
    default:
        text = home_ai_text_idle();
        break;
    }

    home_ai_set_label_if_changed(s_home_ai_button_label,
                                 s_home_ai_button_cache,
                                 sizeof(s_home_ai_button_cache),
                                 text);
}

static void home_ai_sync_timer_cb(lv_timer_t *timer)
{
    home_ai_pending_state_t pending;

    LV_UNUSED(timer);

    if (ui_Home == NULL || s_home_ai_mouth_label == NULL || s_home_ai_copy_label == NULL)
    {
        return;
    }

    home_ai_pending_lock();
    pending = s_home_ai_pending;
    memset(&s_home_ai_pending, 0, sizeof(s_home_ai_pending));
    home_ai_pending_unlock();

    if (pending.state_valid)
    {
        if (pending.state != XZ_SERVICE_LISTENING)
        {
            s_home_ai_stop_pending = false;
            s_home_ai_press_active = false;
        }

        home_ai_update_button_text(pending.state);
        switch (pending.state)
        {
        case XZ_SERVICE_READY:
            home_ai_set_label_if_changed(s_home_ai_mouth_label,
                                         s_home_ai_mouth_cache,
                                         sizeof(s_home_ai_mouth_cache),
                                         home_ai_text_idle());
            if (!pending.copy_valid)
            {
                home_ai_set_label_if_changed(s_home_ai_copy_label,
                                             s_home_ai_copy_cache,
                                             sizeof(s_home_ai_copy_cache),
                                             home_ai_text_prompt());
            }
            home_ai_set_face_if_changed(&aihome_face_calm);
            break;
        case XZ_SERVICE_LISTENING:
            home_ai_set_label_if_changed(s_home_ai_mouth_label,
                                         s_home_ai_mouth_cache,
                                         sizeof(s_home_ai_mouth_cache),
                                         ui_i18n_pick("正在聆听", "Listening"));
            if (!pending.copy_valid)
            {
                home_ai_set_label_if_changed(s_home_ai_copy_label,
                                             s_home_ai_copy_cache,
                                             sizeof(s_home_ai_copy_cache),
                                             ui_i18n_pick("松手发送", "Release to send"));
            }
            home_ai_set_face_if_changed(&aihome_face_happy);
            break;
        case XZ_SERVICE_SPEAKING:
            home_ai_set_label_if_changed(s_home_ai_mouth_label,
                                         s_home_ai_mouth_cache,
                                         sizeof(s_home_ai_mouth_cache),
                                         ui_i18n_pick("正在回答", "Responding"));
            home_ai_set_face_if_changed(&aihome_face_happy);
            break;
        case XZ_SERVICE_CLOSING:
            home_ai_set_label_if_changed(s_home_ai_mouth_label,
                                         s_home_ai_mouth_cache,
                                         sizeof(s_home_ai_mouth_cache),
                                         ui_i18n_pick("服务关闭中", "Closing"));
            home_ai_set_face_if_changed(&aihome_face_tired);
            break;
        default:
            break;
        }
    }

    if (pending.copy_valid)
    {
        home_ai_set_label_if_changed(s_home_ai_copy_label,
                                     s_home_ai_copy_cache,
                                     sizeof(s_home_ai_copy_cache),
                                     pending.copy_text);
    }

    home_ai_update_network_status();
}

static void home_ai_service_boot_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    if (s_home_ai_boot_timer != NULL)
    {
        lv_timer_delete(s_home_ai_boot_timer);
        s_home_ai_boot_timer = NULL;
    }

    if (ui_Home == NULL || s_home_ai_mouth_label == NULL || s_home_ai_copy_label == NULL)
    {
        return;
    }

    xiaozhi_service_register_ui_callbacks(&s_home_ai_cbs);
    if (xiaozhi_service_init() != 0)
    {
        home_ai_set_label_if_changed(s_home_ai_copy_label,
                                     s_home_ai_copy_cache,
                                     sizeof(s_home_ai_copy_cache),
                                     ui_i18n_pick("服务启动失败", "Service failed"));
        home_ai_set_label_if_changed(s_home_ai_mouth_label,
                                     s_home_ai_mouth_cache,
                                     sizeof(s_home_ai_mouth_cache),
                                     ui_i18n_pick("启动失败", "Start Failed"));
        return;
    }

    home_ai_set_label_if_changed(s_home_ai_mouth_label,
                                 s_home_ai_mouth_cache,
                                 sizeof(s_home_ai_mouth_cache),
                                 ui_i18n_pick("连接中...", "Connecting..."));
    home_ai_set_label_if_changed(s_home_ai_copy_label,
                                 s_home_ai_copy_cache,
                                 sizeof(s_home_ai_copy_cache),
                                 ui_i18n_pick("正在连接小豆", "Connecting AI Dou"));

    if (s_home_ai_greeting_timer == NULL)
    {
        s_home_ai_greeting_timer = lv_timer_create(home_ai_greeting_timer_cb,
                                                  HOME_AI_GREETING_DELAY_MS,
                                                  NULL);
    }
}

static void home_ai_greeting_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    if (s_home_ai_greeting_timer != NULL)
    {
        lv_timer_delete(s_home_ai_greeting_timer);
        s_home_ai_greeting_timer = NULL;
    }

    if (ui_Home == NULL)
    {
        return;
    }

    xiaozhi_service_request_greeting();
}

static void home_deferred_start_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    if (s_home_deferred_start_timer != NULL)
    {
        lv_timer_delete(s_home_deferred_start_timer);
        s_home_deferred_start_timer = NULL;
    }

    if (ui_Home == NULL)
    {
        return;
    }

    xiaozhi_weather_request_force_refresh();
    home_ai_runtime_start();
}

static void home_ai_restore_runtime_state(void)
{
    xz_service_state_t state = xiaozhi_service_get_state();

    home_ai_on_service_state_change(state);
    if (state == XZ_SERVICE_READY)
    {
        if (xiaozhi_service_get_session_id() != NULL)
        {
            home_ai_on_chat_output(home_ai_text_prompt());
        }
        else
        {
            home_ai_on_chat_output(ui_i18n_pick("正在恢复连接", "Restoring connection"));
        }
    }
}

static void home_ai_update_network_status(void)
{
    net_manager_service_state_t net_state;
    xz_service_state_t service_state;
    const char *text;

    if (s_home_ai_network_label == NULL)
    {
        return;
    }

    net_state = net_manager_get_service_state();
    service_state = xiaozhi_service_get_state();
    text = home_ai_network_status_text(net_state, service_state);

    if (s_home_ai_last_network_state == net_state &&
        s_home_ai_last_service_state == service_state &&
        strncmp(s_home_ai_network_cache, text, sizeof(s_home_ai_network_cache)) == 0)
    {
        return;
    }

    s_home_ai_last_network_state = net_state;
    s_home_ai_last_service_state = service_state;
    home_ai_set_label_if_changed(s_home_ai_network_label,
                                 s_home_ai_network_cache,
                                 sizeof(s_home_ai_network_cache),
                                 text);
}

static void home_ai_on_service_state_change(xz_service_state_t state)
{
    if (state == XZ_SERVICE_LISTENING && s_home_ai_prev_service_state != XZ_SERVICE_LISTENING)
    {
        petgame_record_ai_interaction();
    }
    s_home_ai_prev_service_state = state;

    home_ai_pending_lock();
    s_home_ai_pending.state = state;
    s_home_ai_pending.state_valid = true;
    s_home_ai_pending.network_dirty = true;
    home_ai_pending_unlock();
}

static void home_ai_on_chat_output(const char *text)
{
    if (text != NULL)
    {
        home_ai_pending_lock();
        rt_snprintf(s_home_ai_pending.copy_text, sizeof(s_home_ai_pending.copy_text), "%s", text);
        s_home_ai_pending.copy_valid = true;
        home_ai_pending_unlock();
    }
}

static void home_ai_on_tts_output(const char *text)
{
    if (text != NULL)
    {
        rt_tick_t now = rt_tick_get();

        if (s_home_ai_last_tts_tick != 0 &&
            (now - s_home_ai_last_tts_tick) < rt_tick_from_millisecond(HOME_AI_TTS_UPDATE_MS))
        {
            return;
        }

        s_home_ai_last_tts_tick = now;
        home_ai_pending_lock();
        rt_snprintf(s_home_ai_pending.copy_text, sizeof(s_home_ai_pending.copy_text), "%s", text);
        s_home_ai_pending.copy_valid = true;
        home_ai_pending_unlock();
    }
}

static void home_ai_on_emoji_change(const char *emoji)
{
    if (emoji != NULL)
    {
        home_ai_pending_lock();
        rt_snprintf(s_home_ai_pending.emoji, sizeof(s_home_ai_pending.emoji), "%s", emoji);
        s_home_ai_pending.emoji_valid = true;
        home_ai_pending_unlock();
    }
}

static void home_ai_on_error(const char *error_msg)
{
    if (error_msg != NULL)
    {
        home_ai_pending_lock();
        rt_snprintf(s_home_ai_pending.copy_text, sizeof(s_home_ai_pending.copy_text), "%s", error_msg);
        s_home_ai_pending.copy_valid = true;
        home_ai_pending_unlock();
        s_home_ai_stop_pending = false;
        s_home_ai_press_active = false;
    }
}

static void home_ai_start_listening(void)
{
    xz_service_state_t state;

    if (s_home_ai_face_img == NULL || s_home_ai_mouth_label == NULL || s_home_ai_copy_label == NULL)
    {
        return;
    }

    if (!net_manager_can_run_ai())
    {
        const char *network_text = home_ai_network_status_text(net_manager_get_service_state(),
                                                               xiaozhi_service_get_state());
        home_ai_set_label_if_changed(s_home_ai_copy_label,
                                     s_home_ai_copy_cache,
                                     sizeof(s_home_ai_copy_cache),
                                     network_text);
        home_ai_update_network_status();
        return;
    }

    state = xiaozhi_service_get_state();
    if (state == XZ_SERVICE_INITING || state == XZ_SERVICE_CLOSING)
    {
        home_ai_set_label_if_changed(s_home_ai_copy_label,
                                     s_home_ai_copy_cache,
                                     sizeof(s_home_ai_copy_cache),
                                     ui_i18n_pick("小豆正在准备", "AI Dou is preparing"));
        return;
    }

    if (state == XZ_SERVICE_LISTENING)
    {
        return;
    }

    s_home_ai_stop_pending = false;
    if (xiaozhi_service_start_listening() != 0)
    {
        home_ai_set_label_if_changed(s_home_ai_copy_label,
                                     s_home_ai_copy_cache,
                                     sizeof(s_home_ai_copy_cache),
                                     ui_i18n_pick("开始录音失败，正在重连", "Failed to record. Reconnecting"));
        home_ai_update_network_status();
        return;
    }

    home_ai_update_button_text(XZ_SERVICE_LISTENING);
    home_ai_set_label_if_changed(s_home_ai_mouth_label,
                                 s_home_ai_mouth_cache,
                                 sizeof(s_home_ai_mouth_cache),
                                 ui_i18n_pick("正在聆听", "Listening"));
    home_ai_set_label_if_changed(s_home_ai_copy_label,
                                 s_home_ai_copy_cache,
                                 sizeof(s_home_ai_copy_cache),
                                 ui_i18n_pick("松手发送", "Release to send"));
    home_ai_set_face_if_changed(&aihome_face_happy);
}

static void home_ai_stop_listening(void)
{
    xz_service_state_t state;

    if (s_home_ai_face_img == NULL || s_home_ai_mouth_label == NULL || s_home_ai_copy_label == NULL)
    {
        return;
    }

    state = xiaozhi_service_get_state();
    if (state != XZ_SERVICE_LISTENING || s_home_ai_stop_pending)
    {
        return;
    }

    s_home_ai_stop_pending = true;
    xiaozhi_service_stop_listening();
    home_ai_update_button_text(XZ_SERVICE_READY);
    home_ai_set_label_if_changed(s_home_ai_mouth_label,
                                 s_home_ai_mouth_cache,
                                 sizeof(s_home_ai_mouth_cache),
                                 ui_i18n_pick("正在发送", "Sending"));
    home_ai_set_label_if_changed(s_home_ai_copy_label,
                                 s_home_ai_copy_cache,
                                 sizeof(s_home_ai_copy_cache),
                                 ui_i18n_pick("录音结束，正在发送", "Sending recording"));
    home_ai_set_face_if_changed(&aihome_face_calm);
}

static void home_ai_talk_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_PRESSED)
    {
        if (!s_home_ai_press_active)
        {
            s_home_ai_press_active = true;
            home_ai_start_listening();
        }
        return;
    }

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
    {
        if (s_home_ai_press_active)
        {
            s_home_ai_press_active = false;
            home_ai_stop_listening();
        }
    }
}

static void home_create_ai_area(lv_obj_t *parent)
{
    lv_obj_t *speech;
    lv_obj_t *button_img;

    s_home_ai_face_img = ui_create_image_slot(parent, 150, 73, 176, 176);
    home_ai_set_face_if_changed(&aihome_face_calm);

    speech = ui_create_image_slot(parent, 318, 106, 160, 124);
    ui_img_set_src(speech, &aihome_bubble);
    s_home_ai_copy_label = ui_create_label(parent,
                                           home_ai_text_prompt(),
                                           342,
                                           124,
                                           116,
                                           86,
                                           21,
                                           LV_TEXT_ALIGN_LEFT,
                                           false,
                                           true);
    lv_obj_set_style_text_line_space(s_home_ai_copy_label, 3, 0);
    lv_label_set_long_mode(s_home_ai_copy_label, LV_LABEL_LONG_WRAP);

    s_home_time_label = ui_create_label(parent,
                                        "21:32",
                                        0,
                                        286,
                                        528,
                                        88,
                                        88,
                                        LV_TEXT_ALIGN_CENTER,
                                        false,
                                        false);
    lv_obj_set_style_text_letter_space(s_home_time_label, 1, 0);

    s_home_date_label = ui_create_label(parent,
                                        "04/28  周二",
                                        0,
                                        389,
                                        528,
                                        38,
                                        28,
                                        LV_TEXT_ALIGN_CENTER,
                                        false,
                                        false);

    button_img = ui_create_image_slot(parent, 104, 532, 320, 94);
    ui_img_set_src(button_img, &aihome_talk_button);

    lv_obj_t *button_border = home_plain_obj(parent, 104, 532, 320, 94, 16, LV_OPA_TRANSP, 0xffffff, 2);
    if (button_border)
    {
        lv_obj_set_style_border_color(button_border, lv_color_hex(0x3f4658), LV_PART_MAIN);
        lv_obj_clear_flag(button_border, LV_OBJ_FLAG_CLICKABLE);
    }

    s_home_ai_talk_card = home_plain_obj(parent, 104, 532, 320, 94, 16, LV_OPA_TRANSP, 0xffffff, 0);
    lv_obj_add_flag(s_home_ai_talk_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_home_ai_talk_card, home_ai_talk_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_home_ai_talk_card, home_ai_talk_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_home_ai_talk_card, home_ai_talk_event_cb, LV_EVENT_PRESS_LOST, NULL);

    s_home_ai_mouth_label = ui_create_hidden_label(parent);
    s_home_ai_button_label = s_home_ai_mouth_label;
    s_home_ai_network_label = ui_create_hidden_label(parent);
}

static void home_ai_runtime_start(void)
{
    if (s_home_ai_pending_mutex == RT_NULL)
    {
        s_home_ai_pending_mutex = rt_mutex_create("hai_ui", RT_IPC_FLAG_FIFO);
    }

    home_ai_pending_lock();
    memset(&s_home_ai_pending, 0, sizeof(s_home_ai_pending));
    home_ai_pending_unlock();
    s_home_ai_stop_pending = false;
    s_home_ai_press_active = false;
    s_home_ai_last_tts_tick = 0;
    s_home_ai_last_network_state = (net_manager_service_state_t)-1;
    s_home_ai_last_service_state = (xz_service_state_t)-1;
    s_home_ai_prev_service_state = (xz_service_state_t)-1;
    memset(s_home_ai_mouth_cache, 0, sizeof(s_home_ai_mouth_cache));
    memset(s_home_ai_copy_cache, 0, sizeof(s_home_ai_copy_cache));
    memset(s_home_ai_button_cache, 0, sizeof(s_home_ai_button_cache));
    memset(s_home_ai_network_cache, 0, sizeof(s_home_ai_network_cache));
    s_home_ai_face_cache = NULL;

    if (s_home_ai_sync_timer == NULL)
    {
        s_home_ai_sync_timer = lv_timer_create(home_ai_sync_timer_cb,
                                              HOME_AI_UI_SYNC_INTERVAL_MS,
                                              NULL);
    }

    home_ai_set_face_if_changed(&aihome_face_calm);
    home_ai_set_label_if_changed(s_home_ai_mouth_label,
                                 s_home_ai_mouth_cache,
                                 sizeof(s_home_ai_mouth_cache),
                                 home_ai_text_idle());
    home_ai_set_label_if_changed(s_home_ai_copy_label,
                                 s_home_ai_copy_cache,
                                 sizeof(s_home_ai_copy_cache),
                                 home_ai_text_prompt());

    xiaozhi_service_register_ui_callbacks(&s_home_ai_cbs);
    if (xiaozhi_service_is_running())
    {
        home_ai_restore_runtime_state();
    }
    else if (s_home_ai_boot_timer == NULL)
    {
        s_home_ai_boot_timer = lv_timer_create(home_ai_service_boot_timer_cb,
                                              HOME_AI_SERVICE_BOOT_MS,
                                              NULL);
    }
}

static void home_ai_runtime_stop(void)
{
    if (s_home_ai_boot_timer != NULL)
    {
        lv_timer_delete(s_home_ai_boot_timer);
        s_home_ai_boot_timer = NULL;
    }
    if (s_home_ai_greeting_timer != NULL)
    {
        lv_timer_delete(s_home_ai_greeting_timer);
        s_home_ai_greeting_timer = NULL;
    }
    if (s_home_ai_sync_timer != NULL)
    {
        lv_timer_delete(s_home_ai_sync_timer);
        s_home_ai_sync_timer = NULL;
    }

    if (ui_runtime_get_active_screen_id() == UI_SCREEN_HOME)
    {
        xiaozhi_service_register_ui_callbacks(NULL);
    }

    home_ai_pending_lock();
    memset(&s_home_ai_pending, 0, sizeof(s_home_ai_pending));
    home_ai_pending_unlock();
    if (s_home_ai_pending_mutex != RT_NULL)
    {
        rt_mutex_delete(s_home_ai_pending_mutex);
        s_home_ai_pending_mutex = RT_NULL;
    }
    s_home_ai_stop_pending = false;
    s_home_ai_press_active = false;
    s_home_ai_last_tts_tick = 0;
    s_home_ai_last_network_state = (net_manager_service_state_t)-1;
    s_home_ai_last_service_state = (xz_service_state_t)-1;
    s_home_ai_prev_service_state = (xz_service_state_t)-1;
    memset(s_home_ai_mouth_cache, 0, sizeof(s_home_ai_mouth_cache));
    memset(s_home_ai_copy_cache, 0, sizeof(s_home_ai_copy_cache));
    memset(s_home_ai_button_cache, 0, sizeof(s_home_ai_button_cache));
    memset(s_home_ai_network_cache, 0, sizeof(s_home_ai_network_cache));
    s_home_ai_face_cache = NULL;
}

const xiaozhi_home_screen_refs_t *ui_home_screen_refs_get(void)
{
    return &s_home_refs;
}

void ui_Home_screen_init(void)
{
    if (ui_Home != NULL)
    {
        return;
    }

    memset(&s_home_refs, 0, sizeof(s_home_refs));
    ui_Home = ui_create_screen_base();
    s_home_refs.screen = ui_Home;

    ui_top_nav_create(ui_Home, UI_TOP_TAB_AI);
    home_create_ai_area(ui_Home);
    ui_bottom_nav_create(ui_Home, UI_BOTTOM_TAB_AI);

    s_home_refs.time_label = s_home_time_label;
    s_home_refs.meta_label = s_home_date_label;

    home_status_refresh();
    if (s_home_status_timer == NULL)
    {
        s_home_status_timer = lv_timer_create(home_status_timer_cb, HOME_STATUS_REFRESH_MS, NULL);
    }

    if (s_home_deferred_start_timer == NULL)
    {
        s_home_deferred_start_timer = lv_timer_create(home_deferred_start_timer_cb,
                                                      HOME_DEFERRED_START_MS,
                                                      NULL);
    }
}

void ui_Home_screen_destroy(void)
{
    if (s_home_deferred_start_timer != NULL)
    {
        lv_timer_delete(s_home_deferred_start_timer);
        s_home_deferred_start_timer = NULL;
    }

    home_ai_runtime_stop();

    if (s_home_status_timer != NULL)
    {
        lv_timer_delete(s_home_status_timer);
        s_home_status_timer = NULL;
    }

    if (ui_Home != NULL)
    {
        lv_obj_delete(ui_Home);
        ui_Home = NULL;
    }

    s_home_time_label = NULL;
    s_home_date_label = NULL;
    s_home_ai_face_img = NULL;
    s_home_ai_mouth_label = NULL;
    s_home_ai_copy_label = NULL;
    s_home_ai_button_label = NULL;
    s_home_ai_network_label = NULL;
    s_home_ai_talk_card = NULL;
    memset(&s_home_refs, 0, sizeof(s_home_refs));
}
