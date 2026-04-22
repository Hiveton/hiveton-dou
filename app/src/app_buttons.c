#include "app_buttons.h"

#include <rtthread.h>
#include <rtdevice.h>
#include <stdbool.h>

#include "bf0_hal.h"
#include "app_watchdog.h"
#include "sleep_manager.h"
#include "ui/ui_dispatch.h"
#include "ui/ui_runtime_adapter.h"

#define APP_BUTTON_PWR_PIN            34
#define APP_BUTTON_B_PIN              43
#define APP_BUTTON_T_PIN              44
#define APP_BUTTON_ACTIVE_HIGH        1
#define APP_BUTTON_LONG_PRESS_MS      1500U
#define APP_BUTTON_SHORT_DEBOUNCE_MS  220U
#define APP_BUTTON_THREAD_STACK_SIZE  2048U
#define APP_BUTTON_THREAD_PRIORITY    3
#define APP_BUTTON_HEAL_INTERVAL_MS   100U

#ifndef APP_BUTTON_DEBUG_VERBOSE
#define APP_BUTTON_DEBUG_VERBOSE 1
#endif

#if APP_BUTTON_DEBUG_VERBOSE
#define APP_BUTTON_LOG(...) rt_kprintf(__VA_ARGS__)
#else
#define APP_BUTTON_LOG(...) do { } while (0)
#endif

#define APP_BUTTON_ERROR_LOG(...) rt_kprintf(__VA_ARGS__)

typedef enum
{
    APP_KEY_PWR = 0,
    APP_KEY_B,
    APP_KEY_T,
    APP_KEY_COUNT
} app_key_id_t;

typedef struct
{
    int32_t pin;
    const char *name;
    int active_level;
    bool pressed;
    bool long_reported;
    bool enabled;
    bool error_reported;
    bool level_valid;
    int last_level;
    rt_tick_t last_short_tick;
} app_key_state_t;

static app_key_state_t s_app_keys[APP_KEY_COUNT] = {
    {APP_BUTTON_PWR_PIN, "pwr", APP_BUTTON_ACTIVE_HIGH, false, false, false, false},
    {APP_BUTTON_B_PIN, "b", APP_BUTTON_ACTIVE_HIGH, false, false, false, false},
    {APP_BUTTON_T_PIN, "t", APP_BUTTON_ACTIVE_HIGH, false, false, false, false},
};

static struct rt_semaphore s_button_irq_sem;
static struct rt_thread s_button_thread;
static rt_uint8_t s_button_thread_stack[APP_BUTTON_THREAD_STACK_SIZE];
static struct rt_timer s_pwr_long_timer;
static volatile rt_uint32_t s_button_pending_mask = 0U;
static volatile bool s_app_buttons_module_enabled = false;
static volatile bool s_button_sem_ready = false;
static bool s_pwr_long_timer_initialized = false;
static bool s_app_buttons_module_error_reported = false;

#define APP_BUTTON_EVT_PWR_PRESS   (1UL << 0)
#define APP_BUTTON_EVT_PWR_SHORT   (1UL << 1)
#define APP_BUTTON_EVT_PWR_LONG    (1UL << 2)
#define APP_BUTTON_EVT_B_SHORT     (1UL << 3)
#define APP_BUTTON_EVT_T_SHORT     (1UL << 4)

static const char *app_buttons_key_name(app_key_id_t key_id)
{
    if (key_id >= 0 && key_id < APP_KEY_COUNT)
    {
        return s_app_keys[key_id].name;
    }

    return "unknown";
}

static void app_buttons_log_module_disabled(const char *reason, rt_err_t result)
{
    if (s_app_buttons_module_error_reported)
    {
        return;
    }

    s_app_buttons_module_error_reported = true;
    if (result == RT_EOK)
    {
        APP_BUTTON_ERROR_LOG("app_buttons: module disabled: %s\n", reason);
    }
    else
    {
        APP_BUTTON_ERROR_LOG("app_buttons: module disabled: %s failed result=%d\n",
                             reason,
                             (int)result);
    }
}

