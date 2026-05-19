#include "ui_dispatch.h"

#include <stdbool.h>

#include "rtthread.h"
#include "bf0_hal.h"
#include "lvgl.h"
#include "ui.h"
#include "ui_font_manager.h"
#include "ui/ui_helpers.h"
#include "ui_runtime_adapter.h"
#include "../sleep_manager.h"
#include "xiaozhi/weather/weather.h"

#define UI_DISPATCH_EVT_ACTIVITY        (1UL << 0)
#define UI_DISPATCH_EVT_TIME_REFRESH    (1UL << 1)
#define UI_DISPATCH_EVT_WEATHER_REFRESH (1UL << 2)
#define UI_DISPATCH_EVT_SWITCH_HOME     (1UL << 3)
#define UI_DISPATCH_EVT_SWITCH_AI_DOU   (1UL << 4)
#define UI_DISPATCH_EVT_STATUS_REFRESH  (1UL << 5)
#define UI_DISPATCH_EVT_SWITCH_STANDBY  (1UL << 6)
#define UI_DISPATCH_EVT_STANDBY_REFRESH (1UL << 7)
#define UI_DISPATCH_EVT_EXIT_STANDBY    (1UL << 8)
#define UI_DISPATCH_EVT_BACK            (1UL << 9)
#define UI_DISPATCH_EVT_HARDKEY_UP      (1UL << 10)
#define UI_DISPATCH_EVT_HARDKEY_DOWN    (1UL << 11)
#define UI_DISPATCH_EVT_POWEROFF_POPUP  (1UL << 12)
#define UI_DISPATCH_EVT_FONT_REFRESH    (1UL << 13)
#define UI_DISPATCH_EVT_HOME_TALK_PRESS (1UL << 14)
#define UI_DISPATCH_EVT_HOME_TALK_RELEASE (1UL << 15)
#define UI_DISPATCH_EVT_SWITCH_REQUEST  (1UL << 16)

static rt_event_t s_ui_dispatch_event = RT_NULL;
static struct rt_event s_ui_dispatch_event_static;
static volatile ui_screen_id_t s_ui_active_screen = UI_SCREEN_NONE;
static lv_obj_t *s_ui_poweroff_popup = NULL;
static lv_obj_t *s_ui_poweroff_status_label = NULL;
static volatile bool s_ui_status_refresh_pending = false;
static volatile ui_screen_id_t s_ui_requested_screen = UI_SCREEN_NONE;
static const rt_uint32_t UI_DISPATCH_EVT_SCREEN_SWITCH_MASK =
    (UI_DISPATCH_EVT_SWITCH_HOME |
     UI_DISPATCH_EVT_SWITCH_AI_DOU |
     UI_DISPATCH_EVT_SWITCH_STANDBY |
     UI_DISPATCH_EVT_SWITCH_REQUEST);
static const rt_uint32_t UI_DISPATCH_EVT_PAGE_TRANSITION_MASK =
    (UI_DISPATCH_EVT_SCREEN_SWITCH_MASK |
     UI_DISPATCH_EVT_EXIT_STANDBY |
     UI_DISPATCH_EVT_STATUS_REFRESH |
     UI_DISPATCH_EVT_TIME_REFRESH |
     UI_DISPATCH_EVT_WEATHER_REFRESH |
     UI_DISPATCH_EVT_STANDBY_REFRESH);
static const rt_uint32_t UI_DISPATCH_EVT_STATUS_MASK =
    (UI_DISPATCH_EVT_STATUS_REFRESH |
     UI_DISPATCH_EVT_TIME_REFRESH |
     UI_DISPATCH_EVT_WEATHER_REFRESH |
     UI_DISPATCH_EVT_STANDBY_REFRESH);

extern void app_set_panel_brightness(rt_uint8_t brightness);

static bool ui_dispatch_try_mark_status_refresh_pending(void)
{
    bool already_pending;

    rt_enter_critical();
    already_pending = s_ui_status_refresh_pending;
    if (!already_pending)
    {
        s_ui_status_refresh_pending = true;
    }
    rt_exit_critical();

    return !already_pending;
}

static void ui_dispatch_clear_status_refresh_pending(void)
{
    rt_enter_critical();
    s_ui_status_refresh_pending = false;
    rt_exit_critical();
}

static void ui_dispatch_set_requested_screen(ui_screen_id_t screen_id)
{
    rt_enter_critical();
    s_ui_requested_screen = screen_id;
    rt_exit_critical();
}

static ui_screen_id_t ui_dispatch_take_requested_screen(void)
{
    ui_screen_id_t screen_id;

    rt_enter_critical();
    screen_id = s_ui_requested_screen;
    s_ui_requested_screen = UI_SCREEN_NONE;
    rt_exit_critical();

    return screen_id;
}

