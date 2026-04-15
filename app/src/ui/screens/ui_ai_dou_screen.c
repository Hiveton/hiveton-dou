#include "ui.h"
#include "ui_i18n.h"
#include "ui_helpers.h"
#include "xiaozhi/xiaozhi_client_public.h"
#include "xiaozhi/xiaozhi_service.h"
#include "network/net_manager.h"
#include "rtthread.h"
#include "petgame.h"
#include <string.h>

lv_obj_t *ui_AI_Dou = NULL;
static lv_obj_t *s_ai_face_img = NULL;
static lv_obj_t *s_ai_face_container = NULL;
static lv_obj_t *s_ai_mouth_label = NULL;
static lv_obj_t *s_ai_copy_label = NULL;
static lv_obj_t *s_network_label = NULL;
static lv_obj_t *s_talk_button = NULL;
static lv_timer_t *s_ai_sync_timer = NULL;
static lv_timer_t *s_ai_boot_timer = NULL;
static lv_timer_t *s_ai_greeting_timer = NULL;
static rt_mutex_t s_ai_pending_mutex = RT_NULL;
static bool s_stop_pending = false;
static rt_tick_t s_ai_last_tts_tick = 0;
static net_manager_service_state_t s_ai_last_network_state = (net_manager_service_state_t)-1;
static xz_service_state_t s_ai_last_service_state = (xz_service_state_t)-1;
static xz_service_state_t s_ai_prev_service_state = (xz_service_state_t)-1;
static char s_ai_mouth_cache[48] = {0};
static char s_ai_copy_cache[200] = {0};
static char s_ai_button_cache[32] = {0};
static char s_ai_network_cache[96] = {0};
static const lv_image_dsc_t *s_ai_face_cache = NULL;

#define AI_UI_SYNC_INTERVAL_MS      200
#define AI_SERVICE_BOOT_DELAY_MS    500
#define AI_GREETING_DELAY_MS        1200
#define AI_TTS_UPDATE_INTERVAL_MS   1200

extern const lv_image_dsc_t funny2;
extern const lv_image_dsc_t sleepy2;

typedef struct
{
    bool state_valid;
    xz_service_state_t state;
    bool copy_valid;
    char copy_text[200];
    bool emoji_valid;
    char emoji[32];
    bool network_dirty;
} ai_ui_pending_state_t;

/* UI回调函数声明 */
static void update_network_status(void);
static void update_talk_button_text(xz_service_state_t state);
static void ai_ui_sync_timer_cb(lv_timer_t *timer);
static void ai_service_boot_timer_cb(lv_timer_t *timer);
static void ai_greeting_timer_cb(lv_timer_t *timer);
static void ai_restore_runtime_state(void);
static void on_service_state_change(xz_service_state_t state);
static void on_chat_output(const char* text);
static void on_tts_output(const char* text);
static void on_emoji_change(const char* emoji);
static void on_error(const char* error_msg);

/* UI回调函数表 */
static xz_service_ui_callbacks_t s_ui_cbs = {
    .on_state_change = on_service_state_change,
    .on_chat_output = on_chat_output,
    .on_tts_output = on_tts_output,
    .on_emoji_change = on_emoji_change,
    .on_error = on_error,
};
static ai_ui_pending_state_t s_ai_pending = {0};

static const char *ai_ui_text_idle(void)
{
    return ui_i18n_pick("静候你开口", "Ready for you");
}

static const char *ai_ui_text_prompt(void)
{
    return ui_i18n_pick("今天想聊什么？你可以问我阅读内容、让我整理想法，或者直接说一句想记录的话。",
                        "What would you like to talk about today? Ask about your reading, let me organize your ideas, or just say something you want to remember.");
}

static const char *ai_ui_service_state_text(xz_service_state_t state)
{
    switch (state)
    {
    case XZ_SERVICE_IDLE:
        return ui_i18n_pick("AI已关闭", "AI idle");
    case XZ_SERVICE_INITING:
        return ui_i18n_pick("AI连接中", "AI connecting");
    case XZ_SERVICE_READY:
        return ui_i18n_pick("AI就绪", "AI ready");
    case XZ_SERVICE_LISTENING:
        return ui_i18n_pick("AI聆听中", "AI listening");
    case XZ_SERVICE_SPEAKING:
        return ui_i18n_pick("AI回答中", "AI speaking");
    case XZ_SERVICE_CLOSING:
        return ui_i18n_pick("AI关闭中", "AI closing");
    default:
        return ui_i18n_pick("AI状态未知", "AI unknown");
    }
}

