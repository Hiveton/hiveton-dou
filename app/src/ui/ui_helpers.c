#include "ui_helpers.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "audio_server.h"
#include "lv_tiny_ttf.h"
#include "rtdevice.h"
#include "ui_runtime_adapter.h"

#define LCD_DEVICE_NAME "lcd"
#define UI_STANDARD_NAV_FONT_SIZE 28
#define UI_STATUS_BAR_HEIGHT 68
#define UI_STATUS_BAR_CALENDAR_TOUCH_W 320
#define UI_STATUS_BAR_DETAIL_TOUCH_X 320
#define UI_STATUS_TOAST_DURATION_MS 3000
#define UI_STATUS_INTERACTION_DEBOUNCE_MS 250
#define UI_NAV_INTERACTION_DEBOUNCE_MS 250
#define UI_STATUS_DETAIL_RELOAD_DEBOUNCE_MS 1500
#define UI_STATUS_MASK_SOLID_GRAY 0xB3B3B3

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
    lv_obj_t *network_card;
    lv_obj_t *network_subtitle_label;
    uint8_t brightness_steps;
    uint8_t volume_steps;
    bool charging;
    bool confirm_visible;
    bool waiting_requested;
    bool suppress_connected_state;
    bool last_bt_connected;
    rt_tick_t last_input_tick;
} ui_status_panel_state_t;

static ui_font_cache_entry_t s_font_cache[20];
static ui_screen_refs_entry_t s_screen_refs[UI_SCREEN_COUNT];
static bool s_ui_helpers_initialized = false;
static lv_coord_t s_screen_width = UI_FIGMA_WIDTH;
static lv_coord_t s_screen_height = UI_FIGMA_HEIGHT;
static const size_t UI_TINY_TTF_GLYPH_CACHE_COUNT = 32U;
static ui_status_panel_state_t s_status_panel = {0};
static volatile int s_status_pending_brightness = -1;
static volatile int s_status_pending_volume = -1;
static volatile int s_status_pending_charge = -1;
static rt_tick_t s_ui_nav_last_tick = 0;
static rt_tick_t s_status_detail_reload_tick = 0;
static bool s_status_detail_reload_pending = false;
static int s_status_last_bt_icon_state = -1;
static int s_status_last_network_icon_state = -1;
static const lv_point_precise_t s_charge_bolt_points[] = {
    {12, 0},
    {2, 13},
    {9, 13},
    {5, 28},
    {17, 10},
    {10, 10},
};

static bool ui_status_accept_interaction(void);
static void ui_status_request_detail_rebuild(void);
static void ui_status_update_panel_visuals(void);
static bool ui_accept_debounced_tick(rt_tick_t *last_tick, uint32_t debounce_ms);
static bool ui_accept_navigation_interaction(void);
static bool ui_status_detail_is_active(void);
static void ui_status_detail_reload_async_cb(void *user_data);
static void ui_status_refresh_connection_icons(bool force);

static bool ui_bt_connection_active(void)
{
    return false;
}

static bool ui_bt_pairing_enabled(void)
{
    return false;
}

