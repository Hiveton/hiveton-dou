#include "app_watchdog.h"

#include <rtthread.h>
#include <drivers/watchdog.h>

#define APP_WATCHDOG_THREAD_STACK_SIZE 1024U
#define APP_WATCHDOG_THREAD_PRIORITY   6
#define APP_WATCHDOG_THREAD_TICK       10
#define APP_WATCHDOG_FEED_INTERVAL_MS  2000U
#define APP_WATCHDOG_MIN_GAP_MS        80U

static struct rt_thread s_app_watchdog_thread;
static rt_uint8_t s_app_watchdog_stack[APP_WATCHDOG_THREAD_STACK_SIZE];
static volatile rt_tick_t s_last_pet_tick = 0;

static rt_bool_t app_watchdog_enabled(void)
{
    return rt_hw_watchdog_get_status() ? RT_TRUE : RT_FALSE;
}

static void app_watchdog_try_pet(void)
{
    rt_tick_t now;
    rt_tick_t min_gap_ticks;

    if (!app_watchdog_enabled())
    {
        return;
    }

    now = rt_tick_get();
    min_gap_ticks = rt_tick_from_millisecond(APP_WATCHDOG_MIN_GAP_MS);
    if ((s_last_pet_tick != 0) && ((now - s_last_pet_tick) < min_gap_ticks))
    {
        return;
    }

    rt_hw_watchdog_pet();
    s_last_pet_tick = now;
}

void app_watchdog_pet(void)
{
    app_watchdog_try_pet();
}

void app_watchdog_touch_hint(void)
{
    app_watchdog_try_pet();
}

void app_watchdog_input_hint(void)
{
    app_watchdog_try_pet();
}

static void app_watchdog_thread_entry(void *parameter)
{
    (void)parameter;

    while (1)
    {
        app_watchdog_try_pet();
        rt_thread_mdelay(APP_WATCHDOG_FEED_INTERVAL_MS);
    }
}

static int app_watchdog_init(void)
{
    if (rt_thread_init(&s_app_watchdog_thread,
                       "app_wdt",
                       app_watchdog_thread_entry,
                       RT_NULL,
                       s_app_watchdog_stack,
                       sizeof(s_app_watchdog_stack),
                       APP_WATCHDOG_THREAD_PRIORITY,
                       APP_WATCHDOG_THREAD_TICK) == RT_EOK)
    {
        rt_thread_startup(&s_app_watchdog_thread);
    }

    return 0;
}
INIT_COMPONENT_EXPORT(app_watchdog_init);
