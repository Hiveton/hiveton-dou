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
#include "app_buttons.h"
#include "bf0_pm.h"
#include "gui_app_pm.h"
#include "ui/ui.h"
#include "ui/ui_dispatch.h"
#include "ui/ui_font_manager.h"
#include "ui/ui_runtime_adapter.h"
#include "network/net_manager.h"
#include "xiaozhi/weather/weather.h"
#include "xiaozhi/bt_env.h"
#include "xiaozhi/xiaozhi_client_public.h"
#include "xiaozhi/xiaozhi_service.h"
#include "mem_section.h"
#include "aw32001_debug.h"
#include "bq27220_monitor.h"
#include "petgame.h"

#define LCD_DEVICE_NAME "lcd"
#define UI_BRIGHTNESS 100U
/* xz_ui 线程栈移到 PSRAM */
#define XZ_UI_THREAD_STACK_SIZE (256 * 1024)  /* 256KB PSRAM */
#define XZ_UI_THREAD_PRIORITY 20
#define XZ_UI_THREAD_TICK 10

#define BACKLIGHT_STEP_DELAY_MS 20
#define BACKLIGHT_LEVEL_MIN 50U
#define BACKLIGHT_LEVEL_MAX 100U
#define TF_MOUNT_THREAD_STACK_SIZE 4096
#define TF_MOUNT_THREAD_PRIORITY 18
#define TF_MOUNT_RETRY_COUNT 120
#define TF_MOUNT_RETRY_DELAY_MS 100U
#define TF_DET_PIN 33
#define TF_DET_DEBOUNCE_MS 80U
#define TF_DET_POLL_INTERVAL_MS 1000U
#define TF_LOG_RATE_LIMIT_MS 5000U

#ifndef APP_TF_DET_DEBUG_LOG
#define APP_TF_DET_DEBUG_LOG 0
#endif

#ifndef APP_KEEP_EPD_CONTENT_ON_SLEEP
#define APP_KEEP_EPD_CONTENT_ON_SLEEP 1
#endif

static struct rt_thread s_ui_thread;
static struct rt_thread s_tf_mount_thread;
static struct rt_semaphore s_tf_mount_sem;

/* xz_ui 线程栈放入 PSRAM section - 使用 L2_RET_BSS section */
#if defined(__CC_ARM) || defined(__CLANG_ARM)
L2_RET_BSS_SECT_BEGIN(xz_ui_stack)
static rt_uint8_t s_ui_thread_stack[XZ_UI_THREAD_STACK_SIZE];
L2_RET_BSS_SECT_END
#else
static rt_uint8_t s_ui_thread_stack[XZ_UI_THREAD_STACK_SIZE] L2_RET_BSS_SECT(xz_ui_stack);
#endif

#if defined(__CC_ARM) || defined(__CLANG_ARM)
L2_RET_BSS_SECT_BEGIN(tf_mount_thread_stack)
ALIGN(RT_ALIGN_SIZE)
static rt_uint8_t s_tf_mount_thread_stack[TF_MOUNT_THREAD_STACK_SIZE];
L2_RET_BSS_SECT_END
#else
ALIGN(RT_ALIGN_SIZE)
static rt_uint8_t s_tf_mount_thread_stack[TF_MOUNT_THREAD_STACK_SIZE]
    L2_RET_BSS_SECT(tf_mount_thread_stack);
#endif

static volatile rt_uint8_t s_ui_debug_open_reading = 0U;
static volatile rt_uint8_t s_xiaozhi_registered = 0U;
static volatile rt_uint8_t s_xiaozhi_registering = 0U;
static volatile rt_uint8_t s_tf_card_present = 0U;
static volatile rt_uint8_t s_tf_det_irq_pending = 0U;
static volatile rt_uint8_t s_tf_det_available = 0U;
static volatile rt_uint8_t s_tf_mount_sem_ready = 0U;
static volatile rt_uint8_t s_tf_storage_ready = 0U;
static rt_uint8_t s_backlight_target_brightness = 0U;
static rt_tick_t s_petgame_reading_last_tick = 0;
static rt_tick_t s_xiaozhi_register_last_try = 0;
static rt_tick_t s_tf_last_no_device_log_tick = 0;
static rt_tick_t s_tf_last_mount_fail_log_tick = 0;
static ui_screen_id_t s_petgame_last_screen = UI_SCREEN_NONE;
bt_app_t g_bt_app_env = {0};
rt_mailbox_t g_bt_app_mb = RT_NULL;
BOOL g_pan_connected = FALSE;
extern BOOL first_pan_connected;

#ifdef BSP_USING_PM
extern bool lv_refreshing_done(void);