static const char *ai_ui_network_state_text(net_manager_service_state_t state)
{
    switch (state)
    {
    case NET_MANAGER_SERVICE_OFFLINE:
        return ui_i18n_pick("网络未连接", "Network offline");
    case NET_MANAGER_SERVICE_RADIO_READY:
        return ui_i18n_pick("网络准备中", "Network readying");
    case NET_MANAGER_SERVICE_LINK_READY:
        return ui_i18n_pick("链路已连接", "Link connected");
    case NET_MANAGER_SERVICE_DNS_READY:
        return ui_i18n_pick("DNS已就绪", "DNS ready");
    case NET_MANAGER_SERVICE_INTERNET_READY:
        return ui_i18n_pick("已连接", "Connected");
    default:
        return ui_i18n_pick("网络状态未知", "Network unknown");
    }
}

static const char *ai_ui_network_status_text(net_manager_service_state_t net_state,
                                             xz_service_state_t svc_state)
{
    (void)svc_state;
    return ai_ui_network_state_text(net_state);
}

static void ai_set_label_if_changed(lv_obj_t *label,
                                    char *cache,
                                    size_t cache_size,
                                    const char *text)
{
    const char *final_text = text != NULL ? text : "";

    if (label == NULL || cache == NULL || cache_size == 0U)
    {
        return;
    }

    if (strncmp(cache, final_text, cache_size) == 0)
    {
        return;
    }

    rt_snprintf(cache, cache_size, "%s", final_text);
    lv_label_set_text(label, final_text);
}

static void ai_set_face_if_changed(const lv_image_dsc_t *img)
{
    if (s_ai_face_img == NULL || img == NULL || s_ai_face_cache == img)
    {
        return;
    }

    s_ai_face_cache = img;
    ui_img_set_src(s_ai_face_img, img);
}

static void ai_ui_pending_lock(void)
{
    if (s_ai_pending_mutex != RT_NULL)
    {
        rt_mutex_take(s_ai_pending_mutex, RT_WAITING_FOREVER);
    }
}

static void ai_ui_pending_unlock(void)
{
    if (s_ai_pending_mutex != RT_NULL)
    {
        rt_mutex_release(s_ai_pending_mutex);
    }
}

static void ai_ui_sync_timer_cb(lv_timer_t *timer)
{
    ai_ui_pending_state_t pending;

    (void)timer;

    if (ui_AI_Dou == NULL || s_ai_mouth_label == NULL || s_ai_copy_label == NULL)
    {
        return;
    }

    ai_ui_pending_lock();
    pending = s_ai_pending;
    memset(&s_ai_pending, 0, sizeof(s_ai_pending));
    ai_ui_pending_unlock();

    if (pending.state_valid)
    {
        if (pending.state != XZ_SERVICE_LISTENING)
        {
            s_stop_pending = false;
        }

        update_talk_button_text(pending.state);
        switch (pending.state)
        {
        case XZ_SERVICE_READY:
            ai_set_label_if_changed(s_ai_mouth_label, s_ai_mouth_cache,
                                    sizeof(s_ai_mouth_cache), ai_ui_text_idle());
            if (!pending.copy_valid)
            {
                ai_set_label_if_changed(s_ai_copy_label, s_ai_copy_cache,
                                        sizeof(s_ai_copy_cache),
                                        ai_ui_text_prompt());
            }
            ai_set_face_if_changed(&funny2);
            break;
        case XZ_SERVICE_LISTENING:
            ai_set_label_if_changed(s_ai_mouth_label, s_ai_mouth_cache,
                                    sizeof(s_ai_mouth_cache), ui_i18n_pick("正在聆听", "Listening"));
            if (!pending.copy_valid)
            {
                ai_set_label_if_changed(s_ai_copy_label, s_ai_copy_cache,
                                        sizeof(s_ai_copy_cache), ui_i18n_pick("请说话...", "Please speak..."));
            }
            ai_set_face_if_changed(&sleepy2);
            break;
        case XZ_SERVICE_SPEAKING:
            ai_set_label_if_changed(s_ai_mouth_label, s_ai_mouth_cache,
                                    sizeof(s_ai_mouth_cache), ui_i18n_pick("正在回答", "Responding"));
            ai_set_face_if_changed(&funny2);
            break;
        case XZ_SERVICE_CLOSING:
            ai_set_label_if_changed(s_ai_mouth_label, s_ai_mouth_cache,
                                    sizeof(s_ai_mouth_cache), ui_i18n_pick("服务关闭中", "Closing"));
            break;
        default:
            break;
        }
    }

    if (pending.copy_valid)
    {
        ai_set_label_if_changed(s_ai_copy_label, s_ai_copy_cache,
                                sizeof(s_ai_copy_cache), pending.copy_text);
    }

    update_network_status();
}

