#include "network/net_manager.h"

#include <dfs_posix.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>

#include "ui/ui_dispatch.h"
#include "ui/ui_helpers.h"
#include "xiaozhi/weather/weather.h"
#include "cat1_modem.h"
#include "bts2_app_inc.h"
#include "bts2_app_interface.h"

#define NET_MANAGER_THREAD_STACK_SIZE 3072
#define NET_MANAGER_THREAD_PRIORITY   17
#define NET_MANAGER_THREAD_TICK       10

#define NET_MANAGER_EVENT_REFRESH     (1U << 0)
#define NET_MANAGER_CONFIG_DIR_NAME   "config"
#define NET_MANAGER_CONFIG_FILE_NAME  "network_mode.cfg"

static struct rt_thread s_net_manager_thread;
static rt_uint8_t s_net_manager_stack[NET_MANAGER_THREAD_STACK_SIZE];
static rt_event_t s_net_manager_event = RT_NULL;
static volatile rt_uint8_t s_bt_connected = 0U;
static volatile rt_uint8_t s_pan_ready = 0U;
static volatile rt_uint8_t s_cat1_ready = 0U;
static volatile rt_uint8_t s_initialized = 0U;
static volatile rt_uint8_t s_bt_stack_ready = 0U;
static volatile rt_uint8_t s_bt_enabled = 0U;
static volatile rt_uint8_t s_4g_enabled = 1U;
static rt_uint8_t s_bt_applied = 0U;
static rt_uint8_t s_4g_applied = 1U;
static rt_tick_t s_bt_last_loss_tick = 0U;
static net_manager_link_t s_active_link = NET_MANAGER_LINK_NONE;
static net_manager_service_state_t s_service_state = NET_MANAGER_SERVICE_OFFLINE;
static net_manager_mode_t s_desired_mode = NET_MANAGER_MODE_4G;
static net_manager_mode_t s_saved_mode_before_sleep = NET_MANAGER_MODE_4G;
static volatile rt_uint8_t s_radios_suspended = 0U;
static rt_uint8_t s_network_ready = 0U;
static rt_uint8_t s_dns_ready = 0U;
static rt_uint8_t s_internet_ready = 0U;

static void net_manager_apply_desired_mode_locked(void);

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
        break;
    case NET_MANAGER_MODE_NONE:
    case NET_MANAGER_MODE_SLEEP:
    default:
        s_bt_enabled = 0U;
        s_4g_enabled = 0U;
        break;
    }
}

static void net_manager_refresh_side_effects(net_manager_link_t previous_link,
                                             net_manager_link_t link)
{
    ui_dispatch_request_status_refresh();
    ui_dispatch_request_time_refresh();

    if ((link != NET_MANAGER_LINK_NONE) &&
        (link != previous_link))
    {
        xiaozhi_weather_request_force_refresh();
    }
}

static net_manager_service_state_t net_manager_resolve_service_state(net_manager_link_t link)
{
    if (s_radios_suspended)
    {
        return NET_MANAGER_SERVICE_OFFLINE;
    }

    if (link != NET_MANAGER_LINK_NONE)
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
    if (s_bt_enabled && s_pan_ready)
    {
        return NET_MANAGER_LINK_BT_PAN;
    }

    if (s_4g_enabled && s_cat1_ready)
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
    rt_uint8_t network_ready = (service_state >= NET_MANAGER_SERVICE_LINK_READY) ? 1U : 0U;
    rt_uint8_t dns_ready = (service_state >= NET_MANAGER_SERVICE_DNS_READY) ? 1U : 0U;
    rt_uint8_t internet_ready = (service_state >= NET_MANAGER_SERVICE_INTERNET_READY) ? 1U : 0U;

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
        net_manager_refresh_side_effects(previous_link, link);
    }
}

static void net_manager_apply_mode(void)
{
    net_manager_apply_desired_mode_locked();

    if (s_bt_applied != s_bt_enabled)
    {
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
                bt_interface_open_bt();
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
            (void)cat1_modem_request_online();
        }
        else
        {
            (void)cat1_modem_request_offline();
            s_cat1_ready = 0U;
        }
        s_4g_applied = s_4g_enabled;
    }
}