static void pm_event_handler(gui_pm_event_type_t event)
{
    switch (event)
    {
    case GUI_PM_EVT_SUSPEND:
        lv_timer_enable(false);
        break;
    case GUI_PM_EVT_RESUME:
        lv_timer_enable(true);
        break;
    case GUI_PM_EVT_SHUTDOWN:
    default:
        break;
    }
}
#endif

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

static int bt_app_interface_event_handle(uint16_t type, uint16_t event_id,
                                         uint8_t *data, uint16_t data_len)
{
    net_manager_notify_bt_event(type, event_id, data, data_len);
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

static rt_uint8_t tf_detect_card_present(void)
{
    if (s_tf_det_available != 0U)
    {
        return (rt_pin_read(TF_DET_PIN) == PIN_HIGH) ? 1U : 0U;
    }

    return (tf_find_device_name() != RT_NULL) ? 1U : 0U;
}

static void tf_notify_state_changed(void)
{
    ui_dispatch_request_status_refresh();
    ui_dispatch_request_time_refresh();
}

static void tf_ensure_media_dirs(const char *mount_path)
{
    static const char *const dir_names[] = {"record", "books", "mp3", "pic", "font", "config"};
    char path[64];
    rt_size_t i;

    if (mount_path == RT_NULL)
    {
        return;
    }

    for (i = 0; i < sizeof(dir_names) / sizeof(dir_names[0]); ++i)
    {
        if (strcmp(mount_path, "/") == 0)
        {
            rt_snprintf(path, sizeof(path), "/%s", dir_names[i]);
        }
        else
        {
            rt_snprintf(path, sizeof(path), "%s/%s", mount_path, dir_names[i]);
        }

        mkdir(path, 0);
    }
}

static rt_uint8_t tf_log_rate_limit(rt_tick_t *last_tick)
{
    rt_tick_t now = rt_tick_get();
    rt_tick_t interval = rt_tick_from_millisecond(TF_LOG_RATE_LIMIT_MS);

    if (*last_tick == 0 || (now - *last_tick) >= interval)
    {
        *last_tick = now;
        return 1U;
    }

    return 0U;
}

static rt_err_t tf_try_unmount(const char *device_name)
{
    rt_device_t device;
    const char *mounted;

    if (device_name == RT_NULL)
    {
        return -RT_ERROR;
    }

    device = rt_device_find(device_name);
    if (device == RT_NULL)
    {
        return -RT_ENOSYS;
    }

    mounted = dfs_filesystem_get_mounted_path(device);
    if (mounted == RT_NULL || mounted[0] == '\0')
    {
        return RT_EOK;
    }

    if (dfs_unmount(mounted) == RT_EOK)
    {
        rt_kprintf("tf: unmounted %s from %s\n", device_name, mounted);
        return RT_EOK;
    }

    rt_kprintf("tf: unmount %s from %s failed errno=%d\n",
               device_name, mounted, rt_get_errno());
    return -RT_ERROR;
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
#if APP_TF_DET_DEBUG_LOG
        rt_kprintf("tf: %s already mounted at %s\n", device_name, mounted);
#endif
        return RT_EOK;
    }

    if (strcmp(mount_path, "/") != 0)
    {
        mkdir(mount_path, 0);
    }

    if (dfs_mount(device_name, mount_path, "elm", 0, 0) == RT_EOK)
    {
        rt_kprintf("tf: mounted %s at %s\n", device_name, mount_path);
        tf_ensure_media_dirs(mount_path);
        return RT_EOK;
    }

    return -RT_ERROR;
}

static void tf_det_irq_handler(void *args)
{
    (void)args;

    s_tf_det_irq_pending = 1U;
    if (s_tf_mount_sem_ready != 0U)
    {
        rt_sem_release(&s_tf_mount_sem);
    }
}

static rt_err_t tf_det_init(void)
{
    rt_err_t result;

    rt_pin_mode(TF_DET_PIN, PIN_MODE_INPUT);
    result = rt_pin_attach_irq(TF_DET_PIN,
                               PIN_IRQ_MODE_RISING_FALLING,
                               tf_det_irq_handler,
                               (void *)(rt_base_t)TF_DET_PIN);
    if (result != RT_EOK)
    {
        rt_kprintf("tf: attach det irq failed=%d\n", result);
        return result;
    }

    result = rt_pin_irq_enable(TF_DET_PIN, PIN_IRQ_ENABLE);
    if (result != RT_EOK)
    {
        rt_kprintf("tf: enable det irq failed=%d\n", result);
        return result;
    }

    s_tf_det_available = 1U;
    s_tf_card_present = (rt_pin_read(TF_DET_PIN) == PIN_HIGH) ? 1U : 0U;
    rt_kprintf("tf: det init pin=%d present=%u level=%d\n",
               TF_DET_PIN,
               (unsigned int)s_tf_card_present,
               rt_pin_read(TF_DET_PIN));
    return RT_EOK;
}

