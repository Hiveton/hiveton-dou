#include "ui_helpers.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "audio_server.h"
#include "lv_tiny_ttf.h"
#include "rtdevice.h"
#include "rtthread.h"
#include "sleep_manager.h"
#include "ui_i18n.h"
#include "ui_status_bar.h"
#include "ui_dispatch.h"
#include "ui_runtime_adapter.h"
#include "../sleep_manager.h"
#include "../bq27220_monitor.h"
#include "../network/net_manager.h"
#include "cat1_modem.h"
#include "../xiaozhi/weather/weather.h"

#define LCD_DEVICE_NAME "lcd"
#define LCD_BACKLIGHT_DEVICE_NAME "lcdlight"
#define UI_STANDARD_NAV_FONT_SIZE 28
#define UI_STANDARD_NAV_HEIGHT 58
#define UI_STANDARD_NAV_BUTTON_WIDTH 84
#define UI_STATUS_BAR_HEIGHT 68
#define UI_STANDARD_SIDE_MARGIN 18
#define UI_STATUS_BAR_CALENDAR_TOUCH_W 320
#define UI_STATUS_BAR_DETAIL_TOUCH_X 320
#define UI_STATUS_TOAST_DURATION_MS 3000
#define UI_STATUS_INTERACTION_DEBOUNCE_MS 250
#define UI_NAV_INTERACTION_DEBOUNCE_MS 250
#define UI_STATUS_DETAIL_RELOAD_DEBOUNCE_MS 1500
#define UI_STATUS_MASK_SOLID_GRAY 0xB3B3B3
#define UI_STATUS_BAR_REFRESH_THREAD_STACK_SIZE 1024
#define UI_STATUS_BAR_REFRESH_THREAD_PRIORITY 22
#define UI_STATUS_BAR_REFRESH_THREAD_TICK 10

typedef enum
{
    UI_STATUS_BLUETOOTH_DISCONNECTED = 0,
    UI_STATUS_BLUETOOTH_WAITING,
    UI_STATUS_BLUETOOTH_CONNECTED,
} ui_status_bluetooth_state_t;

typedef enum
{
    UI_STATUS_SLIDER_BRIGHTNESS = 0,
    UI_STATUS_SLIDER_VOLUME,
} ui_status_slider_kind_t;

typedef enum
{
    UI_STATUS_TOGGLE_BLUETOOTH = 0,
    UI_STATUS_TOGGLE_NETWORK,
} ui_status_toggle_kind_t;

extern const lv_image_dsc_t ble_icon_img;
extern const unsigned char xiaozhi_font[];
extern const int xiaozhi_font_size;
extern const lv_image_dsc_t ble_icon_img_close;
extern const lv_image_dsc_t home_battery;
extern const lv_image_dsc_t home_mic;
extern const lv_image_dsc_t home_volume;
extern const lv_image_dsc_t network_icon_img;
extern const lv_image_dsc_t network_icon_img_close;
void ui_Status_Detail_screen_set_return_target(ui_screen_id_t target);

typedef struct
{
    uint16_t size;
    lv_font_t *font;
} ui_font_cache_entry_t;

typedef struct
{
    lv_obj_t *screen;
    xiaozhi_home_screen_refs_t refs;
    bool used;
} ui_screen_refs_entry_t;

typedef struct
{
    rt_device_t lcd_device;
    rt_device_t lcd_backlight_device;
    lv_timer_t *sync_timer;
    lv_timer_t *toast_timer;
    lv_obj_t *host_screen;
    lv_obj_t *root;
    lv_obj_t *mask;
    lv_obj_t *panel;
    lv_obj_t *toast;
    lv_obj_t *toast_label;
    lv_obj_t *confirm;
    lv_obj_t *brightness_track;
    lv_obj_t *brightness_fill;
    lv_obj_t *brightness_knob;
    lv_obj_t *brightness_slider;
    lv_obj_t *brightness_value_label;
    lv_obj_t *volume_track;
    lv_obj_t *volume_fill;
    lv_obj_t *volume_knob;
    lv_obj_t *volume_slider;
    lv_obj_t *volume_value_label;
    lv_obj_t *bluetooth_card;
    lv_obj_t *bluetooth_title_label;
    lv_obj_t *bluetooth_subtitle_label;
    lv_obj_t *bluetooth_value_label;
    lv_obj_t *network_card;
    lv_obj_t *network_title_label;
    lv_obj_t *network_subtitle_label;
    lv_obj_t *network_value_label;
    uint8_t brightness_steps;
    uint8_t volume_steps;
    bool charging;
    bool confirm_visible;
    bool waiting_requested;
    bool suppress_connected_state;
    bool last_bt_connected;
    bool bluetooth_enabled;
    bool network_enabled;
    bool bluetooth_toggle_initialized;
    bool network_toggle_initialized;
    rt_tick_t last_input_tick;
} ui_status_panel_state_t;

static bool ui_status_screen_is_visible_target(lv_obj_t *screen)
{
    return (screen != NULL) && (screen == lv_screen_active());
}

typedef struct
{
    bool valid;
    bool time_valid;
    int year;
    int month;
    int day;
    int hour;
    int minute;
    bool weather_available;
    int weather_temperature;
    int battery_percent;
    int charge_state;
    uint8_t aw_charge_state;
    uint8_t aw_fault_status;
    int bt_visual_state;
    int network_visual_state;
    net_manager_link_t active_link;
    bool bt_enabled;
    bool net_4g_enabled;
    char network_detail[16];
} ui_status_bar_snapshot_t;

static ui_font_cache_entry_t s_font_cache[20];
static ui_screen_refs_entry_t s_screen_refs[UI_SCREEN_COUNT];
static bool s_ui_helpers_initialized = false;
static lv_coord_t s_screen_width = UI_FIGMA_WIDTH;
static lv_coord_t s_screen_height = UI_FIGMA_HEIGHT;
static const size_t UI_TINY_TTF_GLYPH_CACHE_COUNT = 32U;
static ui_status_panel_state_t s_status_panel = {0};
static struct rt_thread s_status_bar_refresh_thread;
static rt_uint8_t s_status_bar_refresh_thread_stack[UI_STATUS_BAR_REFRESH_THREAD_STACK_SIZE];
static bool s_status_bar_refresh_thread_started = false;
static volatile int s_status_pending_brightness = -1;
static volatile int s_status_pending_volume = -1;
static volatile int s_status_pending_charge = -1;
static volatile int s_status_pending_battery_percent = -1;
static int s_status_applied_charge = -1;
static int s_status_applied_battery_percent = -1;
static rt_tick_t s_ui_nav_last_tick = 0;
static rt_tick_t s_status_detail_reload_tick = 0;
static bool s_status_detail_reload_pending = false;
static int s_status_last_bt_icon_state = -1;
static int s_status_last_network_icon_state = -1;
static bool s_status_last_net_4g_enabled = false;
static char s_status_last_network_text[16];
static ui_status_bar_snapshot_t s_status_bar_snapshot = {0};
static const char *ui_status_weekday_from_index(int weekday)
{
    static const char *const k_weekdays[] = {
        "星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"
    };

    if (weekday >= 0 && weekday < (int)(sizeof(k_weekdays) / sizeof(k_weekdays[0])))
    {
        return k_weekdays[weekday];
    }

    return "星期一";
}

static bool ui_status_accept_interaction(void);
static void ui_status_request_detail_rebuild(void);
static void ui_status_update_panel_visuals(void);
static bool ui_accept_debounced_tick(rt_tick_t *last_tick, uint32_t debounce_ms);
static bool ui_accept_navigation_interaction(void);
static bool ui_status_detail_is_active(void);
static void ui_status_detail_reload_async_cb(void *user_data);
static void ui_status_refresh_connection_icons(bool force);
static void ui_status_bar_refresh_datetime(void);
static uint32_t ui_status_bar_next_refresh_delay_ms(void);
static void ui_status_bar_refresh_thread_entry(void *parameter);
static bool ui_status_backlight_read(uint8_t *brightness);
static void ui_status_backlight_write(uint8_t brightness);
static ui_status_bluetooth_state_t ui_status_get_bluetooth_state(void);
static void ui_status_set_label_text(lv_obj_t *label, const char *text);
static void ui_status_capture_snapshot(ui_status_bar_snapshot_t *snapshot);
static bool ui_status_snapshot_equal(const ui_status_bar_snapshot_t *lhs,
                                     const ui_status_bar_snapshot_t *rhs);
static void ui_status_panel_toggle_event_cb(lv_event_t *e);

static void ui_status_panel_toggle_event_bridge(lv_event_t *e)
{
    ui_status_panel_toggle_event_cb(e);
}

static bool ui_bt_connection_active(void)
{
    return net_manager_bt_connected();
}

static bool ui_bt_pairing_enabled(void)
{
    return false;
}

static bool ui_bt_network_ready(void)
{
    return net_manager_network_ready();
}

static uint32_t ui_status_bar_next_refresh_delay_ms(void)
{
    date_time_t current_time;
    uint32_t delay_ms = 60000U;

    if (xiaozhi_time_get_current(&current_time) == RT_EOK)
    {
        int second = current_time.second;

        if (second < 0)
        {
            second = 0;
        }
        if (second > 59)
        {
            second = 59;
        }

        delay_ms = (uint32_t)(60 - second) * 1000U;
        if (delay_ms == 0U)
        {
            delay_ms = 60000U;
        }
    }

    return delay_ms;
}

static void ui_status_bar_refresh_datetime(void)
{
    date_time_t current_time;
    weather_info_t current_weather = {0};
    bool weather_available = false;
    bool use_fallback_time = false;
    const char *weekday_label = NULL;
    char time_text[16];
    char meta_text[48];
    size_t i;

    memset(&current_time, 0, sizeof(current_time));

    if (xiaozhi_time_get_current(&current_time) != RT_EOK)
    {
        use_fallback_time = true;
    }

    if (!use_fallback_time)
    {
        if (current_time.year < 2026 ||
            current_time.month < 1 || current_time.month > 12 ||
            current_time.day < 1 || current_time.day > 31 ||
            current_time.hour < 0 || current_time.hour > 23 ||
            current_time.minute < 0 || current_time.minute > 59)
        {
            use_fallback_time = true;
        }
    }

    if (use_fallback_time)
    {
        current_time.year = 2026;
        current_time.month = 1;
        current_time.day = 1;
        current_time.hour = 1;
        current_time.minute = 1;
        current_time.second = 0;
        current_time.weekday = 4;
        weekday_label = ui_status_weekday_from_index(current_time.weekday);
    }
    else
    {
        const char *raw_weekday = current_time.weekday_str;

        if (raw_weekday != NULL &&
            raw_weekday[0] != '\0' &&
            strchr(raw_weekday, '?') == NULL)
        {
            weekday_label = ui_i18n_translate_weekday_label(raw_weekday);
        }

        if (weekday_label == NULL || weekday_label[0] == '\0' || strchr(weekday_label, '?') != NULL)
        {
            weekday_label = ui_status_weekday_from_index(current_time.weekday);
        }
    }

    if (xiaozhi_weather_peek(&current_weather) == RT_EOK && current_weather.last_update > 0)
    {
        weather_available = true;
    }

    rt_snprintf(time_text,
                sizeof(time_text),
                "%02d:%02d",
                current_time.hour,
                current_time.minute);

    if (weather_available)
    {
        rt_snprintf(meta_text,
                    sizeof(meta_text),
                    "%04d/%02d/%02d\n%s %d°C",
                    current_time.year,
                    current_time.month,
                    current_time.day,
                    weekday_label,
                    current_weather.temperature);
    }
    else
    {
        rt_snprintf(meta_text,
                    sizeof(meta_text),
                    "%04d/%02d/%02d\n%s",
                    current_time.year,
                    current_time.month,
                    current_time.day,
                    weekday_label);
    }

    for (i = 0; i < sizeof(s_screen_refs) / sizeof(s_screen_refs[0]); ++i)
    {
        xiaozhi_home_screen_refs_t *refs = &s_screen_refs[i].refs;

        if (!s_screen_refs[i].used)
        {
            continue;
        }

        if (refs->time_label != NULL)
        {
            ui_status_set_label_text(refs->time_label, time_text);
        }

        if (refs->meta_label != NULL)
        {
            ui_status_set_label_text(refs->meta_label, meta_text);
        }
    }
}

