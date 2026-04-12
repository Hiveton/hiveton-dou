/*
 * SPDX-FileCopyrightText: 2024-2025 SiFli Technologies(Nanjing) Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "rtthread.h"
#include "rtdevice.h"
#include "dfs_fs.h"
#include "dfs_posix.h"
#include "drv_io.h"
#include "littlevgl2rtt.h"
#include "lvgl.h"
#include "lv_ex_data.h"
#include <finsh.h>
#include "board_hardware.h"
#include "bf0_sibles.h"
#include "bts2_app_inc.h"
#include "ble_connection_manager.h"
#include "bt_connection_manager.h"
#include "audio_manager.h"
#include "bf0_pm.h"
#include "ui/ui.h"
#include "ui/ui_dispatch.h"
#include "ui/ui_runtime_adapter.h"
#include "network/net_manager.h"
#include "xiaozhi/weather/weather.h"
#include "xiaozhi/bt_env.h"
#include "xiaozhi/xiaozhi_client_public.h"
#include "mem_section.h"
#include "aw32001_debug.h"
#include "bq27220_monitor.h"

#define LCD_DEVICE_NAME "lcd"
#define UI_BRIGHTNESS 100U
#define BT_APP_READY 0U
#define BT_APP_CONNECT_PAN 1U
#define BT_APP_CONNECT_PAN_SUCCESS 2U
#define PAN_TIMER_MS 3000U

/* xz_ui 线程栈移到 PSRAM */
#define XZ_UI_THREAD_STACK_SIZE (512 * 1024)  /* 512KB PSRAM */
#define XZ_UI_THREAD_PRIORITY 20
#define XZ_UI_THREAD_TICK 10

#define BACKLIGHT_THREAD_STACK_SIZE 2048
#define BACKLIGHT_THREAD_PRIORITY 19
#define BACKLIGHT_STEP_DELAY_MS 20
#define BACKLIGHT_LEVEL_MIN 50U
#define BACKLIGHT_LEVEL_MAX 100U
#define TF_MOUNT_THREAD_STACK_SIZE 4096
#define TF_MOUNT_THREAD_PRIORITY 18
#define TF_MOUNT_RETRY_COUNT 120
#define TF_MOUNT_RETRY_DELAY_MS 100U

static struct rt_thread s_ui_thread;
static struct rt_thread s_backlight_thread;
static struct rt_thread s_tf_mount_thread;

/* xz_ui 线程栈放入 PSRAM section - 使用 L2_RET_BSS section */
#if defined(__CC_ARM) || defined(__CLANG_ARM)
L2_RET_BSS_SECT_BEGIN(xz_ui_stack)
static rt_uint8_t s_ui_thread_stack[XZ_UI_THREAD_STACK_SIZE];
L2_RET_BSS_SECT_END
#else
static rt_uint8_t s_ui_thread_stack[XZ_UI_THREAD_STACK_SIZE] L2_RET_BSS_SECT(xz_ui_stack);
#endif

#if defined(__CC_ARM) || defined(__CLANG_ARM)
L2_RET_BSS_SECT_BEGIN(backlight_thread_stack)
ALIGN(RT_ALIGN_SIZE)
static rt_uint8_t s_backlight_thread_stack[BACKLIGHT_THREAD_STACK_SIZE];
L2_RET_BSS_SECT_END
L2_RET_BSS_SECT_BEGIN(tf_mount_thread_stack)
ALIGN(RT_ALIGN_SIZE)
static rt_uint8_t s_tf_mount_thread_stack[TF_MOUNT_THREAD_STACK_SIZE];
L2_RET_BSS_SECT_END
#else
ALIGN(RT_ALIGN_SIZE)
static rt_uint8_t s_backlight_thread_stack[BACKLIGHT_THREAD_STACK_SIZE]
    L2_RET_BSS_SECT(backlight_thread_stack);
