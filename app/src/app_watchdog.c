#include "app_watchdog.h"

#include <rtthread.h>
#include <drivers/watchdog.h>
#include <string.h>

#define APP_WATCHDOG_THREAD_STACK_SIZE 1024U
#define APP_WATCHDOG_THREAD_PRIORITY   6
#define APP_WATCHDOG_THREAD_TICK       10
#define APP_WATCHDOG_FEED_INTERVAL_MS  2000U
#define APP_WATCHDOG_DEFAULT_UI_TIMEOUT_MS       10000U
#define APP_WATCHDOG_DEFAULT_NET_TIMEOUT_MS      20000U
#define APP_WATCHDOG_DEFAULT_CAT1_TIMEOUT_MS    180000U
#define APP_WATCHDOG_DEFAULT_XIAOZHI_TIMEOUT_MS  20000U
#define APP_WATCHDOG_DEFAULT_AUDIO_TIMEOUT_MS    10000U
#define APP_WATCHDOG_DEFAULT_READING_TIMEOUT_MS  60000U

static struct rt_thread s_app_watchdog_thread;
static rt_uint8_t s_app_watchdog_stack[APP_WATCHDOG_THREAD_STACK_SIZE];
static struct
{
    app_watchdog_mode_t mode;
    rt_tick_t last_heartbeat[APP_WDT_MODULE_COUNT];
    rt_tick_t last_progress[APP_WDT_MODULE_COUNT];
    rt_tick_t long_task_timeout[APP_WDT_MODULE_COUNT];
    rt_bool_t required[APP_WDT_MODULE_COUNT];
    rt_bool_t long_task_active[APP_WDT_MODULE_COUNT];
} s_app_watchdog_state =
{
    .mode = APP_WDT_MODE_BOOT,
    .required =
    {
        [APP_WDT_MODULE_UI] = RT_TRUE,
    },
};

static const char *const s_app_watchdog_mode_name[] =
{
    "BOOT",
    "ACTIVE",
    "SLEEP",
    "LONG_TASK",
    "SHUTDOWN",
};

static const char *const s_app_watchdog_module_name[] =
{
    "UI",
    "NET",
    "CAT1",
    "XIAOZHI",
    "AUDIO",
    "READING",
    "BUTTON",
    "WEATHER",
};

static rt_base_t app_watchdog_lock(void)
{
    rt_enter_critical();
    return 0;
}

static void app_watchdog_unlock(rt_base_t level)
{
    (void)level;
    rt_exit_critical();
}

static rt_bool_t app_watchdog_module_valid(app_watchdog_module_t module)
{
    return (module >= APP_WDT_MODULE_UI) && (module < APP_WDT_MODULE_COUNT) ? RT_TRUE : RT_FALSE;
}

static rt_bool_t app_watchdog_mode_valid(app_watchdog_mode_t mode)
{
    return (mode >= APP_WDT_MODE_BOOT) && (mode <= APP_WDT_MODE_SHUTDOWN) ? RT_TRUE : RT_FALSE;
}

static const char *app_watchdog_mode_name(app_watchdog_mode_t mode)
{
    if (!app_watchdog_mode_valid(mode))
    {
        return "UNKNOWN";
    }

    return s_app_watchdog_mode_name[mode];
}

static const char *app_watchdog_module_name(app_watchdog_module_t module)
{
    if (!app_watchdog_module_valid(module))
    {
        return "UNKNOWN";
    }

    return s_app_watchdog_module_name[module];
}

static rt_tick_t app_watchdog_ms_to_ticks(uint32_t timeout_ms)
{
    rt_tick_t ticks;

    if (timeout_ms == 0U)
    {
        return 0;
    }

    ticks = rt_tick_from_millisecond(timeout_ms);
    return (ticks == 0) ? 1 : ticks;
}

static uint32_t app_watchdog_default_timeout_ms(app_watchdog_module_t module)
{
    switch (module)
    {
    case APP_WDT_MODULE_UI:
        return APP_WATCHDOG_DEFAULT_UI_TIMEOUT_MS;
    case APP_WDT_MODULE_NET:
        return APP_WATCHDOG_DEFAULT_NET_TIMEOUT_MS;
    case APP_WDT_MODULE_CAT1:
        return APP_WATCHDOG_DEFAULT_CAT1_TIMEOUT_MS;
    case APP_WDT_MODULE_XIAOZHI:
        return APP_WATCHDOG_DEFAULT_XIAOZHI_TIMEOUT_MS;
    case APP_WDT_MODULE_AUDIO:
        return APP_WATCHDOG_DEFAULT_AUDIO_TIMEOUT_MS;
    case APP_WDT_MODULE_READING:
        return APP_WATCHDOG_DEFAULT_READING_TIMEOUT_MS;
    case APP_WDT_MODULE_BUTTON:
    case APP_WDT_MODULE_WEATHER:
    default:
        return 0U;
    }
}