static void app_buttons_log_key_disabled(app_key_state_t *key, const char *reason, rt_err_t result)
{
    if (key == RT_NULL || key->error_reported)
    {
        return;
    }

    key->error_reported = true;
    APP_BUTTON_ERROR_LOG("app_buttons: %s key disabled: %s failed result=%d\n",
                         key->name,
                         reason,
                         (int)result);
}

static void app_buttons_disable_key_irq(app_key_state_t *key)
{
    if (key == RT_NULL)
    {
        return;
    }

    rt_pin_irq_enable(key->pin, PIN_IRQ_DISABLE);
    key->enabled = false;
    key->pressed = false;
    key->long_reported = false;
}

static void app_buttons_disable_key_for_error(app_key_state_t *key, const char *reason, rt_err_t result)
{
    app_buttons_disable_key_irq(key);
    app_buttons_log_key_disabled(key, reason, result);
}

static bool app_buttons_any_key_enabled(void)
{
    app_key_id_t i;

    for (i = 0; i < APP_KEY_COUNT; ++i)
    {
        if (s_app_keys[i].enabled)
        {
            return true;
        }
    }

    return false;
}

static void app_buttons_disable_module_for_error(const char *reason, rt_err_t result)
{
    app_key_id_t i;

    s_app_buttons_module_enabled = false;
    s_button_pending_mask = 0U;

    if (s_pwr_long_timer_initialized)
    {
        rt_timer_stop(&s_pwr_long_timer);
    }

    for (i = 0; i < APP_KEY_COUNT; ++i)
    {
        app_buttons_disable_key_irq(&s_app_keys[i]);
    }

    app_buttons_log_module_disabled(reason, result);
}

static void app_buttons_config_pinmux(void)
{
    HAL_PIN_SetMode(PAD_PA34, 1, PIN_DIGITAL_IO_NORMAL);
    HAL_PIN_SetMode(PAD_PA43, 1, PIN_DIGITAL_IO_NORMAL);
    HAL_PIN_SetMode(PAD_PA44, 1, PIN_DIGITAL_IO_NORMAL);

    HAL_PIN_Set(PAD_PA34, GPIO_A34, PIN_PULLDOWN, 1);
    HAL_PIN_Set(PAD_PA43, GPIO_A43, PIN_PULLDOWN, 1);
    HAL_PIN_Set(PAD_PA44, GPIO_A44, PIN_PULLDOWN, 1);
}

static void app_buttons_refresh_runtime_pinmux(void)
{
    app_key_id_t i;
    rt_err_t result;

    if (!s_app_buttons_module_enabled)
    {
        return;
    }

    HAL_PIN_SetMode(PAD_PA34, 1, PIN_DIGITAL_IO_NORMAL);
    HAL_PIN_SetMode(PAD_PA43, 1, PIN_DIGITAL_IO_NORMAL);
    HAL_PIN_SetMode(PAD_PA44, 1, PIN_DIGITAL_IO_NORMAL);
    HAL_PIN_Set(PAD_PA34, GPIO_A34, PIN_PULLDOWN, 1);
    HAL_PIN_Set(PAD_PA43, GPIO_A43, PIN_PULLDOWN, 1);
    HAL_PIN_Set(PAD_PA44, GPIO_A44, PIN_PULLDOWN, 1);

    for (i = 0; i < APP_KEY_COUNT; ++i)
    {
        if (!s_app_keys[i].enabled)
        {
            continue;
        }

        rt_pin_mode(s_app_keys[i].pin, PIN_MODE_INPUT_PULLDOWN);
        result = rt_pin_irq_enable(s_app_keys[i].pin, PIN_IRQ_ENABLE);
        if (result != RT_EOK)
        {
            app_buttons_disable_key_for_error(&s_app_keys[i], "runtime irq enable", result);
        }
    }

    if (!app_buttons_any_key_enabled())
    {
        app_buttons_disable_module_for_error("all button irqs disabled", RT_EOK);
    }
}

static bool app_buttons_wakeup_only(void)
{
    bool sleeping = sleep_manager_is_sleeping();
    ui_screen_id_t active = ui_dispatch_get_active_screen();

    ui_dispatch_request_activity();

    if (sleeping || active == UI_SCREEN_STANDBY)
    {
        sleep_manager_request_wakeup();
        ui_dispatch_request_exit_standby();
        return true;
    }

    return false;
}

