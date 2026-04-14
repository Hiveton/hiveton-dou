#include "app_buttons.h"

#include <rtthread.h>
#include <rtdevice.h>
#include <stdbool.h>

#include "bf0_hal.h"
#include "sleep_manager.h"
#include "ui/ui_dispatch.h"

#define APP_BUTTON_PWR_PIN            34
#define APP_BUTTON_B_PIN              43
#define APP_BUTTON_T_PIN              44
#define APP_BUTTON_ACTIVE_HIGH        1
#define APP_BUTTON_LONG_PRESS_MS      1500U
#define APP_BUTTON_THREAD_STACK_SIZE  2048U
#define APP_BUTTON_THREAD_PRIORITY    3

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
} app_key_state_t;

static app_key_state_t s_app_keys[APP_KEY_COUNT] = {
    {APP_BUTTON_PWR_PIN, "pwr", APP_BUTTON_ACTIVE_HIGH, false, false},
    {APP_BUTTON_B_PIN, "b", APP_BUTTON_ACTIVE_HIGH, false, false},
    {APP_BUTTON_T_PIN, "t", APP_BUTTON_ACTIVE_HIGH, false, false},
};

static struct rt_semaphore s_button_irq_sem;
static struct rt_thread s_button_thread;
static rt_uint8_t s_button_thread_stack[APP_BUTTON_THREAD_STACK_SIZE];
static struct rt_timer s_pwr_long_timer;
static volatile rt_uint32_t s_button_pending_mask = 0U;

#define APP_BUTTON_EVT_PWR_PRESS   (1UL << 0)
#define APP_BUTTON_EVT_PWR_SHORT   (1UL << 1)
#define APP_BUTTON_EVT_PWR_LONG    (1UL << 2)
#define APP_BUTTON_EVT_B_SHORT     (1UL << 3)
#define APP_BUTTON_EVT_T_SHORT     (1UL << 4)

static void app_buttons_config_pinmux(void)
{
    HAL_PIN_SetMode(PAD_PA34, 1, PIN_DIGITAL_IO_NORMAL);
    HAL_PIN_SetMode(PAD_PA43, 1, PIN_DIGITAL_IO_NORMAL);
    HAL_PIN_SetMode(PAD_PA44, 1, PIN_DIGITAL_IO_NORMAL);

    HAL_PIN_Set(PAD_PA34, GPIO_A34, PIN_PULLDOWN, 1);
    HAL_PIN_Set(PAD_PA43, GPIO_A43, PIN_PULLDOWN, 1);
    HAL_PIN_Set(PAD_PA44, GPIO_A44, PIN_PULLDOWN, 1);
}

static bool app_buttons_wakeup_only(void)
{
    bool sleeping = sleep_manager_is_sleeping();
    ui_screen_id_t active = ui_dispatch_get_active_screen();

    ui_dispatch_request_activity();
    sleep_manager_request_wakeup();

    if (sleeping || active == UI_SCREEN_STANDBY)
    {
        ui_dispatch_request_exit_standby();
        return true;
    }

    return false;
}

static void app_buttons_dispatch_short(app_key_id_t key_id)
{
    switch (key_id)
    {
    case APP_KEY_PWR:
        if (!app_buttons_wakeup_only())
        {
            rt_kprintf("app_buttons: pwr short\n");
            ui_dispatch_request_back();
        }
        break;
    case APP_KEY_B:
        app_buttons_wakeup_only();
        rt_kprintf("app_buttons: b short\n");
        ui_dispatch_request_hardkey_down();
        break;
    case APP_KEY_T:
        app_buttons_wakeup_only();
        rt_kprintf("app_buttons: t short\n");
        ui_dispatch_request_hardkey_up();
        break;
    default:
        break;
    }
}