static bool ui_bt_network_ready(void)
{
    return false;
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

static void ui_status_refresh_connection_icons(bool force)
{
    bool bt_connected = ui_bt_connection_active();
    bool network_ready = ui_bt_network_ready();
    size_t i;

    if (!force &&
        s_status_last_bt_icon_state == (bt_connected ? 1 : 0) &&
        s_status_last_network_icon_state == (network_ready ? 1 : 0))
    {
        return;
    }

    s_status_last_bt_icon_state = bt_connected ? 1 : 0;
    s_status_last_network_icon_state = network_ready ? 1 : 0;

    for (i = 0; i < sizeof(s_screen_refs) / sizeof(s_screen_refs[0]); ++i)
    {
        xiaozhi_home_screen_refs_t *refs = &s_screen_refs[i].refs;

        if (!s_screen_refs[i].used)
        {
            continue;
        }

        if (refs->bluetooth_icon != NULL)
        {
            lv_img_set_src(refs->bluetooth_icon,
                           bt_connected ? &ble_icon_img : &ble_icon_img_close);
        }

        if (refs->network_icon != NULL)
        {
            if (network_ready)
            {
                lv_img_set_src(refs->network_icon, &network_icon_img);
                lv_obj_clear_flag(refs->network_icon, LV_OBJ_FLAG_HIDDEN);
            }
            else
            {
                lv_obj_add_flag(refs->network_icon, LV_OBJ_FLAG_HIDDEN);
            }
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

    for (i = 0; i < sizeof(s_screen_refs) / sizeof(s_screen_refs[0]); ++i)
    {
        lv_obj_t *icon;

        if (!s_screen_refs[i].used)
        {
            continue;
        }

        icon = s_screen_refs[i].refs.standby_charging_icon;
        if (icon == NULL)
        {
            continue;
        }

        if (s_status_panel.charging)
        {
            lv_obj_clear_flag(icon, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void ui_status_update_slider_visual(lv_obj_t *track,
                                           lv_obj_t *fill,
                                           lv_obj_t *knob,
                                           uint8_t min_step,
                                           uint8_t max_step,
                                           uint8_t current_step)
{
    int32_t span;
    int32_t track_w;
    int32_t knob_w;
    int32_t usable_w;
    int32_t knob_x = 0;
    int32_t fill_w;

    if (track == NULL || fill == NULL || knob == NULL)
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
    knob_w = lv_obj_get_width(knob);
    usable_w = track_w - knob_w;
    if (usable_w < 0)
    {
        usable_w = 0;
    }

    span = (int32_t)(max_step - min_step);
    if (span > 0)
    {
        knob_x = ((int32_t)(current_step - min_step) * usable_w + span / 2) / span;
    }

    fill_w = knob_x + knob_w / 2;
    if (fill_w < knob_w / 2)
    {
        fill_w = knob_w / 2;
    }
    if (fill_w > track_w)
    {
        fill_w = track_w;
    }

    lv_obj_set_width(fill, fill_w);
    lv_obj_set_x(knob, lv_obj_get_x(track) + knob_x);
    lv_obj_set_y(knob, lv_obj_get_y(track) - ui_px_y(1));
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
        bool filled = bt_state != UI_STATUS_BLUETOOTH_DISCONNECTED;

        LV_UNUSED(filled);
        ui_apply_basic_object_style(s_status_panel.bluetooth_card, false, 0, 2);

        if (s_status_panel.bluetooth_title_label != NULL)
        {
            lv_obj_set_style_text_color(s_status_panel.bluetooth_title_label,
                                        lv_color_hex(0x000000),
                                        0);
        }
        if (s_status_panel.bluetooth_subtitle_label != NULL)
        {
            lv_obj_set_style_text_color(s_status_panel.bluetooth_subtitle_label,
                                        lv_color_hex(0x000000),
                                        0);
        }

        switch (bt_state)
        {
        case UI_STATUS_BLUETOOTH_CONNECTED:
            lv_label_set_text(s_status_panel.bluetooth_title_label, "蓝牙");
            lv_label_set_text(s_status_panel.bluetooth_subtitle_label, "已连接");
            break;
        case UI_STATUS_BLUETOOTH_WAITING:
            lv_label_set_text(s_status_panel.bluetooth_title_label, "蓝牙连接中");
            lv_label_set_text(s_status_panel.bluetooth_subtitle_label, "请在手机里找到设备并连接");
            break;
        default:
            lv_label_set_text(s_status_panel.bluetooth_title_label,
                              bt_pairing_enabled ? "蓝牙已开启" : "蓝牙未启用");
            lv_label_set_text(s_status_panel.bluetooth_subtitle_label,
                              bt_pairing_enabled ? "当前固件未托管连接流程"
                                                 : "当前固件仅保留 BLE 初始化");
            break;
        }
    }

    if (s_status_panel.network_subtitle_label != NULL)
    {
        lv_label_set_text(s_status_panel.network_subtitle_label,
                          network_ready ? "已联网" : "未联网");
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

    if (s_status_panel.lcd_device != RT_NULL)
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

    pending = s_status_pending_charge;
    if (pending >= 0)
    {
        s_status_panel.charging = pending != 0;
    }

    s_status_panel.brightness_steps = ui_status_brightness_to_steps(brightness);
    s_status_panel.volume_steps = ui_status_volume_to_steps(volume);

    bt_connected = ui_bt_connection_active();
    if (bt_connected && !s_status_panel.last_bt_connected)
    {
        s_status_panel.waiting_requested = false;
        s_status_panel.suppress_connected_state = false;
        ui_status_show_toast("蓝牙连接成功");
    }
    else if (!bt_connected && s_status_panel.last_bt_connected)
    {
        s_status_panel.suppress_connected_state = false;
    }

    s_status_panel.last_bt_connected = bt_connected;

    ui_status_refresh_charging_icons();
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

static void ui_status_bluetooth_card_event_cb(lv_event_t *e)
{
    ui_status_bluetooth_state_t bt_state;

    if (lv_event_get_code(e) != LV_EVENT_RELEASED)
    {
        return;
    }
    if (!ui_status_accept_interaction())
    {
        return;
    }

    bt_state = ui_status_get_bluetooth_state();
    if (bt_state == UI_STATUS_BLUETOOTH_DISCONNECTED)
    {
        ui_status_show_toast("当前固件仅保留 BLE 初始化");
        return;
    }

    ui_status_show_toast("蓝牙连接管理已移除");
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
    int32_t slot_count;
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

    slot_count = (int32_t)(max_step - min_step + 1U);
    step = (rel_x * slot_count) / width + min_step;
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

    if (lv_event_get_code(e) != LV_EVENT_RELEASED || slider == NULL || indev == NULL)
    {
        return;
    }
    if (!ui_status_accept_interaction())
    {
        return;
    }

    if (kind == UI_STATUS_SLIDER_BRIGHTNESS)
    {
        uint8_t actual;

        value = ui_status_slider_step_from_touch(slider, indev, 1U, 5U);
        actual = ui_status_steps_to_brightness(value);
        s_status_panel.brightness_steps = value;
        s_status_pending_brightness = actual;
        if (s_status_panel.lcd_device == RT_NULL)
        {
            s_status_panel.lcd_device = rt_device_find(LCD_DEVICE_NAME);
        }
        if (s_status_panel.lcd_device != RT_NULL)
        {
            rt_device_control(s_status_panel.lcd_device, RTGRAPHIC_CTRL_SET_BRIGHTNESS, &actual);
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
    lv_timer_t *sync_timer;
    lv_timer_t *toast_timer;
    uint8_t brightness_steps;
    uint8_t volume_steps;
    bool charging;
    bool confirm_visible;
    bool waiting_requested;
    bool suppress_connected_state;
    bool last_bt_connected;

    if (lv_event_get_target_obj(e) != s_status_panel.root)
    {
        return;
    }

    lcd_device = s_status_panel.lcd_device;
    sync_timer = s_status_panel.sync_timer;
    toast_timer = s_status_panel.toast_timer;
    brightness_steps = s_status_panel.brightness_steps;
    volume_steps = s_status_panel.volume_steps;
    charging = s_status_panel.charging;
    confirm_visible = s_status_panel.confirm_visible;
    waiting_requested = s_status_panel.waiting_requested;
    suppress_connected_state = s_status_panel.suppress_connected_state;
    last_bt_connected = s_status_panel.last_bt_connected;

    memset(&s_status_panel, 0, sizeof(s_status_panel));
    s_status_panel.lcd_device = lcd_device;
    s_status_panel.sync_timer = sync_timer;
    s_status_panel.toast_timer = toast_timer;
    s_status_panel.brightness_steps = brightness_steps;
    s_status_panel.volume_steps = volume_steps;
    s_status_panel.charging = charging;
    s_status_panel.confirm_visible = confirm_visible;
    s_status_panel.waiting_requested = waiting_requested;
    s_status_panel.suppress_connected_state = suppress_connected_state;
    s_status_panel.last_bt_connected = last_bt_connected;
}

static void ui_status_preserve_runtime_state(rt_device_t *lcd_device,
                                             lv_timer_t **sync_timer,
                                             lv_timer_t **toast_timer,
                                             uint8_t *brightness_steps,
                                             uint8_t *volume_steps,
                                             bool *charging,
                                             bool *confirm_visible,
                                             bool *waiting_requested,
                                             bool *suppress_connected_state,
                                             bool *last_bt_connected)
{
    if (lcd_device != NULL) *lcd_device = s_status_panel.lcd_device;
    if (sync_timer != NULL) *sync_timer = s_status_panel.sync_timer;
    if (toast_timer != NULL) *toast_timer = s_status_panel.toast_timer;
    if (brightness_steps != NULL) *brightness_steps = s_status_panel.brightness_steps;
    if (volume_steps != NULL) *volume_steps = s_status_panel.volume_steps;
    if (charging != NULL) *charging = s_status_panel.charging;
    if (confirm_visible != NULL) *confirm_visible = s_status_panel.confirm_visible;
    if (waiting_requested != NULL) *waiting_requested = s_status_panel.waiting_requested;
    if (suppress_connected_state != NULL) *suppress_connected_state = s_status_panel.suppress_connected_state;
    if (last_bt_connected != NULL) *last_bt_connected = s_status_panel.last_bt_connected;
}

static void ui_status_restore_runtime_state(rt_device_t lcd_device,
                                            lv_timer_t *sync_timer,
                                            lv_timer_t *toast_timer,
                                            uint8_t brightness_steps,
                                            uint8_t volume_steps,
                                            bool charging,
                                            bool confirm_visible,
                                            bool waiting_requested,
                                            bool suppress_connected_state,
                                            bool last_bt_connected)
{
    memset(&s_status_panel, 0, sizeof(s_status_panel));
    s_status_panel.lcd_device = lcd_device;
    s_status_panel.sync_timer = sync_timer;
    s_status_panel.toast_timer = toast_timer;
    s_status_panel.brightness_steps = brightness_steps;
    s_status_panel.volume_steps = volume_steps;
    s_status_panel.charging = charging;
    s_status_panel.confirm_visible = confirm_visible;
    s_status_panel.waiting_requested = waiting_requested;
    s_status_panel.suppress_connected_state = suppress_connected_state;
    s_status_panel.last_bt_connected = last_bt_connected;
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
    lv_obj_t *knob;

    track = lv_obj_create(parent);
    ui_apply_basic_object_style(track, false, ui_px_h(9), 2);
    lv_obj_set_pos(track, ui_px_x(x), ui_px_y(y));
    lv_obj_set_size(track, ui_px_w(w), ui_px_h(18));

    fill = lv_obj_create(track);
    ui_apply_basic_object_style(fill, true, ui_px_h(7), 0);
    lv_obj_set_pos(fill, 0, 0);
    lv_obj_set_size(fill, ui_px_w(10), ui_px_h(18));

    knob = lv_obj_create(parent);
    ui_apply_basic_object_style(knob, true, ui_px_h(10), 0);
    lv_obj_set_pos(knob, ui_px_x(x), ui_px_y(y - 1));
    lv_obj_set_size(knob, ui_px_w(20), ui_px_h(20));

    if (kind == UI_STATUS_SLIDER_BRIGHTNESS)
    {
        s_status_panel.brightness_track = track;
        s_status_panel.brightness_fill = fill;
        s_status_panel.brightness_knob = knob;
    }
    else
    {
        s_status_panel.volume_track = track;
        s_status_panel.volume_fill = fill;
        s_status_panel.volume_knob = knob;
    }

    ui_status_update_slider_visual(track, fill, knob, min_step, max_step, current_step);

    LV_UNUSED(ui_status_create_touch_zone(parent,
                                          x,
                                          y - 10,
                                          w,
                                          40,
                                          ui_status_slider_event_cb,
                                          (void *)(uintptr_t)kind));
}

static void ui_status_build_panel_widgets(lv_obj_t *root,
                                          lv_obj_t *parent,
                                          int32_t panel_x,
                                          int32_t panel_y,
                                          int32_t panel_w)
{
    lv_obj_t *panel;
    lv_obj_t *slider_box;
    lv_obj_t *line;
    lv_obj_t *label;
    lv_obj_t *button;
    lv_obj_t *touch_zone;
    int32_t inner_x;
    int32_t inner_w;
    int32_t slider_w;
    int32_t status_gap;
    int32_t status_card_w;
    int32_t status_right_x;
    int32_t toast_w;
    int32_t toast_x;
    int32_t confirm_w;
    int32_t confirm_x;

    if (root == NULL || parent == NULL)
    {
        return;
    }

    panel = lv_obj_create(parent);
    s_status_panel.panel = panel;
    ui_apply_basic_object_style(panel, false, 0, 2);
    lv_obj_set_pos(panel, ui_px_x(panel_x), ui_px_y(panel_y));
    lv_obj_set_size(panel, ui_px_w(panel_w), ui_px_h(316));
    inner_x = 22;
    inner_w = panel_w - (inner_x * 2);
    slider_w = inner_w - 72;
    status_gap = 14;
    status_card_w = (inner_w - status_gap) / 2;
    status_right_x = inner_x + status_card_w + status_gap;
    toast_w = 418;
    toast_x = (int32_t)(s_screen_width - toast_w) / 2;
    confirm_w = 310;
    confirm_x = (int32_t)(s_screen_width - confirm_w) / 2;

    ui_create_label(panel,
                    "快捷状态",
                    22,
                    18,
                    160,
                    28,
                    24,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    false);

    line = lv_obj_create(panel);
    ui_apply_basic_object_style(line, true, 0, 0);
    lv_obj_set_pos(line, ui_px_x(inner_x), ui_px_y(52));
    lv_obj_set_size(line, ui_px_w(inner_w), ui_px_h(2));

    slider_box = ui_create_card(panel, inner_x, 70, inner_w, 144, UI_SCREEN_NONE, false, 0);

    ui_create_label(slider_box,
                    "屏幕亮度",
                    18,
                    18,
                    160,
                    28,
                    21,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    false);
    s_status_panel.brightness_value_label = ui_create_label(slider_box,
                                                            "3 / 5",
                                                            inner_w - 142,
                                                            18,
                                                            110,
                                                            28,
                                                            21,
                                                            LV_TEXT_ALIGN_RIGHT,
                                                            false,
                                                            false);
    s_status_panel.brightness_slider = NULL;
    ui_status_create_slider_visual(slider_box,
                                   24,
                                   58,
                                   slider_w,
                                   1U,
                                   5U,
                                   s_status_panel.brightness_steps,
                                   UI_STATUS_SLIDER_BRIGHTNESS);

    ui_create_label(slider_box,
                    "声音音量",
                    18,
                    90,
                    160,
                    28,
                    21,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    false);
    s_status_panel.volume_value_label = ui_create_label(slider_box,
                                                        "5 / 10",
                                                        inner_w - 164,
                                                        90,
                                                        132,
                                                        28,
                                                        21,
                                                        LV_TEXT_ALIGN_RIGHT,
                                                        false,
                                                        false);
    s_status_panel.volume_slider = NULL;
    ui_status_create_slider_visual(slider_box,
                                   24,
                                   130,
                                   slider_w,
                                   0U,
                                   10U,
                                   s_status_panel.volume_steps,
                                   UI_STATUS_SLIDER_VOLUME);

    s_status_panel.bluetooth_card = ui_create_card(panel, inner_x, 232, status_card_w, 64, UI_SCREEN_NONE, false, 0);
    s_status_panel.bluetooth_title_label = ui_create_label(s_status_panel.bluetooth_card,
                                                           "蓝牙",
                                                           18,
                                                           10,
                                                           180,
                                                           24,
                                                           22,
                                                           LV_TEXT_ALIGN_LEFT,
                                                           false,
                                                           false);
    s_status_panel.bluetooth_subtitle_label = ui_create_label(s_status_panel.bluetooth_card,
                                                              "点击可连接蓝牙",
                                                              18,
                                                              34,
                                                              192,
                                                              22,
                                                              14,
                                                              LV_TEXT_ALIGN_LEFT,
                                                              false,
                                                              false);
    touch_zone = ui_status_create_touch_zone(panel,
                                             inner_x,
                                             232,
                                             status_card_w,
                                             64,
                                             ui_status_bluetooth_card_event_cb,
                                             NULL);
    lv_obj_move_foreground(touch_zone);

    s_status_panel.network_card = ui_create_card(panel, status_right_x, 232, status_card_w, 64, UI_SCREEN_NONE, true, 0);
    label = ui_create_label(s_status_panel.network_card,
                            "网络状态",
                            18,
                            10,
                            180,
                            24,
                            22,
                            LV_TEXT_ALIGN_LEFT,
                            true,
                            false);
    LV_UNUSED(label);
    s_status_panel.network_subtitle_label = ui_create_label(s_status_panel.network_card,
                                                            "未联网",
                                                            18,
                                                            34,
                                                            192,
                                                            22,
                                                            14,
                                                            LV_TEXT_ALIGN_LEFT,
                                                            true,
                                                            false);

    s_status_panel.toast = lv_obj_create(root);
    ui_apply_basic_object_style(s_status_panel.toast, true, ui_px_h(20), 0);
    lv_obj_set_pos(s_status_panel.toast, ui_px_x(toast_x), ui_px_y(80));
    lv_obj_set_size(s_status_panel.toast, ui_px_w(toast_w), ui_px_h(40));
    lv_obj_add_flag(s_status_panel.toast, LV_OBJ_FLAG_HIDDEN);
    s_status_panel.toast_label = ui_create_label(s_status_panel.toast,
                                                 "蓝牙连接成功",
                                                 0,
                                                 8,
                                                 418,
                                                 24,
                                                 14,
                                                 LV_TEXT_ALIGN_CENTER,
                                                 true,
                                                 false);

    s_status_panel.confirm = lv_obj_create(root);
    ui_apply_basic_object_style(s_status_panel.confirm, false, 0, 2);
    lv_obj_set_pos(s_status_panel.confirm, ui_px_x(confirm_x), ui_px_y(224));
    lv_obj_set_size(s_status_panel.confirm, ui_px_w(confirm_w), ui_px_h(128));
    if (!s_status_panel.confirm_visible)
    {
        lv_obj_add_flag(s_status_panel.confirm, LV_OBJ_FLAG_HIDDEN);
    }

    ui_create_label(s_status_panel.confirm,
                    "确定断开蓝牙连接？",
                    0,
                    24,
                    310,
                    28,
                    22,
                    LV_TEXT_ALIGN_CENTER,
                    false,
                    false);

    button = ui_create_card(s_status_panel.confirm, 26, 76, 112, 32, UI_SCREEN_NONE, false, 0);
    ui_create_label(button, "否", 0, 3, 112, 24, 18, LV_TEXT_ALIGN_CENTER, false, false);
    touch_zone = ui_status_create_touch_zone(s_status_panel.confirm,
                                             26,
                                             76,
                                             112,
                                             32,
                                             ui_status_confirm_no_event_cb,
                                             NULL);
    lv_obj_move_foreground(touch_zone);

    button = ui_create_card(s_status_panel.confirm, 172, 76, 112, 32, UI_SCREEN_NONE, true, 0);
    ui_create_label(button, "是", 0, 3, 112, 24, 18, LV_TEXT_ALIGN_CENTER, true, false);
    touch_zone = ui_status_create_touch_zone(s_status_panel.confirm,
                                             172,
                                             76,
                                             112,
                                             32,
                                             ui_status_confirm_yes_event_cb,
                                             NULL);
    lv_obj_move_foreground(touch_zone);
}

static void ui_status_build_overlay(lv_obj_t *screen)
{
    rt_device_t lcd_device;
    lv_timer_t *sync_timer;
    lv_timer_t *toast_timer;
    uint8_t brightness_steps;
    uint8_t volume_steps;
    bool charging;
    bool confirm_visible;
    bool waiting_requested;
    bool suppress_connected_state;
    bool last_bt_connected;

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
                                         &sync_timer,
                                         &toast_timer,
                                         &brightness_steps,
                                         &volume_steps,
                                         &charging,
                                         &confirm_visible,
                                         &waiting_requested,
                                         &suppress_connected_state,
                                         &last_bt_connected);

        lv_obj_delete(s_status_panel.root);
        ui_status_restore_runtime_state(lcd_device,
                                        sync_timer,
                                        toast_timer,
                                        brightness_steps,
                                        volume_steps,
                                        charging,
                                        confirm_visible,
                                        waiting_requested,
                                        suppress_connected_state,
                                        last_bt_connected);
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

    /* 先完全去掉遮罩层，单独验证花屏是否由遮罩对象触发。 */
    s_status_panel.mask = NULL;
    ui_status_build_panel_widgets(s_status_panel.root,
                                  s_status_panel.root,
                                  18,
                                  UI_STATUS_BAR_HEIGHT,
                                  546);

    lv_obj_move_foreground(s_status_panel.root);
    ui_status_update_panel_visuals();
}

static void ui_status_panel_toggle_event_cb(lv_event_t *e)
{
    ui_screen_id_t current_screen;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    if (!ui_accept_navigation_interaction())
    {
        return;
    }

    current_screen = ui_runtime_get_active_screen_id();
    if (current_screen == UI_SCREEN_NONE || current_screen == UI_SCREEN_STATUS_DETAIL)
    {
        current_screen = UI_SCREEN_HOME;
    }

    ui_Status_Detail_screen_set_return_target(current_screen);
    ui_runtime_switch_to(UI_SCREEN_STATUS_DETAIL);
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
    if (figma_size < 10U)
    {
        return 10U;
    }
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
    lv_obj_set_style_text_font(label, ui_font_get(16), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x000000), 0);
    lv_obj_center(label);
    return badge;
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
    lv_obj_t *bar;
    lv_obj_t *calendar_touch_zone;
    lv_obj_t *bar_touch_zone;
    lv_obj_t *time_label;
    lv_obj_t *meta_label;
    lv_obj_t *battery_label;
    lv_obj_t *charge_icon;
    lv_obj_t *mic_img;
    lv_obj_t *speaker_img;
    lv_obj_t *bluetooth_img;
    lv_obj_t *network_img;
    lv_obj_t *battery_img;

    bar = lv_obj_create(parent);
    ui_apply_basic_object_style(bar, false, 0, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(bar, 2, 0);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_size(bar, s_screen_width, ui_px_h(UI_STATUS_BAR_HEIGHT));

    charge_icon = lv_line_create(bar);
    lv_line_set_points(charge_icon, s_charge_bolt_points, sizeof(s_charge_bolt_points) / sizeof(s_charge_bolt_points[0]));
    lv_obj_set_pos(charge_icon, ui_px_x(4), ui_px_y(20));
    lv_obj_set_size(charge_icon, ui_px_w(18), ui_px_h(28));
    lv_obj_set_style_line_width(charge_icon, 2, 0);
    lv_obj_set_style_line_color(charge_icon, lv_color_hex(0x000000), 0);
    lv_obj_set_style_line_rounded(charge_icon, false, 0);
    lv_obj_add_flag(charge_icon, LV_OBJ_FLAG_HIDDEN);

    time_label = ui_create_label(bar,
                                 "15:30",
                                 24,
                                 14,
                                 118,
                                 38,
                                 34,
                                 LV_TEXT_ALIGN_LEFT,
                                 false,
                                 false);
    meta_label = ui_create_label(bar,
                                 "2026/01/14\n星期三 23°C",
                                 142,
                                 14,
                                 150,
                                 40,
                                 18,
                                 LV_TEXT_ALIGN_LEFT,
                                 false,
                                 true);
    lv_obj_set_style_text_line_space(meta_label, 0, 0);

    mic_img = ui_create_image_slot(bar, 326, 18, 28, 28);
    speaker_img = ui_create_image_slot(bar, 362, 18, 28, 28);
    bluetooth_img = ui_create_image_slot(bar, 398, 18, 28, 28);
    network_img = ui_create_image_slot(bar, 434, 18, 28, 28);
    battery_img = ui_create_image_slot(bar, 472, 18, 48, 28);

    lv_img_set_src(mic_img, &home_mic);
    lv_img_set_src(speaker_img, &home_volume);
    lv_img_set_src(bluetooth_img, &ble_icon_img_close);
    lv_img_set_src(network_img, &network_icon_img_close);
    lv_img_set_src(battery_img, &home_battery);
    lv_obj_add_flag(network_img, LV_OBJ_FLAG_HIDDEN);

    battery_label = ui_create_label(bar,
                                    "80%",
                                    528,
                                    18,
                                    46,
                                    28,
                                    18,
                                    LV_TEXT_ALIGN_LEFT,
                                    false,
                                    false);

    calendar_touch_zone = lv_obj_create(bar);
    ui_apply_basic_object_style(calendar_touch_zone, false, 0, 0);
    lv_obj_set_style_bg_opa(calendar_touch_zone, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(calendar_touch_zone, 0, 0);
    lv_obj_set_pos(calendar_touch_zone, 0, 0);
    lv_obj_set_size(calendar_touch_zone,
                    ui_px_w(UI_STATUS_BAR_CALENDAR_TOUCH_W),
                    ui_px_h(UI_STATUS_BAR_HEIGHT));
    ui_attach_nav_event(calendar_touch_zone, UI_SCREEN_CALENDAR);

    bar_touch_zone = lv_obj_create(bar);
    ui_apply_basic_object_style(bar_touch_zone, false, 0, 0);
    lv_obj_set_style_bg_opa(bar_touch_zone, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar_touch_zone, 0, 0);
    /* 仅右侧状态图标区域可呼出详情，避免抢占左侧已有交互。 */
    lv_obj_set_pos(bar_touch_zone, ui_px_x(UI_STATUS_BAR_DETAIL_TOUCH_X), 0);
    lv_obj_set_size(bar_touch_zone,
                    s_screen_width - ui_px_x(UI_STATUS_BAR_DETAIL_TOUCH_X),
                    ui_px_h(UI_STATUS_BAR_HEIGHT));
    if (enable_detail_touch)
    {
        lv_obj_add_flag(bar_touch_zone, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(bar_touch_zone, ui_status_panel_toggle_event_cb, LV_EVENT_CLICKED, NULL);
    }

    if (refs != NULL)
    {
        lv_obj_t *hidden_refs = lv_obj_create(parent);

        memset(refs, 0, sizeof(*refs));
        refs->screen = parent;
        refs->time_label = time_label;
        refs->meta_label = meta_label;
        refs->bluetooth_icon = bluetooth_img;
        refs->network_icon = network_img;
        refs->battery_percent_label = battery_label;
        refs->standby_charging_icon = charge_icon;

        ui_apply_basic_object_style(hidden_refs, false, 0, 0);
        lv_obj_set_size(hidden_refs, 1, 1);
        lv_obj_set_pos(hidden_refs, -10, -10);
        lv_obj_add_flag(hidden_refs, LV_OBJ_FLAG_HIDDEN);

        refs->weather_icon = lv_img_create(hidden_refs);
        refs->ui_Label_ip = ui_create_hidden_label(hidden_refs);
        refs->tf_dir_label = ui_create_hidden_label(hidden_refs);
        refs->ec800_status_label = ui_create_hidden_label(hidden_refs);
        ui_register_screen_refs(parent, refs);
    }

    ui_status_refresh_charging_icons();
    ui_status_refresh_connection_icons(true);
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

    if (scaffold == NULL || screen == NULL)
    {
        return;
    }

    memset(scaffold, 0, sizeof(*scaffold));
    scaffold->screen = screen;

    ui_build_status_bar_ex(screen, &scaffold->status_refs, enable_detail_touch);

    section = lv_obj_create(screen);
    ui_apply_basic_object_style(section, false, 0, 0);
    lv_obj_set_pos(section, 0, ui_px_y(68));
    lv_obj_set_size(section, s_screen_width, s_screen_height - ui_px_y(68));

    title_bar = lv_obj_create(section);
    ui_apply_basic_object_style(title_bar, false, 0, 0);
    lv_obj_set_style_border_side(title_bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(title_bar, 2, 0);
    lv_obj_set_size(title_bar, s_screen_width, ui_px_h(58));

    ui_create_label(title_bar,
                    title,
                    96,
                    13,
                    s_screen_width - 192,
                    32,
                    UI_STANDARD_NAV_FONT_SIZE,
                    LV_TEXT_ALIGN_CENTER,
                    false,
                    false);
    ui_create_nav_button(title_bar, 0, 0, 96, 58, "返回", back_target);
    ui_create_nav_button(title_bar, s_screen_width - 96, 0, 96, 58, "主页", UI_SCREEN_HOME);

    content = lv_obj_create(section);
    ui_apply_basic_object_style(content, false, 0, 0);
    lv_obj_set_pos(content, 0, ui_px_y(58));
    lv_obj_set_size(content, s_screen_width, s_screen_height - ui_px_y(68 + 58));

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
    lv_timer_t *sync_timer;
    lv_timer_t *toast_timer;
    uint8_t brightness_steps;
    uint8_t volume_steps;
    bool charging;
    bool confirm_visible;
    bool waiting_requested;
    bool suppress_connected_state;
    bool last_bt_connected;

    if (screen == NULL || parent == NULL)
    {
        return;
    }

    ui_status_preserve_runtime_state(&lcd_device,
                                     &sync_timer,
                                     &toast_timer,
                                     &brightness_steps,
                                     &volume_steps,
                                     &charging,
                                     &confirm_visible,
                                     &waiting_requested,
                                     &suppress_connected_state,
                                     &last_bt_connected);
    ui_status_restore_runtime_state(lcd_device,
                                    sync_timer,
                                    toast_timer,
                                    brightness_steps,
                                    volume_steps,
                                    charging,
                                    confirm_visible,
                                    waiting_requested,
                                    suppress_connected_state,
                                    last_bt_connected);
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
    size_t i;

    if (screen == NULL)
    {
        return NULL;
    }

    for (i = 0; i < sizeof(s_screen_refs) / sizeof(s_screen_refs[0]); ++i)
    {
        if (s_screen_refs[i].used && s_screen_refs[i].screen == screen)
        {
            return &s_screen_refs[i].refs;
        }
    }

    return NULL;
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
        s_status_panel.network_card = NULL;
        s_status_panel.network_subtitle_label = NULL;
    }

    for (i = 0; i < sizeof(s_screen_refs) / sizeof(s_screen_refs[0]); ++i)
    {
        if (s_screen_refs[i].screen == screen)
        {
            s_screen_refs[i].screen = NULL;
            memset(&s_screen_refs[i].refs, 0, sizeof(s_screen_refs[i].refs));
            s_screen_refs[i].used = false;
        }
    }
}

bool ui_status_panel_is_visible(void)
{
    return (s_status_panel.root != NULL) &&
           !lv_obj_has_flag(s_status_panel.root, LV_OBJ_FLAG_HIDDEN);
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
    s_status_pending_charge = is_charging ? 1 : 0;
}