static void tf_det_disable_irq(void)
{
    rt_err_t result;

    s_tf_mount_sem_ready = 0U;
    if (s_tf_det_available != 0U)
    {
        result = rt_pin_irq_enable(TF_DET_PIN, PIN_IRQ_DISABLE);
        if (result != RT_EOK)
        {
            rt_kprintf("tf: disable det irq failed=%d\n", result);
        }
        s_tf_det_available = 0U;
    }
    s_tf_det_irq_pending = 0U;
}

static void tf_mount_thread_entry(void *parameter)
{
    const char *device_name;
    rt_uint32_t retry;
    rt_uint8_t present;
    rt_uint8_t state_changed;
    rt_uint8_t first_scan = 1U;

    (void)parameter;

    while (1)
    {
        if (s_tf_det_available != 0U && s_tf_mount_sem_ready != 0U)
        {
            rt_sem_take(&s_tf_mount_sem, RT_WAITING_FOREVER);
            rt_thread_mdelay(TF_DET_DEBOUNCE_MS);
            while (rt_sem_take(&s_tf_mount_sem, RT_WAITING_NO) == RT_EOK)
            {
                s_tf_det_irq_pending = 0U;
            }
#if APP_TF_DET_DEBUG_LOG
            if (s_tf_det_irq_pending != 0U)
            {
                rt_kprintf("tf: det event pin=%d level=%d\n",
                           TF_DET_PIN,
                           rt_pin_read(TF_DET_PIN));
            }
#endif
            s_tf_det_irq_pending = 0U;
        }
        else if (first_scan == 0U)
        {
            rt_thread_mdelay(TF_DET_POLL_INTERVAL_MS);
        }
        first_scan = 0U;

        present = tf_detect_card_present();
        state_changed = (present != s_tf_card_present) ? 1U : 0U;
        if (state_changed != 0U)
        {
            s_tf_card_present = present;
            tf_notify_state_changed();
        }

        if (!present)
        {
            if (state_changed != 0U || s_tf_storage_ready != 0U)
            {
                device_name = tf_find_device_name();
                if (device_name != RT_NULL)
                {
                    (void)tf_try_unmount(device_name);
                }
                ui_font_manager_notify_storage_removed();
                s_tf_storage_ready = 0U;
                rt_kprintf("tf: card removed\n");
            }
            continue;
        }

        if (s_tf_storage_ready != 0U && state_changed == 0U)
        {
            continue;
        }

        device_name = RT_NULL;
        for (retry = 0; retry < TF_MOUNT_RETRY_COUNT; ++retry)
        {
            device_name = tf_find_device_name();
            if (device_name != RT_NULL)
            {
#if APP_TF_DET_DEBUG_LOG
                rt_kprintf("tf: found storage device %s\n", device_name);
#endif
                if (tf_try_mount(device_name, "/") == RT_EOK)
                {
                    if (s_tf_storage_ready == 0U)
                    {
                        ui_font_manager_notify_storage_ready();
                    }
                    s_tf_storage_ready = 1U;
                    break;
                }
            }
            rt_thread_mdelay(TF_MOUNT_RETRY_DELAY_MS);
        }

        if (device_name == RT_NULL)
        {
            if (tf_log_rate_limit(&s_tf_last_no_device_log_tick) != 0U)
            {
                rt_kprintf("tf: no sd device found after %u ms\n",
                           (unsigned int)(TF_MOUNT_RETRY_COUNT * TF_MOUNT_RETRY_DELAY_MS));
            }
        }
        else if (s_tf_storage_ready == 0U)
        {
            if (tf_log_rate_limit(&s_tf_last_mount_fail_log_tick) != 0U)
            {
                rt_kprintf("tf: mount %s failed after %u ms errno=%d\n",
                           device_name,
                           (unsigned int)(TF_MOUNT_RETRY_COUNT * TF_MOUNT_RETRY_DELAY_MS),
                           rt_get_errno());
            }
        }
    }
}

