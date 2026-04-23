#include "network/net_manager.h"

#include <dfs_posix.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>

#include "ui/ui_dispatch.h"
#include "ui/ui_helpers.h"
#include "app_watchdog.h"
#include "xiaozhi/weather/weather.h"
#include "xiaozhi/xiaozhi_client_public.h"
#include "xiaozhi/xiaozhi_service.h"
#include "cat1_modem.h"
#include "bts2_app_inc.h"
#include "bts2_app_interface.h"
#include "bf0_sibles.h"
#include "mem_section.h"

#define NET_MANAGER_THREAD_STACK_SIZE 3072
#define NET_MANAGER_THREAD_PRIORITY   17
#define NET_MANAGER_THREAD_TICK       10
#define NET_MANAGER_REGISTER_THREAD_STACK_SIZE 4096
#define NET_MANAGER_REGISTER_THREAD_PRIORITY   22
#define NET_MANAGER_REGISTER_THREAD_TICK       10

#define NET_MANAGER_EVENT_REFRESH     (1U << 0)
#define NET_MANAGER_REGISTER_EVENT_RUN (1U << 0)
#define NET_MANAGER_CONFIG_DIR_NAME   "config"
#define NET_MANAGER_CONFIG_FILE_NAME  "network_mode.cfg"
#define NET_MANAGER_FORCE_4G_ON_BOOT  1U
#define NET_MANAGER_CLOSE_RETRY_MS    3000U
#define NET_MANAGER_BT_CLOSE_GUARD_MS 3500U
#define NET_MANAGER_POLL_MS           500U
#define NET_MANAGER_XIAOZHI_RESUME_RETRY_MS 10000U
#define NET_MANAGER_BT_APP_READY               0U
#define NET_MANAGER_BT_APP_CONNECT_PAN         1U
#define NET_MANAGER_BT_APP_CONNECT_PAN_SUCCESS  2U
#define NET_MANAGER_PAN_CONNECT_DELAY_MS      3000U
#define NET_MANAGER_XIAOZHI_REGISTER_RETRY_MS 60000U
#define NET_MANAGER_XIAOZHI_REGISTER_DELAY_MS 2000U
#define NET_MANAGER_XIAOZHI_REGISTER_BUSY_RETRY_MS 3000U
#define NET_MANAGER_CAT1_RECOVER_STABLE_POLLS 3U
#define NET_MANAGER_CAT1_RECOVER_GRACE_MS     1500U

#ifdef BLUETOOTH_NAME
static const char *s_local_bt_name = BLUETOOTH_NAME;
#else
static const char *s_local_bt_name = "ink";
#endif

extern bt_app_t g_bt_app_env;
extern BOOL g_pan_connected;
extern BOOL first_pan_connected;

static struct rt_thread s_net_manager_thread;
static struct rt_thread s_net_register_thread;
#if defined(__CC_ARM) || defined(__CLANG_ARM)
L2_RET_BSS_SECT_BEGIN(net_manager_thread_stack)
static rt_uint8_t s_net_manager_stack[NET_MANAGER_THREAD_STACK_SIZE];
static rt_uint8_t s_net_register_stack[NET_MANAGER_REGISTER_THREAD_STACK_SIZE];
L2_RET_BSS_SECT_END
#else
static rt_uint8_t s_net_manager_stack[NET_MANAGER_THREAD_STACK_SIZE]
    L2_RET_BSS_SECT(net_manager_thread_stack);
static rt_uint8_t s_net_register_stack[NET_MANAGER_REGISTER_THREAD_STACK_SIZE]
    L2_RET_BSS_SECT(net_manager_register_stack);
#endif
static rt_event_t s_net_manager_event = RT_NULL;
static rt_event_t s_net_register_event = RT_NULL;
static volatile rt_uint8_t s_bt_connected = 0U;
static volatile rt_uint8_t s_pan_ready = 0U;
static volatile rt_uint8_t s_cat1_ready = 0U;
static volatile rt_uint8_t s_initialized = 0U;
static volatile rt_uint8_t s_bt_stack_ready = 0U;
static volatile rt_uint8_t s_bt_enabled = 0U;
static volatile rt_uint8_t s_4g_enabled = 1U;
static rt_uint8_t s_bt_applied = 0U;
static rt_uint8_t s_4g_applied = 0U;
static rt_uint8_t s_bt_core_requested = 0U;
static rt_uint8_t s_bt_close_pending = 0U;
static rt_uint8_t s_4g_close_pending = 0U;
static rt_uint8_t s_bt_close_guard_pending = 0U;
static rt_tick_t s_bt_close_guard_tick = 0U;
static rt_tick_t s_bt_close_request_tick = 0U;
static rt_tick_t s_4g_close_request_tick = 0U;
static rt_tick_t s_bt_last_loss_tick = 0U;
static rt_uint8_t s_pan_connect_pending = 0U;
static rt_tick_t s_pan_connect_due_tick = 0U;
static volatile rt_uint8_t s_xiaozhi_registered = 0U;
static volatile rt_uint8_t s_xiaozhi_registering = 0U;
static rt_tick_t s_xiaozhi_register_last_try = 0U;
static const char *s_xiaozhi_register_reason = "network_ready";
static rt_uint8_t s_xiaozhi_register_fail_count = 0U;
static volatile rt_uint8_t s_pending_bt_addr_valid = 0U;
static bt_notify_device_mac_t s_pending_bt_addr;
static volatile rt_uint8_t s_pending_bt_stack_valid = 0U;
static volatile rt_uint8_t s_pending_bt_stack_ready = 0U;
static volatile rt_uint8_t s_pending_bt_acl_valid = 0U;
static volatile rt_uint8_t s_pending_bt_acl_connected = 0U;
static volatile rt_uint8_t s_pending_bt_closed_valid = 0U;
static volatile rt_uint8_t s_pending_pan_valid = 0U;
static volatile rt_uint8_t s_pending_pan_ready = 0U;
static volatile rt_uint8_t s_pending_cat1_valid = 0U;
static volatile rt_uint8_t s_pending_cat1_ready = 0U;
static volatile rt_uint8_t s_cat1_poll_blocked_after_false = 0U;
static rt_uint8_t s_cat1_poll_true_count = 0U;
static rt_tick_t s_cat1_false_notify_tick = 0U;
static volatile rt_uint8_t s_pending_pan_action = 0U;
static rt_tick_t s_pending_pan_delay_ms = 0U;
static volatile rt_uint8_t s_pending_mode_valid = 0U;
static volatile rt_uint8_t s_pending_mode_save = 0U;
static volatile net_manager_mode_t s_pending_mode = NET_MANAGER_MODE_NONE;
static rt_tick_t s_xiaozhi_network_resume_last_tick = 0U;
static net_manager_link_t s_active_link = NET_MANAGER_LINK_NONE;
static net_manager_service_state_t s_service_state = NET_MANAGER_SERVICE_OFFLINE;
static net_manager_mode_t s_desired_mode = NET_MANAGER_MODE_4G;
static net_manager_mode_t s_saved_mode_before_sleep = NET_MANAGER_MODE_4G;
static volatile rt_uint8_t s_radios_suspended = 0U;
static rt_uint8_t s_network_ready = 0U;
static rt_uint8_t s_dns_ready = 0U;
static rt_uint8_t s_internet_ready = 0U;

