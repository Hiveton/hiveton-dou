#include "network/app_network.h"
#include "network/app_network_dns.h"

#include "network/net_http_lock.h"
#include "network/net_manager.h"
#include "cat1_modem.h"
#include <lwip/dns.h>
#include <lwip/sys.h>
#include <stdint.h>
#include <string.h>

#define APP_NETWORK_INTERNET_CHECK_OK_MS   30000U
#define APP_NETWORK_INTERNET_CHECK_FAIL_MS 5000U
#define APP_NETWORK_DNS_WAIT_MS            3500U
#define APP_NETWORK_DNS_STEP_MS            100U

typedef struct
{
    app_network_client_t client;
    char hostname[96];
    int result;
    int log_state;
    rt_tick_t tick;
} app_network_internet_cache_t;

typedef struct
{
    volatile int done;
    bool active;
    uint32_t generation;
    char hostname[96];
    ip_addr_t addr;
} app_network_dns_lookup_t;

static struct rt_mutex s_app_network_check_mutex;
static bool s_app_network_check_mutex_ready = false;
static app_network_dns_lookup_t s_app_network_dns_lookup;
static app_network_client_t s_app_network_internet_client = APP_NETWORK_CLIENT_GENERIC;
static char s_app_network_internet_hostname[96];
static int s_app_network_internet_result = -1;
static int s_app_network_internet_log_state = -1;
static rt_tick_t s_app_network_internet_tick = 0;

static rt_err_t app_network_check_ensure_mutex(void)
{
    rt_err_t result = RT_EOK;

    if (s_app_network_check_mutex_ready)
    {
        return RT_EOK;
    }

    rt_enter_critical();
    if (!s_app_network_check_mutex_ready)
    {
        result = rt_mutex_init(&s_app_network_check_mutex, "app_net", RT_IPC_FLAG_PRIO);
        if (result == RT_EOK)
        {
            s_app_network_check_mutex_ready = true;
        }
    }
    rt_exit_critical();

    return result;
}

static bool app_network_check_lock(void)
{
    if (app_network_check_ensure_mutex() != RT_EOK)
    {
        return false;
    }

    return rt_mutex_take(&s_app_network_check_mutex, RT_WAITING_FOREVER) == RT_EOK;
}

static void app_network_check_unlock(void)
{
    if (s_app_network_check_mutex_ready)
    {
        (void)rt_mutex_release(&s_app_network_check_mutex);
    }
}

static void app_network_dns_set_done(int done)
{
    s_app_network_dns_lookup.done = done;
}

static int app_network_dns_get_done(void)
{
    return s_app_network_dns_lookup.done;
}

static uint32_t app_network_dns_reset(const char *hostname)
{
    s_app_network_dns_lookup.done = 0;
    s_app_network_dns_lookup.active = true;
    s_app_network_dns_lookup.generation++;
    if (s_app_network_dns_lookup.generation == 0U)
    {
        s_app_network_dns_lookup.generation = 1U;
    }
    rt_snprintf(s_app_network_dns_lookup.hostname,
                sizeof(s_app_network_dns_lookup.hostname),
                "%s",
                hostname != RT_NULL ? hostname : "");
    memset(&s_app_network_dns_lookup.addr, 0, sizeof(s_app_network_dns_lookup.addr));

    return s_app_network_dns_lookup.generation;
}

static void app_network_dns_finish(void)
{
    s_app_network_dns_lookup.active = false;
}

static app_network_internet_cache_t app_network_internet_cache_snapshot(void)
{
    app_network_internet_cache_t cache;

    rt_enter_critical();
    cache.client = s_app_network_internet_client;
    rt_snprintf(cache.hostname, sizeof(cache.hostname), "%s", s_app_network_internet_hostname);
    cache.result = s_app_network_internet_result;
    cache.log_state = s_app_network_internet_log_state;
    cache.tick = s_app_network_internet_tick;
    rt_exit_critical();

    return cache;
}

