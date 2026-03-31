#include "rtthread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bf0_hal.h"
#include "drv_io.h"
#include "drv_flash.h"


#ifndef BSP_USING_SPI_NAND
    #error Board does not support NAND.
#endif

/*This example do not use FS, so use FS region for NAND test*/
#define NAND_RW_TEST_START_ADDR         FS_REGION_START_ADDR

static int nand_rw_check()
{
    int i, j, res;
    uint32_t *buf32;
    uint32_t addr = NAND_RW_TEST_START_ADDR;
    rt_tick_t start_time, end_time;
    uint32_t elapsed_ticks;
    float speed; // unit: KB/s
    int write_count = 0; // Write count
    int read_count = 0;  // Read count

    /* Nand pinmux setting and initialize(by call rt_nand_init) at system beginning , do not show here*/

    /* Get block/page size, it not fixed to 2KB/128KB for some chip */
    FLASH_HandleTypeDef *nand_handler = (FLASH_HandleTypeDef *)rt_nand_get_handle(addr);
    if (nand_handler == NULL)   // make sure it is NAND address
    {
        rt_kprintf("Address 0x%x is not for NAND FLASH\n", addr);
        return -1;
    }

    uint32_t nand_blk_size = HAL_NAND_BLOCK_SIZE(nand_handler);
    uint32_t nand_page_size = HAL_NAND_PAGE_SIZE(nand_handler);
    rt_kprintf("NAND ID 0x%x, total size 0x%x, page size 0x%x, block size 0x%x\n",
               rt_nand_read_id(addr), rt_nand_get_total_size(addr), nand_page_size, nand_blk_size);

    uint8_t *data_buf = rt_malloc(nand_page_size);
    if (data_buf == NULL)
    {
        rt_kprintf("Malloc data buffer fail\n");
        return -1;
    }
    uint8_t *cmp_buf = rt_malloc(nand_page_size);
    if (cmp_buf == NULL)
    {
        rt_free(data_buf);
        rt_kprintf("Malloc compare buffer fail\n");
        return -2;
    }

    /* Erase a block */
    rt_kprintf("Erase nand at address 0x%x\n", addr);
    res = rt_nand_erase_block(addr);
    if (res != 0)
    {
        rt_kprintf("Erase fail at address 0x%x, res %d\n", addr, res);
        goto exit;
    }

    /* Add data should be FF after erase, read to check */
    rt_kprintf("Check erase data\n");
    for (i = 0; i < nand_blk_size / nand_page_size; i++)
    {
        res = rt_nand_read_page(addr + i * nand_page_size, data_buf, nand_page_size, NULL, 0);
        read_count++; // Increase the reading count
        if (res != nand_page_size)
        {
            rt_kprintf("Read page fail at pos 0x%x\n", addr + i * nand_page_size);
            res = 1;
            goto exit;
        }

        buf32 = (uint32_t *)data_buf;
        for (j = 0; j < nand_page_size / 4; j++)
        {
            if (*buf32++ != 0xffffffff)
            {
                rt_kprintf("Data not 0xffffffff after erase at pos 0x%x\n", addr + i * nand_page_size + j * 4);
                res = 2;
                goto exit;
            }
        }
    }

    /* Initial test pattern and write to nand*/
    srand(rt_tick_get());
    buf32 = (uint32_t *)data_buf;
    for (i = 0; i < nand_page_size / 4; i++)
        *buf32++ = rand();

    /* Write speed test */
    rt_kprintf("Write nand at address 0x%x with length 0x%x\n", addr, nand_blk_size);
    start_time = rt_tick_get();

    for (i = 0; i < nand_blk_size / nand_page_size; i++)
    {
        res = rt_nand_write_page(addr + i * nand_page_size, data_buf, nand_page_size, NULL, 0);
        write_count++; // Increase the write count
        if (res != nand_page_size)
        {
            rt_kprintf("write page fail at pos 0x%x\n", addr + i * nand_page_size);
            res = 3;
            goto exit;
        }
    }

    end_time = rt_tick_get();
    elapsed_ticks = end_time - start_time;
    if (elapsed_ticks > 0)
    {
        // Calculate write speed: Total data volume (KB) / Time (s)
        speed = ((float)nand_blk_size / 1024.0) / ((float)elapsed_ticks / RT_TICK_PER_SECOND);
        rt_kprintf("Write speed: %.2f KB/s (time: %d ticks, pages: %d)\n", speed, elapsed_ticks, write_count);
        // Calculate the number of operations per second
        float ops_per_sec = (float)write_count / ((float)elapsed_ticks / RT_TICK_PER_SECOND);
        rt_kprintf("Write Ops: %d pages, %.2f ops/sec\n", write_count, ops_per_sec);
    }
    else
    {
        rt_kprintf("Write completed (pages: %d)\n", write_count);
    }

    /* Read speed test and check data */
    rt_kprintf("Read nand and check write result at address 0x%x\n", addr);
    start_time = rt_tick_get();

    for (i = 0; i < nand_blk_size / nand_page_size; i++)
    {
        rt_memset((void *)cmp_buf, 0, nand_page_size);
        res = rt_nand_read_page(addr + i * nand_page_size, cmp_buf, nand_page_size, NULL, 0);
        read_count++; // Increase the reading count
        if (res != nand_page_size)
        {
            rt_kprintf("Read page fail at pos 0x%x\n", addr + i * nand_page_size);
            res = 4;
            goto exit;
        }

        if (memcmp(cmp_buf, data_buf, nand_page_size) != 0)
        {
            rt_kprintf("Read data not same to source at pos 0x%x\n", addr + i * nand_page_size);
            res = 5;
            goto exit;
        }
    }

    end_time = rt_tick_get();
    elapsed_ticks = end_time - start_time;
    if (elapsed_ticks > 0)
    {
        // Calculate read speed: Total data volume (KB) / Time (s)
        speed = ((float)nand_blk_size / 1024.0) / ((float)elapsed_ticks / RT_TICK_PER_SECOND);
        rt_kprintf("Read speed: %.2f KB/s (time: %d ticks, pages: %d)\n", speed, elapsed_ticks, read_count);
        // Calculate the number of operations per second
        float ops_per_sec = (float)read_count / ((float)elapsed_ticks / RT_TICK_PER_SECOND);
        rt_kprintf("Read Ops: %d pages, %.2f ops/sec\n", read_count, ops_per_sec);
    }
    else
    {
        rt_kprintf("Read completed (pages: %d)\n", read_count);
    }

    res = 0;
    rt_kprintf("NAND test pass with addr 0x%x , length 0x%x\n", addr, nand_blk_size);

exit:
    rt_free(data_buf);
    rt_free(cmp_buf);
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
    int res = nand_rw_check();
    rt_kprintf("NAND test done with res %d at tick %d\n", res, rt_tick_get());

    /* Infinite loop */
    rt_kprintf("Begin idle loop\n");
    while (1)
    {
        rt_thread_mdelay(10000);    // Let system breath.
    }
    return 0;
}