static void ui_thread_entry(void *parameter)
{
    rt_err_t result;
    rt_uint32_t delay_ms;
    rt_tick_t now;
    ui_screen_id_t active;
    rt_tick_t reading_delta;
#ifdef BSP_USING_PM
    rt_device_t lcd_device = RT_NULL;
#endif

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
#ifdef BSP_USING_PM
    lcd_device = rt_device_find(LCD_DEVICE_NAME);
    gui_ctx_init();
    gui_pm_init(lcd_device, pm_event_handler);
#endif

    set_panel_brightness(s_backlight_target_brightness);
    rt_kprintf("ui: before ui_init\n");
    ui_init();
    rt_kprintf("ui: after ui_init\n");
    rt_kprintf("ui: before switch home\n");
    ui_runtime_switch_to(UI_SCREEN_HOME);
    rt_kprintf("ui: after switch home\n");
    ui_font_manager_notify_storage_ready();
    bq27220_monitor_start();
    rt_kprintf("ui: xz_ui thread ready, ai_dou loaded\n");
    petgame_init();
    petgame_set_reading_active(false);

    while (1)
    {
        const rt_tick_t reading_interval = rt_tick_from_millisecond(1000U);

        now = rt_tick_get();
        active = ui_runtime_get_active_screen_id();
        if (active == UI_SCREEN_READING_LIST || active == UI_SCREEN_READING_DETAIL)
        {
            petgame_set_reading_active(true);
            if (s_petgame_last_screen != active)
            {
                s_petgame_last_screen = active;
                if (s_petgame_reading_last_tick == 0U)
                {
                    s_petgame_reading_last_tick = now;
                }
            }

            reading_delta = now - s_petgame_reading_last_tick;
            if (reading_delta >= reading_interval)
            {
                uint32_t add_seconds = reading_delta / reading_interval;
                petgame_add_reading_seconds(add_seconds);
                s_petgame_reading_last_tick += add_seconds * reading_interval;
            }
        }
        else
        {
            petgame_set_reading_active(false);
            s_petgame_last_screen = UI_SCREEN_NONE;
            s_petgame_reading_last_tick = 0U;
        }

        petgame_process();

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

#ifdef BSP_USING_PM
        if (gui_is_force_close())
        {
            if (lv_refreshing_done())
            {
                gui_suspend();
#if !APP_KEEP_EPD_CONTENT_ON_SLEEP && !defined(BSP_LCDC_USING_EPD_8BIT)
                lv_obj_invalidate(lv_screen_active());
                lv_display_trigger_activity(NULL);
#endif
            }
            else
            {
                rt_thread_mdelay(delay_ms);
            }
            continue;
        }
#endif

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

static rt_err_t start_tf_mount_thread(void)
{
    rt_err_t result;

    result = rt_sem_init(&s_tf_mount_sem, "tf_det", 0, RT_IPC_FLAG_FIFO);
    if (result != RT_EOK)
    {
        rt_kprintf("tf: semaphore init failed=%d, fallback to polling\n", result);
    }
    else
    {
        s_tf_mount_sem_ready = 1U;
    }

    if (s_tf_mount_sem_ready != 0U)
    {
        result = tf_det_init();
        if (result != RT_EOK)
        {
            s_tf_det_available = 0U;
            rt_kprintf("tf: det unavailable, fallback to polling\n");
        }
    }

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
        rt_kprintf("tf: thread init failed=%d, continue without auto remount\n", result);
        tf_det_disable_irq();
        return RT_EOK;
    }

    result = rt_thread_startup(&s_tf_mount_thread);
    if (result != RT_EOK)
    {
        rt_kprintf("tf: thread startup failed=%d, continue without auto remount\n", result);
        tf_det_disable_irq();
        return RT_EOK;
    }

    if (s_tf_det_available != 0U && s_tf_mount_sem_ready != 0U)
    {
        rt_sem_release(&s_tf_mount_sem);
    }
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
    xz_prepare_tls_allocator();
    app_buttons_init();
    board_backlight_set(0U);
    aw32001_debug_ensure_charge_enabled();
    rt_kprintf("ui: boot\n");

    result = start_tf_mount_thread();
    if (result != RT_EOK)
    {
        rt_kprintf("tf: mount service unavailable=%d, continue boot\n", result);
    }

    /* Initialize audio manager */
    audio_manager_init();

    result = xiaozhi_weather_service_start();
    if (result != RT_EOK)
    {
        rt_kprintf("weather: service start failed=%d\n", result);
    }

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

    result = net_manager_init();
    if (result != RT_EOK)
    {
        rt_kprintf("net: manager init failed=%d\n", result);
    }

    result = start_ui_thread();
    if (result != RT_EOK)
    {
        return 0;
    }

    while (1)
    {
        if (rt_mb_recv(g_bt_app_mb, &bt_event,
                       rt_tick_from_millisecond(1000)) != RT_EOK)
        {
            continue;
        }
        net_manager_handle_bt_mailbox_event(bt_event);
    }
}