static void update_talk_button_text(xz_service_state_t state)
{
    lv_obj_t *label;
    const char *text = ui_i18n_pick("点击开始说话", "Tap to talk");

    if (s_talk_button == NULL)
    {
        return;
    }

    label = lv_obj_get_child(s_talk_button, 0);
    if (label == NULL)
    {
        return;
    }

    switch (state)
    {
    case XZ_SERVICE_LISTENING:
        text = ui_i18n_pick("点击停止并发送", "Tap to stop and send");
        break;
    case XZ_SERVICE_SPEAKING:
        text = ui_i18n_pick("点击重新说话", "Tap to speak again");
        break;
    case XZ_SERVICE_INITING:
        text = ui_i18n_pick("连接中...", "Connecting...");
        break;
    case XZ_SERVICE_CLOSING:
        text = ui_i18n_pick("关闭中...", "Closing...");
        break;
    default:
        text = ui_i18n_pick("点击开始说话", "Tap to talk");
        break;
    }

    if (strncmp(s_ai_button_cache, text, sizeof(s_ai_button_cache)) != 0)
    {
        rt_snprintf(s_ai_button_cache, sizeof(s_ai_button_cache), "%s", text);
        lv_label_set_text(label, text);
    }
}

static void ai_service_boot_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (s_ai_boot_timer != NULL)
    {
        lv_timer_delete(s_ai_boot_timer);
        s_ai_boot_timer = NULL;
    }

    if (ui_AI_Dou == NULL || s_ai_mouth_label == NULL || s_ai_copy_label == NULL)
    {
        return;
    }

    xiaozhi_service_register_ui_callbacks(&s_ui_cbs);
    if (xiaozhi_service_init() != 0)
    {
        ai_set_label_if_changed(s_ai_copy_label, s_ai_copy_cache,
                                sizeof(s_ai_copy_cache),
                                ui_i18n_pick("服务启动失败，请检查蓝牙网络连接",
                                             "Service failed to start. Please check Bluetooth and network."));
        ai_set_label_if_changed(s_ai_mouth_label, s_ai_mouth_cache,
                                sizeof(s_ai_mouth_cache), ui_i18n_pick("启动失败", "Start Failed"));
        return;
    }

    ai_set_label_if_changed(s_ai_mouth_label, s_ai_mouth_cache,
                            sizeof(s_ai_mouth_cache), ui_i18n_pick("连接小智中", "Connecting Xiaozhi"));
    ai_set_label_if_changed(s_ai_copy_label, s_ai_copy_cache,
                            sizeof(s_ai_copy_cache),
                            ui_i18n_pick("页面已稳定，正在连接小智...", "The page is ready. Connecting Xiaozhi..."));

    if (s_ai_greeting_timer == NULL)
    {
        s_ai_greeting_timer = lv_timer_create(ai_greeting_timer_cb,
                                              AI_GREETING_DELAY_MS,
                                              NULL);
    }
}

static void ai_greeting_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (s_ai_greeting_timer != NULL)
    {
        lv_timer_delete(s_ai_greeting_timer);
        s_ai_greeting_timer = NULL;
    }

    if (ui_AI_Dou == NULL)
    {
        return;
    }

    xiaozhi_service_request_greeting();
}

static void ai_restore_runtime_state(void)
{
    xz_service_state_t state = xiaozhi_service_get_state();

    on_service_state_change(state);

    if (state == XZ_SERVICE_READY)
    {
        if (xiaozhi_service_get_session_id() != NULL)
        {
            on_chat_output(ui_i18n_pick("小智已保持连接", "Xiaozhi is still connected"));
        }
        else
        {
            on_chat_output(ui_i18n_pick("正在恢复小智状态...", "Restoring Xiaozhi state..."));
        }
    }
}