ALIGN(RT_ALIGN_SIZE)
static rt_uint8_t s_tf_mount_thread_stack[TF_MOUNT_THREAD_STACK_SIZE]
    L2_RET_BSS_SECT(tf_mount_thread_stack);
#endif

static volatile rt_uint8_t s_ui_debug_open_reading = 0U;
static volatile rt_uint8_t s_xiaozhi_registered = 0U;
static rt_uint8_t s_backlight_target_brightness = 0U;
bt_app_t g_bt_app_env = {0};
rt_mailbox_t g_bt_app_mb = RT_NULL;
BOOL g_pan_connected = FALSE;
extern BOOL first_pan_connected;

#ifdef BLUETOOTH_NAME
static const char *s_local_bt_name = BLUETOOTH_NAME;
#else
static const char *s_local_bt_name = "ink";
#endif

void HAL_MspInit(void)
{
    BSP_IO_Init();
    set_pinmux();
}

static void bt_app_connect_pan_timeout_handle(void *parameter)
{
    (void)parameter;

    if ((g_bt_app_mb != RT_NULL) && g_bt_app_env.bt_connected)
    {
        rt_mb_send(g_bt_app_mb, BT_APP_CONNECT_PAN);
    }
}

static int bt_app_interface_event_handle(uint16_t type, uint16_t event_id,
                                         uint8_t *data, uint16_t data_len)
{
    int pan_connect_pending = 0;

    (void)data_len;

    if (type == BT_NOTIFY_COMMON)
    {
        switch (event_id)
        {
        case BT_NOTIFY_COMMON_BT_STACK_READY:
            if (g_bt_app_mb != RT_NULL)
            {
                rt_mb_send(g_bt_app_mb, BT_APP_READY);
            }
            break;
        case BT_NOTIFY_COMMON_ENCRYPTION:
        {
            bt_notify_device_mac_t *mac = (bt_notify_device_mac_t *)data;

            if (mac != RT_NULL)
            {
                g_bt_app_env.bd_addr = *mac;
                pan_connect_pending = 1;
                net_manager_notify_bt_acl(true);
            }
            break;
        }
        case BT_NOTIFY_COMMON_PAIR_IND:
        {
            bt_notify_device_base_info_t *info =
                (bt_notify_device_base_info_t *)data;

            if ((info != RT_NULL) && (info->res == BTS2_SUCC))
            {
                g_bt_app_env.bd_addr = info->mac;
                pan_connect_pending = 1;
                net_manager_notify_bt_acl(true);
            }
            break;
        }
        case BT_NOTIFY_COMMON_ACL_DISCONNECTED:
            g_bt_app_env.bt_connected = FALSE;
            g_pan_connected = FALSE;
            net_manager_notify_bt_acl(false);
            net_manager_notify_pan_ready(false);
            if (g_bt_app_env.pan_connect_timer != RT_NULL)
            {
                rt_timer_stop(g_bt_app_env.pan_connect_timer);
            }
            break;
        default:
            break;
        }

        if (pan_connect_pending)
        {
            g_bt_app_env.bt_connected = TRUE;
            if (g_bt_app_env.pan_connect_timer == RT_NULL)
            {
                g_bt_app_env.pan_connect_timer =
                    rt_timer_create("connect_pan",
                                    bt_app_connect_pan_timeout_handle,
                                    &g_bt_app_env,
                                    rt_tick_from_millisecond(PAN_TIMER_MS),
                                    RT_TIMER_FLAG_SOFT_TIMER);
            }
            else
            {
                rt_timer_stop(g_bt_app_env.pan_connect_timer);
            }

            if (g_bt_app_env.pan_connect_timer != RT_NULL)
            {
                rt_timer_start(g_bt_app_env.pan_connect_timer);
            }
        }
    }
    else if (type == BT_NOTIFY_PAN)
    {
        switch (event_id)
        {
        case BT_NOTIFY_PAN_PROFILE_CONNECTED:
            if (g_bt_app_env.pan_connect_timer != RT_NULL)
            {
                rt_timer_stop(g_bt_app_env.pan_connect_timer);
            }
            g_pan_connected = TRUE;
            first_pan_connected = TRUE;
            net_manager_notify_pan_ready(true);
            if (g_bt_app_mb != RT_NULL)
            {
                rt_mb_send(g_bt_app_mb, BT_APP_CONNECT_PAN_SUCCESS);
            }
            break;
        case BT_NOTIFY_PAN_PROFILE_DISCONNECTED:
            g_pan_connected = FALSE;
            net_manager_notify_pan_ready(false);
            break;
        default:
            break;
        }
    }
    else if (type == BT_NOTIFY_HID)
    {
        switch (event_id)
        {
        case BT_NOTIFY_HID_PROFILE_CONNECTED:
            if (!g_pan_connected)
            {
                if (g_bt_app_env.pan_connect_timer != RT_NULL)
                {
                    rt_timer_stop(g_bt_app_env.pan_connect_timer);
                }
                bt_interface_conn_ext((char *)&g_bt_app_env.bd_addr,
                                      BT_PROFILE_PAN);
            }
            break;
        default:
            break;
        }
    }

    return 0;
}