static void app_buttons_dispatch_short(app_key_id_t key_id)
{
    app_watchdog_input_hint();

    switch (key_id)
    {
    case APP_KEY_PWR:
        {
            bool sleeping = sleep_manager_is_sleeping();
            ui_screen_id_t active = ui_dispatch_get_active_screen();

            if (sleeping || active == UI_SCREEN_STANDBY)
            {
                ui_dispatch_request_activity();
                sleep_manager_request_wakeup();
                ui_dispatch_request_exit_standby();
            }
            else
            {
                APP_BUTTON_LOG("app_buttons: pwr short active=%d\n", (int)active);
                ui_dispatch_request_back();
            }
        }
        break;
    case APP_KEY_B:
        app_buttons_wakeup_only();
        APP_BUTTON_LOG("app_buttons: b short dispatch\n");
        ui_dispatch_request_hardkey_down();
        break;
    case APP_KEY_T:
        app_buttons_wakeup_only();
        APP_BUTTON_LOG("app_buttons: t short dispatch\n");
        ui_dispatch_request_hardkey_up();
        break;
    default:
        break;
    }
}

static void app_buttons_dispatch_long(app_key_id_t key_id)
{
    app_watchdog_input_hint();

    if (key_id == APP_KEY_PWR)
    {
        if (!app_buttons_wakeup_only())
        {
            APP_BUTTON_LOG("app_buttons: pwr long\n");
            ui_dispatch_request_poweroff_confirm();
        }
    }
}

static app_key_state_t *app_buttons_find_key_by_pin(int32_t pin, app_key_id_t *key_id)
{
    app_key_id_t i;

    for (i = 0; i < APP_KEY_COUNT; ++i)
    {
        if (s_app_keys[i].pin == pin)
        {
            if (key_id != RT_NULL)
            {
                *key_id = i;
            }
            return &s_app_keys[i];
        }
    }

    return RT_NULL;
}

static void app_buttons_set_pending(rt_uint32_t mask)
{
    if (!s_app_buttons_module_enabled || !s_button_sem_ready)
    {
        return;
    }

    rt_enter_critical();
    s_button_pending_mask |= mask;
    rt_exit_critical();
    (void)rt_sem_release(&s_button_irq_sem);
}

static bool app_buttons_short_debounce_allow(app_key_state_t *key)
{
    rt_tick_t now;
    rt_tick_t debounce_ticks;

    if (key == RT_NULL)
    {
        return false;
    }

    now = rt_tick_get();
    debounce_ticks = rt_tick_from_millisecond(APP_BUTTON_SHORT_DEBOUNCE_MS);
    if (key->last_short_tick != 0 &&
        (rt_tick_t)(now - key->last_short_tick) < debounce_ticks)
    {
        return false;
    }

    key->last_short_tick = now;
    return true;
}

static void app_buttons_handle_b_level(app_key_state_t *key, int level, const char *source)
{
    if (key == RT_NULL)
    {
        return;
    }

    if (!key->level_valid)
    {
        key->last_level = level;
        key->level_valid = true;
        key->pressed = false;
        return;
    }

    if (level == key->last_level)
    {
        key->pressed = false;
        return;
    }

    if (!key->pressed && app_buttons_short_debounce_allow(key))
    {
        key->pressed = true;
        APP_BUTTON_LOG("app_buttons: b %s edge short pin=%ld idle=%d level=%d\n",
                       source != RT_NULL ? source : "unknown",
                       (long)key->pin,
                       key->last_level,
                       level);
        app_buttons_set_pending(APP_BUTTON_EVT_B_SHORT);
    }
}

static void app_buttons_pwr_long_timeout(void *parameter)
{
    app_key_state_t *key = (app_key_state_t *)parameter;

    if (s_app_buttons_module_enabled && key != RT_NULL && key->enabled && key->pressed && !key->long_reported)
    {
        key->long_reported = true;
        app_buttons_set_pending(APP_BUTTON_EVT_PWR_LONG);
    }
}