static void ui_status_bar_refresh_thread_entry(void *parameter)
{
    LV_UNUSED(parameter);

    while (1)
    {
        uint32_t delay_ms = ui_status_bar_next_refresh_delay_ms();

        if (sleep_manager_is_sleeping())
        {
            if (delay_ms < 60000U)
            {
                delay_ms = 60000U;
            }
            rt_thread_mdelay(delay_ms);
            continue;
        }

        if (delay_ms < 1000U)
        {
            delay_ms = 1000U;
        }

        rt_thread_mdelay(delay_ms);

        if (sleep_manager_is_sleeping() ||
            ui_runtime_get_active_screen_id() == UI_SCREEN_STANDBY)
        {
            continue;
        }

        ui_dispatch_request_time_refresh();
    }
}

static bool ui_status_backlight_read(uint8_t *brightness)
{
    rt_size_t read_size;

    if (brightness == NULL)
    {
        return false;
    }

    if (s_status_panel.lcd_backlight_device == RT_NULL)
    {
        s_status_panel.lcd_backlight_device = rt_device_find(LCD_BACKLIGHT_DEVICE_NAME);
    }

    if (s_status_panel.lcd_backlight_device == RT_NULL)
    {
        return false;
    }

    read_size = rt_device_read(s_status_panel.lcd_backlight_device, 0, brightness, 1);
    return read_size == 1;
}

static void ui_status_backlight_write(uint8_t brightness)
{
    if (brightness > 100U)
    {
        brightness = 100U;
    }

    if (s_status_panel.lcd_backlight_device == RT_NULL)
    {
        s_status_panel.lcd_backlight_device = rt_device_find(LCD_BACKLIGHT_DEVICE_NAME);
    }

    if (s_status_panel.lcd_backlight_device != RT_NULL)
    {
        rt_device_write(s_status_panel.lcd_backlight_device, 0, &brightness, 1);
    }
}

static lv_font_t *ui_font_cache_get(ui_font_cache_entry_t *cache,
                                    size_t cache_count,
                                    const unsigned char *font_data,
                                    size_t font_size,
                                    uint16_t actual_size)
{
    size_t i;

    for (i = 0; i < cache_count; ++i)
    {
        if (cache[i].font != NULL && cache[i].size == actual_size)
        {
            return cache[i].font;
        }
    }

    for (i = 0; i < cache_count; ++i)
    {
        if (cache[i].font == NULL)
        {
            cache[i].font = lv_tiny_ttf_create_data_ex(font_data,
                                                       font_size,
                                                       actual_size,
                                                       LV_FONT_KERNING_NORMAL,
                                                       UI_TINY_TTF_GLYPH_CACHE_COUNT);
            cache[i].size = actual_size;
            if (cache[i].font != NULL)
            {
                return cache[i].font;
            }
            break;
        }
    }

    if (cache_count > 0U)
    {
        lv_font_t *closest_font = NULL;
        uint16_t closest_delta = UINT16_MAX;

        for (i = 0; i < cache_count; ++i)
        {
            if (cache[i].font != NULL)
            {
                uint16_t delta = (cache[i].size > actual_size)
                                     ? (cache[i].size - actual_size)
                                     : (actual_size - cache[i].size);

                if (closest_font == NULL || delta < closest_delta)
                {
                    closest_font = cache[i].font;
                    closest_delta = delta;
                }
            }
        }

        return closest_font;
    }

    return NULL;
}

static void ui_register_screen_refs(lv_obj_t *screen,
                                    const xiaozhi_home_screen_refs_t *refs)
{
    size_t i;

    if (screen == NULL || refs == NULL)
    {
        return;
    }

    for (i = 0; i < sizeof(s_screen_refs) / sizeof(s_screen_refs[0]); ++i)
    {
        if (s_screen_refs[i].screen == screen || !s_screen_refs[i].used)
        {
            s_screen_refs[i].screen = screen;
            s_screen_refs[i].refs = *refs;
            s_screen_refs[i].refs.screen = screen;
            s_screen_refs[i].used = true;
            return;
        }
    }
}

static void ui_refresh_metrics(void)
{
    lv_coord_t width = lv_disp_get_hor_res(NULL);
    lv_coord_t height = lv_disp_get_ver_res(NULL);

    if (width > 0)
    {
        s_screen_width = width;
    }
    if (height > 0)
    {
        s_screen_height = height;
    }
}

static lv_coord_t ui_scale_value(int32_t value, int32_t base, lv_coord_t actual)
{
    int32_t scaled;

    if (base <= 0)
    {
        return (lv_coord_t)value;
    }

    scaled = (value * actual + base / 2) / base;
    if (scaled < 0)
    {
        return 0;
    }

    return (lv_coord_t)scaled;
}