static void net_manager_apply_desired_mode_locked(void);
static void net_manager_clear_pan_connect_delay(void);
static void net_manager_start_pan_connect_delay(rt_tick_t delay_ms);
static void net_manager_request_pan_connect_delay(rt_tick_t delay_ms);
static void net_manager_request_pan_clear(void);
static void net_manager_try_pan_connect(void);
static void net_manager_schedule_register_xiaozhi_device(const char *reason);
static void net_manager_register_thread_entry(void *parameter);
static void net_manager_service_background_work(void);
static void net_manager_apply_pending_notifications(void);
static void net_manager_apply_pending_control_requests(void);
static void net_manager_apply_requested_mode(net_manager_mode_t mode, rt_uint8_t save_config);
static void net_manager_request_mode_async(net_manager_mode_t mode, rt_uint8_t save_config);
static void net_manager_apply_bt_closed(void);
static void net_manager_apply_bt_stack_ready(bool ready);
static void net_manager_apply_bt_acl(bool connected);
static void net_manager_apply_pan_ready(bool ready);
static void net_manager_apply_cat1_ready(bool ready);
static void net_manager_poll_cat1_ready_locked(void);
static rt_bool_t net_manager_bt_has_paired_addr(void);
static rt_bool_t net_manager_bt_runtime_closing(void);
static rt_bool_t net_manager_4g_runtime_closing(void);
static void net_manager_enforce_mutual_exclusion_locked(void);
static rt_bool_t net_manager_bt_close_ready_locked(void);
static rt_bool_t net_manager_bt_selected_locked(void);
static rt_bool_t net_manager_4g_selected_locked(void);

static rt_bool_t net_manager_bt_selected_locked(void)
{
    return (!s_radios_suspended && s_bt_enabled && !s_4g_enabled) ? RT_TRUE : RT_FALSE;
}

static rt_bool_t net_manager_4g_selected_locked(void)
{
    return (!s_radios_suspended && s_4g_enabled && !s_bt_enabled) ? RT_TRUE : RT_FALSE;
}

static rt_bool_t net_manager_bt_runtime_active(void)
{
    return (s_bt_applied != 0U ||
            s_bt_connected != 0U ||
            s_pan_ready != 0U) ? RT_TRUE : RT_FALSE;
}

static rt_bool_t net_manager_4g_runtime_active(void)
{
    return (s_4g_applied != 0U ||
            s_cat1_ready != 0U ||
            cat1_modem_is_ready()) ? RT_TRUE : RT_FALSE;
}

static rt_bool_t net_manager_bt_runtime_closing(void)
{
    return (s_bt_applied != 0U ||
            s_bt_connected != 0U ||
            s_pan_ready != 0U ||
            s_pan_connect_pending != 0U) ? RT_TRUE : RT_FALSE;
}

static rt_bool_t net_manager_4g_runtime_closing(void)
{
    return (s_4g_applied != 0U ||
            s_cat1_ready != 0U ||
            cat1_modem_is_ready()) ? RT_TRUE : RT_FALSE;
}

static rt_bool_t net_manager_bt_close_ready_locked(void)
{
    return (s_bt_applied != 0U ||
            s_bt_connected != 0U ||
            s_pan_ready != 0U ||
            s_pan_connect_pending != 0U) ? RT_TRUE : RT_FALSE;
}

static const char *const s_net_manager_config_dir_candidates[] = {
    "/config",
    "/tf/config",
    "/sd/config",
    "/sd0/config",
    "config",
};

static bool net_manager_dir_exists(const char *path)
{
    struct stat info;

    if (path == RT_NULL)
    {
        return false;
    }

    if (stat(path, &info) != 0)
    {
        return false;
    }

    return (info.st_mode & S_IFDIR) != 0;
}

static bool net_manager_build_config_path(char *buffer, size_t buffer_size, bool ensure_dir)
{
    rt_size_t i;
    const char *config_dir = RT_NULL;

    if (buffer == RT_NULL || buffer_size == 0U)
    {
        return false;
    }

    for (i = 0; i < sizeof(s_net_manager_config_dir_candidates) / sizeof(s_net_manager_config_dir_candidates[0]); ++i)
    {
        if (net_manager_dir_exists(s_net_manager_config_dir_candidates[i]))
        {
            config_dir = s_net_manager_config_dir_candidates[i];
            break;
        }
    }

    if (config_dir == RT_NULL)
    {
        if (!ensure_dir)
        {
            buffer[0] = '\0';
            return false;
        }

        mkdir("/" NET_MANAGER_CONFIG_DIR_NAME, 0);
        config_dir = "/" NET_MANAGER_CONFIG_DIR_NAME;
    }

    rt_snprintf(buffer, buffer_size, "%s/%s", config_dir, NET_MANAGER_CONFIG_FILE_NAME);
    return true;
}

static void net_manager_save_mode_config(net_manager_mode_t mode)
{
    int fd;
    char config_path[64];
    const char *value = NULL;

    if (mode == NET_MANAGER_MODE_BT)
    {
        value = "bt\n";
    }
    else if (mode == NET_MANAGER_MODE_4G)
    {
        value = "4g\n";
    }
    else
    {
        return;
    }

    if (!net_manager_build_config_path(config_path, sizeof(config_path), true))
    {
        return;
    }

    fd = open(config_path, O_WRONLY | O_CREAT | O_TRUNC, 0);
    if (fd < 0)
    {
        return;
    }

    (void)write(fd, value, rt_strlen(value));
    close(fd);
}

static void net_manager_load_mode_config(void)
{
    int fd;
    int length;
    char buffer[16];
    char config_path[64];

#if NET_MANAGER_FORCE_4G_ON_BOOT
    s_desired_mode = NET_MANAGER_MODE_4G;
    s_saved_mode_before_sleep = NET_MANAGER_MODE_4G;
    net_manager_apply_desired_mode_locked();
    net_manager_save_mode_config(NET_MANAGER_MODE_4G);
    return;
#endif

    if (!net_manager_build_config_path(config_path, sizeof(config_path), false))
    {
        return;
    }

    fd = open(config_path, O_RDONLY, 0);
    if (fd < 0)
    {
        return;
    }

    length = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);
    if (length <= 0)
    {
        return;
    }

    buffer[length] = '\0';
    if (rt_strncmp(buffer, "bt", 2) == 0)
    {
        s_desired_mode = NET_MANAGER_MODE_BT;
        s_saved_mode_before_sleep = NET_MANAGER_MODE_BT;
    }
    else if (rt_strncmp(buffer, "4g", 2) == 0)
    {
        s_desired_mode = NET_MANAGER_MODE_4G;
        s_saved_mode_before_sleep = NET_MANAGER_MODE_4G;
    }

    net_manager_apply_desired_mode_locked();
}

