#include "rtthread.h"
#include "bf0_hal.h"

/**
 * @brief  Main program
 * @param  None
 * @retval 0 if success, otherwise failure number
 */
int main(void)
{
    rt_kprintf("cherryusb device msc_ram demo!\n");

    extern void msc_ram_init(uint8_t busid, uint32_t reg_base);
    msc_ram_init(0, (uintptr_t)USBC_BASE);

    while (1)
    {
        rt_thread_mdelay(1000);
    }
    return 0;
}