static void app_buttons_dispatch_long(app_key_id_t key_id)
{
    if (key_id == APP_KEY_PWR)
    {
        if (!app_buttons_wakeup_only())
        {
            rt_kprintf("app_buttons: pwr long\n");
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
    rt_enter_critical();
    s_button_pending_mask |= mask;
    rt_exit_critical();
    rt_sem_release(&s_button_irq_sem);
}

static void app_buttons_pwr_long_timeout(void *parameter)
{
    app_key_state_t *key = (app_key_state_t *)parameter;

    if (key != RT_NULL && key->pressed && !key->long_reported)
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
    int level = rt_pin_read(pin);
    bool active;

    if (key == RT_NULL)
    {
        return;
    }

    active = (level == key->active_level);
    rt_kprintf("app_buttons: irq pin=%ld level=%d\n", (long)pin, level);

    if (key_id == APP_KEY_T)
    {
        if (active)
        {
            app_buttons_set_pending(APP_BUTTON_EVT_T_SHORT);
        }
        return;
    }

    if (key_id == APP_KEY_B)
    {
        if (active)
        {
            app_buttons_set_pending(APP_BUTTON_EVT_B_SHORT);
        }
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

static void app_buttons_thread_entry(void *parameter)
{
    rt_uint32_t pending;

    (void)parameter;

    while (1)
    {
        rt_sem_take(&s_button_irq_sem, RT_WAITING_FOREVER);

        rt_enter_critical();
        pending = s_button_pending_mask;
        s_button_pending_mask = 0U;
        rt_exit_critical();

        if ((pending & APP_BUTTON_EVT_PWR_PRESS) != 0U)
        {
            ui_dispatch_request_activity();
            sleep_manager_request_wakeup();
            if (sleep_manager_is_sleeping() || (ui_dispatch_get_active_screen() == UI_SCREEN_STANDBY))
            {
                ui_dispatch_request_exit_standby();
            }
        }

        if ((pending & APP_BUTTON_EVT_T_SHORT) != 0U)
        {
            app_buttons_dispatch_short(APP_KEY_T);
        }

        if ((pending & APP_BUTTON_EVT_B_SHORT) != 0U)
        {
            app_buttons_dispatch_short(APP_KEY_B);
        }

        if ((pending & APP_BUTTON_EVT_PWR_SHORT) != 0U)
        {
            app_buttons_dispatch_short(APP_KEY_PWR);
        }

        if ((pending & APP_BUTTON_EVT_PWR_LONG) != 0U)
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

    if (initialized)
    {
        return;
    }

    rt_kprintf("app_buttons: init start pwr=%d b=%d t=%d\n",
               (int)APP_BUTTON_PWR_PIN,
               (int)APP_BUTTON_B_PIN,
               (int)APP_BUTTON_T_PIN);

    app_buttons_config_pinmux();

    result = rt_sem_init(&s_button_irq_sem, "app_key", 0, RT_IPC_FLAG_FIFO);
    RT_ASSERT(result == RT_EOK);

    rt_timer_init(&s_pwr_long_timer,
                  "btn_pwr",
                  app_buttons_pwr_long_timeout,
                  &s_app_keys[APP_KEY_PWR],
                  rt_tick_from_millisecond(APP_BUTTON_LONG_PRESS_MS),
                  RT_TIMER_FLAG_ONE_SHOT);

    for (i = 0; i < APP_KEY_COUNT; ++i)
    {
        rt_pin_mode(s_app_keys[i].pin, PIN_MODE_INPUT_PULLDOWN);
        s_app_keys[i].pressed = false;
        s_app_keys[i].long_reported = false;

        result = rt_pin_attach_irq(s_app_keys[i].pin,
                                   PIN_IRQ_MODE_RISING_FALLING,
                                   app_buttons_irq_handler,
                                   (void *)(rt_ubase_t)s_app_keys[i].pin);
        RT_ASSERT(result == RT_EOK);
        rt_pin_irq_enable(s_app_keys[i].pin, PIN_IRQ_ENABLE);

        rt_kprintf("app_buttons: %s irq enabled pin=%ld level=%d\n",
                   s_app_keys[i].name,
                   (long)s_app_keys[i].pin,
                   rt_pin_read(s_app_keys[i].pin));
    }

    result = rt_thread_init(&s_button_thread,
                            "app_key",
                            app_buttons_thread_entry,
                            RT_NULL,
                            s_button_thread_stack,
                            sizeof(s_button_thread_stack),
                            APP_BUTTON_THREAD_PRIORITY,
                            5);
    RT_ASSERT(result == RT_EOK);
    rt_thread_startup(&s_button_thread);
    rt_kprintf("app_buttons: worker started priority=%d\n", APP_BUTTON_THREAD_PRIORITY);

    initialized = true;
}