static void net_manager_request_status_refresh_if_needed(net_manager_mode_t previous_desired_mode,
                                                         rt_uint8_t previous_bt_stack_ready,
                                                         rt_uint8_t previous_bt_enabled,
                                                         rt_uint8_t previous_bt_connected,
                                                         rt_uint8_t previous_pan_ready,
                                                         rt_uint8_t previous_4g_enabled,
                                                         rt_uint8_t previous_cat1_ready,
                                                         rt_uint8_t previous_radios_suspended)
{
    if (previous_desired_mode != s_desired_mode ||
        previous_bt_stack_ready != s_bt_stack_ready ||
        previous_bt_enabled != s_bt_enabled ||
        previous_bt_connected != s_bt_connected ||
        previous_pan_ready != s_pan_ready ||
        previous_4g_enabled != s_4g_enabled ||
        previous_cat1_ready != s_cat1_ready ||
        previous_radios_suspended != s_radios_suspended)
    {
        ui_dispatch_request_status_refresh();
    }
}

static void net_manager_signal_refresh(void)
{
    if (s_net_manager_event != RT_NULL)
    {
        rt_event_send(s_net_manager_event, NET_MANAGER_EVENT_REFRESH);
    }
}

static net_manager_mode_t net_manager_get_runtime_mode_locked(void)
{
    if (s_radios_suspended)
    {
        return NET_MANAGER_MODE_SLEEP;
    }

    if (s_bt_enabled && s_bt_stack_ready)
    {
        return NET_MANAGER_MODE_BT;
    }

    if (s_4g_enabled)
    {
        return NET_MANAGER_MODE_4G;
    }

    return NET_MANAGER_MODE_NONE;
}

static void net_manager_apply_desired_mode_locked(void)
{
    if (s_radios_suspended)
    {
        s_bt_enabled = 0U;
        s_4g_enabled = 0U;
        s_pan_connect_pending = 0U;
        s_pan_connect_due_tick = 0U;
        net_manager_enforce_mutual_exclusion_locked();
        return;
    }

    switch (s_desired_mode)
    {
    case NET_MANAGER_MODE_BT:
        s_bt_enabled = 1U;
        s_4g_enabled = 0U;
        break;
    case NET_MANAGER_MODE_4G:
        s_bt_enabled = 0U;
        s_4g_enabled = 1U;
        s_pan_connect_pending = 0U;
        s_pan_connect_due_tick = 0U;
        break;
    case NET_MANAGER_MODE_NONE:
    case NET_MANAGER_MODE_SLEEP:
    default:
        s_bt_enabled = 0U;
        s_4g_enabled = 0U;
        s_pan_connect_pending = 0U;
        s_pan_connect_due_tick = 0U;
        break;
    }

    net_manager_enforce_mutual_exclusion_locked();
}

static void net_manager_enforce_mutual_exclusion_locked(void)
{
    if (s_radios_suspended)
    {
        s_bt_enabled = 0U;
        s_4g_enabled = 0U;
        s_bt_connected = 0U;
        s_pan_ready = 0U;
        s_cat1_ready = 0U;
        s_cat1_poll_blocked_after_false = 0U;
        s_cat1_poll_true_count = 0U;
        s_cat1_false_notify_tick = 0U;
        s_pan_connect_pending = 0U;
        s_pan_connect_due_tick = 0U;
        g_bt_app_env.bt_connected = FALSE;
        g_pan_connected = FALSE;
        return;
    }

    if (s_bt_enabled)
    {
        s_4g_enabled = 0U;
        s_cat1_ready = 0U;
        s_cat1_poll_blocked_after_false = 0U;
        s_cat1_poll_true_count = 0U;
        s_cat1_false_notify_tick = 0U;
        return;
    }

    if (s_4g_enabled)
    {
        s_bt_connected = 0U;
        s_pan_ready = 0U;
        s_pan_connect_pending = 0U;
        s_pan_connect_due_tick = 0U;
        g_bt_app_env.bt_connected = FALSE;
        g_pan_connected = FALSE;
        return;
    }

    s_bt_connected = 0U;
    s_pan_ready = 0U;
    s_cat1_ready = 0U;
    s_cat1_poll_blocked_after_false = 0U;
    s_cat1_poll_true_count = 0U;
    s_cat1_false_notify_tick = 0U;
    s_pan_connect_pending = 0U;
    s_pan_connect_due_tick = 0U;
    g_bt_app_env.bt_connected = FALSE;
    g_pan_connected = FALSE;
}

static rt_bool_t net_manager_bt_has_paired_addr(void)
{
    rt_size_t i;

    for (i = 0; i < sizeof(g_bt_app_env.bd_addr.addr); ++i)
    {
        if (g_bt_app_env.bd_addr.addr[i] != 0x00U &&
            g_bt_app_env.bd_addr.addr[i] != 0xFFU)
        {
            return RT_TRUE;
        }
    }

    return RT_FALSE;
}

static void net_manager_clear_pan_connect_delay(void)
{
    s_pan_connect_pending = 0U;
    s_pan_connect_due_tick = 0U;
}

static void net_manager_start_pan_connect_delay(rt_tick_t delay_ms)
{
    if (s_desired_mode != NET_MANAGER_MODE_BT || !s_bt_enabled)
    {
        return;
    }

    s_pan_connect_pending = 1U;
    s_pan_connect_due_tick = rt_tick_get() + rt_tick_from_millisecond(delay_ms);
}

static void net_manager_request_pan_connect_delay(rt_tick_t delay_ms)
{
    s_pending_pan_delay_ms = delay_ms;
    s_pending_pan_action = 2U;
    net_manager_signal_refresh();
}

static void net_manager_request_pan_clear(void)
{
    s_pending_pan_action = 1U;
    net_manager_signal_refresh();
}

static void net_manager_try_pan_connect(void)
{
    if (!s_pan_connect_pending)
    {
        return;
    }

    if (s_desired_mode != NET_MANAGER_MODE_BT || !s_bt_enabled)
    {
        net_manager_clear_pan_connect_delay();
        return;
    }

    if (s_pan_connect_due_tick != 0U &&
        rt_tick_get() < s_pan_connect_due_tick)
    {
        return;
    }

    net_manager_clear_pan_connect_delay();

    if (!net_manager_bt_has_paired_addr())
    {
        return;
    }

    rt_kprintf("bt: connect PAN\n");
    g_bt_app_env.bt_connected = TRUE;
    bt_interface_conn_ext((char *)&g_bt_app_env.bd_addr, BT_PROFILE_PAN);
}

static rt_tick_t net_manager_register_retry_ticks(void)
{
    uint32_t retry_ms;

    if (s_xiaozhi_register_fail_count <= 1U)
    {
        retry_ms = NET_MANAGER_XIAOZHI_REGISTER_RETRY_MS;
    }
    else if (s_xiaozhi_register_fail_count == 2U)
    {
        retry_ms = 180000U;
    }
    else if (s_xiaozhi_register_fail_count == 3U)
    {
        retry_ms = 300000U;
    }
    else
    {
        retry_ms = 600000U;
    }

    return rt_tick_from_millisecond(retry_ms);
}

static void net_manager_schedule_register_xiaozhi_device(const char *reason)
{
    rt_tick_t now_tick;

    if (s_xiaozhi_registered != 0U || s_xiaozhi_registering != 0U)
    {
        return;
    }

    if (!net_manager_can_run_ai())
    {
        return;
    }

    if (xiaozhi_service_get_state() != XZ_SERVICE_IDLE)
    {
        return;
    }

    now_tick = rt_tick_get();
    if (s_xiaozhi_register_last_try != 0 &&
        (rt_tick_t)(now_tick - s_xiaozhi_register_last_try) <
            net_manager_register_retry_ticks())
    {
        return;
    }

    s_xiaozhi_register_reason = reason ? reason : "network_ready";
    if (s_net_register_event != RT_NULL)
    {
        rt_event_send(s_net_register_event, NET_MANAGER_REGISTER_EVENT_RUN);
    }
}