static void net_manager_thread_entry(void *parameter)
{
    rt_uint32_t events = 0U;

    (void)parameter;
    s_bt_last_loss_tick = rt_tick_get();

    while (1)
    {
        if (s_net_manager_event != RT_NULL)
        {
            rt_event_recv(s_net_manager_event,
                          NET_MANAGER_EVENT_REFRESH,
                          RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                          RT_WAITING_FOREVER,
                          &events);
        }

        net_manager_apply_mode();
        s_cat1_ready = cat1_modem_is_ready() ? 1U : 0U;
        net_manager_update_runtime_state();
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

    if (s_net_manager_event == RT_NULL)
    {
        s_net_manager_event = rt_event_create("netmgr", RT_IPC_FLAG_FIFO);
        if (s_net_manager_event == RT_NULL)
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

    rt_thread_startup(&s_net_manager_thread);
    net_manager_signal_refresh();
    s_initialized = 1U;
    return RT_EOK;
}

void net_manager_request_bt_mode(void)
{
    net_manager_mode_t previous_desired_mode = s_desired_mode;
    rt_uint8_t previous_bt_enabled = s_bt_enabled;
    rt_uint8_t previous_4g_enabled = s_4g_enabled;

    s_desired_mode = NET_MANAGER_MODE_BT;
    net_manager_save_mode_config(s_desired_mode);
    if (!s_radios_suspended)
    {
        net_manager_apply_desired_mode_locked();
    }
    s_bt_last_loss_tick = rt_tick_get();
    net_manager_request_status_refresh_if_needed(previous_desired_mode,
                                                 s_bt_stack_ready,
                                                 previous_bt_enabled,
                                                 s_bt_connected,
                                                 s_pan_ready,
                                                 previous_4g_enabled,
                                                 s_cat1_ready,
                                                 s_radios_suspended);
    net_manager_signal_refresh();
}

void net_manager_request_4g_mode(void)
{
    net_manager_mode_t previous_desired_mode = s_desired_mode;
    rt_uint8_t previous_bt_enabled = s_bt_enabled;
    rt_uint8_t previous_4g_enabled = s_4g_enabled;

    s_desired_mode = NET_MANAGER_MODE_4G;
    net_manager_save_mode_config(s_desired_mode);
    if (!s_radios_suspended)
    {
        net_manager_apply_desired_mode_locked();
    }
    s_bt_last_loss_tick = rt_tick_get();
    net_manager_request_status_refresh_if_needed(previous_desired_mode,
                                                 s_bt_stack_ready,
                                                 previous_bt_enabled,
                                                 s_bt_connected,
                                                 s_pan_ready,
                                                 previous_4g_enabled,
                                                 s_cat1_ready,
                                                 s_radios_suspended);
    net_manager_signal_refresh();
}

void net_manager_request_none_mode(void)
{
    net_manager_mode_t previous_desired_mode = s_desired_mode;
    rt_uint8_t previous_bt_enabled = s_bt_enabled;
    rt_uint8_t previous_4g_enabled = s_4g_enabled;

    s_desired_mode = NET_MANAGER_MODE_NONE;
    if (!s_radios_suspended)
    {
        net_manager_apply_desired_mode_locked();
    }
    s_bt_last_loss_tick = rt_tick_get();
    net_manager_request_status_refresh_if_needed(previous_desired_mode,
                                                 s_bt_stack_ready,
                                                 previous_bt_enabled,
                                                 s_bt_connected,
                                                 s_pan_ready,
                                                 previous_4g_enabled,
                                                 s_cat1_ready,
                                                 s_radios_suspended);
    net_manager_signal_refresh();
}

void net_manager_suspend_for_sleep(void)
{
    net_manager_mode_t previous_desired_mode = s_desired_mode;
    rt_uint8_t previous_bt_enabled = s_bt_enabled;
    rt_uint8_t previous_4g_enabled = s_4g_enabled;
    rt_uint8_t previous_cat1_ready = s_cat1_ready;
    rt_uint8_t previous_radios_suspended = s_radios_suspended;

    if (!s_radios_suspended)
    {
        s_saved_mode_before_sleep = s_desired_mode;
    }

    s_radios_suspended = 1U;
    s_desired_mode = NET_MANAGER_MODE_SLEEP;
    net_manager_apply_desired_mode_locked();
    s_cat1_ready = 0U;
    net_manager_update_runtime_state();
    net_manager_request_status_refresh_if_needed(previous_desired_mode,
                                                 s_bt_stack_ready,
                                                 previous_bt_enabled,
                                                 s_bt_connected,
                                                 s_pan_ready,
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

void net_manager_notify_bt_stack_ready(bool ready)
{
    rt_uint8_t previous_bt_stack_ready = s_bt_stack_ready;
    rt_uint8_t previous_bt_connected = s_bt_connected;
    rt_uint8_t previous_pan_ready = s_pan_ready;

    s_bt_stack_ready = ready ? 1U : 0U;

    if (!ready)
    {
        s_bt_applied = 0U;
        s_bt_connected = 0U;
        s_pan_ready = 0U;
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

void net_manager_notify_bt_acl(bool connected)
{
    rt_uint8_t previous_bt_connected = s_bt_connected;
    rt_uint8_t previous_bt_enabled = s_bt_enabled;
    rt_uint8_t previous_4g_enabled = s_4g_enabled;

    s_bt_connected = connected ? 1U : 0U;
    if (connected && s_desired_mode != NET_MANAGER_MODE_BT)
    {
        s_bt_applied = 1U;
    }
    if (!connected)
    {
        s_bt_last_loss_tick = rt_tick_get();
    }
    net_manager_update_runtime_state();
    net_manager_request_status_refresh_if_needed(s_desired_mode,
                                                 s_bt_stack_ready,
                                                 previous_bt_enabled,
                                                 previous_bt_connected,
                                                 s_pan_ready,
                                                 previous_4g_enabled,
                                                 s_cat1_ready,
                                                 s_radios_suspended);
    net_manager_signal_refresh();
}

void net_manager_notify_pan_ready(bool ready)
{
    rt_uint8_t previous_bt_connected = s_bt_connected;
    rt_uint8_t previous_pan_ready = s_pan_ready;
    rt_uint8_t previous_bt_enabled = s_bt_enabled;
    rt_uint8_t previous_4g_enabled = s_4g_enabled;

    s_pan_ready = ready ? 1U : 0U;
    if (ready)
    {
        s_bt_connected = 1U;
        if (s_desired_mode != NET_MANAGER_MODE_BT)
        {
            s_bt_applied = 1U;
        }
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
    return (s_active_link != NET_MANAGER_LINK_NONE) && !s_radios_suspended;
}

bool net_manager_can_run_ai(void)
{
    return (s_active_link != NET_MANAGER_LINK_NONE);
}

void net_manager_notify_cat1_ready(bool ready)
{
    rt_uint8_t previous_cat1_ready = s_cat1_ready;
    rt_uint8_t previous_bt_enabled = s_bt_enabled;
    rt_uint8_t previous_4g_enabled = s_4g_enabled;

    s_cat1_ready = ready ? 1U : 0U;
    if (ready && s_desired_mode != NET_MANAGER_MODE_4G)
    {
        s_4g_applied = 1U;
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
}

bool net_manager_bt_enabled(void)
{
    return s_bt_enabled != 0U;
}

bool net_manager_bt_connected(void)
{
    return s_bt_connected != 0U;
}

bool net_manager_4g_enabled(void)
{
    return s_4g_enabled != 0U;
}

bool net_manager_network_ready(void)
{
    return s_active_link != NET_MANAGER_LINK_NONE;
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
    snapshot->network_ready = (s_active_link != NET_MANAGER_LINK_NONE);
    snapshot->bt_stack_ready = (s_bt_stack_ready != 0U);
    snapshot->bt_enabled = (s_bt_enabled != 0U);
    snapshot->bt_connected = (s_bt_connected != 0U);
    snapshot->pan_ready = (s_pan_ready != 0U);
    snapshot->net_4g_enabled = (s_4g_enabled != 0U);
    snapshot->cat1_ready = (s_cat1_ready != 0U);
    snapshot->radios_suspended = (s_radios_suspended != 0U);
}
