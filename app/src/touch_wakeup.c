#include "touch_wakeup.h"

#include "rtthread.h"
#include "board.h"
#include "bf0_hal_aon.h"

#include "sleep_manager.h"
#include "ui/ui_dispatch.h"

#define TOUCH_WAKEUP_THREAD_STACK_SIZE 1024
#define TOUCH_WAKEUP_THREAD_PRIORITY 8
#define TOUCH_WAKEUP_THREAD_TICK 10
#define TOUCH_WAKEUP_REQUEST_DEBOUNCE_MS 500U

static rt_sem_t s_touch_wakeup_sem = RT_NULL;
static rt_thread_t s_touch_wakeup_thread = RT_NULL;
static rt_bool_t s_touch_wakeup_started = RT_FALSE;
static rt_tick_t s_touch_wakeup_last_request_tick = 0U;

static rt_bool_t touch_wakeup_try_mark_started(void)
{
    rt_bool_t already_started;

    rt_enter_critical();
    already_started = s_touch_wakeup_started;
    if (!already_started)
    {
        s_touch_wakeup_started = RT_TRUE;
    }
    rt_exit_critical();

    return !already_started;
}

static void touch_wakeup_clear_started(void)
{
    rt_enter_critical();
    s_touch_wakeup_started = RT_FALSE;
    rt_exit_critical();
}

static void touch_wakeup_thread_entry(void *parameter)
{
    (void)parameter;

    while (1)
    {
        if (rt_sem_take(s_touch_wakeup_sem, RT_WAITING_FOREVER) != RT_EOK)
        {
            continue;
        }

        if (sleep_manager_is_sleeping() ||
            ui_dispatch_get_active_screen() == UI_SCREEN_STANDBY)
        {
            rt_tick_t now = rt_tick_get();
            rt_tick_t debounce_ticks = rt_tick_from_millisecond(TOUCH_WAKEUP_REQUEST_DEBOUNCE_MS);

            if (s_touch_wakeup_last_request_tick != 0U &&
                (rt_tick_t)(now - s_touch_wakeup_last_request_tick) < debounce_ticks)
            {
                continue;
            }

            s_touch_wakeup_last_request_tick = now;
            rt_kprintf("touch_wakeup: request wakeup\n");
            sleep_manager_request_wakeup();
        }
    }
}

static void touch_wakeup_enable_aon_source(void)
{
#ifdef TOUCH_IRQ_PIN
    int8_t wakeup_pin = HAL_HPAON_QueryWakeupPin(hwp_gpio1, TOUCH_IRQ_PIN);

    if (wakeup_pin < 0)
    {
        rt_kprintf("touch_wakeup: TOUCH_IRQ_PIN=%d has no HPAON wake pin\n",
                   TOUCH_IRQ_PIN);
        return;
    }

    HPAON_WakeupSrcTypeDef src =
        (HPAON_WakeupSrcTypeDef)(HPAON_WAKEUP_SRC_PIN0 + wakeup_pin);
    HAL_StatusTypeDef status = HAL_HPAON_EnableWakeupSrc(src, AON_PIN_MODE_LOW);
    rt_kprintf("touch_wakeup: aon irq=%d wake_pin=%d status=%d\n",
               TOUCH_IRQ_PIN, wakeup_pin, status);
#else
    rt_kprintf("touch_wakeup: TOUCH_IRQ_PIN not configured\n");
#endif
}

void touch_wakeup_init(void)
{
    if (!touch_wakeup_try_mark_started())
    {
        return;
    }

    s_touch_wakeup_sem = rt_sem_create("tpwake", 0, RT_IPC_FLAG_FIFO);
    if (s_touch_wakeup_sem == RT_NULL)
    {
        rt_kprintf("touch_wakeup: sem create failed\n");
        touch_wakeup_clear_started();
        return;
    }

    s_touch_wakeup_thread = rt_thread_create("tpwake",
                                             touch_wakeup_thread_entry,
                                             RT_NULL,
                                             TOUCH_WAKEUP_THREAD_STACK_SIZE,
                                             TOUCH_WAKEUP_THREAD_PRIORITY,
                                             TOUCH_WAKEUP_THREAD_TICK);
    if (s_touch_wakeup_thread == RT_NULL)
    {
        rt_kprintf("touch_wakeup: thread create failed\n");
        rt_sem_delete(s_touch_wakeup_sem);
        s_touch_wakeup_sem = RT_NULL;
        touch_wakeup_clear_started();
        return;
    }

    rt_thread_startup(s_touch_wakeup_thread);

    touch_wakeup_enable_aon_source();
}

void app_touch_wakeup_notify_irq(void)
{
    if (s_touch_wakeup_sem != RT_NULL)
    {
        (void)rt_sem_release(s_touch_wakeup_sem);
    }
}