static void net_manager_register_thread_entry(void *parameter)
{
    rt_uint32_t events = 0U;

    (void)parameter;

    while (1)
    {
        rt_tick_t now_tick;
        int reg_result;
        const char *reason;

        if (s_net_register_event == RT_NULL)
        {
            rt_thread_mdelay(1000);
            continue;
        }

        if (rt_event_recv(s_net_register_event,
                          NET_MANAGER_REGISTER_EVENT_RUN,
                          RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                          RT_WAITING_FOREVER,
                          &events) != RT_EOK)
        {
            continue;
        }

        if (s_xiaozhi_registered != 0U || s_xiaozhi_registering != 0U)
        {
            continue;
        }

        if (!net_manager_can_run_ai() ||
            xiaozhi_service_get_state() != XZ_SERVICE_IDLE)
        {
            continue;
        }

        now_tick = rt_tick_get();
        if (s_xiaozhi_register_last_try != 0 &&
            (rt_tick_t)(now_tick - s_xiaozhi_register_last_try) <
                net_manager_register_retry_ticks())
        {
            continue;
        }

        s_xiaozhi_registering = 1U;
        s_xiaozhi_register_last_try = now_tick;
        rt_thread_mdelay(NET_MANAGER_XIAOZHI_REGISTER_DELAY_MS);
        if (!net_manager_can_run_ai() ||
            xiaozhi_service_get_state() != XZ_SERVICE_IDLE)
        {
            s_xiaozhi_registering = 0U;
            continue;
        }

        reason = s_xiaozhi_register_reason;
        rt_kprintf("xz: device registration start reason=%s\n",
                   reason ? reason : "network_ready");
        reg_result = register_device_with_server();
        if (reg_result == 0)
        {
            s_xiaozhi_registered = 1U;
            s_xiaozhi_register_fail_count = 0U;
            rt_kprintf("xz: device registration complete\n");
        }
        else if (reg_result == -RT_EBUSY)
        {
            rt_kprintf("xz: device registration busy=%d, retry soon\n",
                       reg_result);
            s_xiaozhi_register_last_try = 0U;
            rt_thread_mdelay(NET_MANAGER_XIAOZHI_REGISTER_BUSY_RETRY_MS);
            s_xiaozhi_registering = 0U;
            net_manager_schedule_register_xiaozhi_device("busy");
            continue;
        }
        else
        {
            if (s_xiaozhi_register_fail_count < 255U)
            {
                ++s_xiaozhi_register_fail_count;
            }
            rt_kprintf("xz: device registration failed=%d, will retry on next network ready\n",
                       reg_result);
        }
        s_xiaozhi_registering = 0U;
    }
}

static void net_manager_refresh_side_effects(net_manager_link_t previous_link,
                                             net_manager_link_t link,
                                             rt_uint8_t previous_internet_ready,
                                             rt_uint8_t internet_ready)
{
    ui_dispatch_request_status_refresh();
    ui_dispatch_request_time_refresh();

    if ((internet_ready != 0U) &&
        (previous_internet_ready == 0U || link != previous_link))
    {
        rt_tick_t now_tick = rt_tick_get();

        xiaozhi_weather_request_force_refresh();
        if (xiaozhi_service_is_running() &&
            xiaozhi_service_get_state() == XZ_SERVICE_READY &&
            (s_xiaozhi_network_resume_last_tick == 0U ||
             (rt_tick_t)(now_tick - s_xiaozhi_network_resume_last_tick) >=
                 rt_tick_from_millisecond(NET_MANAGER_XIAOZHI_RESUME_RETRY_MS)))
        {
            s_xiaozhi_network_resume_last_tick = now_tick;
            xiaozhi_service_request_greeting();
        }
    }
}

static net_manager_service_state_t net_manager_resolve_service_state(net_manager_link_t link)
{
    if (s_radios_suspended)
    {
        return NET_MANAGER_SERVICE_OFFLINE;
    }

    if (link == NET_MANAGER_LINK_BT_PAN)
    {
        return NET_MANAGER_SERVICE_INTERNET_READY;
    }

    if (link == NET_MANAGER_LINK_4G_CAT1)
    {
        return NET_MANAGER_SERVICE_INTERNET_READY;
    }

    if (s_bt_enabled || s_4g_enabled)
    {
        return NET_MANAGER_SERVICE_RADIO_READY;
    }

    return NET_MANAGER_SERVICE_OFFLINE;
}

static net_manager_link_t net_manager_resolve_active_link(void)
{
    if (s_radios_suspended)
    {
        return NET_MANAGER_LINK_NONE;
    }

    if (net_manager_bt_selected_locked() && s_pan_ready)
    {
        return NET_MANAGER_LINK_BT_PAN;
    }

    if (net_manager_4g_selected_locked() && s_cat1_ready)
    {
        return NET_MANAGER_LINK_4G_CAT1;
    }

    return NET_MANAGER_LINK_NONE;
}

static void net_manager_update_runtime_state(void)
{
    net_manager_link_t previous_link = s_active_link;
    net_manager_service_state_t previous_service_state = s_service_state;
    rt_uint8_t previous_network_ready = s_network_ready;
    rt_uint8_t previous_dns_ready = s_dns_ready;
    rt_uint8_t previous_internet_ready = s_internet_ready;
    net_manager_link_t link = net_manager_resolve_active_link();
    net_manager_service_state_t service_state = net_manager_resolve_service_state(link);
    rt_uint8_t network_ready = (link != NET_MANAGER_LINK_NONE) ? 1U : 0U;
    rt_uint8_t dns_ready = (service_state >= NET_MANAGER_SERVICE_DNS_READY) ? 1U : 0U;
    rt_uint8_t internet_ready = (service_state >= NET_MANAGER_SERVICE_INTERNET_READY) ? 1U : 0U;

    net_manager_enforce_mutual_exclusion_locked();
    link = net_manager_resolve_active_link();
    service_state = net_manager_resolve_service_state(link);
    network_ready = (link != NET_MANAGER_LINK_NONE) ? 1U : 0U;
    dns_ready = (service_state >= NET_MANAGER_SERVICE_DNS_READY) ? 1U : 0U;
    internet_ready = (service_state >= NET_MANAGER_SERVICE_INTERNET_READY) ? 1U : 0U;

    s_active_link = link;
    s_service_state = service_state;
    s_network_ready = network_ready;
    s_dns_ready = dns_ready;
    s_internet_ready = internet_ready;

    if (link != previous_link)
    {
        rt_kprintf("net: active link -> %d\n", (int)link);
    }

    if (service_state != previous_service_state)
    {
        rt_kprintf("net: service state -> %d\n", (int)service_state);
    }

    if (link != previous_link ||
        service_state != previous_service_state ||
        network_ready != previous_network_ready ||
        dns_ready != previous_dns_ready ||
        internet_ready != previous_internet_ready)
    {
        net_manager_refresh_side_effects(previous_link,
                                         link,
                                         previous_internet_ready,
                                         internet_ready);
    }
}

