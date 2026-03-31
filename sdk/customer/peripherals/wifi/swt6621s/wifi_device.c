/*
 * SPDX-FileCopyrightText: 2019-2022 SiFli Technologies(Nanjing) Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "rtthread.h"
#include "drivers/pin.h"
#include "swt6621s_wlan_port.h"
#include <drivers/mmcsd_card.h>
#include <drivers/mmcsd_core.h>
#include <drivers/mmcsd_host.h>
#ifdef BSP_USING_SDHCI
    #include "drv_sdhci.h"
#elif BSP_USING_SD_LINE
    #include "drv_sdio.h"
#endif
/**
 * @brief Local WiFi control callback context.
 *
 * Stored in `device->user_data` after device open, holding suspend/resume/
 * detect-sleep callbacks and their arguments for low-power and wakeup flow.
 */
static struct wifi_ctl wifi_cb_ctx;

/**
 * @brief Get MMC/SD subsystem ready state.
 * @return Non-zero if initialized and ready; 0 if not ready.
 */
uint8_t mmcsd_get_stat(void);

/**
 * @brief Set MMC/SD subsystem ready state flag.
 * @param stat 0 for not ready; non-zero for ready.
 */
void mmcsd_set_stat(uint8_t stat);

/**
 * @brief Poll SDIO/MMC subsystem until ready.
 *
 * Waits up to ~5 seconds (50 * 100ms) and checks readiness via
 * `mmcsd_get_stat()` on each iteration.
 *
 * @return 0 on success (ready); -1 on timeout/failure.
 */
int rt_check_sdio_ready(void)
{
    uint8_t timeout = 50; /* max 5s wait for mmcsd */
    while (timeout --)
    {
        if (mmcsd_get_stat())
        {
            break;
        }
        rt_thread_mdelay(100);
    }
    if (!timeout)
    {
        rt_kprintf("mmcsd init fail\n");
        return -1;
    }
    return 0;
}
/**
 * @brief Suspend WiFi (enter low-power).
 *
 * Powers off WiFi and the wakeup output GPIO, and deinitializes the SDMMC
 * controller.
 *
 * @return Always returns 0.
 */
int rt_wifi_suspend(void)
{
    int pin_f = WIFI_POWER_PIN < GPIO1_PIN_NUM ? 1 : 0;
    BSP_GPIO_Set(pin_f ? WIFI_POWER_PIN : (WIFI_POWER_PIN - GPIO1_PIN_NUM), 0, pin_f);
    pin_f = WIFI_WAKEUP_OUT_PIN < GPIO1_PIN_NUM ? 1 : 0;
    BSP_GPIO_Set(pin_f ? WIFI_WAKEUP_OUT_PIN : (WIFI_WAKEUP_OUT_PIN - GPIO1_PIN_NUM), 0, pin_f);
    rt_hw_sdmmc_deinit(0);
    return 0;
}
/**
 * @brief Resume WiFi (exit low-power).
 *
 * Re-enables power and wakeup output, clears MMC/SD ready state, reinitializes
 * SDMMC, and waits for SDIO subsystem readiness.
 *
 * @return 0 on success; -1 if SDIO initialization fails.
 */
int rt_wifi_resume(void)
{
    int pin_f = WIFI_POWER_PIN < GPIO1_PIN_NUM ? 1 : 0;
    BSP_GPIO_Set(pin_f ? WIFI_POWER_PIN : (WIFI_POWER_PIN - GPIO1_PIN_NUM), 1, pin_f);

    pin_f = WIFI_WAKEUP_OUT_PIN < GPIO1_PIN_NUM ? 1 : 0;
    BSP_GPIO_Set(pin_f ? WIFI_WAKEUP_OUT_PIN : (WIFI_WAKEUP_OUT_PIN - GPIO1_PIN_NUM), 1, pin_f);

    mmcsd_set_stat(0);
    rt_hw_sdmmc_init();
    if (rt_check_sdio_ready() != 0)
    {
        rt_kprintf("sdio init fail\n");
        return -1;
    }
    return 0;
}

/**
 * @brief Sleep-wakeup handshake callback.
 *
 * If `WIFI_WAKEUP_IN_PIN` is low, sends a short pulse on
 * `WIFI_WAKEUP_OUT_PIN` and polls within a limited number of attempts until
 * the input returns high.
 */
void wifi_resume_callback(void)
{
    uint16_t timer_t = 1000;
    if (rt_pin_read(WIFI_WAKEUP_IN_PIN) == 0)
    {
        int pin_f = WIFI_WAKEUP_OUT_PIN < GPIO1_PIN_NUM ? 1 : 0;
        BSP_GPIO_Set(pin_f ? WIFI_WAKEUP_OUT_PIN : (WIFI_WAKEUP_OUT_PIN - GPIO1_PIN_NUM), 0, pin_f);
        HAL_Delay_us_(10);
        BSP_GPIO_Set(pin_f ? WIFI_WAKEUP_OUT_PIN : (WIFI_WAKEUP_OUT_PIN - GPIO1_PIN_NUM), 1, pin_f);
        while (!rt_pin_read(WIFI_WAKEUP_IN_PIN) && timer_t--)
        {
            HAL_Delay_us_(10);
        }
    }
}
/* Adapters to match void (*)(void*) signature */
/**
 * @brief Suspend adapter (matches `void (*)(void*)`).
 * @param arg Unused.
 */
static void wifi_suspend_adapter(void *arg)
{
    rt_wifi_suspend();
}

/**
 * @brief Resume adapter (matches `void (*)(void*)`).
 * @param arg Unused.
 */
static void wifi_resume_adapter(void *arg)
{
    rt_wifi_resume();
}
/**
 * @brief Detect sleep adapter (matches `void (*)(void*)`).
 * @param arg Unused.
 */
static void wifi_detect_slp_adapter(void *arg)
{
    wifi_resume_callback();
}

/**
 * @brief WiFi device initialization (application init stage).
 *
 * - Find and open the device named by `SKW_DEV_NAME`.
 * - Register suspend/resume/detect-sleep callbacks in `device->user_data`.
 * - Initialize WLAN management.
 * - Configure wakeup IRQ.
 *
 * @return 0 on success; -1 if device open fails.
 */
int rt_wifi_init(void)
{
    rt_device_t device = rt_device_find(SKW_DEV_NAME);
    if (device != RT_NULL)
    {
        rt_kprintf("wifi device has been registered\n");
        if (rt_device_open(device, RT_DEVICE_FLAG_RDWR) != RT_EOK)
        {
            rt_kprintf("wifi device open failed\n");
            return -1;
        }
        /* Register suspend/resume callbacks in device user_data */
        struct wifi_ctl *ctl = (struct wifi_ctl *)device->user_data;
        if (ctl == RT_NULL)
        {
            device->user_data = &wifi_cb_ctx;
            ctl = &wifi_cb_ctx;
        }
        ctl->suspend.cb = wifi_suspend_adapter;
        ctl->suspend.arg = RT_NULL;
        ctl->resume.cb = wifi_resume_adapter;
        ctl->resume.arg = RT_NULL;
        ctl->detect_slp.cb = wifi_detect_slp_adapter;
        ctl->detect_slp.arg = RT_NULL;
    }
    swt6621s_wlan_mgnt_init();
    return 0;
}
INIT_APP_EXPORT(rt_wifi_init);