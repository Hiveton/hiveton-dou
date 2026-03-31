#include "rtthread.h"
#include "bf0_hal.h"
#include "drv_io.h"
#include "stdio.h"
#include "dfs_file.h"
#include "drv_flash.h"
#include "bf0_hal_aon.h"
#ifndef FS_REGION_START_ADDR
    #error "Need to define file system start address!"
#else
    #define FS_ROOT "root"
    #define FS_ROOT_PATH "/"
#endif

int mnt_init(void)
{
    rt_kprintf("0x%x %d\n", FS_REGION_START_ADDR, FS_REGION_SIZE);
    register_mtd_device(FS_REGION_START_ADDR, FS_REGION_SIZE, FS_ROOT); // 1M for bootloader

    if (dfs_mount(FS_ROOT, FS_ROOT_PATH, "elm", 0, 0) == 0) // fs exist
    {
        rt_kprintf("mount fs on flash to root success\n");
    }
    else
    {
        rt_kprintf("mount fs on flash to root fail\n");
        if (dfs_mkfs("elm", FS_ROOT) == 0)//Format file system
        {
            rt_kprintf("make elm fs on flash sucess, mount again\n");
            if (dfs_mount(FS_ROOT, "/", "elm", 0, 0) == 0)
                rt_kprintf("mount fs on flash success\n");
            else
            {
                rt_kprintf("mount to fs on flash fail\n");
                return RT_ERROR;
            }
        }
        else
        {
            rt_kprintf("dfs_mkfs elm flash fail\n");
            return RT_ERROR;
        }
    }
    return RT_EOK;
}
INIT_ENV_EXPORT(mnt_init);


void cmd_fs_write_t(char *path, int num, int page_max)
{
    struct dfs_fd fd_test_sd;
    uint32_t open_time = 0, end_time = 0;
    float test_time = 0.0;
    float speed_test = 0.0;
    char *buff = rt_malloc(page_max);
    memset(buff, 0x55, page_max);
    uint32_t write_num = num;
    uint32_t write_byt = write_num * page_max;

    if (dfs_file_open(&fd_test_sd, path, O_RDWR | O_CREAT | O_TRUNC) == 0)
    {
        open_time = HAL_GTIMER_READ();
        while (write_num--)
        {
            dfs_file_write(&fd_test_sd, buff, page_max);
        }
        end_time = HAL_GTIMER_READ();
    }
    dfs_file_close(&fd_test_sd);
    test_time = ((end_time - open_time) / HAL_LPTIM_GetFreq());
    speed_test = (write_byt) / (test_time);
    rt_kprintf("%s path=%s num=%d byt testtime=%.6lfS,speed_test=%.6lfKB/s\n", __func__, path, write_byt, test_time, speed_test / 1024);
    rt_free(buff);
}

void cmd_fs_write(int argc, char **argv)
{
    cmd_fs_write_t(argv[1], atoi(argv[2]), atoi(argv[3]));
}
FINSH_FUNCTION_EXPORT_ALIAS(cmd_fs_write, __cmd_fs_write, test write speed);

void cmd_fs_read_t(char *path, int num, int page_max)
{
    struct dfs_fd fd_test_sd;
    uint32_t open_time = 0, end_time = 0;
    float test_time = 0.0;
    float speed_test = 0.0;
    char *buff = rt_malloc(page_max);
    memset(buff, 0x55, page_max);
    uint32_t read_num = num;
    uint32_t write_byt = read_num * page_max;
    if (dfs_file_open(&fd_test_sd, path, O_RDONLY) == 0)
    {
        open_time = HAL_GTIMER_READ();
        while (read_num)
        {
            rt_memset(buff, 0, page_max);
            dfs_file_read(&fd_test_sd, buff, page_max);
            read_num--;
        }
        end_time = HAL_GTIMER_READ();
    }
    dfs_file_close(&fd_test_sd);
    test_time = ((end_time - open_time) / HAL_LPTIM_GetFreq());
    speed_test = (write_byt) / (test_time);
    rt_kprintf("%s path=%s num=%d byt testtime=%.6lfS,speed_test=%.6lfKB/s\n", __func__, path, write_byt, test_time, speed_test / 1024);
    rt_free(buff);
}

void cmd_fs_read(int argc, char **argv)
{
    cmd_fs_read_t(argv[1], atoi(argv[2]), atoi(argv[3]));
}
FINSH_FUNCTION_EXPORT_ALIAS(cmd_fs_read, __cmd_fs_read, test read speed);


int main(void)
{
    HAL_RCC_EnableModule(RCC_MOD_USBC);
    /* Output a message on console using printf function */
    rt_kprintf("Use help to check USB host command!\n");
    /* Infinite loop */
    while (1)
    {
        rt_thread_mdelay(10000);    // Let system breath.
    }
    return 0;
}