static void set_panel_brightness(rt_uint8_t brightness)
{
    if (brightness > BACKLIGHT_LEVEL_MAX)
    {
        brightness = BACKLIGHT_LEVEL_MAX;
    }
    else if ((brightness != 0U) && (brightness < BACKLIGHT_LEVEL_MIN))
    {
        brightness = BACKLIGHT_LEVEL_MIN;
    }

    s_backlight_target_brightness = brightness;
    board_backlight_set_level(brightness);
}

void app_set_panel_brightness(rt_uint8_t brightness)
{
    set_panel_brightness(brightness);
}

rt_uint8_t app_get_panel_brightness(void)
{
    return s_backlight_target_brightness;
}

static void backlight_thread_entry(void *parameter)
{
    (void)parameter;

    board_backlight_set_level(0U);

    while (1)
    {
        rt_thread_mdelay(1000);
    }
}

static const char *tf_find_device_name(void)
{
    static const char *const candidates[] = {"sd0", "sd1", "sd2", "sdio0"};
    rt_size_t i;

    for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i)
    {
        if (rt_device_find(candidates[i]) != RT_NULL)
        {
            return candidates[i];
        }
    }

    return RT_NULL;
}

static rt_err_t tf_try_mount(const char *device_name, const char *mount_path)
{
    rt_device_t device;
    const char *mounted;

    if (device_name == RT_NULL || mount_path == RT_NULL)
    {
        return -RT_ERROR;
    }

    device = rt_device_find(device_name);
    if (device == RT_NULL)
    {
        return -RT_ENOSYS;
    }

    mounted = dfs_filesystem_get_mounted_path(device);
    if (mounted != RT_NULL && mounted[0] != '\0')
    {
        rt_kprintf("tf: %s already mounted at %s\n", device_name, mounted);
        return RT_EOK;
    }

    if (strcmp(mount_path, "/") != 0)
    {
        mkdir(mount_path, 0);
    }

    if (dfs_mount(device_name, mount_path, "elm", 0, 0) == RT_EOK)
    {
        rt_kprintf("tf: mounted %s at %s\n", device_name, mount_path);
        return RT_EOK;
    }

    rt_kprintf("tf: mount %s -> %s failed errno=%d\n", device_name, mount_path, rt_get_errno());
    return -RT_ERROR;
}