static void net_manager_service_background_work(void)
{
    net_manager_try_pan_connect();
    net_manager_schedule_register_xiaozhi_device("poll");
}

static void net_manager_apply_pending_notifications(void)
{
    if (s_pending_bt_addr_valid)
    {
        s_pending_bt_addr_valid = 0U;
        g_bt_app_env.bd_addr = s_pending_bt_addr;
    }

    if (s_pending_pan_action != 0U)
    {
        rt_uint8_t action = s_pending_pan_action;
        rt_tick_t delay_ms = s_pending_pan_delay_ms;

        s_pending_pan_action = 0U;
        if (action == 1U)
        {
            net_manager_clear_pan_connect_delay();
        }
        else if (action == 2U)
        {
            net_manager_start_pan_connect_delay(delay_ms);
        }
    }

    if (s_pending_bt_stack_valid)
    {
        bool ready = s_pending_bt_stack_ready != 0U;

        s_pending_bt_stack_valid = 0U;
        net_manager_apply_bt_stack_ready(ready);
    }

    if (s_pending_bt_closed_valid)
    {
        s_pending_bt_closed_valid = 0U;
        net_manager_apply_bt_closed();
    }

    if (s_pending_bt_acl_valid)
    {
        bool connected = s_pending_bt_acl_connected != 0U;

        s_pending_bt_acl_valid = 0U;
        net_manager_apply_bt_acl(connected);
    }

    if (s_pending_pan_valid)
    {
        bool ready = s_pending_pan_ready != 0U;

        s_pending_pan_valid = 0U;
        net_manager_apply_pan_ready(ready);
    }

    if (s_pending_cat1_valid)
    {
        bool ready = s_pending_cat1_ready != 0U;

        s_pending_cat1_valid = 0U;
        net_manager_apply_cat1_ready(ready);
    }
}

static void net_manager_request_mode_async(net_manager_mode_t mode, rt_uint8_t save_config)
{
    s_pending_mode = mode;
    s_pending_mode_save = save_config ? 1U : 0U;
    s_pending_mode_valid = 1U;
    net_manager_signal_refresh();
}

static void net_manager_apply_requested_mode(net_manager_mode_t mode, rt_uint8_t save_config)
{
    net_manager_mode_t previous_desired_mode = s_desired_mode;
    rt_uint8_t previous_bt_enabled = s_bt_enabled;
    rt_uint8_t previous_4g_enabled = s_4g_enabled;

    if (s_radios_suspended && mode != NET_MANAGER_MODE_SLEEP)
    {
        if (mode == NET_MANAGER_MODE_BT ||
            mode == NET_MANAGER_MODE_4G ||
            mode == NET_MANAGER_MODE_NONE)
        {
            s_saved_mode_before_sleep = mode;
        }
        net_manager_update_runtime_state();
        return;
    }

    if (save_config)
    {
        net_manager_save_mode_config(mode);
    }

    s_desired_mode = mode;
    if (mode == NET_MANAGER_MODE_4G)
    {
        s_bt_close_guard_pending = 1U;
        s_bt_close_guard_tick = rt_tick_get();
    }
    else if (mode == NET_MANAGER_MODE_BT || mode == NET_MANAGER_MODE_NONE)
    {
        s_bt_close_guard_pending = 0U;
        s_bt_close_guard_tick = 0U;
    }
    net_manager_apply_desired_mode_locked();
    s_bt_last_loss_tick = rt_tick_get();
    net_manager_request_status_refresh_if_needed(previous_desired_mode,
                                                 s_bt_stack_ready,
                                                 previous_bt_enabled,
                                                 s_bt_connected,
                                                 s_pan_ready,
                                                 previous_4g_enabled,
                                                 s_cat1_ready,
                                                 s_radios_suspended);
}

static void net_manager_apply_pending_control_requests(void)
{
    if (s_pending_mode_valid)
    {
        net_manager_mode_t mode = s_pending_mode;
        rt_uint8_t save_config = s_pending_mode_save;

        s_pending_mode_valid = 0U;
        s_pending_mode_save = 0U;
        net_manager_apply_requested_mode(mode, save_config);
    }
}

static void net_manager_apply_mode(void)
{
    rt_tick_t now_tick = rt_tick_get();

    net_manager_apply_desired_mode_locked();

    if (s_bt_enabled)
    {
        s_bt_close_pending = 0U;
        s_bt_close_request_tick = 0U;
    }

    if (s_4g_enabled)
    {
        s_4g_close_pending = 0U;
        s_4g_close_request_tick = 0U;
    }

    if (s_4g_enabled && s_bt_close_guard_pending)
    {
        if (net_manager_bt_close_ready_locked() &&
            (!s_bt_close_pending ||
             (rt_tick_t)(now_tick - s_bt_close_request_tick) >=
                 rt_tick_from_millisecond(NET_MANAGER_CLOSE_RETRY_MS)))
        {
            s_bt_close_pending = 1U;
            s_bt_close_request_tick = now_tick;
            bt_interface_close_bt();
        }

        s_bt_connected = 0U;
        s_pan_ready = 0U;
        s_bt_applied = 0U;

        if ((rt_tick_t)(now_tick - s_bt_close_guard_tick) <
            rt_tick_from_millisecond(NET_MANAGER_BT_CLOSE_GUARD_MS))
        {
            net_manager_update_runtime_state();
            net_manager_signal_refresh();
            return;
        }

        s_bt_close_guard_pending = 0U;
        s_bt_close_pending = 0U;
        s_bt_close_request_tick = 0U;
    }

    if (s_bt_enabled && net_manager_4g_runtime_active())
    {
        if (!s_4g_close_pending ||
            (rt_tick_t)(now_tick - s_4g_close_request_tick) >=
                rt_tick_from_millisecond(NET_MANAGER_CLOSE_RETRY_MS))
        {
            s_4g_close_pending = 1U;
            s_4g_close_request_tick = now_tick;
            (void)cat1_modem_request_offline();
        }
        s_cat1_ready = 0U;
        s_4g_applied = 0U;
        net_manager_enforce_mutual_exclusion_locked();
        if (!cat1_modem_is_ready())
        {
            s_4g_close_pending = 0U;
            s_4g_close_request_tick = 0U;
        }
        net_manager_update_runtime_state();
        net_manager_signal_refresh();
        return;
    }

    if (s_4g_enabled && net_manager_bt_runtime_active())
    {
        if (!s_bt_applied &&
            !s_bt_connected &&
            !s_pan_ready &&
            !s_pan_connect_pending)
        {
            s_bt_applied = 0U;
        }
        else
        {
            if (!s_bt_close_pending ||
                (rt_tick_t)(now_tick - s_bt_close_request_tick) >=
                    rt_tick_from_millisecond(NET_MANAGER_CLOSE_RETRY_MS))
            {
                s_bt_close_pending = 1U;
                s_bt_close_request_tick = now_tick;
                bt_interface_close_bt();
            }
            s_bt_applied = 1U;
            s_bt_connected = 0U;
            s_pan_ready = 0U;
            net_manager_enforce_mutual_exclusion_locked();
            net_manager_update_runtime_state();
            return;
        }
    }

    if (s_bt_applied != s_bt_enabled)
    {
        if (s_bt_enabled && !s_bt_stack_ready)
        {
            if (!s_bt_core_requested)
            {
                s_bt_core_requested = 1U;
                sifli_ble_enable();
            }
            return;
        }

        if (!s_bt_stack_ready)
        {
            if (!s_bt_enabled)
            {
                s_bt_applied = 0U;
            }
        }
        else
        {
            if (s_bt_enabled)
            {
                if (!s_4g_applied)
                {
                    bt_interface_open_bt();
                }
            }
            else
            {
                bt_interface_close_bt();
                s_bt_connected = 0U;
                s_pan_ready = 0U;
            }
            s_bt_applied = s_bt_enabled;
        }
    }

    if (s_4g_applied != s_4g_enabled)
    {
        if (s_4g_enabled)
        {
            if (!s_bt_applied)
            {
                (void)cat1_modem_request_online();
            }
        }
        else
        {
            (void)cat1_modem_request_offline();
            s_cat1_ready = 0U;
        }
        s_4g_applied = s_4g_enabled;
    }

    net_manager_enforce_mutual_exclusion_locked();
}