/* 网络状态标签更新 */
static void update_network_status(void)
{
    net_manager_service_state_t net_state;
    xz_service_state_t service_state;
    const char *text;

    if (s_network_label == NULL) return;

    net_state = net_manager_get_service_state();
    service_state = xiaozhi_service_get_state();
    text = ai_ui_network_status_text(net_state, service_state);

    if (s_ai_last_network_state == net_state &&
        s_ai_last_service_state == service_state &&
        strncmp(s_ai_network_cache, text, sizeof(s_ai_network_cache)) == 0)
    {
        return;
    }

    s_ai_last_network_state = net_state;
    s_ai_last_service_state = service_state;
    lv_obj_set_style_text_color(s_network_label, lv_color_hex(0x000000), 0);
    ai_set_label_if_changed(s_network_label, s_ai_network_cache,
                            sizeof(s_ai_network_cache), text);
}

/* 服务状态变化回调 */
static void on_service_state_change(xz_service_state_t state)
{
    if (state == XZ_SERVICE_LISTENING && s_ai_prev_service_state != XZ_SERVICE_LISTENING)
    {
        petgame_record_ai_interaction();
    }
    s_ai_prev_service_state = state;

    ai_ui_pending_lock();
    s_ai_pending.state = state;
    s_ai_pending.state_valid = true;
    s_ai_pending.network_dirty = true;
    ai_ui_pending_unlock();
}

/* 对话输出回调 */
static void on_chat_output(const char* text)
{
    if (text != NULL) {
        ai_ui_pending_lock();
        rt_snprintf(s_ai_pending.copy_text, sizeof(s_ai_pending.copy_text), "%s", text);
        s_ai_pending.copy_valid = true;
        ai_ui_pending_unlock();
    }
}

/* TTS输出回调 */
static void on_tts_output(const char* text)
{
    if (text != NULL) {
        rt_tick_t now = rt_tick_get();

        if (s_ai_last_tts_tick != 0 &&
            (now - s_ai_last_tts_tick) < rt_tick_from_millisecond(AI_TTS_UPDATE_INTERVAL_MS))
        {
            return;
        }

        s_ai_last_tts_tick = now;
        ai_ui_pending_lock();
        rt_snprintf(s_ai_pending.copy_text, sizeof(s_ai_pending.copy_text), "%s", text);
        s_ai_pending.copy_valid = true;
        ai_ui_pending_unlock();
    }
}

/* 表情变化回调 */
static void on_emoji_change(const char* emoji)
{
    if (emoji != NULL) {
        ai_ui_pending_lock();
        rt_snprintf(s_ai_pending.emoji, sizeof(s_ai_pending.emoji), "%s", emoji);
        s_ai_pending.emoji_valid = true;
        ai_ui_pending_unlock();
    }
}

/* 错误回调 */
static void on_error(const char* error_msg)
{
    if (error_msg != NULL) {
        ai_ui_pending_lock();
        rt_snprintf(s_ai_pending.copy_text, sizeof(s_ai_pending.copy_text), "%s", error_msg);
        s_ai_pending.copy_valid = true;
        ai_ui_pending_unlock();
        s_stop_pending = false;
    }
}

