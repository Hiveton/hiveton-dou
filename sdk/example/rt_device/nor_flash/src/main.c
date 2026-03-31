#include "rtthread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bf0_hal.h"
#include "drv_io.h"
#include "drv_flash.h"



#ifndef BSP_USING_SPI_FLASH
    #error Board does not support NOR FLASH.
#endif

/*This example do not use FS, so use FS region for NAND test*/
#define FLASH_RW_TEST_START_ADDR         (FS_REGION_START_ADDR)
#define FLASH_RW_TEST_LENGTH            (FS_REGION_SIZE)

static int nor_rw_check()
{
    int i, res;
    uint32_t addr = FLASH_RW_TEST_START_ADDR;
    uint32_t total = FLASH_RW_TEST_LENGTH > 0x100000 ? 0x100000 : FLASH_RW_TEST_LENGTH; // max test 1MB
    uint32_t *outptr = (uint32_t *)FLASH_RW_TEST_START_ADDR;
    rt_tick_t start_time, end_time;
    uint32_t elapsed_ticks;
    float speed; // unit: KB/s
    int write_count = 0; // Write count
    int read_count = 0;  // Read count

    /* Nor flash pinmux setting and initialize at system beginning , do not show here*/

    FLASH_HandleTypeDef *handle = rt_flash_get_handle_by_addr(addr);
    if (handle == NULL) // make sure it is NOR address
    {
        rt_kprintf("Address 0x%x not NOR FLASH\n", addr);
        return -1;
    }

    rt_kprintf("NOR FLASH ID 0x%x, with total size 0x%x\n", rt_flash_read_id(addr), rt_flash_get_total_size(addr));

    /*Erase all test mem and check if all ff */
    rt_kprintf("Erase flash with addr 0x%x, length 0x%x\n", addr, total);
    res = rt_flash_erase(addr, total);
    if (res != 0)
    {
        rt_kprintf("FLASH ERASE fail %d\n", res);
        return -1;
    }

    outptr = (uint32_t *)FLASH_RW_TEST_START_ADDR;
    for (i = 0; i < total / 4; i++)
    {
        if (*outptr != 0xffffffff)
        {
            rt_kprintf("Data not 0xffffffff but 0x%08x after erase at pos 0x%x\n", *outptr, addr + i * 4);
            return -2;
        }
        outptr++;
    }
    rt_kprintf("Erase and check result done\n");

    /* Write data to flash */
    uint8_t *data = rt_malloc(SPI_NOR_SECT_SIZE);
    if (data == NULL)
    {
        rt_kprintf("Malloc buffer fail\n");
        return -3;
    }
    srand(rt_tick_get());
    for (i = 0; i < SPI_NOR_SECT_SIZE / 4; i++)
        data[i] = rand();

    /* Write speed test */
    rt_kprintf("Write flash with addr 0x%x, length 0x%x\n", addr, total);
    start_time = rt_tick_get();

    for (i = 0; i < total / SPI_NOR_SECT_SIZE; i++)
    {
        res = rt_flash_write(addr + i * SPI_NOR_SECT_SIZE, data, SPI_NOR_SECT_SIZE);
        write_count++; // Increase the write count
        if (res != SPI_NOR_SECT_SIZE)
        {
            rt_kprintf("Write flash fail\n");
            res = 1;
            goto exit;
        }
    }

    end_time = rt_tick_get();
    elapsed_ticks = end_time - start_time;
    if (elapsed_ticks > 0)
    {
        // Calculate write speed: Total data volume (KB) / Time (s)
        speed = ((float)total / 1024.0) / ((float)elapsed_ticks / RT_TICK_PER_SECOND);
        rt_kprintf("Write speed: %.2f KB/s (time: %d ticks, sectors: %d)\n", speed, elapsed_ticks, write_count);
        // Calculate the number of operations per second
        float ops_per_sec = (float)write_count / ((float)elapsed_ticks / RT_TICK_PER_SECOND);
        rt_kprintf("Write Ops: %d sectors, %.2f ops/sec\n", write_count, ops_per_sec);
    }
    else
    {
        rt_kprintf("Write completed (sectors: %d)\n", write_count);
    }

    /* Read speed test */
    rt_kprintf("Read flash and check result with addr 0x%x, length 0x%x\n", addr, total);
    start_time = rt_tick_get();

    for (i = 0; i < total / SPI_NOR_SECT_SIZE; i++)
    {
        if (memcmp(data, (const void *)(addr + i * SPI_NOR_SECT_SIZE), SPI_NOR_SECT_SIZE) != 0)
        {
            rt_kprintf("compare flash fail\n");
            res = 2;
            goto exit;
        }
        read_count++; // Increase the reading count
    }

    end_time = rt_tick_get();
    elapsed_ticks = end_time - start_time;
    if (elapsed_ticks > 0)
    {
        // Calculate read speed: Total data volume (KB) / Time (s)
        speed = ((float)total / 1024.0) / ((float)elapsed_ticks / RT_TICK_PER_SECOND);
        rt_kprintf("Read speed: %.2f KB/s (time: %d ticks, sectors: %d)\n", speed, elapsed_ticks, read_count);
        // Calculate the number of operations per second
        float ops_per_sec = (float)read_count / ((float)elapsed_ticks / RT_TICK_PER_SECOND);
        rt_kprintf("Read Ops: %d sectors, %.2f ops/sec\n", read_count, ops_per_sec);
    }
    else
    {
        rt_kprintf("Read completed (sectors: %d)\n", read_count);
    }

    res = 0;
    rt_kprintf("NOR FLASH test pass with addr 0x%x, length 0x%x\n", addr, total);

exit:
    rt_free(data);
    return res;
}
/**
  * @brief  Main program
  * @param  None
  * @retval 0 if success, otherwise failure number
  */
int main(void)
{
    rt_kprintf("Enter main loop at tick %d\n", rt_tick_get());

    /* Begin user test */
    int res = nor_rw_check();
    rt_kprintf("NOR FLASH test done with res %d at tick %d\n", res, rt_tick_get());

    /* Infinite loop */
    rt_kprintf("Begin idle loop\n");
    while (1)
    {
        rt_thread_mdelay(10000);    // Let system breath.
    }
    return 0;
}