static void app_buttons_irq_handler(void *args)
{
    int32_t pin = (int32_t)(rt_ubase_t)args;
    app_key_id_t key_id = APP_KEY_COUNT;
    app_key_state_t *key = app_buttons_find_key_by_pin(pin, &key_id);
    int level;
    bool active;

    if (!s_app_buttons_module_enabled || key == RT_NULL || !key->enabled)
    {
        return;
    }

    level = rt_pin_read(pin);
    active = (level == key->active_level);
    APP_BUTTON_LOG("app_buttons: irq pin=%ld level=%d\n", (long)pin, level);

    if (key_id == APP_KEY_T)
    {
        bool was_pressed = key->pressed;

        key->pressed = active;
        if (active && !was_pressed)
        {
            APP_BUTTON_LOG("app_buttons: %s irq short\n", app_buttons_key_name(key_id));
            app_buttons_set_pending(APP_BUTTON_EVT_T_SHORT);
        }
        return;
    }

    if (key_id == APP_KEY_B)
    {
        app_buttons_handle_b_level(key, level, "irq");
        return;
    }

    if (active)
    {
        key->pressed = true;
        key->long_reported = false;
        app_buttons_set_pending(APP_BUTTON_EVT_PWR_PRESS);
        rt_timer_start(&s_pwr_long_timer);
    }
    else
    {
        bool was_pressed = key->pressed;

        key->pressed = false;
        rt_timer_stop(&s_pwr_long_timer);
        if (was_pressed && !key->long_reported)
        {
            app_buttons_set_pending(APP_BUTTON_EVT_PWR_SHORT);
        }
    }
}

static void app_buttons_poll_short_key(app_key_id_t key_id, rt_uint32_t event_mask)
{
    app_key_state_t *key;
    int level;
    bool active;
    bool was_pressed;

    if (key_id <= APP_KEY_PWR || key_id >= APP_KEY_COUNT)
    {
        return;
    }

    key = &s_app_keys[key_id];
    if (!s_app_buttons_module_enabled || !key->enabled)
    {
        return;
    }

    level = rt_pin_read(key->pin);
    if (key_id == APP_KEY_B)
    {
        app_buttons_handle_b_level(key, level, "poll");
        return;
    }

    active = (level == key->active_level);
    was_pressed = key->pressed;
    key->pressed = active;

    if (active && !was_pressed)
    {
        APP_BUTTON_LOG("app_buttons: %s poll short pin=%ld level=%d\n",
                       key->name,
                       (long)key->pin,
                       level);
        app_buttons_set_pending(event_mask);
    }
}

static void app_buttons_poll_short_keys(void)
{
    app_buttons_poll_short_key(APP_KEY_T, APP_BUTTON_EVT_T_SHORT);
    app_buttons_poll_short_key(APP_KEY_B, APP_BUTTON_EVT_B_SHORT);
}

static void app_buttons_thread_entry(void *parameter)
{
    rt_uint32_t pending;

    (void)parameter;

    while (1)
    {
        if (rt_sem_take(&s_button_irq_sem, rt_tick_from_millisecond(APP_BUTTON_HEAL_INTERVAL_MS)) != RT_EOK)
        {
            app_buttons_refresh_runtime_pinmux();
            app_buttons_poll_short_keys();
            if (!s_app_buttons_module_enabled)
            {
                return;
            }
            continue;
        }

        if (!s_app_buttons_module_enabled)
        {
            return;
        }

        rt_enter_critical();
        pending = s_button_pending_mask;
        s_button_pending_mask = 0U;
        rt_exit_critical();

        if (((pending & APP_BUTTON_EVT_PWR_PRESS) != 0U) && s_app_keys[APP_KEY_PWR].enabled)
        {
            app_watchdog_input_hint();
            if (sleep_manager_is_sleeping() || (ui_dispatch_get_active_screen() == UI_SCREEN_STANDBY))
            {
                ui_dispatch_request_activity();
                sleep_manager_request_wakeup();
                ui_dispatch_request_exit_standby();
            }
        }

        if (((pending & APP_BUTTON_EVT_T_SHORT) != 0U) && s_app_keys[APP_KEY_T].enabled)
        {
            app_buttons_dispatch_short(APP_KEY_T);
        }

        if (((pending & APP_BUTTON_EVT_B_SHORT) != 0U) && s_app_keys[APP_KEY_B].enabled)
        {
            app_buttons_dispatch_short(APP_KEY_B);
        }

        if (((pending & APP_BUTTON_EVT_PWR_SHORT) != 0U) && s_app_keys[APP_KEY_PWR].enabled)
        {
            app_buttons_dispatch_short(APP_KEY_PWR);
        }

        if (((pending & APP_BUTTON_EVT_PWR_LONG) != 0U) && s_app_keys[APP_KEY_PWR].enabled)
        {
            app_buttons_dispatch_long(APP_KEY_PWR);
        }
    }
}