static void ui_apply_basic_object_style(lv_obj_t *obj,
                                        bool filled,
                                        int32_t radius,
                                        uint16_t border_width)
{
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj,
                              filled ? lv_color_hex(0x000000) : lv_color_hex(0xFFFFFF),
                              0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(obj, border_width, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

static void ui_nav_event_cb(lv_event_t *e)
{
    uintptr_t raw_target = (uintptr_t)lv_event_get_user_data(e);
    ui_screen_id_t target = (ui_screen_id_t)raw_target;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED || target == UI_SCREEN_NONE)
    {
        return;
    }

    if (!ui_accept_navigation_interaction())
    {
        return;
    }

    lv_display_trigger_activity(NULL);
    ui_runtime_switch_to(target);
}

static uint8_t ui_status_brightness_to_steps(uint8_t brightness)
{
    uint8_t steps = (uint8_t)((brightness + 19U) / 20U);

    if (steps < 1U)
    {
        steps = 1U;
    }
    if (steps > 5U)
    {
        steps = 5U;
    }

    return steps;
}

static uint8_t ui_status_steps_to_brightness(uint8_t steps)
{
    if (steps < 1U)
    {
        steps = 1U;
    }
    if (steps > 5U)
    {
        steps = 5U;
    }

    return (uint8_t)(steps * 20U);
}

static uint8_t ui_status_volume_to_steps(uint8_t volume)
{
    uint16_t scaled = (uint16_t)volume * 10U + 7U;
    uint8_t steps = (uint8_t)(scaled / 15U);

    if (steps > 10U)
    {
        steps = 10U;
    }

    return steps;
}

static uint8_t ui_status_steps_to_volume(uint8_t steps)
{
    if (steps > 10U)
    {
        steps = 10U;
    }

    return (uint8_t)(((uint16_t)steps * 15U + 5U) / 10U);
}

static bool ui_accept_debounced_tick(rt_tick_t *last_tick, uint32_t debounce_ms)
{
    rt_tick_t now;
    rt_tick_t debounce;

    if (last_tick == NULL)
    {
        return false;
    }

    now = rt_tick_get();
    debounce = rt_tick_from_millisecond(debounce_ms);
    if ((*last_tick != 0U) && ((now - *last_tick) < debounce))
    {
        return false;
    }

    *last_tick = now;
    return true;
}

static bool ui_accept_navigation_interaction(void)
{
    if (s_status_detail_reload_pending)
    {
        return false;
    }

    return ui_accept_debounced_tick(&s_ui_nav_last_tick,
                                    UI_NAV_INTERACTION_DEBOUNCE_MS);
}

static bool ui_status_detail_is_active(void)
{
    return ui_runtime_get_active_screen_id() == UI_SCREEN_STATUS_DETAIL;
}

static void ui_status_style_badge(lv_obj_t *obj, bool filled)
{
    if (obj == NULL)
    {
        return;
    }

    lv_obj_set_style_radius(obj, ui_px_x(10), 0);
    lv_obj_set_style_border_width(obj, 2, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x000000), 0);
    lv_obj_set_style_pad_left(obj, ui_px_x(6), 0);
    lv_obj_set_style_pad_right(obj, ui_px_x(6), 0);
    lv_obj_set_style_pad_top(obj, ui_px_y(1), 0);
    lv_obj_set_style_pad_bottom(obj, ui_px_y(1), 0);

    if (filled)
    {
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(obj, lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_color(obj, lv_color_hex(0xFFFFFF), 0);
    }
    else
    {
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(obj, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_color(obj, lv_color_hex(0x000000), 0);
    }
}

static void ui_status_set_label_text(lv_obj_t *label, const char *text)
{
    const char *old_text;

    if (label == NULL || text == NULL)
    {
        return;
    }

    old_text = lv_label_get_text(label);
    if ((old_text == NULL) || (strcmp(old_text, text) != 0))
    {
        lv_label_set_text(label, text);
    }
}

static void ui_status_set_object_hidden(lv_obj_t *obj, bool hidden)
{
    bool hidden_now;

    if (obj == NULL)
    {
        return;
    }

    hidden_now = lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
    if (hidden)
    {
        if (!hidden_now)
        {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }
    }
    else
    {
        if (hidden_now)
        {
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static int ui_status_get_cat1_visual_state(char *detail_text, size_t detail_size)
{
    char status_text[96];

    if (detail_text != NULL && detail_size > 0U)
    {
        detail_text[0] = '\0';
    }

    cat1_modem_get_status_text(status_text, sizeof(status_text));

    if (strstr(status_text, "已联网") != NULL)
    {
        if (detail_text != NULL && detail_size > 0U)
        {
            rt_snprintf(detail_text, detail_size, "在线");
        }
        return 3;
    }

    if (strstr(status_text, "LTE已注册") != NULL ||
        strstr(status_text, "分组域已注册") != NULL ||
        strstr(status_text, "获取IP地址") != NULL ||
        strstr(status_text, "激活数据链路") != NULL ||
        strstr(status_text, "配置APN") != NULL)
    {
        if (detail_text != NULL && detail_size > 0U)
        {
            rt_snprintf(detail_text, detail_size, "注册");
        }
        return 2;
    }

    if (strstr(status_text, "启动入网流程") != NULL ||
        strstr(status_text, "AT客户端已连接") != NULL ||
        strstr(status_text, "检查SIM卡") != NULL ||
        strstr(status_text, "等待网络注册") != NULL ||
        strstr(status_text, "搜网中") != NULL ||
        strstr(status_text, "待首页启动") != NULL ||
        strstr(status_text, "关闭回显") != NULL ||
        strstr(status_text, "模组上电中") != NULL ||
        strstr(status_text, "准备重新上电") != NULL ||
        strstr(status_text, "断电重启中") != NULL)
    {
        if (detail_text != NULL && detail_size > 0U)
        {
            rt_snprintf(detail_text, detail_size, "搜网");
        }
        return 1;
    }

    if (strstr(status_text, "SIM未就绪") != NULL)
    {
        if (detail_text != NULL && detail_size > 0U)
        {
            rt_snprintf(detail_text, detail_size, "SIM");
        }
        return 1;
    }

    if (strstr(status_text, "注册超时") != NULL ||
        strstr(status_text, "模组无响应") != NULL ||
        strstr(status_text, "未获取到IP") != NULL ||
        strstr(status_text, "IP已丢失") != NULL ||
        strstr(status_text, "激活失败") != NULL)
    {
        if (detail_text != NULL && detail_size > 0U)
        {
            rt_snprintf(detail_text, detail_size, "异常");
        }
        return -1;
    }

    if (detail_text != NULL && detail_size > 0U)
    {
        rt_snprintf(detail_text, detail_size, "离线");
    }
    return 0;
}

static void ui_status_capture_snapshot(ui_status_bar_snapshot_t *snapshot)
{
    date_time_t current_time;
    bq27220_power_snapshot_t power_snapshot;
    weather_info_t current_weather = {0};
    bool use_fallback_time = false;
    char cat1_detail[16];

    if (snapshot == NULL)
    {
        return;
    }

    rt_memset(snapshot, 0, sizeof(*snapshot));
    snapshot->valid = true;

    rt_memset(&current_time, 0, sizeof(current_time));
    if (xiaozhi_time_get_current(&current_time) != RT_EOK)
    {
        use_fallback_time = true;
    }
    else if (current_time.year < 2026 ||
             current_time.month < 1 || current_time.month > 12 ||
             current_time.day < 1 || current_time.day > 31 ||
             current_time.hour < 0 || current_time.hour > 23 ||
             current_time.minute < 0 || current_time.minute > 59)
    {
        use_fallback_time = true;
    }

    if (use_fallback_time)
    {
        current_time.year = 2026;
        current_time.month = 1;
        current_time.day = 1;
        current_time.hour = 1;
        current_time.minute = 1;
        current_time.second = 0;
    }

    snapshot->time_valid = !use_fallback_time;
    snapshot->year = current_time.year;
    snapshot->month = current_time.month;
    snapshot->day = current_time.day;
    snapshot->hour = current_time.hour;
    snapshot->minute = current_time.minute;

    rt_memset(&current_weather, 0, sizeof(current_weather));
    if (xiaozhi_weather_peek(&current_weather) == RT_EOK && current_weather.last_update > 0)
    {
        snapshot->weather_available = true;
        snapshot->weather_temperature = current_weather.temperature;
    }
    else
    {
        snapshot->weather_available = false;
        snapshot->weather_temperature = INT32_MIN;
    }

    rt_memset(&power_snapshot, 0, sizeof(power_snapshot));
    bq27220_monitor_get_power_snapshot(&power_snapshot);
    if (power_snapshot.valid)
    {
        snapshot->battery_percent = (int)power_snapshot.battery_percent;
        snapshot->charge_state = power_snapshot.charging ? 1 : 0;
        snapshot->aw_charge_state = power_snapshot.aw_charge_state;
        snapshot->aw_fault_status = power_snapshot.aw_fault_status;
    }
    else
    {
        snapshot->battery_percent = (s_status_pending_battery_percent >= 0) ?
                                    s_status_pending_battery_percent :
                                    s_status_applied_battery_percent;
        snapshot->charge_state = (s_status_pending_charge >= 0) ?
                                 s_status_pending_charge :
                                 s_status_applied_charge;
        snapshot->aw_charge_state = 0U;
        snapshot->aw_fault_status = 0U;
    }

    snapshot->bt_visual_state = (int)ui_status_get_bluetooth_state();
    snapshot->active_link = net_manager_get_active_link();
    snapshot->bt_enabled = net_manager_bt_enabled();
    snapshot->net_4g_enabled = net_manager_4g_enabled();
    snapshot->network_visual_state = ui_status_get_cat1_visual_state(cat1_detail, sizeof(cat1_detail));

    if (snapshot->active_link == NET_MANAGER_LINK_BT_PAN)
    {
        rt_snprintf(snapshot->network_detail, sizeof(snapshot->network_detail), "在线");
    }
    else if (snapshot->net_4g_enabled)
    {
        rt_snprintf(snapshot->network_detail, sizeof(snapshot->network_detail), "%s", cat1_detail);
    }
    else
    {
        rt_snprintf(snapshot->network_detail, sizeof(snapshot->network_detail), "关闭");
    }
}

static bool ui_status_snapshot_equal(const ui_status_bar_snapshot_t *lhs,
                                     const ui_status_bar_snapshot_t *rhs)
{
    if (lhs == NULL || rhs == NULL)
    {
        return false;
    }

    return lhs->valid == rhs->valid &&
           lhs->time_valid == rhs->time_valid &&
           lhs->year == rhs->year &&
           lhs->month == rhs->month &&
           lhs->day == rhs->day &&
           lhs->hour == rhs->hour &&
           lhs->minute == rhs->minute &&
           lhs->weather_available == rhs->weather_available &&
           lhs->weather_temperature == rhs->weather_temperature &&
           lhs->battery_percent == rhs->battery_percent &&
           lhs->charge_state == rhs->charge_state &&
           lhs->aw_charge_state == rhs->aw_charge_state &&
           lhs->aw_fault_status == rhs->aw_fault_status &&
           lhs->bt_visual_state == rhs->bt_visual_state &&
           lhs->network_visual_state == rhs->network_visual_state &&
           lhs->active_link == rhs->active_link &&
           lhs->bt_enabled == rhs->bt_enabled &&
           lhs->net_4g_enabled == rhs->net_4g_enabled &&
           strcmp(lhs->network_detail, rhs->network_detail) == 0;
}

static void ui_status_refresh_connection_icons(bool force)
{
    net_manager_link_t active_link = net_manager_get_active_link();
    bool bt_enabled = net_manager_bt_enabled();
    bool net_4g_enabled = net_manager_4g_enabled();
    char cat1_detail[16];
    const char *network_text = NULL;
    size_t i;

    ui_status_get_cat1_visual_state(cat1_detail, sizeof(cat1_detail));

    if (active_link == NET_MANAGER_LINK_BT_PAN)
    {
        network_text = "在线";
    }
    else if (net_4g_enabled)
    {
        network_text = cat1_detail;
    }
    else
    {
        network_text = "关闭";
    }

    if (!force &&
        s_status_last_bt_icon_state == (bt_enabled ? 1 : 0) &&
        s_status_last_network_icon_state == (int)active_link &&
        s_status_last_net_4g_enabled == net_4g_enabled &&
        strcmp(s_status_last_network_text, network_text) == 0)
    {
        return;
    }

    s_status_last_bt_icon_state = bt_enabled ? 1 : 0;
    s_status_last_network_icon_state = (int)active_link;
    s_status_last_net_4g_enabled = net_4g_enabled;
    rt_snprintf(s_status_last_network_text, sizeof(s_status_last_network_text), "%s", network_text);

    for (i = 0; i < sizeof(s_screen_refs) / sizeof(s_screen_refs[0]); ++i)
    {
        xiaozhi_home_screen_refs_t *refs = &s_screen_refs[i].refs;

        if (!s_screen_refs[i].used)
        {
            continue;
        }

        if (refs->bluetooth_icon != NULL)
        {
            lv_img_set_src(refs->bluetooth_icon, &ble_icon_img);
            ui_status_set_object_hidden(refs->bluetooth_icon, false);
            lv_obj_set_style_opa(refs->bluetooth_icon,
                                 bt_enabled ? LV_OPA_COVER : LV_OPA_50,
                                 0);
        }

        if (refs->network_icon != NULL)
        {
            lv_img_set_src(refs->network_icon, &network_icon_img);
            ui_status_set_object_hidden(refs->network_icon, false);
            lv_obj_set_style_opa(refs->network_icon,
                                 (active_link == NET_MANAGER_LINK_BT_PAN || net_4g_enabled) ? LV_OPA_COVER : LV_OPA_50,
                                 0);
        }

        if (refs->ec800_status_label != NULL)
        {
            ui_status_set_label_text(refs->ec800_status_label, "");
            ui_status_set_object_hidden(refs->ec800_status_label, true);
        }
    }
}

static void ui_status_hide_toast(void)
{
    if (ui_status_detail_is_active())
    {
        return;
    }

    if (s_status_panel.toast != NULL)
    {
        lv_obj_add_flag(s_status_panel.toast, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_status_panel.toast_timer != NULL)
    {
        lv_timer_pause(s_status_panel.toast_timer);
    }
}

static ui_status_bluetooth_state_t ui_status_get_bluetooth_state(void)
{
    if (!s_status_panel.suppress_connected_state && ui_bt_connection_active())
    {
        return UI_STATUS_BLUETOOTH_CONNECTED;
    }

    if (s_status_panel.waiting_requested)
    {
        return UI_STATUS_BLUETOOTH_WAITING;
    }

    return UI_STATUS_BLUETOOTH_DISCONNECTED;
}

static void ui_status_refresh_charging_icons(void)
{
    size_t i;

    if (s_status_pending_charge < 0)
    {
        return;
    }

    s_status_applied_charge = s_status_pending_charge;
    s_status_panel.charging = s_status_applied_charge != 0;

    for (i = 0; i < sizeof(s_screen_refs) / sizeof(s_screen_refs[0]); ++i)
    {
        lv_obj_t *icon;
        bool hidden_now;

        if (!s_screen_refs[i].used)
        {
            continue;
        }

        icon = s_screen_refs[i].refs.standby_charging_icon;
        if (icon == NULL)
        {
            continue;
        }

        hidden_now = lv_obj_has_flag(icon, LV_OBJ_FLAG_HIDDEN);
        if (s_status_panel.charging)
        {
            if (hidden_now)
            {
                lv_obj_clear_flag(icon, LV_OBJ_FLAG_HIDDEN);
            }
        }
        else
        {
            if (!hidden_now)
            {
                lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

static void ui_status_refresh_battery_percent(void)
{
    size_t i;
    char battery_text[8];
    int percent = s_status_pending_battery_percent;

    if (percent < 0)
    {
        return;
    }
    if (percent > 100)
    {
        percent = 100;
    }

    s_status_applied_battery_percent = percent;
    rt_snprintf(battery_text, sizeof(battery_text), "%d%%", percent);

    for (i = 0; i < sizeof(s_screen_refs) / sizeof(s_screen_refs[0]); ++i)
    {
        lv_obj_t *label;
        lv_obj_t *fill;
        lv_coord_t target_fill_w;

        if (!s_screen_refs[i].used)
        {
            continue;
        }

        label = s_screen_refs[i].refs.battery_percent_label;
        if (label != NULL)
        {
            const char *old_text = lv_label_get_text(label);

            if ((old_text == NULL) || (strcmp(old_text, battery_text) != 0))
            {
                lv_label_set_text(label, battery_text);
            }
        }

        fill = s_screen_refs[i].refs.battery_arc;
        if (fill != NULL)
        {
            target_fill_w = ui_px_w((percent <= 0) ? 0 : ((percent * 30 + 99) / 100));
            if (lv_obj_get_width(fill) != target_fill_w)
            {
                lv_obj_set_width(fill, target_fill_w);
            }
        }
    }
}

static void ui_status_async_refresh_charge_cb(void *user_data)
{
    LV_UNUSED(user_data);
    ui_status_refresh_charging_icons();
}

static void ui_status_async_refresh_battery_cb(void *user_data)
{
    LV_UNUSED(user_data);
    ui_status_refresh_battery_percent();
}

static void ui_status_update_slider_visual(lv_obj_t *track,
                                           lv_obj_t *fill,
                                           lv_obj_t *knob,
                                           uint8_t min_step,
                                           uint8_t max_step,
                                           uint8_t current_step)
{
    int32_t track_w;
    int32_t fill_w;

    if (track == NULL || fill == NULL)
    {
        return;
    }

    if (current_step < min_step)
    {
        current_step = min_step;
    }
    if (current_step > max_step)
    {
        current_step = max_step;
    }

    track_w = lv_obj_get_width(track);
    if ((max_step > min_step) && (track_w > 0))
    {
        int32_t numerator = (int32_t)(current_step - min_step) * track_w;
        int32_t denominator = (int32_t)(max_step - min_step);

        fill_w = (numerator + denominator / 2) / denominator;
    }
    else
    {
        fill_w = track_w;
    }

    if (fill_w < 0)
    {
        fill_w = 0;
    }
    if (fill_w > track_w)
    {
        fill_w = track_w;
    }

    lv_obj_set_width(fill, fill_w);
    if (knob != NULL)
    {
        lv_obj_add_flag(knob, LV_OBJ_FLAG_HIDDEN);
    }
}

static bool ui_status_accept_interaction(void)
{
    return ui_accept_debounced_tick(&s_status_panel.last_input_tick,
                                    UI_STATUS_INTERACTION_DEBOUNCE_MS);
}

static void ui_status_update_panel_visuals(void)
{
    char value_text[16];
    ui_status_bluetooth_state_t bt_state = ui_status_get_bluetooth_state();
    bool bt_pairing_enabled = ui_bt_pairing_enabled();
    bool network_ready = ui_bt_network_ready();
    bool bt_enabled = net_manager_bt_enabled();
    bool net_4g_enabled = net_manager_4g_enabled();
    const char *bt_subtitle;
    const char *network_subtitle;

    if (!s_status_panel.bluetooth_toggle_initialized)
    {
        s_status_panel.bluetooth_enabled = bt_enabled;
        s_status_panel.bluetooth_toggle_initialized = true;
    }

    if (!s_status_panel.network_toggle_initialized)
    {
        s_status_panel.network_enabled = net_4g_enabled;
        s_status_panel.network_toggle_initialized = true;
    }

    s_status_panel.bluetooth_enabled = bt_enabled;
    s_status_panel.network_enabled = net_4g_enabled;

    if (s_status_panel.brightness_value_label != NULL)
    {
        rt_snprintf(value_text, sizeof(value_text), "%u / 5", s_status_panel.brightness_steps);
        lv_label_set_text(s_status_panel.brightness_value_label, value_text);
    }

    if (s_status_panel.volume_value_label != NULL)
    {
        rt_snprintf(value_text, sizeof(value_text), "%u / 10", s_status_panel.volume_steps);
        lv_label_set_text(s_status_panel.volume_value_label, value_text);
    }

    ui_status_update_slider_visual(s_status_panel.brightness_track,
                                   s_status_panel.brightness_fill,
                                   s_status_panel.brightness_knob,
                                   1U,
                                   5U,
                                   s_status_panel.brightness_steps);
    ui_status_update_slider_visual(s_status_panel.volume_track,
                                   s_status_panel.volume_fill,
                                   s_status_panel.volume_knob,
                                   0U,
                                   10U,
                                   s_status_panel.volume_steps);

    if (s_status_panel.bluetooth_card != NULL)
    {
        ui_apply_basic_object_style(s_status_panel.bluetooth_card, false, 0, 2);

        if (s_status_panel.bluetooth_title_label != NULL)
        {
            lv_label_set_text(s_status_panel.bluetooth_title_label, ui_i18n_pick("蓝牙", "Bluetooth"));
        }
        if (s_status_panel.bluetooth_subtitle_label != NULL)
        {
            switch (bt_state)
            {
            case UI_STATUS_BLUETOOTH_CONNECTED:
                bt_subtitle = ui_i18n_pick("已连接", "Connected");
                break;
            case UI_STATUS_BLUETOOTH_WAITING:
                bt_subtitle = ui_i18n_pick("连接中", "Connecting");
                break;
            default:
                bt_subtitle = bt_enabled ? (bt_pairing_enabled ? ui_i18n_pick("已开启", "Enabled") : ui_i18n_pick("待连接", "Idle")) : ui_i18n_pick("未开启", "Disabled");
                break;
            }
            lv_label_set_text(s_status_panel.bluetooth_subtitle_label, bt_subtitle);
        }
        if (s_status_panel.bluetooth_value_label != NULL)
        {
            lv_label_set_text(s_status_panel.bluetooth_value_label,
                              s_status_panel.bluetooth_enabled ? ui_i18n_pick("开", "On") : ui_i18n_pick("关", "Off"));
        }
    }

    if (s_status_panel.network_card != NULL)
    {
        ui_apply_basic_object_style(s_status_panel.network_card, false, 0, 2);

        if (s_status_panel.network_title_label != NULL)
        {
            lv_label_set_text(s_status_panel.network_title_label, "4G");
        }
        if (s_status_panel.network_subtitle_label != NULL)
        {
            if (!net_4g_enabled)
            {
                network_subtitle = ui_i18n_pick("未开启", "Disabled");
            }
            else
            {
                network_subtitle = network_ready ? ui_i18n_pick("已联网", "Online") : ui_i18n_pick("未联网", "Offline");
            }
            lv_label_set_text(s_status_panel.network_subtitle_label, network_subtitle);
        }
        if (s_status_panel.network_value_label != NULL)
        {
            lv_label_set_text(s_status_panel.network_value_label,
                              s_status_panel.network_enabled ? ui_i18n_pick("开", "On") : ui_i18n_pick("关", "Off"));
        }
    }
}

static void ui_status_toast_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    ui_status_hide_toast();
}

static void ui_status_show_toast(const char *text)
{
    if (ui_status_detail_is_active())
    {
        return;
    }

    if (s_status_panel.root == NULL ||
        s_status_panel.toast == NULL ||
        s_status_panel.toast_label == NULL ||
        lv_obj_has_flag(s_status_panel.root, LV_OBJ_FLAG_HIDDEN))
    {
        return;
    }

    lv_label_set_text(s_status_panel.toast_label, text != NULL ? text : "");
    lv_obj_clear_flag(s_status_panel.toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_status_panel.toast);

    if (s_status_panel.toast_timer != NULL)
    {
        lv_timer_set_repeat_count(s_status_panel.toast_timer, 1);
        lv_timer_resume(s_status_panel.toast_timer);
        lv_timer_reset(s_status_panel.toast_timer);
    }
}

static void ui_status_hide_confirm(void)
{
    s_status_panel.confirm_visible = false;

    if (ui_status_detail_is_active())
    {
        return;
    }

    if (s_status_panel.confirm != NULL)
    {
        lv_obj_add_flag(s_status_panel.confirm, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ui_status_close_panel(void)
{
    ui_status_hide_confirm();
    ui_status_hide_toast();

    if (s_status_panel.root != NULL)
    {
        lv_obj_add_flag(s_status_panel.root, LV_OBJ_FLAG_HIDDEN);

        if (s_status_panel.host_screen != NULL)
        {
            lv_obj_update_layout(s_status_panel.host_screen);
            lv_obj_invalidate(s_status_panel.host_screen);
            lv_refr_now(NULL);
        }
    }
}

static void ui_status_sync_timer_cb(lv_timer_t *timer)
{
    int pending;
    uint8_t brightness = 50U;
    uint8_t volume = 8U;
    bool bt_connected;

    LV_UNUSED(timer);

    if (s_status_panel.lcd_device == RT_NULL)
    {
        s_status_panel.lcd_device = rt_device_find(LCD_DEVICE_NAME);
    }

    if (!ui_status_backlight_read(&brightness) && s_status_panel.lcd_device != RT_NULL)
    {
        rt_device_control(s_status_panel.lcd_device, RTGRAPHIC_CTRL_GET_BRIGHTNESS, &brightness);
    }

    pending = s_status_pending_brightness;
    if (pending >= 0)
    {
        brightness = (uint8_t)pending;
    }

    pending = s_status_pending_volume;
    if (pending >= 0)
    {
        volume = (uint8_t)pending;
    }
    else
    {
        volume = audio_server_get_private_volume(AUDIO_TYPE_LOCAL_MUSIC);
    }

    pending = s_status_pending_battery_percent;
    if (pending > 100)
    {
        s_status_pending_battery_percent = 100;
    }
    else if (pending < 0)
    {
        s_status_pending_battery_percent = pending;
    }

    s_status_panel.brightness_steps = ui_status_brightness_to_steps(brightness);
    s_status_panel.volume_steps = ui_status_volume_to_steps(volume);

    bt_connected = ui_bt_connection_active();
    if (bt_connected && !s_status_panel.last_bt_connected)
    {
        s_status_panel.waiting_requested = false;
        s_status_panel.suppress_connected_state = false;
        ui_status_show_toast(ui_i18n_pick("蓝牙连接成功", "Bluetooth connected"));
    }
    else if (!bt_connected && s_status_panel.last_bt_connected)
    {
        s_status_panel.suppress_connected_state = false;
    }

    s_status_panel.last_bt_connected = bt_connected;

    ui_status_refresh_charging_icons();
    ui_status_refresh_battery_percent();
    ui_status_refresh_connection_icons(false);
    ui_status_update_panel_visuals();
}

static void ui_status_sync_now(void)
{
    ui_status_sync_timer_cb(NULL);
}

static void ui_status_confirm_yes_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_RELEASED)
    {
        return;
    }
    if (!ui_status_accept_interaction())
    {
        return;
    }

    s_status_panel.waiting_requested = false;
    s_status_panel.suppress_connected_state = true;
    s_status_panel.waiting_requested = false;
    s_status_panel.suppress_connected_state = false;
    ui_status_hide_confirm();
    ui_status_request_detail_rebuild();
}

static void ui_status_confirm_no_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_RELEASED)
    {
        return;
    }
    if (!ui_status_accept_interaction())
    {
        return;
    }

    ui_status_hide_confirm();
    ui_status_request_detail_rebuild();
}

static void ui_status_request_detail_rebuild(void)
{
    if (!ui_status_detail_is_active())
    {
        ui_status_update_panel_visuals();
        return;
    }

    if (s_status_detail_reload_pending)
    {
        return;
    }

    if (!ui_accept_debounced_tick(&s_status_detail_reload_tick,
                                  UI_STATUS_DETAIL_RELOAD_DEBOUNCE_MS))
    {
        return;
    }

    s_status_detail_reload_pending = true;
    lv_async_call(ui_status_detail_reload_async_cb, NULL);
}

static void ui_status_detail_reload_async_cb(void *user_data)
{
    LV_UNUSED(user_data);

    if (ui_status_detail_is_active())
    {
        ui_runtime_reload(UI_SCREEN_STATUS_DETAIL);
    }

    s_status_detail_reload_pending = false;
}

static void ui_status_toggle_card_event_cb(lv_event_t *e)
{
    ui_status_toggle_kind_t kind = (ui_status_toggle_kind_t)(uintptr_t)lv_event_get_user_data(e);

    if (lv_event_get_code(e) != LV_EVENT_RELEASED)
    {
        return;
    }
    if (!ui_status_accept_interaction())
    {
        return;
    }

    if (kind == UI_STATUS_TOGGLE_BLUETOOTH)
    {
        if (net_manager_bt_enabled())
        {
            net_manager_request_4g_mode();
        }
        else
        {
            net_manager_request_bt_mode();
        }
        s_status_panel.bluetooth_enabled = net_manager_bt_enabled();
        s_status_panel.network_enabled = net_manager_4g_enabled();
        s_status_panel.bluetooth_toggle_initialized = true;
        s_status_panel.network_toggle_initialized = true;
    }
    else
    {
        if (net_manager_4g_enabled())
        {
            net_manager_request_bt_mode();
        }
        else
        {
            net_manager_request_4g_mode();
        }
        s_status_panel.bluetooth_enabled = net_manager_bt_enabled();
        s_status_panel.network_enabled = net_manager_4g_enabled();
        s_status_panel.bluetooth_toggle_initialized = true;
        s_status_panel.network_toggle_initialized = true;
    }

    ui_status_update_panel_visuals();
    ui_status_refresh_connection_icons(true);
}

static lv_obj_t *ui_status_create_touch_zone(lv_obj_t *parent,
                                             int32_t x,
                                             int32_t y,
                                             int32_t w,
                                             int32_t h,
                                             lv_event_cb_t event_cb,
                                             void *user_data)
{
    lv_obj_t *zone = lv_obj_create(parent);

    lv_obj_remove_style_all(zone);
    lv_obj_set_pos(zone, ui_px_x(x), ui_px_y(y));
    lv_obj_set_size(zone, ui_px_w(w), ui_px_h(h));
    lv_obj_set_style_bg_opa(zone, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(zone, 0, 0);
    lv_obj_add_flag(zone, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(zone, LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    if (event_cb != NULL)
    {
        lv_obj_add_event_cb(zone, event_cb, LV_EVENT_RELEASED, user_data);
    }

    return zone;
}

static uint8_t ui_status_slider_step_from_touch(lv_obj_t *zone,
                                                lv_indev_t *indev,
                                                uint8_t min_step,
                                                uint8_t max_step)
{
    lv_area_t area;
    lv_point_t point;
    int32_t width;
    int32_t rel_x;
    int32_t range;
    int32_t step;

    if (zone == NULL || indev == NULL)
    {
        return min_step;
    }

    lv_obj_get_coords(zone, &area);
    lv_indev_get_point(indev, &point);
    width = area.x2 - area.x1 + 1;
    if (width <= 1)
    {
        return min_step;
    }

    rel_x = point.x - area.x1;
    if (rel_x < 0)
    {
        rel_x = 0;
    }
    if (rel_x >= width)
    {
        rel_x = width - 1;
    }

    range = (int32_t)(max_step - min_step);
    if (range <= 0)
    {
        return min_step;
    }

    step = (rel_x * range + width / 2) / width + min_step;
    if (step > max_step)
    {
        step = max_step;
    }

    return (uint8_t)step;
}

static void ui_status_slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target_obj(e);
    ui_status_slider_kind_t kind = (ui_status_slider_kind_t)(uintptr_t)lv_event_get_user_data(e);
    lv_indev_t *indev = lv_event_get_indev(e);
    uint8_t value;
    lv_event_code_t code = lv_event_get_code(e);

    if ((code != LV_EVENT_PRESSED) &&
        (code != LV_EVENT_PRESSING) &&
        (code != LV_EVENT_RELEASED) &&
        (code != LV_EVENT_CLICKED))
    {
        return;
    }

    if (slider == NULL || indev == NULL)
    {
        return;
    }

    if (kind == UI_STATUS_SLIDER_BRIGHTNESS)
    {
        uint8_t actual;
        bool wrote_backlight;

        value = ui_status_slider_step_from_touch(slider, indev, 1U, 5U);
        actual = ui_status_steps_to_brightness(value);
        s_status_panel.brightness_steps = value;
        s_status_pending_brightness = actual;
        wrote_backlight = false;
        if (s_status_panel.lcd_backlight_device == RT_NULL)
        {
            s_status_panel.lcd_backlight_device = rt_device_find(LCD_BACKLIGHT_DEVICE_NAME);
        }
        if (s_status_panel.lcd_backlight_device != RT_NULL)
        {
            ui_status_backlight_write(actual);
            wrote_backlight = true;
        }

        if (!wrote_backlight)
        {
            if (s_status_panel.lcd_device == RT_NULL)
            {
                s_status_panel.lcd_device = rt_device_find(LCD_DEVICE_NAME);
            }
            if (s_status_panel.lcd_device != RT_NULL)
            {
                rt_device_control(s_status_panel.lcd_device, RTGRAPHIC_CTRL_SET_BRIGHTNESS, &actual);
            }
        }
    }
    else
    {
        uint8_t actual;

        value = ui_status_slider_step_from_touch(slider, indev, 0U, 10U);
        actual = ui_status_steps_to_volume(value);
        s_status_panel.volume_steps = value;
        s_status_pending_volume = actual;
        audio_server_set_private_volume(AUDIO_TYPE_LOCAL_MUSIC, actual);
    }

    ui_status_request_detail_rebuild();
}

static void ui_status_mask_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    if (s_status_panel.confirm != NULL && !lv_obj_has_flag(s_status_panel.confirm, LV_OBJ_FLAG_HIDDEN))
    {
        ui_status_hide_confirm();
    }
    else
    {
        ui_status_close_panel();
    }
}

static void ui_status_root_delete_event_cb(lv_event_t *e)
{
    rt_device_t lcd_device;
    rt_device_t lcd_backlight_device;
    lv_timer_t *sync_timer;
    lv_timer_t *toast_timer;
    uint8_t brightness_steps;
    uint8_t volume_steps;
    bool charging;
    bool confirm_visible;
    bool waiting_requested;
    bool suppress_connected_state;
    bool last_bt_connected;
    bool bluetooth_enabled;
    bool network_enabled;
    bool bluetooth_toggle_initialized;
    bool network_toggle_initialized;

    if (lv_event_get_target_obj(e) != s_status_panel.root)
    {
        return;
    }

    lcd_device = s_status_panel.lcd_device;
    lcd_backlight_device = s_status_panel.lcd_backlight_device;
    sync_timer = s_status_panel.sync_timer;
    toast_timer = s_status_panel.toast_timer;
    brightness_steps = s_status_panel.brightness_steps;
    volume_steps = s_status_panel.volume_steps;
    charging = s_status_panel.charging;
    confirm_visible = s_status_panel.confirm_visible;
    waiting_requested = s_status_panel.waiting_requested;
    suppress_connected_state = s_status_panel.suppress_connected_state;
    last_bt_connected = s_status_panel.last_bt_connected;
    bluetooth_enabled = s_status_panel.bluetooth_enabled;
    network_enabled = s_status_panel.network_enabled;
    bluetooth_toggle_initialized = s_status_panel.bluetooth_toggle_initialized;
    network_toggle_initialized = s_status_panel.network_toggle_initialized;

    memset(&s_status_panel, 0, sizeof(s_status_panel));
    s_status_panel.lcd_device = lcd_device;
    s_status_panel.lcd_backlight_device = lcd_backlight_device;
    s_status_panel.sync_timer = sync_timer;
    s_status_panel.toast_timer = toast_timer;
    s_status_panel.brightness_steps = brightness_steps;
    s_status_panel.volume_steps = volume_steps;
    s_status_panel.charging = charging;
    s_status_panel.confirm_visible = confirm_visible;
    s_status_panel.waiting_requested = waiting_requested;
    s_status_panel.suppress_connected_state = suppress_connected_state;
    s_status_panel.last_bt_connected = last_bt_connected;
    s_status_panel.bluetooth_enabled = bluetooth_enabled;
    s_status_panel.network_enabled = network_enabled;
    s_status_panel.bluetooth_toggle_initialized = bluetooth_toggle_initialized;
    s_status_panel.network_toggle_initialized = network_toggle_initialized;
}

static void ui_status_preserve_runtime_state(rt_device_t *lcd_device,
                                             rt_device_t *lcd_backlight_device,
                                             lv_timer_t **sync_timer,
                                             lv_timer_t **toast_timer,
                                             uint8_t *brightness_steps,
                                             uint8_t *volume_steps,
                                             bool *charging,
                                             bool *confirm_visible,
                                             bool *waiting_requested,
                                             bool *suppress_connected_state,
                                             bool *last_bt_connected,
                                             bool *bluetooth_enabled,
                                             bool *network_enabled,
                                             bool *bluetooth_toggle_initialized,
                                             bool *network_toggle_initialized)
{
    if (lcd_device != NULL) *lcd_device = s_status_panel.lcd_device;
    if (lcd_backlight_device != NULL) *lcd_backlight_device = s_status_panel.lcd_backlight_device;
    if (sync_timer != NULL) *sync_timer = s_status_panel.sync_timer;
    if (toast_timer != NULL) *toast_timer = s_status_panel.toast_timer;
    if (brightness_steps != NULL) *brightness_steps = s_status_panel.brightness_steps;
    if (volume_steps != NULL) *volume_steps = s_status_panel.volume_steps;
    if (charging != NULL) *charging = s_status_panel.charging;
    if (confirm_visible != NULL) *confirm_visible = s_status_panel.confirm_visible;
    if (waiting_requested != NULL) *waiting_requested = s_status_panel.waiting_requested;
    if (suppress_connected_state != NULL) *suppress_connected_state = s_status_panel.suppress_connected_state;
    if (last_bt_connected != NULL) *last_bt_connected = s_status_panel.last_bt_connected;
    if (bluetooth_enabled != NULL) *bluetooth_enabled = s_status_panel.bluetooth_enabled;
    if (network_enabled != NULL) *network_enabled = s_status_panel.network_enabled;
    if (bluetooth_toggle_initialized != NULL) *bluetooth_toggle_initialized = s_status_panel.bluetooth_toggle_initialized;
    if (network_toggle_initialized != NULL) *network_toggle_initialized = s_status_panel.network_toggle_initialized;
}

static void ui_status_restore_runtime_state(rt_device_t lcd_device,
                                            rt_device_t lcd_backlight_device,
                                            lv_timer_t *sync_timer,
                                            lv_timer_t *toast_timer,
                                            uint8_t brightness_steps,
                                            uint8_t volume_steps,
                                            bool charging,
                                            bool confirm_visible,
                                            bool waiting_requested,
                                            bool suppress_connected_state,
                                            bool last_bt_connected,
                                            bool bluetooth_enabled,
                                            bool network_enabled,
                                            bool bluetooth_toggle_initialized,
                                            bool network_toggle_initialized)
{
    memset(&s_status_panel, 0, sizeof(s_status_panel));
    s_status_panel.lcd_device = lcd_device;
    s_status_panel.lcd_backlight_device = lcd_backlight_device;
    s_status_panel.sync_timer = sync_timer;
    s_status_panel.toast_timer = toast_timer;
    s_status_panel.brightness_steps = brightness_steps;
    s_status_panel.volume_steps = volume_steps;
    s_status_panel.charging = charging;
    s_status_panel.confirm_visible = confirm_visible;
    s_status_panel.waiting_requested = waiting_requested;
    s_status_panel.suppress_connected_state = suppress_connected_state;
    s_status_panel.last_bt_connected = last_bt_connected;
    s_status_panel.bluetooth_enabled = bluetooth_enabled;
    s_status_panel.network_enabled = network_enabled;
    s_status_panel.bluetooth_toggle_initialized = bluetooth_toggle_initialized;
    s_status_panel.network_toggle_initialized = network_toggle_initialized;
}

static void ui_status_create_slider_visual(lv_obj_t *parent,
                                           int32_t x,
                                           int32_t y,
                                           int32_t w,
                                           uint8_t min_step,
                                           uint8_t max_step,
                                           uint8_t current_step,
                                           ui_status_slider_kind_t kind)
{
    lv_obj_t *track;
    lv_obj_t *fill;
    const lv_coord_t track_h = ui_px_h(12);

    track = lv_obj_create(parent);
    ui_apply_basic_object_style(track, false, ui_px_h(6), 2);
    lv_obj_set_pos(track, ui_px_x(x), ui_px_y(y));
    lv_obj_set_size(track, ui_px_w(w), track_h);
    lv_obj_clear_flag(track, LV_OBJ_FLAG_CLICKABLE);

    fill = lv_obj_create(track);
    ui_apply_basic_object_style(fill, true, ui_px_h(6), 0);
    lv_obj_set_pos(fill, 0, 0);
    lv_obj_set_size(fill, ui_px_w(10), track_h);
    lv_obj_clear_flag(fill, LV_OBJ_FLAG_CLICKABLE);

    if (kind == UI_STATUS_SLIDER_BRIGHTNESS)
    {
        s_status_panel.brightness_track = track;
        s_status_panel.brightness_fill = fill;
        s_status_panel.brightness_knob = NULL;
    }
    else
    {
        s_status_panel.volume_track = track;
        s_status_panel.volume_fill = fill;
        s_status_panel.volume_knob = NULL;
    }

    ui_status_update_slider_visual(track, fill, NULL, min_step, max_step, current_step);

    {
        lv_obj_t *touch_zone = ui_status_create_touch_zone(parent,
                                                           x,
                                                           y - 14,
                                                           w,
                                                           44,
                                                           NULL,
                                                           NULL);
        lv_obj_add_event_cb(touch_zone, ui_status_slider_event_cb, LV_EVENT_PRESSED, (void *)(uintptr_t)kind);
        lv_obj_add_event_cb(touch_zone, ui_status_slider_event_cb, LV_EVENT_PRESSING, (void *)(uintptr_t)kind);
        lv_obj_add_event_cb(touch_zone, ui_status_slider_event_cb, LV_EVENT_RELEASED, (void *)(uintptr_t)kind);
        lv_obj_add_event_cb(touch_zone, ui_status_slider_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)kind);
        lv_obj_move_foreground(touch_zone);
    }
}

static void ui_status_build_panel_widgets(lv_obj_t *root,
                                          lv_obj_t *parent,
                                          int32_t panel_x,
                                          int32_t panel_y,
                                          int32_t panel_w)
{
    lv_obj_t *panel;
    lv_obj_t *line;
    lv_obj_t *touch_zone;
    int32_t inner_x;
    int32_t inner_w;
    int32_t value_x;
    int32_t slider_w;
    int32_t status_gap;
    int32_t status_card_w;
    int32_t status_right_x;

    if (root == NULL || parent == NULL)
    {
        return;
    }

    panel = lv_obj_create(parent);
    s_status_panel.panel = panel;
    ui_apply_basic_object_style(panel, false, 0, 2);
    lv_obj_set_style_border_side(panel,
                                 LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT | LV_BORDER_SIDE_BOTTOM,
                                 0);
    lv_obj_set_pos(panel, ui_px_x(panel_x), ui_px_y(panel_y));
    lv_obj_set_size(panel, ui_px_w(panel_w), ui_px_h(314));
    inner_x = 18;
    inner_w = panel_w - (inner_x * 2);
    value_x = inner_x + inner_w - 92;
    slider_w = inner_w;
    status_gap = 12;
    status_card_w = (inner_w - status_gap) / 2;
    status_right_x = inner_x + status_card_w + status_gap;

    ui_create_label(panel,
                    ui_i18n_pick("设备控制", "Device Control"),
                    inner_x,
                    16,
                    180,
                    28,
                    26,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    false);
    ui_create_label(panel,
                    ui_i18n_pick("亮度、音量、蓝牙和 4G", "Brightness, volume, Bluetooth, and 4G"),
                    panel_w - 220,
                    18,
                    202,
                    24,
                    18,
                    LV_TEXT_ALIGN_RIGHT,
                    false,
                    false);

    line = lv_obj_create(panel);
    ui_apply_basic_object_style(line, true, 0, 0);
    lv_obj_set_pos(line, ui_px_x(inner_x), ui_px_y(52));
    lv_obj_set_size(line, ui_px_w(inner_w), ui_px_h(2));

    ui_create_label(panel,
                    ui_i18n_pick("屏幕亮度", "Brightness"),
                    inner_x,
                    74,
                    160,
                    28,
                    23,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    false);
    s_status_panel.brightness_value_label = ui_create_label(panel,
                                                            "3 / 5",
                                                            value_x,
                                                            74,
                                                            92,
                                                            28,
                                                            23,
                                                            LV_TEXT_ALIGN_RIGHT,
                                                            false,
                                                            false);
    s_status_panel.brightness_slider = NULL;
    ui_status_create_slider_visual(panel,
                                   inner_x,
                                   108,
                                   slider_w,
                                   1U,
                                   5U,
                                   s_status_panel.brightness_steps,
                                   UI_STATUS_SLIDER_BRIGHTNESS);

    ui_create_label(panel,
                    ui_i18n_pick("声音音量", "Volume"),
                    inner_x,
                    152,
                    160,
                    28,
                    23,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    false);
    s_status_panel.volume_value_label = ui_create_label(panel,
                                                        "5 / 10",
                                                        value_x,
                                                        152,
                                                        92,
                                                        28,
                                                        23,
                                                        LV_TEXT_ALIGN_RIGHT,
                                                        false,
                                                        false);
    s_status_panel.volume_slider = NULL;
    ui_status_create_slider_visual(panel,
                                   inner_x,
                                   186,
                                   slider_w,
                                   0U,
                                   10U,
                                   s_status_panel.volume_steps,
                                   UI_STATUS_SLIDER_VOLUME);

    s_status_panel.bluetooth_card = ui_create_card(panel, inner_x, 236, status_card_w, 60, UI_SCREEN_NONE, false, 0);
    s_status_panel.bluetooth_title_label = ui_create_label(s_status_panel.bluetooth_card,
                                                           ui_i18n_pick("蓝牙", "Bluetooth"),
                                                           14,
                                                           9,
                                                           96,
                                                           24,
                                                           23,
                                                           LV_TEXT_ALIGN_LEFT,
                                                           false,
                                                           false);
    s_status_panel.bluetooth_subtitle_label = ui_create_label(s_status_panel.bluetooth_card,
                                                              ui_i18n_pick("未开启", "Disabled"),
                                                              14,
                                                              33,
                                                              110,
                                                              22,
                                                              16,
                                                              LV_TEXT_ALIGN_LEFT,
                                                              false,
                                                              false);
    s_status_panel.bluetooth_value_label = ui_create_label(s_status_panel.bluetooth_card,
                                                           ui_i18n_pick("关", "Off"),
                                                           status_card_w - 56,
                                                           18,
                                                           36,
                                                           24,
                                                           26,
                                                           LV_TEXT_ALIGN_CENTER,
                                                           false,
                                                           false);
    touch_zone = ui_status_create_touch_zone(panel,
                                             inner_x,
                                             236,
                                             status_card_w,
                                             60,
                                             ui_status_toggle_card_event_cb,
                                             (void *)(uintptr_t)UI_STATUS_TOGGLE_BLUETOOTH);
    lv_obj_move_foreground(touch_zone);

    s_status_panel.network_card = ui_create_card(panel, status_right_x, 236, status_card_w, 60, UI_SCREEN_NONE, false, 0);
    s_status_panel.network_title_label = ui_create_label(s_status_panel.network_card,
                                                         "4G",
                                                         14,
                                                         9,
                                                         96,
                                                         24,
                                                         23,
                                                         LV_TEXT_ALIGN_LEFT,
                                                         false,
                                                         false);
    s_status_panel.network_subtitle_label = ui_create_label(s_status_panel.network_card,
                                                            ui_i18n_pick("未联网", "Offline"),
                                                            14,
                                                            33,
                                                            110,
                                                            22,
                                                            16,
                                                            LV_TEXT_ALIGN_LEFT,
                                                            false,
                                                            false);
    s_status_panel.network_value_label = ui_create_label(s_status_panel.network_card,
                                                         ui_i18n_pick("关", "Off"),
                                                         status_card_w - 56,
                                                         18,
                                                         36,
                                                         24,
                                                         26,
                                                         LV_TEXT_ALIGN_CENTER,
                                                         false,
                                                         false);
    touch_zone = ui_status_create_touch_zone(panel,
                                             status_right_x,
                                             236,
                                             status_card_w,
                                             60,
                                             ui_status_toggle_card_event_cb,
                                             (void *)(uintptr_t)UI_STATUS_TOGGLE_NETWORK);
    lv_obj_move_foreground(touch_zone);
}

static void ui_status_build_overlay(lv_obj_t *screen)
{
    rt_device_t lcd_device;
    rt_device_t lcd_backlight_device;
    lv_timer_t *sync_timer;
    lv_timer_t *toast_timer;
    uint8_t brightness_steps;
    uint8_t volume_steps;
    bool charging;
    bool confirm_visible;
    bool waiting_requested;
    bool suppress_connected_state;
    bool last_bt_connected;
    bool bluetooth_enabled;
    bool network_enabled;
    bool bluetooth_toggle_initialized;
    bool network_toggle_initialized;

    if (screen == NULL)
    {
        return;
    }

    if (s_status_panel.root != NULL && s_status_panel.host_screen == screen)
    {
        return;
    }

    if (s_status_panel.root != NULL)
    {
        ui_status_preserve_runtime_state(&lcd_device,
                                         &lcd_backlight_device,
                                         &sync_timer,
                                         &toast_timer,
                                         &brightness_steps,
                                         &volume_steps,
                                         &charging,
                                         &confirm_visible,
                                         &waiting_requested,
                                         &suppress_connected_state,
                                         &last_bt_connected,
                                         &bluetooth_enabled,
                                         &network_enabled,
                                         &bluetooth_toggle_initialized,
                                         &network_toggle_initialized);

        lv_obj_delete(s_status_panel.root);
        ui_status_restore_runtime_state(lcd_device,
                                        lcd_backlight_device,
                                        sync_timer,
                                        toast_timer,
                                        brightness_steps,
                                        volume_steps,
                                        charging,
                                        confirm_visible,
                                        waiting_requested,
                                        suppress_connected_state,
                                        last_bt_connected,
                                        bluetooth_enabled,
                                        network_enabled,
                                        bluetooth_toggle_initialized,
                                        network_toggle_initialized);
    }

    s_status_panel.host_screen = screen;
    s_status_panel.root = lv_obj_create(screen);
    ui_apply_basic_object_style(s_status_panel.root, false, 0, 0);
    lv_obj_set_style_bg_opa(s_status_panel.root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_status_panel.root, 0, 0);
    lv_obj_set_pos(s_status_panel.root, 0, 0);
    lv_obj_set_size(s_status_panel.root, s_screen_width, s_screen_height);
    lv_obj_add_flag(s_status_panel.root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_status_panel.root, ui_status_root_delete_event_cb, LV_EVENT_DELETE, NULL);

    s_status_panel.mask = lv_obj_create(s_status_panel.root);
    lv_obj_remove_style_all(s_status_panel.mask);
    lv_obj_set_pos(s_status_panel.mask, 0, 0);
    lv_obj_set_size(s_status_panel.mask, s_screen_width, s_screen_height);
    lv_obj_add_flag(s_status_panel.mask, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_status_panel.mask, ui_status_mask_event_cb, LV_EVENT_CLICKED, NULL);

    ui_status_build_panel_widgets(s_status_panel.root,
                                  s_status_panel.root,
                                  0,
                                  UI_STATUS_BAR_HEIGHT,
                                  s_screen_width);

    lv_obj_move_foreground(s_status_panel.root);
    ui_status_update_panel_visuals();
}

static void ui_status_panel_toggle_event_cb(lv_event_t *e)
{
    lv_obj_t *screen;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    if (!ui_accept_navigation_interaction())
    {
        return;
    }

    screen = lv_screen_active();
    if (screen == NULL)
    {
        return;
    }

    ui_status_build_overlay(screen);
    if (s_status_panel.root == NULL)
    {
        return;
    }

    if (lv_obj_has_flag(s_status_panel.root, LV_OBJ_FLAG_HIDDEN))
    {
        ui_status_sync_now();
        ui_status_update_panel_visuals();
        lv_obj_clear_flag(s_status_panel.root, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_status_panel.root);
    }
    else
    {
        ui_status_close_panel();
    }
}

void ui_helpers_init(void)
{
    if (s_ui_helpers_initialized)
    {
        ui_refresh_metrics();
        if (s_status_panel.toast_timer == NULL)
        {
            s_status_panel.toast_timer = lv_timer_create(ui_status_toast_timer_cb,
                                                         UI_STATUS_TOAST_DURATION_MS,
                                                         NULL);
            lv_timer_set_repeat_count(s_status_panel.toast_timer, 1);
            lv_timer_pause(s_status_panel.toast_timer);
        }
        return;
    }

    memset(s_font_cache, 0, sizeof(s_font_cache));
    memset(s_screen_refs, 0, sizeof(s_screen_refs));
    memset(&s_status_panel, 0, sizeof(s_status_panel));
    s_ui_nav_last_tick = 0;
    s_status_detail_reload_tick = 0;
    s_status_detail_reload_pending = false;
    s_status_last_bt_icon_state = -1;
    s_status_last_network_icon_state = -1;
    s_status_last_net_4g_enabled = false;
    s_status_last_network_text[0] = '\0';
    ui_refresh_metrics();
    s_status_panel.brightness_steps = 3U;
    s_status_panel.volume_steps = 5U;
    s_status_panel.toast_timer = lv_timer_create(ui_status_toast_timer_cb,
                                                 UI_STATUS_TOAST_DURATION_MS,
                                                 NULL);
    if (s_status_panel.toast_timer != NULL)
    {
        lv_timer_set_repeat_count(s_status_panel.toast_timer, 1);
        lv_timer_pause(s_status_panel.toast_timer);
    }
    ui_status_bar_refresh_datetime();
    s_ui_helpers_initialized = true;
}

void ui_helpers_deinit(void)
{
    if (s_status_panel.root != NULL)
    {
        lv_obj_delete(s_status_panel.root);
        s_status_panel.root = NULL;
    }
    if (s_status_panel.sync_timer != NULL)
    {
        lv_timer_delete(s_status_panel.sync_timer);
        s_status_panel.sync_timer = NULL;
    }
    if (s_status_panel.toast_timer != NULL)
    {
        lv_timer_delete(s_status_panel.toast_timer);
        s_status_panel.toast_timer = NULL;
    }
    ui_helpers_reset_font_cache();

    memset(s_screen_refs, 0, sizeof(s_screen_refs));
    memset(&s_status_panel, 0, sizeof(s_status_panel));
    s_ui_nav_last_tick = 0;
    s_status_detail_reload_tick = 0;
    s_status_detail_reload_pending = false;
    s_status_last_bt_icon_state = -1;
    s_status_last_network_icon_state = -1;
    s_status_last_net_4g_enabled = false;
    s_status_last_network_text[0] = '\0';
    s_status_pending_charge = -1;
    s_status_applied_charge = -1;
    s_status_pending_battery_percent = -1;
    s_status_applied_battery_percent = -1;
    memset(&s_status_bar_snapshot, 0, sizeof(s_status_bar_snapshot));
    s_ui_helpers_initialized = false;
}

void ui_helpers_reset_font_cache(void)
{
    size_t i;

    for (i = 0; i < sizeof(s_font_cache) / sizeof(s_font_cache[0]); ++i)
    {
        if (s_font_cache[i].font != NULL)
        {
            lv_tiny_ttf_destroy(s_font_cache[i].font);
            s_font_cache[i].font = NULL;
            s_font_cache[i].size = 0;
        }
    }
}

lv_coord_t ui_px_x(int32_t value)
{
    return (lv_coord_t)value;
}

lv_coord_t ui_px_y(int32_t value)
{
    return (lv_coord_t)value;
}

lv_coord_t ui_px_w(int32_t value)
{
    return ui_px_x(value);
}

lv_coord_t ui_px_h(int32_t value)
{
    return ui_px_y(value);
}

uint16_t ui_scaled_font_size(uint16_t figma_size)
{
    return figma_size;
}

lv_font_t *ui_font_get_actual(uint16_t actual_size)
{
    lv_font_t *font;

    ui_helpers_init();
    font = ui_font_cache_get(s_font_cache,
                             sizeof(s_font_cache) / sizeof(s_font_cache[0]),
                             xiaozhi_font,
                             (size_t)xiaozhi_font_size,
                             actual_size);
    if (font != NULL)
    {
        return font;
    }

    return (lv_font_t *)LV_FONT_DEFAULT;
}

lv_font_t *ui_font_get(uint16_t figma_size)
{
    return ui_font_get_actual(ui_scaled_font_size(figma_size));
}

lv_obj_t *ui_create_screen_base(void)
{
    lv_obj_t *screen;

    ui_helpers_init();
    screen = lv_obj_create(NULL);
    ui_apply_basic_object_style(screen, false, 0, 0);
    lv_obj_set_size(screen, s_screen_width, s_screen_height);
    return screen;
}

lv_obj_t *ui_create_card(lv_obj_t *parent,
                         int32_t x,
                         int32_t y,
                         int32_t w,
                         int32_t h,
                         ui_screen_id_t target,
                         bool filled,
                         int32_t radius)
{
    lv_obj_t *card = lv_obj_create(parent);

    ui_apply_basic_object_style(card,
                                filled,
                                radius >= 0 ? ui_px_x(radius) : 0,
                                2);
    lv_obj_set_pos(card, ui_px_x(x), ui_px_y(y));
    lv_obj_set_size(card, ui_px_w(w), ui_px_h(h));

    if (target != UI_SCREEN_NONE)
    {
        ui_attach_nav_event(card, target);
    }

    return card;
}

lv_obj_t *ui_create_label(lv_obj_t *parent,
                          const char *text,
                          int32_t x,
                          int32_t y,
                          int32_t w,
                          int32_t h,
                          uint16_t figma_font_size,
                          lv_text_align_t align,
                          bool inverted,
                          bool wrap)
{
    lv_obj_t *label = lv_label_create(parent);
    (void)inverted;

    lv_label_set_text(label, text != NULL ? text : "");
    lv_label_set_long_mode(label, wrap ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_CLIP);
    if (w > 0 || h > 0)
    {
        lv_obj_set_size(label,
                        w > 0 ? ui_px_w(w) : LV_SIZE_CONTENT,
                        h > 0 ? ui_px_h(h) : LV_SIZE_CONTENT);
    }
    lv_obj_set_pos(label, ui_px_x(x), ui_px_y(y));
    lv_obj_set_style_text_font(label, ui_font_get(figma_font_size), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_align(label, align, 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    return label;
}

lv_obj_t *ui_create_button(lv_obj_t *parent,
                           int32_t x,
                           int32_t y,
                           int32_t w,
                           int32_t h,
                           const char *text,
                           uint16_t figma_font_size,
                           ui_screen_id_t target,
                           bool filled)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *label;
    (void)filled;

    ui_apply_basic_object_style(button, false, 0, 2);
    lv_obj_set_pos(button, ui_px_x(x), ui_px_y(y));
    lv_obj_set_size(button, ui_px_w(w), ui_px_h(h));
    if (target != UI_SCREEN_NONE)
    {
        ui_attach_nav_event(button, target);
    }

    label = lv_label_create(button);
    lv_label_set_text(label, text != NULL ? text : "");
    lv_obj_set_style_text_font(label, ui_font_get(figma_font_size), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x000000), 0);
    lv_obj_center(label);
    return button;
}

lv_obj_t *ui_create_nav_button(lv_obj_t *parent,
                               int32_t x,
                               int32_t y,
                               int32_t w,
                               int32_t h,
                               const char *text,
                               ui_screen_id_t target)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *label;

    ui_apply_basic_object_style(button, false, 0, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
    lv_obj_set_pos(button, ui_px_x(x), ui_px_y(y));
    lv_obj_set_size(button, ui_px_w(w), ui_px_h(h));
    if (target != UI_SCREEN_NONE)
    {
        ui_attach_nav_event(button, target);
    }

    label = lv_label_create(button);
    lv_label_set_text(label, text != NULL ? text : "");
    lv_obj_set_style_text_font(label, ui_font_get(UI_STANDARD_NAV_FONT_SIZE), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x000000), 0);
    lv_obj_center(label);
    return button;
}

lv_obj_t *ui_create_icon_badge(lv_obj_t *parent,
                               int32_t x,
                               int32_t y,
                               int32_t w,
                               int32_t h,
                               const char *text)
{
    lv_obj_t *badge = lv_obj_create(parent);
    lv_obj_t *label;

    ui_apply_basic_object_style(badge, false, ui_px_x(h / 2), 2);
    lv_obj_set_pos(badge, ui_px_x(x), ui_px_y(y));
    lv_obj_set_size(badge, ui_px_w(w), ui_px_h(h));

    label = lv_label_create(badge);
    lv_label_set_text(label, text != NULL ? text : "");
    lv_obj_set_style_text_font(label, ui_font_get(18), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x000000), 0);
    lv_obj_center(label);
    return label;
}

lv_obj_t *ui_create_image_slot(lv_obj_t *parent,
                               int32_t x,
                               int32_t y,
                               int32_t w,
                               int32_t h)
{
    lv_obj_t *img = lv_img_create(parent);

    lv_obj_set_pos(img, ui_px_x(x), ui_px_y(y));
    lv_obj_set_size(img, ui_px_w(w), ui_px_h(h));
    return img;
}

lv_obj_t *ui_create_hidden_label(lv_obj_t *parent)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_label_set_text(label, "");
    lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    return label;
}

void ui_attach_nav_event(lv_obj_t *obj, ui_screen_id_t target)
{
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(obj, ui_nav_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)target);
}

void ui_build_status_bar_ex(lv_obj_t *parent,
                            xiaozhi_home_screen_refs_t *refs,
                            bool enable_detail_touch)
{
    ui_status_bar_component_build(parent,
                                  refs,
                                  enable_detail_touch,
                                  ui_status_panel_toggle_event_bridge);
}

void ui_build_status_bar(lv_obj_t *parent, xiaozhi_home_screen_refs_t *refs)
{
    ui_build_status_bar_ex(parent, refs, true);
}

void ui_build_standard_screen_ex(ui_screen_scaffold_t *scaffold,
                                 lv_obj_t *screen,
                                 const char *title,
                                 ui_screen_id_t back_target,
                                 bool enable_detail_touch)
{
    lv_obj_t *section;
    lv_obj_t *content;
    lv_obj_t *title_bar;
    char title_buffer[128];
    size_t title_len = 0U;

    if (scaffold == NULL || screen == NULL)
    {
        return;
    }

    memset(scaffold, 0, sizeof(*scaffold));
    scaffold->screen = screen;
    title_buffer[0] = '\0';
    if (title != NULL)
    {
        title_len = strnlen(title, sizeof(title_buffer) - 1U);
        memcpy(title_buffer, title, title_len);
        title_buffer[title_len] = '\0';
    }

    ui_build_status_bar_ex(screen, &scaffold->status_refs, enable_detail_touch);

    section = lv_obj_create(screen);
    ui_apply_basic_object_style(section, false, 0, 0);
    lv_obj_set_pos(section, 0, ui_px_y(68));
    lv_obj_set_size(section, s_screen_width, s_screen_height - ui_px_y(68));

    title_bar = lv_obj_create(section);
    ui_apply_basic_object_style(title_bar, false, 0, 0);
    lv_obj_set_style_bg_color(title_bar, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(title_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(title_bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(title_bar, 2, 0);
    lv_obj_set_size(title_bar, s_screen_width, ui_px_h(UI_STANDARD_NAV_HEIGHT));

    ui_create_label(title_bar,
                    title_buffer,
                    UI_STANDARD_SIDE_MARGIN + UI_STANDARD_NAV_BUTTON_WIDTH + 8,
                    13,
                    s_screen_width - ((UI_STANDARD_SIDE_MARGIN + UI_STANDARD_NAV_BUTTON_WIDTH + 8) * 2),
                    32,
                    UI_STANDARD_NAV_FONT_SIZE,
                    LV_TEXT_ALIGN_CENTER,
                    false,
                    false);
    ui_create_nav_button(title_bar,
                         UI_STANDARD_SIDE_MARGIN,
                         0,
                         UI_STANDARD_NAV_BUTTON_WIDTH,
                         UI_STANDARD_NAV_HEIGHT,
                         ui_i18n_pick("返回", "Back"),
                         back_target);
    ui_create_nav_button(title_bar,
                         s_screen_width - UI_STANDARD_SIDE_MARGIN - UI_STANDARD_NAV_BUTTON_WIDTH,
                         0,
                         UI_STANDARD_NAV_BUTTON_WIDTH,
                         UI_STANDARD_NAV_HEIGHT,
                         ui_i18n_pick("主页", "Home"),
                         UI_SCREEN_HOME);

    content = lv_obj_create(section);
    ui_apply_basic_object_style(content, false, 0, 0);
    lv_obj_set_pos(content, 0, ui_px_y(UI_STANDARD_NAV_HEIGHT));
    lv_obj_set_size(content, s_screen_width, s_screen_height - ui_px_y(68 + UI_STANDARD_NAV_HEIGHT));

    scaffold->content = content;
}

void ui_build_standard_screen(ui_screen_scaffold_t *scaffold,
                              lv_obj_t *screen,
                              const char *title,
                              ui_screen_id_t back_target)
{
    ui_build_standard_screen_ex(scaffold, screen, title, back_target, true);
}

void ui_build_status_detail_content(lv_obj_t *screen, lv_obj_t *parent)
{
    rt_device_t lcd_device;
    rt_device_t lcd_backlight_device;
    lv_timer_t *sync_timer;
    lv_timer_t *toast_timer;
    uint8_t brightness_steps;
    uint8_t volume_steps;
    bool charging;
    bool confirm_visible;
    bool waiting_requested;
    bool suppress_connected_state;
    bool last_bt_connected;
    bool bluetooth_enabled;
    bool network_enabled;
    bool bluetooth_toggle_initialized;
    bool network_toggle_initialized;

    if (screen == NULL || parent == NULL)
    {
        return;
    }

    ui_status_preserve_runtime_state(&lcd_device,
                                     &lcd_backlight_device,
                                     &sync_timer,
                                     &toast_timer,
                                     &brightness_steps,
                                     &volume_steps,
                                     &charging,
                                     &confirm_visible,
                                     &waiting_requested,
                                     &suppress_connected_state,
                                     &last_bt_connected,
                                     &bluetooth_enabled,
                                     &network_enabled,
                                     &bluetooth_toggle_initialized,
                                     &network_toggle_initialized);
    ui_status_restore_runtime_state(lcd_device,
                                    lcd_backlight_device,
                                    sync_timer,
                                    toast_timer,
                                    brightness_steps,
                                    volume_steps,
                                    charging,
                                    confirm_visible,
                                    waiting_requested,
                                    suppress_connected_state,
                                    last_bt_connected,
                                    bluetooth_enabled,
                                    network_enabled,
                                    bluetooth_toggle_initialized,
                                    network_toggle_initialized);
    s_status_panel.host_screen = screen;
    s_status_panel.root = screen;
    s_status_panel.mask = NULL;

    ui_status_build_panel_widgets(screen, parent, 24, 24, 480);
    ui_status_hide_toast();
    ui_status_sync_now();
    ui_status_update_panel_visuals();
}

const xiaozhi_home_screen_refs_t *ui_screen_refs_get(lv_obj_t *screen)
{
    return ui_status_bar_component_refs_get(screen);
}

void ui_screen_refs_unregister(lv_obj_t *screen)
{
    size_t i;

    if (screen == NULL)
    {
        return;
    }

    if (s_status_panel.host_screen == screen)
    {
        s_status_panel.host_screen = NULL;
        s_status_panel.root = NULL;
        s_status_panel.mask = NULL;
        s_status_panel.panel = NULL;
        s_status_panel.toast = NULL;
        s_status_panel.toast_label = NULL;
        s_status_panel.confirm = NULL;
        s_status_panel.brightness_track = NULL;
        s_status_panel.brightness_fill = NULL;
        s_status_panel.brightness_knob = NULL;
        s_status_panel.brightness_slider = NULL;
        s_status_panel.brightness_value_label = NULL;
        s_status_panel.volume_track = NULL;
        s_status_panel.volume_fill = NULL;
        s_status_panel.volume_knob = NULL;
        s_status_panel.volume_slider = NULL;
        s_status_panel.volume_value_label = NULL;
        s_status_panel.bluetooth_card = NULL;
        s_status_panel.bluetooth_title_label = NULL;
        s_status_panel.bluetooth_subtitle_label = NULL;
        s_status_panel.bluetooth_value_label = NULL;
        s_status_panel.network_card = NULL;
        s_status_panel.network_title_label = NULL;
        s_status_panel.network_subtitle_label = NULL;
        s_status_panel.network_value_label = NULL;
    }

    ui_status_bar_component_refs_unregister(screen);
}

bool ui_status_panel_is_visible(void)
{
    return (s_status_panel.root != NULL) &&
           !lv_obj_has_flag(s_status_panel.root, LV_OBJ_FLAG_HIDDEN);
}

void ui_refresh_global_status_bar(void)
{
    ui_status_bar_component_refresh();
}

void ui_force_refresh_global_status_bar(void)
{
    ui_status_bar_component_force_refresh();
}

void xiaozhi_ui_update_brightness(int brightness)
{
    if (brightness < 0)
    {
        brightness = 0;
    }
    if (brightness > 100)
    {
        brightness = 100;
    }

    s_status_pending_brightness = brightness;
}

void xiaozhi_ui_update_volume(int volume)
{
    if (volume < 0)
    {
        volume = 0;
    }
    if (volume > 15)
    {
        volume = 15;
    }

    s_status_pending_volume = volume;
}

void xiaozhi_ui_update_charge_status(uint8_t is_charging)
{
    ui_status_bar_component_update_charge(is_charging);
}

void xiaozhi_ui_update_battery_percent(uint8_t percent)
{
    ui_status_bar_component_update_battery_percent(percent);
}