/* 按钮事件回调 */
static void ai_talk_button_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    xz_service_state_t state;
    
    if (s_ai_face_img == NULL || s_ai_mouth_label == NULL || s_ai_copy_label == NULL)
    {
        return;
    }

    if (code != LV_EVENT_CLICKED)
    {
        return;
    }
    
    /* 检查网络状态 */
    if (!net_manager_can_run_ai()) {
        const char *network_text = ai_ui_network_status_text(net_manager_get_service_state(),
                                                             xiaozhi_service_get_state());
        ai_set_label_if_changed(s_ai_copy_label, s_ai_copy_cache,
                                sizeof(s_ai_copy_cache), network_text);
        update_network_status();
        return;
    }
    
    state = xiaozhi_service_get_state();

    if (state == XZ_SERVICE_INITING || state == XZ_SERVICE_CLOSING)
    {
        lv_label_set_text(s_ai_copy_label, ui_i18n_pick("小智正在准备，请稍后再试", "Xiaozhi is getting ready. Please try again shortly."));
        return;
    }

    if (s_stop_pending)
    {
        update_talk_button_text(XZ_SERVICE_READY);
        lv_label_set_text(s_ai_copy_label, ui_i18n_pick("正在发送给小智，请稍候...", "Sending to Xiaozhi, please wait..."));
        return;
    }

    if (state == XZ_SERVICE_LISTENING)
    {
        s_stop_pending = true;
        xiaozhi_service_stop_listening();
        update_talk_button_text(XZ_SERVICE_READY);
        lv_label_set_text(s_ai_mouth_label, ui_i18n_pick("正在发送", "Sending"));
        lv_label_set_text(s_ai_copy_label, ui_i18n_pick("录音结束，正在发送给小智...", "Recording finished. Sending to Xiaozhi..."));
        return;
    }

    s_stop_pending = false;
    xiaozhi_service_start_listening();
    if (state == XZ_SERVICE_SPEAKING)
    {
        ai_set_label_if_changed(s_ai_mouth_label, s_ai_mouth_cache,
                                sizeof(s_ai_mouth_cache), ui_i18n_pick("正在聆听", "Listening"));
        ai_set_label_if_changed(s_ai_copy_label, s_ai_copy_cache,
                                sizeof(s_ai_copy_cache),
                                ui_i18n_pick("已打断当前回答，开始重新录音...", "Current reply interrupted. Recording again..."));
    }
    else
    {
        ai_set_label_if_changed(s_ai_copy_label, s_ai_copy_cache,
                                sizeof(s_ai_copy_cache),
                                ui_i18n_pick("开始录音，再点一次即可停止并发送", "Recording started. Tap again to stop and send."));
    }
}

/* 页面初始化 */
void ui_AI_Dou_screen_init(void)
{
    ui_screen_scaffold_t page;
    lv_obj_t *face;
    lv_obj_t *dialog_card;
    lv_obj_t *talk_button;
    
    if (ui_AI_Dou != NULL)
    {
        return;
    }
    
    ui_AI_Dou = ui_create_screen_base();
    ui_build_standard_screen(&page, ui_AI_Dou, ui_i18n_pick("AI小豆", "AI Dou"), UI_SCREEN_HOME);

    if (s_ai_pending_mutex == RT_NULL)
    {
        s_ai_pending_mutex = rt_mutex_create("ai_ui", RT_IPC_FLAG_FIFO);
    }
    ai_ui_pending_lock();
    memset(&s_ai_pending, 0, sizeof(s_ai_pending));
    ai_ui_pending_unlock();
    s_stop_pending = false;
    s_ai_last_tts_tick = 0;
    s_ai_last_network_state = (net_manager_service_state_t)-1;
    s_ai_last_service_state = (xz_service_state_t)-1;
    s_ai_prev_service_state = (xz_service_state_t)-1;
    memset(s_ai_mouth_cache, 0, sizeof(s_ai_mouth_cache));
    memset(s_ai_copy_cache, 0, sizeof(s_ai_copy_cache));
    memset(s_ai_button_cache, 0, sizeof(s_ai_button_cache));
    memset(s_ai_network_cache, 0, sizeof(s_ai_network_cache));
    s_ai_face_cache = NULL;
    if (s_ai_sync_timer == NULL)
    {
        s_ai_sync_timer = lv_timer_create(ai_ui_sync_timer_cb,
                                          AI_UI_SYNC_INTERVAL_MS,
                                          NULL);
    }
    
    s_network_label = NULL;
    
    face = ui_create_card(page.content, 174, 34, 180, 180, UI_SCREEN_NONE, false, 90);
    s_ai_face_container = face;
    lv_obj_set_style_border_width(face, 0, 0);
    lv_obj_set_style_bg_opa(face, LV_OPA_TRANSP, 0);
    s_ai_face_img = lv_img_create(face);
    ai_set_face_if_changed(&funny2);
    lv_obj_center(s_ai_face_img);

    dialog_card = ui_create_card(page.content, 24, 250, 480, 190, UI_SCREEN_NONE, false, 0);
    s_ai_mouth_label = ui_create_label(dialog_card,
                                       ui_i18n_pick("初始化中...", "Initializing..."),
                                       0,
                                       20,
                                       480,
                                       39,
                                       32,
                                       LV_TEXT_ALIGN_CENTER,
                                       false,
                                       false);
    s_ai_copy_label = ui_create_label(dialog_card,
                                      ui_i18n_pick("正在启动AI服务...", "Starting AI service..."),
                                      24,
                                      76,
                                      432,
                                      84,
                                      21,
                                      LV_TEXT_ALIGN_CENTER,
                                      false,
                                      true);
    
    talk_button = ui_create_button(page.content,
                                   164,
                                   475,
                                   200,
                                   64,
                                   ui_i18n_pick("点击开始说话", "Tap to talk"),
                                   28,
                                   UI_SCREEN_NONE,
                                   true);
    s_talk_button = talk_button;
    lv_obj_set_style_radius(talk_button, ui_px_x(32), 0);
    lv_obj_add_event_cb(talk_button, ai_talk_button_event_cb, LV_EVENT_CLICKED, NULL);
    ui_create_label(page.content,
                    ui_i18n_pick("点击一次开始录音，再点击一次停止并发送给小智。也可直接说\"你好小智\"唤醒。",
                                 "Tap once to start recording, tap again to stop and send. You can also wake it with \"Hello Xiaozhi\"."),
                    52,
                    555,
                    424,
                    32,
                    17,
                    LV_TEXT_ALIGN_CENTER,
                    false,
                    true);
    
    ai_set_label_if_changed(s_ai_mouth_label, s_ai_mouth_cache,
                            sizeof(s_ai_mouth_cache), ui_i18n_pick("页面准备中...", "Preparing page..."));
    ai_set_label_if_changed(s_ai_copy_label, s_ai_copy_cache,
                            sizeof(s_ai_copy_cache),
                            ui_i18n_pick("先完成页面渲染，再启动小智服务...", "Finishing page rendering before starting Xiaozhi..."));
    lv_obj_update_layout(ui_AI_Dou);
    lv_obj_invalidate(ui_AI_Dou);
    lv_refr_now(NULL);

    xiaozhi_service_register_ui_callbacks(&s_ui_cbs);
    if (xiaozhi_service_is_running())
    {
        ai_restore_runtime_state();
    }
    else if (s_ai_boot_timer == NULL)
    {
        s_ai_boot_timer = lv_timer_create(ai_service_boot_timer_cb,
                                          AI_SERVICE_BOOT_DELAY_MS,
                                          NULL);
    }
}