static void app_watchdog_mark_heartbeat(app_watchdog_module_t module, rt_bool_t mark_progress)
{
    rt_base_t level;
    rt_tick_t now;

    if (!app_watchdog_module_valid(module))
    {
        return;
    }

    now = rt_tick_get();
    level = app_watchdog_lock();
    s_app_watchdog_state.last_heartbeat[module] = now;
    if (mark_progress)
    {
        s_app_watchdog_state.last_progress[module] = now;
    }
    app_watchdog_unlock(level);
}

void app_watchdog_set_mode(app_watchdog_mode_t mode)
{
    rt_base_t level;

    if (!app_watchdog_mode_valid(mode))
    {
        return;
    }

    level = app_watchdog_lock();
    s_app_watchdog_state.mode = mode;
    app_watchdog_unlock(level);
}

void app_watchdog_heartbeat(app_watchdog_module_t module)
{
    app_watchdog_mark_heartbeat(module, RT_FALSE);
}

void app_watchdog_begin_long_task(app_watchdog_module_t module, uint32_t timeout_ms)
{
    rt_base_t level;
    rt_tick_t now;
    rt_tick_t timeout_ticks;

    if (!app_watchdog_module_valid(module))
    {
        return;
    }

    if (timeout_ms == 0U)
    {
        timeout_ms = app_watchdog_default_timeout_ms(module);
    }

    timeout_ticks = app_watchdog_ms_to_ticks(timeout_ms);
    now = rt_tick_get();

    level = app_watchdog_lock();
    s_app_watchdog_state.long_task_active[module] = RT_TRUE;
    s_app_watchdog_state.long_task_timeout[module] = timeout_ticks;
    s_app_watchdog_state.last_progress[module] = now;
    s_app_watchdog_state.last_heartbeat[module] = now;
    app_watchdog_unlock(level);
}

void app_watchdog_progress(app_watchdog_module_t module)
{
    app_watchdog_mark_heartbeat(module, RT_TRUE);
}

void app_watchdog_end_long_task(app_watchdog_module_t module)
{
    rt_base_t level;

    if (!app_watchdog_module_valid(module))
    {
        return;
    }

    level = app_watchdog_lock();
    s_app_watchdog_state.long_task_active[module] = RT_FALSE;
    s_app_watchdog_state.long_task_timeout[module] = 0;
    app_watchdog_unlock(level);
}

void app_watchdog_set_module_required(app_watchdog_module_t module, bool required)
{
    rt_base_t level;

    if (!app_watchdog_module_valid(module))
    {
        return;
    }

    level = app_watchdog_lock();
    s_app_watchdog_state.required[module] = required ? RT_TRUE : RT_FALSE;
    app_watchdog_unlock(level);
}

void app_watchdog_pet(void)
{
    app_watchdog_progress(APP_WDT_MODULE_UI);
}

void app_watchdog_touch_hint(void)
{
    app_watchdog_progress(APP_WDT_MODULE_UI);
}

void app_watchdog_input_hint(void)
{
    app_watchdog_heartbeat(APP_WDT_MODULE_BUTTON);
}

typedef struct
{
    app_watchdog_mode_t mode;
    rt_tick_t last_heartbeat[APP_WDT_MODULE_COUNT];
    rt_tick_t last_progress[APP_WDT_MODULE_COUNT];
    rt_tick_t long_task_timeout[APP_WDT_MODULE_COUNT];
    rt_bool_t required[APP_WDT_MODULE_COUNT];
    rt_bool_t long_task_active[APP_WDT_MODULE_COUNT];
} app_watchdog_snapshot_t;

static void app_watchdog_snapshot(app_watchdog_snapshot_t *snapshot)
{
    rt_base_t level;

    level = app_watchdog_lock();
    memcpy(snapshot, &s_app_watchdog_state, sizeof(*snapshot));
    app_watchdog_unlock(level);
}