static void tf_mount_thread_entry(void *parameter)
{
    const char *device_name = RT_NULL;
    rt_uint32_t retry;

    (void)parameter;

    for (retry = 0; retry < TF_MOUNT_RETRY_COUNT; ++retry)
    {
        device_name = tf_find_device_name();
        if (device_name != RT_NULL)
        {
            break;
        }
        rt_thread_mdelay(TF_MOUNT_RETRY_DELAY_MS);
    }

    if (device_name == RT_NULL)
    {
        rt_kprintf("tf: no sd device found after %u ms\n",
                   (unsigned int)(TF_MOUNT_RETRY_COUNT * TF_MOUNT_RETRY_DELAY_MS));
        return;
    }

    rt_kprintf("tf: found storage device %s\n", device_name);
    (void)tf_try_mount(device_name, "/");
}

static void ui_thread_entry(void *parameter)
{
    rt_err_t result;
    rt_uint32_t delay_ms;

    (void)parameter;

    result = littlevgl2rtt_init(LCD_DEVICE_NAME);
    if (result != RT_EOK)
    {
        rt_kprintf("ui: littlevgl2rtt_init failed=%d\n", result);
        return;
    }

    rt_kprintf("ui: littlevgl2rtt_init ok\n");
    lv_ex_data_pool_init();
    rt_kprintf("ui: lv_ex_data pool ready\n");
    result = ui_dispatch_init();
    if (result != RT_EOK)
    {
        rt_kprintf("ui: ui_dispatch_init failed=%d\n", result);
        return;
    }
    set_panel_brightness(s_backlight_target_brightness);
    rt_kprintf("ui: before ui_init\n");
    ui_init();
    rt_kprintf("ui: after ui_init\n");
    rt_kprintf("ui: before switch home\n");
    ui_runtime_switch_to(UI_SCREEN_HOME);
    rt_kprintf("ui: after switch home\n");
    rt_kprintf("ui: xz_ui thread ready, ai_dou loaded\n");

    while (1)
    {
        if (s_ui_debug_open_reading != 0U)
        {
            s_ui_debug_open_reading = 0U;
            rt_kprintf("ui: debug switch to reading detail\n");
            ui_runtime_switch_to(UI_SCREEN_READING_DETAIL);
        }

        ui_dispatch_process_pending();
        delay_ms = lv_task_handler();
        if (delay_ms < 5U)
        {
            delay_ms = 5U;
        }
        rt_thread_mdelay(delay_ms);
    }
}

static rt_err_t start_ui_thread(void)
{
    rt_err_t result;

    result = rt_thread_init(&s_ui_thread,
                            "xz_ui",
                            ui_thread_entry,
                            RT_NULL,
                            s_ui_thread_stack,
                            sizeof(s_ui_thread_stack),
                            XZ_UI_THREAD_PRIORITY,
                            XZ_UI_THREAD_TICK);
    if (result != RT_EOK)
    {
        rt_kprintf("ui: thread init failed=%d\n", result);
        return result;
    }

    rt_thread_startup(&s_ui_thread);
    return RT_EOK;
}

static rt_err_t start_backlight_thread(void)
{
    rt_err_t result;

    result = rt_thread_init(&s_backlight_thread,
                            "backlight",
                            backlight_thread_entry,
                            RT_NULL,
                            s_backlight_thread_stack,
                            sizeof(s_backlight_thread_stack),
                            BACKLIGHT_THREAD_PRIORITY,
                            XZ_UI_THREAD_TICK);
    if (result != RT_EOK)
    {
        rt_kprintf("backlight: thread init failed=%d\n", result);
        return result;
    }

    rt_thread_startup(&s_backlight_thread);
    return RT_EOK;
}

static rt_err_t start_tf_mount_thread(void)
{
    rt_err_t result;

    result = rt_thread_init(&s_tf_mount_thread,
                            "tf_mount",
                            tf_mount_thread_entry,
                            RT_NULL,
                            s_tf_mount_thread_stack,
                            sizeof(s_tf_mount_thread_stack),
                            TF_MOUNT_THREAD_PRIORITY,
                            XZ_UI_THREAD_TICK);
    if (result != RT_EOK)
    {
        rt_kprintf("tf: thread init failed=%d\n", result);
        return result;
    }

    rt_thread_startup(&s_tf_mount_thread);
    return RT_EOK;
}