static void ui_dispatch_poweroff_popup_close(void)
{
    if (s_ui_poweroff_popup != NULL)
    {
        lv_obj_del(s_ui_poweroff_popup);
        s_ui_poweroff_popup = NULL;
        s_ui_poweroff_status_label = NULL;
    }
}

static void ui_dispatch_poweroff_cancel_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        ui_dispatch_poweroff_popup_close();
    }
}

static void ui_dispatch_shutdown_now(void)
{
    rt_kprintf("shutdown...\n");
    app_set_panel_brightness(0U);
    rt_thread_mdelay(80);
    HAL_PMU_EnterShutdown();

    while (1)
    {
        rt_thread_mdelay(1000);
    }
}

static void ui_dispatch_poweroff_confirm_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        if (s_ui_poweroff_status_label != NULL)
        {
            lv_label_set_text(s_ui_poweroff_status_label, "正在关机...");
        }
        lv_obj_invalidate(lv_layer_top());
        lv_refr_now(NULL);
        ui_dispatch_shutdown_now();
    }
}

static void ui_dispatch_show_poweroff_popup(void)
{
    lv_obj_t *panel;
    lv_obj_t *cancel_btn;
    lv_obj_t *power_btn;
    lv_obj_t *power_label;

    if (s_ui_poweroff_popup != NULL)
    {
        return;
    }

    s_ui_poweroff_popup = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_ui_poweroff_popup);
    lv_obj_set_size(s_ui_poweroff_popup,
                    lv_display_get_horizontal_resolution(NULL),
                    lv_display_get_vertical_resolution(NULL));
    lv_obj_set_style_bg_color(s_ui_poweroff_popup, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_ui_poweroff_popup, LV_OPA_30, 0);
    lv_obj_add_flag(s_ui_poweroff_popup, LV_OBJ_FLAG_CLICKABLE);

    panel = ui_create_card(s_ui_poweroff_popup, 54, 254, 420, 250, UI_SCREEN_NONE, false, 16);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    (void)ui_create_label(panel,
                          "是否关机？",
                          34,
                          30,
                          352,
                          40,
                          30,
                          LV_TEXT_ALIGN_CENTER,
                          false,
                          false);

    s_ui_poweroff_status_label = ui_create_label(panel,
                                                 "设备将进入关机状态\n长按电源键可重新开机",
                                                 40,
                                                 82,
                                                 340,
                                                 66,
                                                 20,
                                                 LV_TEXT_ALIGN_CENTER,
                                                 false,
                                                 true);
    lv_obj_set_style_text_color(s_ui_poweroff_status_label, lv_color_hex(0x30384f), 0);

    cancel_btn = ui_create_button(panel, 36, 170, 154, 56, "取消", 24, UI_SCREEN_NONE, false);
    lv_obj_set_style_radius(cancel_btn, 12, 0);
    lv_obj_add_event_cb(cancel_btn, ui_dispatch_poweroff_cancel_cb, LV_EVENT_CLICKED, NULL);

    power_btn = ui_create_button(panel, 230, 170, 154, 56, "关机", 24, UI_SCREEN_NONE, false);
    lv_obj_set_style_radius(power_btn, 12, 0);
    lv_obj_set_style_bg_color(power_btn, lv_color_hex(0x30384f), 0);
    lv_obj_set_style_bg_opa(power_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(power_btn, 0, 0);
    power_label = lv_obj_get_child(power_btn, 0);
    if (power_label != NULL)
    {
        lv_obj_set_style_text_color(power_label, lv_color_hex(0xffffff), 0);
    }
    lv_obj_add_event_cb(power_btn, ui_dispatch_poweroff_confirm_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_update_layout(s_ui_poweroff_popup);
    lv_obj_invalidate(s_ui_poweroff_popup);
    lv_refr_now(NULL);
}

rt_err_t ui_dispatch_init(void)
{
    if (s_ui_dispatch_event != RT_NULL)
    {
        return RT_EOK;
    }

    if (rt_event_init(&s_ui_dispatch_event_static, "ui_disp", RT_IPC_FLAG_FIFO) != RT_EOK)
    {
        return -RT_ENOMEM;
    }
    s_ui_dispatch_event = &s_ui_dispatch_event_static;

    return RT_EOK;
}

static void ui_dispatch_send(rt_uint32_t evt)
{
    if (s_ui_dispatch_event != RT_NULL)
    {
        rt_uint32_t dedup_mask = 0U;

        if ((evt & UI_DISPATCH_EVT_PAGE_TRANSITION_MASK) != 0U)
        {
            dedup_mask = UI_DISPATCH_EVT_PAGE_TRANSITION_MASK;
        }
        else if ((evt & UI_DISPATCH_EVT_STATUS_MASK) != 0U)
        {
            dedup_mask = (evt & UI_DISPATCH_EVT_STATUS_MASK);
        }
        else if ((evt & UI_DISPATCH_EVT_ACTIVITY) != 0U)
        {
            dedup_mask = UI_DISPATCH_EVT_ACTIVITY;
        }

        if (dedup_mask != 0U)
        {
            rt_uint32_t dropped;

            (void)rt_event_recv(s_ui_dispatch_event,
                                dedup_mask,
                                RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                                0,
                                &dropped);
        }

        rt_event_send(s_ui_dispatch_event, evt);
    }
}

void ui_dispatch_process_pending(void)
{
    rt_uint32_t events;

    if (s_ui_dispatch_event == RT_NULL)
    {
        return;
    }

    while (rt_event_recv(s_ui_dispatch_event,
                         UI_DISPATCH_EVT_ACTIVITY |
                         UI_DISPATCH_EVT_STATUS_REFRESH |
                         UI_DISPATCH_EVT_TIME_REFRESH |
                         UI_DISPATCH_EVT_WEATHER_REFRESH |
                         UI_DISPATCH_EVT_STANDBY_REFRESH |
                         UI_DISPATCH_EVT_EXIT_STANDBY |
                         UI_DISPATCH_EVT_BACK |
                         UI_DISPATCH_EVT_HARDKEY_UP |
                         UI_DISPATCH_EVT_HARDKEY_DOWN |
                         UI_DISPATCH_EVT_HOME_TALK_PRESS |
                         UI_DISPATCH_EVT_HOME_TALK_RELEASE |
                         UI_DISPATCH_EVT_POWEROFF_POPUP |
                         UI_DISPATCH_EVT_FONT_REFRESH |
                         UI_DISPATCH_EVT_SWITCH_HOME |
                         UI_DISPATCH_EVT_SWITCH_AI_DOU |
                         UI_DISPATCH_EVT_SWITCH_REQUEST |
                         UI_DISPATCH_EVT_SWITCH_STANDBY,
                         RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                         0,
                         &events) == RT_EOK)
    {
        if ((events & UI_DISPATCH_EVT_ACTIVITY) != 0U)
        {
            sleep_manager_report_activity();
            lv_display_trigger_activity(NULL);
        }

        if ((events & UI_DISPATCH_EVT_BACK) != 0U)
        {
            ui_runtime_go_back();
        }

        if ((events & UI_DISPATCH_EVT_EXIT_STANDBY) != 0U)
        {
            ui_runtime_exit_standby();
        }

        if ((events & UI_DISPATCH_EVT_SWITCH_HOME) != 0U)
        {
            ui_runtime_switch_to(UI_SCREEN_HOME);
        }

        if ((events & UI_DISPATCH_EVT_SWITCH_AI_DOU) != 0U)
        {
            ui_runtime_switch_to(UI_SCREEN_AI_DOU);
        }

        if ((events & UI_DISPATCH_EVT_SWITCH_STANDBY) != 0U)
        {
            ui_runtime_switch_to(UI_SCREEN_STANDBY);
        }

        if ((events & UI_DISPATCH_EVT_SWITCH_REQUEST) != 0U)
        {
            ui_screen_id_t target = ui_dispatch_take_requested_screen();

            if (target != UI_SCREEN_NONE)
            {
                ui_runtime_switch_to(target);
            }
        }

        if ((events & UI_DISPATCH_EVT_STATUS_REFRESH) != 0U)
        {
            ui_dispatch_clear_status_refresh_pending();
            ui_refresh_global_status_bar();
            if (ui_dispatch_get_active_screen() == UI_SCREEN_SETTINGS)
            {
                ui_settings_refresh_summaries();
            }
        }

        if ((events & UI_DISPATCH_EVT_HARDKEY_UP) != 0U)
        {
            ui_runtime_handle_hardkey_nav(-1);
        }

        if ((events & UI_DISPATCH_EVT_HARDKEY_DOWN) != 0U)
        {
            ui_runtime_handle_hardkey_nav(1);
        }

        if ((events & UI_DISPATCH_EVT_HOME_TALK_PRESS) != 0U)
        {
            if (ui_dispatch_get_active_screen() == UI_SCREEN_HOME)
            {
                ui_home_ai_hardware_talk_press();
            }
        }

        if ((events & UI_DISPATCH_EVT_HOME_TALK_RELEASE) != 0U)
        {
            if (ui_dispatch_get_active_screen() == UI_SCREEN_HOME)
            {
                ui_home_ai_hardware_talk_release();
            }
        }

        if ((events & UI_DISPATCH_EVT_TIME_REFRESH) != 0U)
        {
            time_ui_update_callback();
        }

        if ((events & UI_DISPATCH_EVT_WEATHER_REFRESH) != 0U)
        {
            weather_ui_update_callback();
        }

        if ((events & UI_DISPATCH_EVT_STANDBY_REFRESH) != 0U)
        {
            ui_standby_screen_refresh_now();
        }

        if ((events & UI_DISPATCH_EVT_POWEROFF_POPUP) != 0U)
        {
            ui_dispatch_show_poweroff_popup();
        }

        if ((events & UI_DISPATCH_EVT_FONT_REFRESH) != 0U)
        {
            ui_font_manager_rebuild_ui();
        }
    }
}

void ui_dispatch_request_activity(void)
{
    sleep_manager_report_activity();
    ui_dispatch_send(UI_DISPATCH_EVT_ACTIVITY);
}

void ui_dispatch_request_status_refresh(void)
{
    if (s_ui_dispatch_event == RT_NULL)
    {
        return;
    }

    if (!ui_dispatch_try_mark_status_refresh_pending())
    {
        return;
    }

    ui_dispatch_send(UI_DISPATCH_EVT_STATUS_REFRESH);
}

void ui_dispatch_request_time_refresh(void)
{
    ui_dispatch_send(UI_DISPATCH_EVT_TIME_REFRESH);
}

void ui_dispatch_request_weather_refresh(void)
{
    ui_dispatch_send(UI_DISPATCH_EVT_WEATHER_REFRESH);
}

void ui_dispatch_request_standby_refresh(void)
{
    ui_dispatch_send(UI_DISPATCH_EVT_STANDBY_REFRESH);
}

void ui_dispatch_request_exit_standby(void)
{
    ui_dispatch_send(UI_DISPATCH_EVT_EXIT_STANDBY);
}

void ui_dispatch_request_back(void)
{
    ui_dispatch_request_activity();
    ui_dispatch_send(UI_DISPATCH_EVT_BACK);
}

void ui_dispatch_request_hardkey_up(void)
{
    ui_dispatch_request_activity();
    ui_dispatch_send(UI_DISPATCH_EVT_HARDKEY_UP);
}

void ui_dispatch_request_hardkey_down(void)
{
    ui_dispatch_request_activity();
    ui_dispatch_send(UI_DISPATCH_EVT_HARDKEY_DOWN);
}

void ui_dispatch_request_home_ai_talk_press(void)
{
    ui_dispatch_request_activity();
    ui_dispatch_send(UI_DISPATCH_EVT_HOME_TALK_PRESS);
}

void ui_dispatch_request_home_ai_talk_release(void)
{
    ui_dispatch_request_activity();
    ui_dispatch_send(UI_DISPATCH_EVT_HOME_TALK_RELEASE);
}

void ui_dispatch_request_poweroff_confirm(void)
{
    ui_dispatch_send(UI_DISPATCH_EVT_POWEROFF_POPUP);
}

void ui_dispatch_request_font_refresh(void)
{
    ui_dispatch_send(UI_DISPATCH_EVT_FONT_REFRESH);
}

void ui_dispatch_request_screen_switch(ui_screen_id_t screen_id)
{
    ui_screen_id_t active = ui_dispatch_get_active_screen();

    if (screen_id == active && active != UI_SCREEN_STANDBY)
    {
        ui_dispatch_request_activity();
        return;
    }

    switch (screen_id)
    {
    case UI_SCREEN_NONE:
        ui_dispatch_request_exit_standby();
        break;
    case UI_SCREEN_HOME:
        ui_dispatch_request_activity();
        ui_dispatch_send(UI_DISPATCH_EVT_SWITCH_HOME);
        break;
    case UI_SCREEN_AI_DOU:
        ui_dispatch_request_activity();
        ui_dispatch_send(UI_DISPATCH_EVT_SWITCH_AI_DOU);
        break;
    case UI_SCREEN_STANDBY:
        ui_dispatch_send(UI_DISPATCH_EVT_SWITCH_STANDBY);
        break;
    default:
        ui_dispatch_request_activity();
        ui_dispatch_set_requested_screen(screen_id);
        ui_dispatch_send(UI_DISPATCH_EVT_SWITCH_REQUEST);
        break;
    }
}

void ui_dispatch_set_active_screen(ui_screen_id_t screen_id)
{
    rt_enter_critical();
    s_ui_active_screen = screen_id;
    rt_exit_critical();
}

ui_screen_id_t ui_dispatch_get_active_screen(void)
{
    ui_screen_id_t screen_id;

    rt_enter_critical();
    screen_id = s_ui_active_screen;
    rt_exit_critical();

    return screen_id;
}
