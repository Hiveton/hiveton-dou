#include "rtthread.h"
#include "bf0_hal.h"
#include "drv_io.h"
#include "stdio.h"
#include "string.h"
#include <rtthread.h>
#include "board.h"
#include "drv_flash.h"
#include "bf0_pm.h"
static void rtc_wakeup_callback(void *parameter)
{
    int pin_value = HAL_GPIO_ReadPin(hwp_gpio1, 32);
    //rt_kprintf("PA32 pin value: %d\n", pin_value);
}
static void set_rtc_wakeup(void)
{
    pm_enable_rtc_wakeup();
    rt_timer_t rtc_timer;
    rtc_timer = rt_timer_create("rtc_wk",
                                rtc_wakeup_callback,
                                RT_NULL,
                                rt_tick_from_millisecond(100),
                                RT_TIMER_FLAG_PERIODIC | RT_TIMER_FLAG_HARD_TIMER);

    if (rtc_timer != RT_NULL)
    {
        rt_timer_start(rtc_timer);
    }
}
const static pm_policy_t my_pm_policy[] =
{
#ifdef PM_DEEP_ENABLE
    {5, PM_SLEEP_MODE_DEEP},
#endif /* SOC_BF0_HCPU */

};

int main(void)
{
    rt_pm_policy_register(sizeof(my_pm_policy) / sizeof(my_pm_policy[0]), my_pm_policy);

    HAL_LPAON_Sleep();
    /* Output a message on console using printf function */
    rt_kprintf("Start PM_GPIO!\n");

    BSP_GPIO_Set(21, 0, 1);
    //rt_pm_request(PM_SLEEP_MODE_IDLE);
    /* Infinite loop */

    int  fq_48 = rt_pm_run_enter(PM_RUN_MODE_MEDIUM_SPEED);
    rt_kprintf("Set the frequency to 48M %d\n", fq_48);
    set_rtc_wakeup();
    while (1)
    {
        rt_thread_mdelay(1000);
    }
    return 0;
}