static void app_network_internet_cache_store(app_network_client_t client,
                                             const char *hostname,
                                             int result,
                                             rt_tick_t tick)
{
    rt_enter_critical();
    s_app_network_internet_client = client;
    rt_snprintf(s_app_network_internet_hostname,
                sizeof(s_app_network_internet_hostname),
                "%s",
                hostname != RT_NULL ? hostname : "");
    s_app_network_internet_result = result;
    s_app_network_internet_tick = tick;
    rt_exit_critical();
}

static void app_network_internet_cache_store_success(app_network_client_t client,
                                                    const char *hostname,
                                                    rt_tick_t tick)
{
    rt_enter_critical();
    s_app_network_internet_client = client;
    rt_snprintf(s_app_network_internet_hostname,
                sizeof(s_app_network_internet_hostname),
                "%s",
                hostname != RT_NULL ? hostname : "");
    s_app_network_internet_result = 1;
    s_app_network_internet_tick = tick;
    s_app_network_internet_log_state = -1;
    rt_exit_critical();
}

static bool app_network_internet_log_state_matches(int reason)
{
    bool matches;

    rt_enter_critical();
    matches = (s_app_network_internet_log_state == reason);
    rt_exit_critical();

    return matches;
}

static void app_network_internet_log_state_store(int reason)
{
    rt_enter_critical();
    s_app_network_internet_log_state = reason;
    rt_exit_critical();
}

static app_network_mode_t app_network_map_mode(net_manager_mode_t mode)
{
    switch (mode)
    {
    case NET_MANAGER_MODE_BT:
        return APP_NETWORK_MODE_BT;
    case NET_MANAGER_MODE_4G:
        return APP_NETWORK_MODE_4G;
    case NET_MANAGER_MODE_NONE:
    case NET_MANAGER_MODE_SLEEP:
    default:
        return APP_NETWORK_MODE_NONE;
    }
}

static app_network_link_t app_network_map_link(net_manager_link_t link)
{
    switch (link)
    {
    case NET_MANAGER_LINK_BT_PAN:
        return APP_NETWORK_LINK_BT_PAN;
    case NET_MANAGER_LINK_4G_CAT1:
        return APP_NETWORK_LINK_4G_CAT1;
    case NET_MANAGER_LINK_NONE:
    default:
        return APP_NETWORK_LINK_NONE;
    }
}

static app_network_state_t app_network_map_state(net_manager_service_state_t state)
{
    switch (state)
    {
    case NET_MANAGER_SERVICE_RADIO_READY:
        return APP_NETWORK_STATE_RADIO_READY;
    case NET_MANAGER_SERVICE_LINK_READY:
        return APP_NETWORK_STATE_LINK_READY;
    case NET_MANAGER_SERVICE_DNS_READY:
        return APP_NETWORK_STATE_DNS_READY;
    case NET_MANAGER_SERVICE_INTERNET_READY:
        return APP_NETWORK_STATE_INTERNET_READY;
    case NET_MANAGER_SERVICE_OFFLINE:
    default:
        return APP_NETWORK_STATE_OFFLINE;
    }
}

static bool app_network_http_client_from_app(app_network_client_t client,
                                             net_http_client_t *out_client)
{
    if (out_client == RT_NULL)
    {
        return false;
    }

    switch (client)
    {
    case APP_NETWORK_CLIENT_AI:
    case APP_NETWORK_CLIENT_OTA:
        *out_client = NET_HTTP_CLIENT_XIAOZHI;
        return true;
    case APP_NETWORK_CLIENT_GENERIC:
    case APP_NETWORK_CLIENT_WEATHER:
        *out_client = NET_HTTP_CLIENT_GENERIC;
        return true;
    default:
        return false;
    }
}

static bool app_network_internet_available(void)
{
    net_manager_snapshot_t net_snapshot;

    net_manager_get_snapshot(&net_snapshot);
    return (!net_snapshot.radios_suspended && net_manager_internet_ready());
}