static void ui_dbg_reading(void)
{
    if (!ui_reading_list_prepare_selected_file())
    {
        rt_kprintf("ui_dbg_reading: no readable text file found on TF\n");
        return;
    }

    rt_kprintf("ui_dbg_reading: queued switch to reading detail\n");
    s_ui_debug_open_reading = 1U;
}
MSH_CMD_EXPORT(ui_dbg_reading, switch to reading detail screen);

int main(void)
{
    rt_err_t result;
    uint32_t bt_event = 0;

#ifdef RT_USING_PM
    rt_pm_request(PM_SLEEP_MODE_IDLE);
    pm_scenario_start(PM_SCENARIO_UI);
    rt_kprintf("pm_test: lock idle sleep and keep UI scenario active\n");
#endif

    check_poweron_reason();
    set_pinmux();
    xiaozhi_time_use_china_timezone();
    board_backlight_set(0U);
    aw32001_debug_ensure_charge_enabled();
    rt_kprintf("ui: boot\n");

    result = start_backlight_thread();
    if (result != RT_EOK)
    {
        return 0;
    }

    result = start_tf_mount_thread();
    if (result != RT_EOK)
    {
        return 0;
    }

    /* Initialize audio manager */
    audio_manager_init();

    g_bt_app_mb = rt_mb_create("bt_app", 8, RT_IPC_FLAG_FIFO);
    if (g_bt_app_mb == RT_NULL)
    {
        rt_kprintf("bt: mailbox create failed\n");
        return 0;
    }

#ifdef BSP_BT_CONNECTION_MANAGER
    bt_cm_set_profile_target(BT_CM_HID, BT_LINK_PHONE, 1);
#endif
    bt_interface_register_bt_event_notify_callback(
        bt_app_interface_event_handle);
    sifli_ble_enable();

    result = start_ui_thread();
    if (result != RT_EOK)
    {
        return 0;
    }

    bq27220_monitor_start();

    result = xiaozhi_weather_service_start();
    if (result != RT_EOK)
    {
        rt_kprintf("weather: service start failed=%d\n", result);
    }

    result = net_manager_init();
    if (result != RT_EOK)
    {
        rt_kprintf("net: manager init failed=%d\n", result);
    }

    while (1)
    {
        if (rt_mb_recv(g_bt_app_mb, &bt_event, RT_WAITING_FOREVER) != RT_EOK)
        {
            continue;
        }

        if (bt_event == BT_APP_READY)
        {
            bt_interface_set_local_name(strlen(s_local_bt_name),
                                        (void *)s_local_bt_name);
            rt_kprintf("bt: stack ready, name=%s\n", s_local_bt_name);
        }
        else if (bt_event == BT_APP_CONNECT_PAN)
        {
            if (g_bt_app_env.bt_connected)
            {
                rt_kprintf("bt: connect PAN\n");
                bt_interface_conn_ext((char *)&g_bt_app_env.bd_addr,
                                      BT_PROFILE_PAN);
            }
        }
        else if (bt_event == BT_APP_CONNECT_PAN_SUCCESS)
        {
            rt_kprintf("bt: PAN connected\n");

            if (s_xiaozhi_registered == 0U)
            {
                int reg_result;

                /* Follow the reference flow: PAN is ready first, then register
                 * the device once against the OTA service. */
                rt_thread_mdelay(2000);
                reg_result = register_device_with_server();
                if (reg_result == 0)
                {
                    s_xiaozhi_registered = 1U;
                    rt_kprintf("xz: device registration complete\n");
                }
                else
                {
                    rt_kprintf("xz: device registration failed=%d, will retry on next PAN connect\n",
                               reg_result);
                }
            }

        }
    }
}