static void net_manager_thread_entry(void *parameter)
{
    rt_uint32_t events = 0U;

    (void)parameter;
    s_bt_last_loss_tick = rt_tick_get();
    app_watchdog_set_module_required(APP_WDT_MODULE_NET, true);

    while (1)
    {
        if (s_net_manager_event != RT_NULL)
        {
            rt_event_recv(s_net_manager_event,
                          NET_MANAGER_EVENT_REFRESH,
                          RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                          rt_tick_from_millisecond(NET_MANAGER_POLL_MS),
                          &events);
        }

        net_manager_apply_pending_control_requests();
        net_manager_apply_pending_notifications();
        net_manager_apply_mode();
        net_manager_poll_cat1_ready_locked();
        net_manager_enforce_mutual_exclusion_locked();
        net_manager_update_runtime_state();
        net_manager_service_background_work();
        app_watchdog_heartbeat(APP_WDT_MODULE_NET);
    }
}

rt_err_t net_manager_init(void)
{
    rt_err_t result;

    if (s_initialized)
    {
        return RT_EOK;
    }

    net_manager_load_mode_config();
#if NET_MANAGER_FORCE_4G_ON_BOOT
    s_desired_mode = NET_MANAGER_MODE_4G;
    s_saved_mode_before_sleep = NET_MANAGER_MODE_4G;
    s_bt_enabled = 0U;
    s_4g_enabled = 1U;
    s_bt_close_guard_pending = 1U;
    s_bt_close_guard_tick = rt_tick_get();
#endif

    if (s_net_manager_event == RT_NULL)
    {
        s_net_manager_event = rt_event_create("netmgr", RT_IPC_FLAG_FIFO);
        if (s_net_manager_event == RT_NULL)
        {
            return -RT_ENOMEM;
        }
    }

    if (s_net_register_event == RT_NULL)
    {
        s_net_register_event = rt_event_create("netreg", RT_IPC_FLAG_FIFO);
        if (s_net_register_event == RT_NULL)
        {
            return -RT_ENOMEM;
        }
    }

    result = cat1_modem_init();
    if (result != RT_EOK)
    {
        return result;
    }

    result = rt_thread_init(&s_net_manager_thread,
                            "net_mgr",
                            net_manager_thread_entry,
                            RT_NULL,
                            s_net_manager_stack,
                            sizeof(s_net_manager_stack),
                            NET_MANAGER_THREAD_PRIORITY,
                            NET_MANAGER_THREAD_TICK);
    if (result != RT_EOK)
    {
        return result;
    }

    result = rt_thread_init(&s_net_register_thread,
                            "net_reg",
                            net_manager_register_thread_entry,
                            RT_NULL,
                            s_net_register_stack,
                            sizeof(s_net_register_stack),
                            NET_MANAGER_REGISTER_THREAD_PRIORITY,
                            NET_MANAGER_REGISTER_THREAD_TICK);
    if (result != RT_EOK)
    {
        return result;
    }

    rt_thread_startup(&s_net_manager_thread);
    rt_thread_startup(&s_net_register_thread);
    app_watchdog_set_module_required(APP_WDT_MODULE_NET, true);
    net_manager_signal_refresh();
    s_initialized = 1U;
    return RT_EOK;
}

void net_manager_request_bt_mode(void)
{
    net_manager_request_mode_async(NET_MANAGER_MODE_BT, 1U);
}

void net_manager_request_4g_mode(void)
{
    net_manager_request_mode_async(NET_MANAGER_MODE_4G, 1U);
}

void net_manager_request_none_mode(void)
{
    net_manager_request_mode_async(NET_MANAGER_MODE_NONE, 0U);
}

void net_manager_suspend_for_sleep(void)
{
    net_manager_mode_t previous_desired_mode = s_desired_mode;
    rt_uint8_t previous_bt_enabled = s_bt_enabled;
    rt_uint8_t previous_bt_connected = s_bt_connected;
    rt_uint8_t previous_pan_ready = s_pan_ready;
    rt_uint8_t previous_4g_enabled = s_4g_enabled;
    rt_uint8_t previous_cat1_ready = s_cat1_ready;
    rt_uint8_t previous_radios_suspended = s_radios_suspended;

    if (!s_radios_suspended)
    {
        s_saved_mode_before_sleep = s_desired_mode;
    }

    s_pending_mode_valid = 0U;
    s_pending_mode_save = 0U;
    s_radios_suspended = 1U;
    s_desired_mode = NET_MANAGER_MODE_SLEEP;
    net_manager_apply_desired_mode_locked();
    net_manager_request_pan_clear();
    net_manager_update_runtime_state();
    net_manager_request_status_refresh_if_needed(previous_desired_mode,
                                                 s_bt_stack_ready,
                                                 previous_bt_enabled,
                                                 previous_bt_connected,
                                                 previous_pan_ready,
                                                 previous_4g_enabled,
                                                 previous_cat1_ready,
                                                 previous_radios_suspended);
    net_manager_signal_refresh();
}

void net_manager_resume_after_wake(void)
{
    net_manager_mode_t previous_desired_mode = s_desired_mode;
    rt_uint8_t previous_bt_enabled = s_bt_enabled;
    rt_uint8_t previous_4g_enabled = s_4g_enabled;
    rt_uint8_t previous_radios_suspended = s_radios_suspended;

    s_radios_suspended = 0U;
    s_desired_mode = s_saved_mode_before_sleep;
    net_manager_apply_desired_mode_locked();
    s_bt_last_loss_tick = rt_tick_get();
    net_manager_request_status_refresh_if_needed(previous_desired_mode,
                                                 s_bt_stack_ready,
                                                 previous_bt_enabled,
                                                 s_bt_connected,
                                                 s_pan_ready,
                                                 previous_4g_enabled,
                                                 s_cat1_ready,
                                                 previous_radios_suspended);
    net_manager_signal_refresh();
}