static void app_network_dns_found_callback(const char *name, const ip_addr_t *ipaddr,
                                           void *callback_arg)
{
    uint32_t generation = (uint32_t)(uintptr_t)callback_arg;

    if (!s_app_network_dns_lookup.active ||
        generation != s_app_network_dns_lookup.generation ||
        name == RT_NULL ||
        strcmp(name, s_app_network_dns_lookup.hostname) != 0)
    {
        return;
    }

    if (ipaddr != RT_NULL)
    {
        s_app_network_dns_lookup.addr = *ipaddr;
        app_network_dns_set_done(1);
        rt_kprintf("app_network DNS lookup succeeded, IP: %s\n", ipaddr_ntoa(ipaddr));
    }
    else
    {
        app_network_dns_set_done(-1);
    }
}

static bool app_network_wait_dns_lookup_done(void)
{
    uint32_t waited_ms = 0;

    while (waited_ms < APP_NETWORK_DNS_WAIT_MS)
    {
        int done = app_network_dns_get_done();

        if (done == 1)
        {
            return true;
        }

        if (done < 0)
        {
            return false;
        }

        rt_thread_mdelay(APP_NETWORK_DNS_STEP_MS);
        waited_ms += APP_NETWORK_DNS_STEP_MS;
    }

    return app_network_dns_get_done() == 1;
}

static bool app_network_internet_cache_hit(app_network_client_t client,
                                           const char *hostname,
                                           int expected_result,
                                           rt_tick_t now_tick)
{
    app_network_internet_cache_t cache = app_network_internet_cache_snapshot();
    rt_tick_t ttl;

    if (cache.client != client ||
        strcmp(cache.hostname, hostname) != 0 ||
        cache.result != expected_result ||
        cache.tick == 0U)
    {
        return false;
    }

    ttl = rt_tick_from_millisecond((expected_result == 1) ?
                                   APP_NETWORK_INTERNET_CHECK_OK_MS :
                                   APP_NETWORK_INTERNET_CHECK_FAIL_MS);
    return (rt_tick_t)(now_tick - cache.tick) < ttl;
}

static void app_network_report_internet_failure(int reason, const char *hostname)
{
    if (app_network_internet_log_state_matches(reason))
    {
        return;
    }

    if (reason == 0)
    {
        rt_kprintf("app_network link inactive, skip DNS check for %s\n",
                   hostname);
    }
    else
    {
        rt_kprintf("app_network could not resolve %s\n", hostname);
    }

    app_network_internet_log_state_store(reason);
}

void app_network_get_snapshot(app_network_snapshot_t *snapshot)
{
    net_manager_snapshot_t net_snapshot;

    if (snapshot == RT_NULL)
    {
        return;
    }

    net_manager_get_snapshot(&net_snapshot);

    snapshot->desired_mode = app_network_map_mode(net_snapshot.desired_mode);
    snapshot->runtime_mode = app_network_map_mode(net_snapshot.runtime_mode);
    snapshot->active_link = app_network_map_link(net_snapshot.active_link);
    snapshot->state = app_network_map_state(net_manager_get_service_state());
    snapshot->radios_suspended = net_snapshot.radios_suspended;
    snapshot->can_use_network = app_network_internet_available();
}

bool app_network_can_run(app_network_client_t client)
{
    switch (client)
    {
    case APP_NETWORK_CLIENT_AI:
        return net_manager_can_run_ai();
    case APP_NETWORK_CLIENT_WEATHER:
        return net_manager_can_run_weather();
    case APP_NETWORK_CLIENT_GENERIC:
    case APP_NETWORK_CLIENT_OTA:
        return app_network_internet_available();
    default:
        return false;
    }
}