static rt_bool_t app_watchdog_check_active(const app_watchdog_snapshot_t *snapshot,
                                           rt_tick_t now,
                                           app_watchdog_module_t *failed_module,
                                           rt_tick_t *failed_last_tick)
{
    app_watchdog_module_t module;

    for (module = APP_WDT_MODULE_UI; module < APP_WDT_MODULE_COUNT; module++)
    {
        rt_tick_t last_tick;
        rt_tick_t timeout_ticks;

        if (!snapshot->required[module])
        {
            continue;
        }

        last_tick = snapshot->last_heartbeat[module];
        timeout_ticks = app_watchdog_ms_to_ticks(app_watchdog_default_timeout_ms(module));
        if (last_tick == 0)
        {
            *failed_module = module;
            *failed_last_tick = last_tick;
            return RT_FALSE;
        }

        if ((timeout_ticks != 0) && ((now - last_tick) > timeout_ticks))
        {
            *failed_module = module;
            *failed_last_tick = last_tick;
            return RT_FALSE;
        }
    }

    return RT_TRUE;
}

static rt_bool_t app_watchdog_check_long_task(const app_watchdog_snapshot_t *snapshot,
                                              rt_tick_t now,
                                              app_watchdog_module_t *failed_module,
                                              rt_tick_t *failed_last_tick)
{
    app_watchdog_module_t module;
    rt_bool_t any_active;

    any_active = RT_FALSE;
    for (module = APP_WDT_MODULE_UI; module < APP_WDT_MODULE_COUNT; module++)
    {
        rt_tick_t last_tick;
        rt_tick_t timeout_ticks;

        if (!snapshot->long_task_active[module])
        {
            continue;
        }

        any_active = RT_TRUE;
        last_tick = snapshot->last_progress[module];
        timeout_ticks = snapshot->long_task_timeout[module];
        if (last_tick == 0)
        {
            *failed_module = module;
            *failed_last_tick = last_tick;
            return RT_FALSE;
        }

        if ((timeout_ticks != 0) && ((now - last_tick) > timeout_ticks))
        {
            *failed_module = module;
            *failed_last_tick = last_tick;
            return RT_FALSE;
        }
    }

    if (!any_active)
    {
        return RT_TRUE;
    }

    return RT_TRUE;
}

static void app_watchdog_log_failure(const app_watchdog_snapshot_t *snapshot,
                                     app_watchdog_module_t failed_module,
                                     rt_tick_t failed_last_tick,
                                     rt_tick_t now)
{
    rt_tick_t age;

    age = (failed_last_tick == 0) ? 0 : (now - failed_last_tick);
    rt_kprintf("app_wdt failed: mode=%s module=%s last=%lu age=%lu now=%lu\n",
               app_watchdog_mode_name(snapshot->mode),
               app_watchdog_module_name(failed_module),
               (unsigned long)failed_last_tick,
               (unsigned long)age,
               (unsigned long)now);
}

static void app_watchdog_thread_entry(void *parameter)
{
    (void)parameter;

    while (1)
    {
        app_watchdog_snapshot_t snapshot;
        app_watchdog_mode_t mode;
        rt_tick_t now;
        rt_bool_t should_pet;
        app_watchdog_module_t failed_module;
        rt_tick_t failed_last_tick;

        if (!rt_hw_watchdog_get_status())
        {
            rt_thread_mdelay(APP_WATCHDOG_FEED_INTERVAL_MS);
            continue;
        }

        app_watchdog_snapshot(&snapshot);
        mode = snapshot.mode;
        now = rt_tick_get();
        should_pet = RT_TRUE;

        if ((mode == APP_WDT_MODE_ACTIVE) || (mode == APP_WDT_MODE_LONG_TASK))
        {
            should_pet = RT_FALSE;
        }

        if ((mode == APP_WDT_MODE_BOOT) || (mode == APP_WDT_MODE_SLEEP) || (mode == APP_WDT_MODE_SHUTDOWN))
        {
            should_pet = RT_TRUE;
        }
        else if (mode == APP_WDT_MODE_ACTIVE)
        {
            should_pet = app_watchdog_check_active(&snapshot, now, &failed_module, &failed_last_tick);
        }
        else if (mode == APP_WDT_MODE_LONG_TASK)
        {
            should_pet = app_watchdog_check_long_task(&snapshot, now, &failed_module, &failed_last_tick);
            if (!should_pet)
            {
                app_watchdog_log_failure(&snapshot, failed_module, failed_last_tick, now);
            }
        }

        if (mode == APP_WDT_MODE_ACTIVE && !should_pet)
        {
            app_watchdog_log_failure(&snapshot, failed_module, failed_last_tick, now);
        }

        if (should_pet)
        {
            rt_hw_watchdog_pet();
        }

        rt_thread_mdelay(APP_WATCHDOG_FEED_INTERVAL_MS);
    }
}

static int app_watchdog_init(void)
{
    rt_hw_watchdog_hook(0);

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
