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

static rt_event_t s_ui_dispatch_event = RT_NULL;
static volatile ui_screen_id_t s_ui_active_screen = UI_SCREEN_NONE;
static lv_obj_t *s_ui_poweroff_popup = NULL;
static volatile bool s_ui_status_refresh_pending = false;

static void ui_dispatch_poweroff_popup_close(void)
{
    if (s_ui_poweroff_popup != NULL)
    {
        lv_obj_del(s_ui_poweroff_popup);
        s_ui_poweroff_popup = NULL;
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
        ui_dispatch_poweroff_popup_close();
        ui_dispatch_shutdown_now();
    }
}

static void ui_dispatch_show_poweroff_popup(void)
{
    lv_obj_t *panel;
    lv_obj_t *label;
    lv_obj_t *btn;

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

    panel = lv_obj_create(s_ui_poweroff_popup);
    lv_obj_set_size(panel, 392, 186);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    label = lv_label_create(panel);
    lv_label_set_text(label, "确认关机");
    lv_obj_set_style_text_color(label, lv_color_hex(0x000000), 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 28);

    btn = lv_btn_create(panel);
    lv_obj_set_size(btn, 132, 52);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_LEFT, 34, -24);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_add_event_cb(btn, ui_dispatch_poweroff_cancel_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(btn);
    lv_label_set_text(label, "取消");
    lv_obj_center(label);

    btn = lv_btn_create(panel);
    lv_obj_set_size(btn, 162, 52);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, -28, -24);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_add_event_cb(btn, ui_dispatch_poweroff_confirm_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(btn);
    lv_label_set_text(label, "确认关机");
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(label);
}

rt_err_t ui_dispatch_init(void)
{
    if (s_ui_dispatch_event != RT_NULL)
    {
        return RT_EOK;
    }

    s_ui_dispatch_event = rt_event_create("ui_disp", RT_IPC_FLAG_FIFO);
    if (s_ui_dispatch_event == RT_NULL)
    {
        return -RT_ENOMEM;
    }

    return RT_EOK;
}

static void ui_dispatch_send(rt_uint32_t evt)
{
    if (s_ui_dispatch_event != RT_NULL)
    {
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
                         UI_DISPATCH_EVT_POWEROFF_POPUP |
                         UI_DISPATCH_EVT_FONT_REFRESH |
                         UI_DISPATCH_EVT_SWITCH_HOME |
                         UI_DISPATCH_EVT_SWITCH_AI_DOU |
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

        if ((events & UI_DISPATCH_EVT_STATUS_REFRESH) != 0U)
        {
            s_ui_status_refresh_pending = false;
            ui_refresh_global_status_bar();
        }

        if ((events & UI_DISPATCH_EVT_HARDKEY_UP) != 0U)
        {
            ui_runtime_handle_hardkey_nav(-1);
        }

        if ((events & UI_DISPATCH_EVT_HARDKEY_DOWN) != 0U)
        {
            ui_runtime_handle_hardkey_nav(1);
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

    if (s_ui_status_refresh_pending)
    {
        return;
    }

    s_ui_status_refresh_pending = true;
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
    ui_dispatch_send(UI_DISPATCH_EVT_BACK);
}

void ui_dispatch_request_hardkey_up(void)
{
    ui_dispatch_send(UI_DISPATCH_EVT_HARDKEY_UP);
}

void ui_dispatch_request_hardkey_down(void)
{
    ui_dispatch_send(UI_DISPATCH_EVT_HARDKEY_DOWN);
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
    switch (screen_id)
    {
    case UI_SCREEN_NONE:
        ui_dispatch_request_exit_standby();
        break;
    case UI_SCREEN_HOME:
        ui_dispatch_send(UI_DISPATCH_EVT_SWITCH_HOME);
        break;
    case UI_SCREEN_AI_DOU:
        ui_dispatch_send(UI_DISPATCH_EVT_SWITCH_AI_DOU);
        break;
    case UI_SCREEN_STANDBY:
        ui_dispatch_send(UI_DISPATCH_EVT_SWITCH_STANDBY);
        break;
    default:
        break;
    }
}

void ui_dispatch_set_active_screen(ui_screen_id_t screen_id)
{
    s_ui_active_screen = screen_id;
}

ui_screen_id_t ui_dispatch_get_active_screen(void)
{
    return s_ui_active_screen;
}