bool app_network_check_internet(app_network_client_t client, const char *hostname)
{
    ip_addr_t addr = {0};
    rt_tick_t now_tick = rt_tick_get();
    err_t err;
    bool result = false;
    uint32_t generation;

    if (hostname == RT_NULL || hostname[0] == '\0')
    {
        return app_network_can_run(client);
    }

    if (!app_network_can_run(client))
    {
        app_network_report_internet_failure(0, hostname);
        app_network_internet_cache_store(client, hostname, 0, now_tick);
        return false;
    }

    if (app_network_internet_cache_hit(client, hostname, 1, now_tick))
    {
        return true;
    }

    if (app_network_internet_cache_hit(client, hostname, 0, now_tick))
    {
        return false;
    }

    if (!app_network_check_lock())
    {
        return false;
    }

    generation = app_network_dns_reset(hostname);
    err = dns_gethostbyname(hostname,
                            &addr,
                            app_network_dns_found_callback,
                            (void *)(uintptr_t)generation);
    if (err == ERR_OK)
    {
        app_network_internet_cache_store_success(client, hostname, now_tick);
        rt_kprintf("app_network DNS lookup cached, IP: %s\n", ipaddr_ntoa(&addr));
        result = true;
        goto __exit;
    }

    if (err == ERR_INPROGRESS && app_network_wait_dns_lookup_done())
    {
        app_network_internet_cache_store_success(client, hostname, now_tick);
        result = true;
        goto __exit;
    }

    app_network_report_internet_failure(1, hostname);
    app_network_internet_cache_store(client, hostname, 0, now_tick);

__exit:
    app_network_dns_finish();
    app_network_check_unlock();
    return result;
}

err_t app_network_dns_gethostbyname(const char *hostname, ip_addr_t *addr,
                                    dns_found_callback found, void *callback_arg)
{
    return dns_gethostbyname(hostname, addr, found, callback_arg);
}

rt_err_t app_network_request_mode(app_network_mode_t mode)
{
    switch (mode)
    {
    case APP_NETWORK_MODE_BT:
        net_manager_request_bt_mode();
        return RT_EOK;
    case APP_NETWORK_MODE_4G:
        net_manager_request_4g_mode();
        return RT_EOK;
    case APP_NETWORK_MODE_NONE:
        net_manager_request_none_mode();
        return RT_EOK;
    default:
        return -RT_EINVAL;
    }
}

rt_err_t app_network_http_begin(app_network_client_t client, rt_int32_t timeout_ms)
{
    net_http_client_t http_client;

    if (!app_network_http_client_from_app(client, &http_client))
    {
        return -RT_EINVAL;
    }

    return net_http_lock_take(http_client, timeout_ms);
}

void app_network_http_end(app_network_client_t client)
{
    net_http_client_t http_client;

    if (!app_network_http_client_from_app(client, &http_client))
    {
        return;
    }

    net_http_lock_release(http_client);
}

const char *app_network_state_text(app_network_state_t state)
{
    switch (state)
    {
    case APP_NETWORK_STATE_INTERNET_READY:
        return "网络已连接";
    case APP_NETWORK_STATE_DNS_READY:
        return "DNS已就绪";
    case APP_NETWORK_STATE_LINK_READY:
        return "链路已连接";
    case APP_NETWORK_STATE_RADIO_READY:
        return "网络准备中";
    case APP_NETWORK_STATE_OFFLINE:
    default:
        return "网络未连接";
    }
}

const char *app_network_link_text(app_network_link_t link)
{
    switch (link)
    {
    case APP_NETWORK_LINK_BT_PAN:
        return "蓝牙";
    case APP_NETWORK_LINK_4G_CAT1:
        return "4G";
    case APP_NETWORK_LINK_NONE:
    default:
        return "无网络";
    }
}

bool app_network_get_network_time(time_t *utc_time)
{
    if (utc_time == RT_NULL)
    {
        return false;
    }

    if (net_manager_get_active_link() != NET_MANAGER_LINK_4G_CAT1)
    {
        return false;
    }

    return cat1_modem_get_network_time(utc_time);
}
