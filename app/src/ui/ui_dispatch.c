#include "ui_dispatch.h"

#include "lvgl.h"
#include "ui/ui_helpers.h"
#include "ui_runtime_adapter.h"
#include "xiaozhi/weather/weather.h"

#define UI_DISPATCH_EVT_ACTIVITY        (1UL << 0)
#define UI_DISPATCH_EVT_TIME_REFRESH    (1UL << 1)
#define UI_DISPATCH_EVT_WEATHER_REFRESH (1UL << 2)
#define UI_DISPATCH_EVT_SWITCH_HOME     (1UL << 3)
#define UI_DISPATCH_EVT_SWITCH_AI_DOU   (1UL << 4)
#define UI_DISPATCH_EVT_STATUS_REFRESH  (1UL << 5)

static rt_event_t s_ui_dispatch_event = RT_NULL;
static volatile ui_screen_id_t s_ui_active_screen = UI_SCREEN_NONE;

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
                         UI_DISPATCH_EVT_SWITCH_HOME |
                         UI_DISPATCH_EVT_SWITCH_AI_DOU,
                         RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                         0,
                         &events) == RT_EOK)
    {
        if ((events & UI_DISPATCH_EVT_ACTIVITY) != 0U)
        {
            lv_display_trigger_activity(NULL);
        }

        if ((events & UI_DISPATCH_EVT_STATUS_REFRESH) != 0U)
        {
            ui_refresh_global_status_bar();
        }

        if ((events & UI_DISPATCH_EVT_SWITCH_HOME) != 0U)
        {
            ui_runtime_switch_to(UI_SCREEN_HOME);
        }

        if ((events & UI_DISPATCH_EVT_SWITCH_AI_DOU) != 0U)
        {
            ui_runtime_switch_to(UI_SCREEN_AI_DOU);
        }

        if ((events & UI_DISPATCH_EVT_TIME_REFRESH) != 0U)
        {
            time_ui_update_callback();
        }

        if ((events & UI_DISPATCH_EVT_WEATHER_REFRESH) != 0U)
        {
            weather_ui_update_callback();
        }
    }
}

void ui_dispatch_request_activity(void)
{
    ui_dispatch_send(UI_DISPATCH_EVT_ACTIVITY);
}

void ui_dispatch_request_status_refresh(void)
{
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

void ui_dispatch_request_screen_switch(ui_screen_id_t screen_id)
{
    switch (screen_id)
    {
    case UI_SCREEN_HOME:
        ui_dispatch_send(UI_DISPATCH_EVT_SWITCH_HOME);
        break;
    case UI_SCREEN_AI_DOU:
        ui_dispatch_send(UI_DISPATCH_EVT_SWITCH_AI_DOU);
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