void app_buttons_init(void)
{
    static bool initialized = false;
    app_key_id_t i;
    rt_err_t result;
    bool any_key_enabled = false;

    if (initialized)
    {
        return;
    }

    initialized = true;

    APP_BUTTON_LOG("app_buttons: init start pwr=%d b=%d t=%d\n",
               (int)APP_BUTTON_PWR_PIN,
               (int)APP_BUTTON_B_PIN,
               (int)APP_BUTTON_T_PIN);

    app_buttons_config_pinmux();

    result = rt_sem_init(&s_button_irq_sem, "app_key", 0, RT_IPC_FLAG_FIFO);
    if (result != RT_EOK)
    {
        s_button_sem_ready = false;
        app_buttons_log_module_disabled("semaphore init", result);
        return;
    }
    s_button_sem_ready = true;

    rt_timer_init(&s_pwr_long_timer,
                  "btn_pwr",
                  app_buttons_pwr_long_timeout,
                  &s_app_keys[APP_KEY_PWR],
                  rt_tick_from_millisecond(APP_BUTTON_LONG_PRESS_MS),
                  RT_TIMER_FLAG_ONE_SHOT);
    s_pwr_long_timer_initialized = true;

    for (i = 0; i < APP_KEY_COUNT; ++i)
    {
        rt_pin_mode(s_app_keys[i].pin, PIN_MODE_INPUT_PULLDOWN);
        s_app_keys[i].pressed = false;
        s_app_keys[i].long_reported = false;
        s_app_keys[i].enabled = false;
        s_app_keys[i].last_level = rt_pin_read(s_app_keys[i].pin);
        s_app_keys[i].level_valid = true;
        s_app_keys[i].last_short_tick = 0;

        result = rt_pin_attach_irq(s_app_keys[i].pin,
                                   PIN_IRQ_MODE_RISING_FALLING,
                                   app_buttons_irq_handler,
                                   (void *)(rt_ubase_t)s_app_keys[i].pin);
        if (result != RT_EOK)
        {
            app_buttons_disable_key_for_error(&s_app_keys[i], "irq attach", result);
            continue;
        }

        result = rt_pin_irq_enable(s_app_keys[i].pin, PIN_IRQ_ENABLE);
        if (result != RT_EOK)
        {
            app_buttons_disable_key_for_error(&s_app_keys[i], "irq enable", result);
            continue;
        }

        s_app_keys[i].enabled = true;
        any_key_enabled = true;

        APP_BUTTON_LOG("app_buttons: %s irq enabled pin=%ld level=%d\n",
                   s_app_keys[i].name,
                   (long)s_app_keys[i].pin,
                   rt_pin_read(s_app_keys[i].pin));
    }

    if (!any_key_enabled)
    {
        app_buttons_disable_module_for_error("no button irq available", RT_EOK);
        return;
    }

    result = rt_thread_init(&s_button_thread,
                            "app_key",
                            app_buttons_thread_entry,
                            RT_NULL,
                            s_button_thread_stack,
                            sizeof(s_button_thread_stack),
                            APP_BUTTON_THREAD_PRIORITY,
                            5);
    if (result != RT_EOK)
    {
        app_buttons_disable_module_for_error("thread init", result);
        return;
    }

    result = rt_thread_startup(&s_button_thread);
    if (result != RT_EOK)
    {
        app_buttons_disable_module_for_error("thread startup", result);
        return;
    }

    s_app_buttons_module_enabled = true;
    APP_BUTTON_LOG("app_buttons: worker started priority=%d\n", APP_BUTTON_THREAD_PRIORITY);
}