void net_manager_notify_bt_event(uint16_t type, uint16_t event_id,
                                 uint8_t *data, uint16_t data_len)
{
    (void)data_len;

    if (type == BT_NOTIFY_COMMON)
    {
        switch (event_id)
        {
        case BT_NOTIFY_COMMON_BT_STACK_READY:
            if (!net_manager_bt_selected_locked())
            {
                rt_kprintf("bt: stack ready outside BT mode, schedule close\n");
                net_manager_notify_bt_stack_ready(true);
                net_manager_signal_refresh();
                break;
            }
            bt_interface_set_local_name(rt_strlen(s_local_bt_name),
                                        (void *)s_local_bt_name);
            rt_kprintf("bt: stack ready, name=%s\n", s_local_bt_name);
            net_manager_notify_bt_stack_ready(true);
            net_manager_request_pan_connect_delay(0U);
            break;
        case BT_NOTIFY_COMMON_CLOSE_COMPLETE:
            net_manager_request_pan_clear();
            s_pending_bt_closed_valid = 1U;
            net_manager_notify_bt_acl(false);
            net_manager_notify_pan_ready(false);
            net_manager_signal_refresh();
            break;
        case BT_NOTIFY_COMMON_ENCRYPTION:
        {
            bt_notify_device_mac_t *mac = (bt_notify_device_mac_t *)data;

            if (mac != RT_NULL)
            {
                s_pending_bt_addr = *mac;
                s_pending_bt_addr_valid = 1U;
                net_manager_notify_bt_acl(true);
                net_manager_request_pan_connect_delay(NET_MANAGER_PAN_CONNECT_DELAY_MS);
            }
            break;
        }
        case BT_NOTIFY_COMMON_PAIR_IND:
        {
            bt_notify_device_base_info_t *info =
                (bt_notify_device_base_info_t *)data;

            if ((info != RT_NULL) && (info->res == BTS2_SUCC))
            {
                s_pending_bt_addr = info->mac;
                s_pending_bt_addr_valid = 1U;
                net_manager_notify_bt_acl(true);
                net_manager_request_pan_connect_delay(NET_MANAGER_PAN_CONNECT_DELAY_MS);
            }
            break;
        }
        case BT_NOTIFY_COMMON_ACL_DISCONNECTED:
            net_manager_request_pan_clear();
            net_manager_notify_bt_acl(false);
            net_manager_notify_pan_ready(false);
            break;
        default:
            break;
        }
    }
    else if (type == BT_NOTIFY_PAN)
    {
        switch (event_id)
        {
        case BT_NOTIFY_PAN_PROFILE_CONNECTED:
            net_manager_request_pan_clear();
            net_manager_notify_pan_ready(true);
            net_manager_schedule_register_xiaozhi_device("bt_pan");
            break;
        case BT_NOTIFY_PAN_PROFILE_DISCONNECTED:
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
                net_manager_request_pan_connect_delay(0U);
                net_manager_signal_refresh();
            }
            break;
        default:
            break;
        }
    }
}

static void net_manager_apply_bt_stack_ready(bool ready)
{
    rt_uint8_t previous_bt_stack_ready = s_bt_stack_ready;
    rt_uint8_t previous_bt_connected = s_bt_connected;
    rt_uint8_t previous_pan_ready = s_pan_ready;

    if (!ready &&
        previous_bt_stack_ready == 0U &&
        !net_manager_bt_runtime_closing())
    {
        return;
    }

    s_bt_stack_ready = ready ? 1U : 0U;

    if (ready && !s_bt_enabled)
    {
        s_bt_applied = 1U;
    }

    if (!ready)
    {
        g_bt_app_env.bt_connected = FALSE;
        g_pan_connected = FALSE;
        s_bt_close_pending = 0U;
        s_bt_close_request_tick = 0U;
        s_bt_applied = 0U;
        s_bt_connected = 0U;
        s_pan_ready = 0U;
        net_manager_clear_pan_connect_delay();
        net_manager_update_runtime_state();
    }

    net_manager_request_status_refresh_if_needed(s_desired_mode,
                                                 previous_bt_stack_ready,
                                                 s_bt_enabled,
                                                 previous_bt_connected,
                                                 previous_pan_ready,
                                                 s_4g_enabled,
                                                 s_cat1_ready,
                                                 s_radios_suspended);
    net_manager_signal_refresh();
}

static void net_manager_apply_bt_closed(void)
{
    rt_uint8_t previous_bt_stack_ready = s_bt_stack_ready;
    rt_uint8_t previous_bt_connected = s_bt_connected;
    rt_uint8_t previous_pan_ready = s_pan_ready;

    g_bt_app_env.bt_connected = FALSE;
    g_pan_connected = FALSE;
    s_bt_applied = 0U;
    s_bt_connected = 0U;
    s_pan_ready = 0U;
    s_bt_close_pending = 0U;
    s_bt_close_request_tick = 0U;
    net_manager_clear_pan_connect_delay();
    net_manager_update_runtime_state();

    net_manager_request_status_refresh_if_needed(s_desired_mode,
                                                 previous_bt_stack_ready,
                                                 s_bt_enabled,
                                                 previous_bt_connected,
                                                 previous_pan_ready,
                                                 s_4g_enabled,
                                                 s_cat1_ready,
                                                 s_radios_suspended);
    net_manager_signal_refresh();
}

static void net_manager_apply_bt_acl(bool connected)
{
    rt_uint8_t previous_bt_connected = s_bt_connected;
    rt_uint8_t previous_pan_ready = s_pan_ready;
    rt_uint8_t previous_bt_enabled = s_bt_enabled;
    rt_uint8_t previous_4g_enabled = s_4g_enabled;

    if (connected && !net_manager_bt_selected_locked())
    {
        return;
    }

    if (!connected &&
        previous_bt_connected == 0U &&
        !net_manager_bt_runtime_closing())
    {
        return;
    }

    s_bt_connected = connected ? 1U : 0U;
    g_bt_app_env.bt_connected = connected ? TRUE : FALSE;
    if (!connected)
    {
        g_pan_connected = FALSE;
        s_pan_ready = 0U;
        s_bt_last_loss_tick = rt_tick_get();
    }
    net_manager_update_runtime_state();
    net_manager_request_status_refresh_if_needed(s_desired_mode,
                                                 s_bt_stack_ready,
                                                 previous_bt_enabled,
                                                 previous_bt_connected,
                                                 previous_pan_ready,
                                                 previous_4g_enabled,
                                                 s_cat1_ready,
                                                 s_radios_suspended);
    net_manager_signal_refresh();
}

static void net_manager_apply_pan_ready(bool ready)
{
    rt_uint8_t previous_bt_connected = s_bt_connected;
    rt_uint8_t previous_pan_ready = s_pan_ready;
    rt_uint8_t previous_bt_enabled = s_bt_enabled;
    rt_uint8_t previous_4g_enabled = s_4g_enabled;

    if (ready && !net_manager_bt_selected_locked())
    {
        return;
    }

    if (!ready &&
        previous_pan_ready == 0U &&
        !net_manager_bt_runtime_closing())
    {
        return;
    }

    s_pan_ready = ready ? 1U : 0U;
    g_pan_connected = ready ? TRUE : FALSE;
    if (ready)
    {
        first_pan_connected = TRUE;
        s_bt_connected = 1U;
        g_bt_app_env.bt_connected = TRUE;
    }
    if (!ready)
    {
        s_bt_last_loss_tick = rt_tick_get();
    }
    net_manager_update_runtime_state();
    net_manager_request_status_refresh_if_needed(s_desired_mode,
                                                 s_bt_stack_ready,
                                                 previous_bt_enabled,
                                                 previous_bt_connected,
                                                 previous_pan_ready,
                                                 previous_4g_enabled,
                                                 s_cat1_ready,
                                                 s_radios_suspended);
    net_manager_signal_refresh();

    if (ready)
    {
        net_manager_schedule_register_xiaozhi_device("bt_pan");
    }
}