/* 页面销毁 */
void ui_AI_Dou_screen_destroy(void)
{
    if (s_ai_boot_timer != NULL)
    {
        lv_timer_delete(s_ai_boot_timer);
        s_ai_boot_timer = NULL;
    }
    if (s_ai_greeting_timer != NULL)
    {
        lv_timer_delete(s_ai_greeting_timer);
        s_ai_greeting_timer = NULL;
    }
    xiaozhi_service_register_ui_callbacks(NULL);
    if (s_ai_sync_timer != NULL)
    {
        lv_timer_delete(s_ai_sync_timer);
        s_ai_sync_timer = NULL;
    }
    ai_ui_pending_lock();
    memset(&s_ai_pending, 0, sizeof(s_ai_pending));
    ai_ui_pending_unlock();
    s_stop_pending = false;
    s_ai_last_tts_tick = 0;
    s_ai_last_network_state = (net_manager_service_state_t)-1;
    s_ai_last_service_state = (xz_service_state_t)-1;
    s_ai_prev_service_state = (xz_service_state_t)-1;
    memset(s_ai_mouth_cache, 0, sizeof(s_ai_mouth_cache));
    memset(s_ai_copy_cache, 0, sizeof(s_ai_copy_cache));
    memset(s_ai_button_cache, 0, sizeof(s_ai_button_cache));
    memset(s_ai_network_cache, 0, sizeof(s_ai_network_cache));
    s_ai_face_cache = NULL;
    
    if (ui_AI_Dou != NULL)
    {
        lv_obj_delete(ui_AI_Dou);
        ui_AI_Dou = NULL;
    }
    
    s_ai_face_img = NULL;
    s_ai_mouth_label = NULL;
    s_ai_copy_label = NULL;
    s_network_label = NULL;
    s_talk_button = NULL;
    s_ai_face_container = NULL;
}
