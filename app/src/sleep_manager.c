#include "sleep_manager.h"

#include <string.h>
#include <limits.h>

#include <rtthread.h>
#include <rtdevice.h>
#include <drivers/alarm.h>

#include "app_watchdog.h"
#include "config/app_config.h"
#include "network/net_manager.h"
#include "ui/ui_dispatch.h"
#include "gui_app_pm.h"

/*
 * EPD panels retain pixels while the system sleeps. Keep the app-layer default
 * aligned with GUI PM so EPD sleep does not regress into a clear/blank path.
 */
#ifndef APP_KEEP_EPD_CONTENT_ON_SLEEP
#if defined(BSP_LCDC_USING_EPD_8BIT) || defined(LCD_USING_ST7789_GTM024_08_SPI8P)
#define APP_KEEP_EPD_CONTENT_ON_SLEEP 1
#else
#define APP_KEEP_EPD_CONTENT_ON_SLEEP 0
#endif
#endif

#if (defined(BSP_LCDC_USING_EPD_8BIT) || defined(LCD_USING_ST7789_GTM024_08_SPI8P)) && !APP_KEEP_EPD_CONTENT_ON_SLEEP
#error "EPD targets must keep display content on sleep; do not clear or blank the panel."
#endif

#define SLEEP_MANAGER_NETWORK_SETUP_GRACE_MS 180000U

static bool s_sleeping = false;
static bool s_standby_pending = false;
static ui_screen_id_t s_last_source_screen = UI_SCREEN_HOME;
static rt_alarm_t s_minute_alarm = RT_NULL;
static bool s_activity_reported = false;
static rt_tick_t s_last_activity_tick = 0;

static void sleep_manager_ensure_alarm(void);

static bool sleep_manager_is_idle_exempt_screen(ui_screen_id_t screen_id)
{
    return (screen_id == UI_SCREEN_NONE ||
            screen_id == UI_SCREEN_STANDBY ||
            screen_id == UI_SCREEN_READING_DETAIL);
}

static void sleep_manager_begin_sleep_cycle(void)
{
    if (s_sleeping)
    {
        return;
    }

    s_sleeping = true;
    app_watchdog_set_mode(APP_WDT_MODE_SLEEP);
    s_standby_pending = false;
    sleep_manager_ensure_alarm();
    if (s_minute_alarm != RT_NULL)
    {
        if (rt_alarm_start(s_minute_alarm) != RT_EOK)
        {
            rt_kprintf("sleep_mgr: minute alarm start failed\n");
        }
    }
    net_manager_suspend_for_sleep();
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
    uint32_t timeout_sec = app_config_get_display_standby_timeout_sec();

    if (timeout_sec > (UINT_MAX / 1000U))
    {
        return UINT_MAX;
    }

    return timeout_sec * 1000U;
}

void sleep_manager_report_activity(void)
{
    s_activity_reported = true;
    s_last_activity_tick = rt_tick_get();
}

bool sleep_manager_should_enter_standby(ui_screen_id_t active_id, uint32_t inactive_ms)
{
    uint32_t idle_timeout_ms;
    uint32_t report_idle_ms;
    net_manager_snapshot_t net_snapshot;

    if (sleep_manager_is_idle_exempt_screen(active_id))
    {
        return false;
    }

    report_idle_ms = sleep_manager_get_report_idle_ms();
    idle_timeout_ms = sleep_manager_get_idle_timeout_ms();
    net_manager_get_snapshot(&net_snapshot);
    if (net_snapshot.desired_mode == NET_MANAGER_MODE_4G &&
        net_snapshot.net_4g_enabled &&
        !net_snapshot.cat1_ready &&
        (inactive_ms < SLEEP_MANAGER_NETWORK_SETUP_GRACE_MS ||
         report_idle_ms < SLEEP_MANAGER_NETWORK_SETUP_GRACE_MS))
    {
        return false;
    }

    return inactive_ms >= idle_timeout_ms && report_idle_ms >= idle_timeout_ms;
}

void sleep_manager_on_enter_standby(ui_screen_id_t from_screen)
{
    if (from_screen != UI_SCREEN_NONE && from_screen != UI_SCREEN_STANDBY)
    {
        s_last_source_screen = from_screen;
    }

    if (s_sleeping)
    {
        s_standby_pending = false;
        return;
    }

    s_standby_pending = true;
}

void sleep_manager_on_exit_standby(ui_screen_id_t target_screen)
{
    (void)target_screen;

    s_standby_pending = false;
    if (!s_sleeping)
    {
        return;
    }

    s_sleeping = false;
    app_watchdog_set_mode(APP_WDT_MODE_ACTIVE);
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
    ui_screen_id_t active_screen = ui_dispatch_get_active_screen();

    if (active_screen == UI_SCREEN_STANDBY)
    {
        if (s_sleeping && gui_pm_is_ready())
        {
            gui_pm_fsm(GUI_PM_ACTION_WAKEUP);
        }
        ui_dispatch_request_exit_standby();
        return;
    }

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
        if (!s_standby_pending)
        {
            return;
        }

        sleep_manager_begin_sleep_cycle();
    }

    if (!s_sleeping)
    {
        return;
    }

    if (gui_pm_is_ready())
    {
        gui_pm_fsm(GUI_PM_ACTION_SLEEP);
    }
}
