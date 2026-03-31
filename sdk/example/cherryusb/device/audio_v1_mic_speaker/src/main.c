#include "rtthread.h"
#include "bf0_hal.h"
#include "audio_v1_mic_speaker.h"

#define UAC_THREAD_STACK_SIZE 2048
#define UAC_THREAD_PRIORITY   15

void uac_thread_entry(void *parameter)
{
    audio_v1_task(0);
}

/**
 * @brief  Main program
 * @param  None
 * @retval 0 if success, otherwise failure number
 */
int main(void)
{
    rt_kprintf("cherryusb device audio mic demo!\n");

    audio_v1_init(0, (uintptr_t)USBC_BASE);

    rt_thread_t tid = rt_thread_create("uac_task",
                                       uac_thread_entry,
                                       RT_NULL,
                                       UAC_THREAD_STACK_SIZE,
                                       UAC_THREAD_PRIORITY,
                                       10);

    if (tid != RT_NULL)
    {
        rt_thread_startup(tid);
        rt_kprintf("uac_task thread started successfully.\n");
    }
    else
    {
        rt_kprintf("Failed to create uac_task thread.\n");
        return -1;
    }

    while (1)
    {
        rt_thread_mdelay(2000);
    }

    return 0;
}