void net_manager_handle_bt_mailbox_event(rt_uint32_t bt_event)
{
    switch (bt_event)
    {
    case NET_MANAGER_BT_APP_READY:
        break;
    case NET_MANAGER_BT_APP_CONNECT_PAN:
        net_manager_request_pan_connect_delay(0U);
        net_manager_signal_refresh();
        break;
    case NET_MANAGER_BT_APP_CONNECT_PAN_SUCCESS:
        net_manager_schedule_register_xiaozhi_device("bt_pan");
        break;
    default:
        break;
    }
}

net_manager_service_state_t net_manager_get_service_state(void)
{
    return s_service_state;
}

bool net_manager_service_ready(void)
{
    return (s_service_state >= NET_MANAGER_SERVICE_LINK_READY);
}

bool net_manager_dns_ready(void)
{
    return (s_dns_ready != 0U);
}

bool net_manager_internet_ready(void)
{
    return (s_internet_ready != 0U);
}

bool net_manager_can_run_weather(void)
{
    return (s_internet_ready != 0U) && !s_radios_suspended;
}

bool net_manager_can_run_ai(void)
{
    return (s_internet_ready != 0U) && !s_radios_suspended;
}

static void net_manager_apply_cat1_ready(bool ready)
{
    rt_uint8_t previous_cat1_ready = s_cat1_ready;
    rt_uint8_t previous_bt_enabled = s_bt_enabled;
    rt_uint8_t previous_4g_enabled = s_4g_enabled;

    if (ready && !net_manager_4g_selected_locked())
    {
        return;
    }

    if (!ready &&
        previous_cat1_ready == 0U &&
        !net_manager_4g_runtime_closing())
    {
        return;
    }

    s_cat1_ready = ready ? 1U : 0U;
    if (ready)
    {
        s_cat1_poll_blocked_after_false = 0U;
        s_cat1_poll_true_count = 0U;
        s_cat1_false_notify_tick = 0U;
    }
    else
    {
        s_cat1_poll_blocked_after_false = 1U;
        s_cat1_poll_true_count = 0U;
        s_cat1_false_notify_tick = rt_tick_get();
        s_4g_close_pending = 0U;
        s_4g_close_request_tick = 0U;
    }
    net_manager_update_runtime_state();
    net_manager_request_status_refresh_if_needed(s_desired_mode,
                                                 s_bt_stack_ready,
                                                 previous_bt_enabled,
                                                 s_bt_connected,
                                                 s_pan_ready,
                                                 previous_4g_enabled,
                                                 previous_cat1_ready,
                                                 s_radios_suspended);
    net_manager_signal_refresh();

    if (ready)
    {
        net_manager_schedule_register_xiaozhi_device("cat1");
    }
}

static void net_manager_poll_cat1_ready_locked(void)
{
    rt_uint8_t polled_ready;

    if (!s_4g_enabled)
    {
        s_cat1_ready = 0U;
        s_cat1_poll_blocked_after_false = 0U;
        s_cat1_poll_true_count = 0U;
        s_cat1_false_notify_tick = 0U;
        return;
    }

    polled_ready = cat1_modem_is_ready() ? 1U : 0U;
    if (polled_ready == 0U)
    {
        s_cat1_ready = 0U;
        s_cat1_poll_blocked_after_false = 0U;
        s_cat1_poll_true_count = 0U;
        s_cat1_false_notify_tick = 0U;
        return;
    }

    if (s_cat1_poll_blocked_after_false == 0U)
    {
        s_cat1_ready = 1U;
        s_cat1_poll_true_count = 0U;
        s_cat1_false_notify_tick = 0U;
        return;
    }

    if (s_cat1_poll_true_count < 255U)
    {
        ++s_cat1_poll_true_count;
    }

    if (s_cat1_poll_true_count >= NET_MANAGER_CAT1_RECOVER_STABLE_POLLS ||
        (s_cat1_false_notify_tick != 0U &&
         (rt_tick_t)(rt_tick_get() - s_cat1_false_notify_tick) >=
             rt_tick_from_millisecond(NET_MANAGER_CAT1_RECOVER_GRACE_MS)))
    {
        s_cat1_ready = 1U;
        s_cat1_poll_blocked_after_false = 0U;
        s_cat1_poll_true_count = 0U;
        s_cat1_false_notify_tick = 0U;
    }
}

void net_manager_notify_bt_stack_ready(bool ready)
{
    s_pending_bt_stack_ready = ready ? 1U : 0U;
    s_pending_bt_stack_valid = 1U;
    net_manager_signal_refresh();
}

void net_manager_notify_bt_acl(bool connected)
{
    s_pending_bt_acl_connected = connected ? 1U : 0U;
    s_pending_bt_acl_valid = 1U;
    net_manager_signal_refresh();
}

void net_manager_notify_pan_ready(bool ready)
{
    s_pending_pan_ready = ready ? 1U : 0U;
    s_pending_pan_valid = 1U;
    net_manager_signal_refresh();
}

void net_manager_notify_cat1_ready(bool ready)
{
    s_pending_cat1_ready = ready ? 1U : 0U;
    s_pending_cat1_valid = 1U;
    net_manager_signal_refresh();
}

bool net_manager_bt_enabled(void)
{
    return s_bt_enabled != 0U;
}

bool net_manager_bt_connected(void)
{
    return s_bt_connected != 0U;
}

rt_bool_t dfu_pan_bluetooth_sniff_allowed(void)
{
    return (s_bt_enabled != 0U && s_bt_stack_ready != 0U) ? RT_TRUE : RT_FALSE;
}

bool net_manager_4g_enabled(void)
{
    return s_4g_enabled != 0U;
}

bool net_manager_network_ready(void)
{
    return (s_network_ready != 0U);
}

bool net_manager_is_4g_active(void)
{
    return s_active_link == NET_MANAGER_LINK_4G_CAT1;
}

net_manager_link_t net_manager_get_active_link(void)
{
    return s_active_link;
}

net_manager_mode_t net_manager_get_desired_mode(void)
{
    return s_desired_mode;
}

void net_manager_get_snapshot(net_manager_snapshot_t *snapshot)
{
    if (snapshot == RT_NULL)
    {
        return;
    }

    snapshot->desired_mode = s_desired_mode;
    snapshot->runtime_mode = net_manager_get_runtime_mode_locked();
    snapshot->active_link = s_active_link;
    snapshot->network_ready = (s_network_ready != 0U);
    snapshot->bt_stack_ready = (s_bt_stack_ready != 0U);
    snapshot->bt_enabled = (s_bt_enabled != 0U);
    snapshot->bt_connected = (s_bt_connected != 0U);
    snapshot->pan_ready = (s_pan_ready != 0U);
    snapshot->net_4g_enabled = (s_4g_enabled != 0U);
    snapshot->cat1_ready = (s_cat1_ready != 0U);
    snapshot->radios_suspended = (s_radios_suspended != 0U);
}
