#include "sleep_manager.h"

#include <string.h>
#include <limits.h>

#include <rtthread.h>
#include <rtdevice.h>
#include <drivers/alarm.h>

#include "network/net_manager.h"
#include "ui/ui_dispatch.h"
#include "gui_app_pm.h"

#define SLEEP_MANAGER_IDLE_TIMEOUT_MS 60000U

static bool s_sleeping = false;
static ui_screen_id_t s_last_source_screen = UI_SCREEN_HOME;
static rt_alarm_t s_minute_alarm = RT_NULL;
static bool s_activity_reported = false;
static rt_tick_t s_last_activity_tick = 0;

static bool sleep_manager_is_idle_exempt_screen(ui_screen_id_t screen_id)
{
    return (screen_id == UI_SCREEN_NONE ||
            screen_id == UI_SCREEN_STANDBY ||
            screen_id == UI_SCREEN_AI_DOU ||
            screen_id == UI_SCREEN_READING_DETAIL);
}

static void sleep_manager_minute_alarm_cb(rt_alarm_t alarm, time_t timestamp)
{
    (void)alarm;
    (void)timestamp;

    if (!s_sleeping)
    {
        return;
    }

    if (gui_pm_is_ready())
    {
        gui_pm_fsm(GUI_PM_ACTION_WAKEUP);
    }
    ui_dispatch_request_standby_refresh();
}

static uint32_t sleep_manager_get_report_idle_ms(void)
{
    rt_tick_t now;
    rt_tick_t elapsed;

    if (!s_activity_reported)
    {
        return UINT_MAX;
    }

    now = rt_tick_get();
    elapsed = now - s_last_activity_tick;
    return (uint32_t)((elapsed * 1000U) / RT_TICK_PER_SECOND);
}

static void sleep_manager_ensure_alarm(void)
{
    struct rt_alarm_setup setup;

    if (s_minute_alarm != RT_NULL)
    {
        return;
    }

    memset(&setup, 0, sizeof(setup));
    setup.flag = RT_ALARM_MINUTE;
    setup.wktime.tm_sec = 0;
    s_minute_alarm = rt_alarm_create(sleep_manager_minute_alarm_cb, &setup);
    if (s_minute_alarm == RT_NULL)
    {
        rt_kprintf("sleep_mgr: minute alarm create failed\n");
    }
}

uint32_t sleep_manager_get_idle_timeout_ms(void)
{
    return SLEEP_MANAGER_IDLE_TIMEOUT_MS;
}

void sleep_manager_report_activity(void)
{
    s_activity_reported = true;
    s_last_activity_tick = rt_tick_get();
}

bool sleep_manager_should_enter_standby(ui_screen_id_t active_id, uint32_t inactive_ms)
{
    uint32_t report_idle_ms;

    if (sleep_manager_is_idle_exempt_screen(active_id))
    {
        return false;
    }

    report_idle_ms = sleep_manager_get_report_idle_ms();
    return inactive_ms >= SLEEP_MANAGER_IDLE_TIMEOUT_MS &&
           report_idle_ms >= SLEEP_MANAGER_IDLE_TIMEOUT_MS;
}

void sleep_manager_on_enter_standby(ui_screen_id_t from_screen)
{
    if (from_screen != UI_SCREEN_NONE && from_screen != UI_SCREEN_STANDBY)
    {
        s_last_source_screen = from_screen;
    }

    if (s_sleeping)
    {
        return;
    }

    s_sleeping = true;
    sleep_manager_ensure_alarm();
    if (s_minute_alarm != RT_NULL)
    {
        if (rt_alarm_start(s_minute_alarm) != RT_EOK)
        {
            rt_kprintf("sleep_mgr: minute alarm start failed\n");
        }
    }
    net_manager_suspend_for_sleep();
    if (gui_pm_is_ready())
    {
        gui_pm_fsm(GUI_PM_ACTION_SLEEP);
    }
}

void sleep_manager_on_exit_standby(ui_screen_id_t target_screen)
{
    (void)target_screen;

    if (!s_sleeping)
    {
        return;
    }

    s_sleeping = false;
    if (s_minute_alarm != RT_NULL)
    {
        rt_alarm_stop(s_minute_alarm);
    }
    net_manager_resume_after_wake();
    if (gui_pm_is_ready())
    {
        gui_pm_fsm(GUI_PM_ACTION_WAKEUP);
    }
    ui_dispatch_request_activity();
}

bool sleep_manager_is_sleeping(void)
{
    return s_sleeping;
}

void sleep_manager_request_wakeup(void)
{
    if (s_sleeping)
    {
        if (gui_pm_is_ready())
        {
            gui_pm_fsm(GUI_PM_ACTION_WAKEUP);
        }
        ui_dispatch_request_exit_standby();
        return;
    }

    if (gui_pm_is_ready())
    {
        gui_pm_fsm(GUI_PM_ACTION_WAKEUP);
    }
    ui_dispatch_request_activity();
}

void sleep_manager_resume_sleep_cycle(void)
{
    if (!s_sleeping)
    {
        return;
    }

    if (gui_pm_is_ready())
    {
        gui_pm_fsm(GUI_PM_ACTION_SLEEP);
    }
}
