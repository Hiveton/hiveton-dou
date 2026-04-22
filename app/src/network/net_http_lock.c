#include "network/net_http_lock.h"

static struct rt_mutex s_net_http_mutex;
static volatile rt_uint8_t s_net_http_mutex_ready = 0U;
static volatile rt_uint8_t s_xiaozhi_active = 0U;
static volatile net_http_client_t s_owner_client = NET_HTTP_CLIENT_GENERIC;

static rt_err_t net_http_lock_ensure_ready(void)
{
    rt_err_t result = RT_EOK;

    if (s_net_http_mutex_ready != 0U)
    {
        return RT_EOK;
    }

    rt_enter_critical();
    if (s_net_http_mutex_ready == 0U)
    {
        result = rt_mutex_init(&s_net_http_mutex, "httpx", RT_IPC_FLAG_PRIO);
        if (result == RT_EOK)
        {
            s_net_http_mutex_ready = 1U;
        }
        else
        {
            rt_kprintf("net_http: mutex init failed (%d)\n", result);
        }
    }
    rt_exit_critical();

    return result;
}

static rt_tick_t net_http_timeout_to_ticks(rt_int32_t timeout_ms)
{
    if (timeout_ms < 0)
    {
        return RT_WAITING_FOREVER;
    }

    return rt_tick_from_millisecond((rt_uint32_t)timeout_ms);
}

static const char *net_http_client_name(net_http_client_t client)
{
    switch (client)
    {
    case NET_HTTP_CLIENT_XIAOZHI:
        return "xiaozhi";
    case NET_HTTP_CLIENT_GENERIC:
    default:
        return "generic";
    }
}

static void net_http_set_xiaozhi_active_internal(bool active)
{
    s_xiaozhi_active = active ? 1U : 0U;
}

rt_err_t net_http_lock_take(net_http_client_t client, rt_int32_t timeout_ms)
{
    rt_err_t result;

    result = net_http_lock_ensure_ready();
    if (result != RT_EOK)
    {
        return result;
    }

    if (client == NET_HTTP_CLIENT_GENERIC && net_http_should_defer_generic())
    {
        rt_kprintf("net_http: generic deferred while xiaozhi active\n");
        return -RT_EBUSY;
    }

    if (client == NET_HTTP_CLIENT_XIAOZHI)
    {
        net_http_set_xiaozhi_active_internal(true);
    }

    result = rt_mutex_take(&s_net_http_mutex, net_http_timeout_to_ticks(timeout_ms));
    if (result != RT_EOK)
    {
        if (client == NET_HTTP_CLIENT_XIAOZHI)
        {
            net_http_set_xiaozhi_active_internal(false);
        }

        rt_kprintf("net_http: %s lock take failed (%d, timeout=%ldms)\n",
                   net_http_client_name(client),
                   result,
                   (long)timeout_ms);
        return result;
    }

    s_owner_client = client;
    return RT_EOK;
}

void net_http_lock_release(net_http_client_t client)
{
    rt_err_t result;

    if (s_net_http_mutex_ready == 0U)
    {
        if (client == NET_HTTP_CLIENT_XIAOZHI)
        {
            net_http_set_xiaozhi_active_internal(false);
        }
        return;
    }

    result = rt_mutex_release(&s_net_http_mutex);
    if (result != RT_EOK)
    {
        rt_kprintf("net_http: %s lock release failed (%d, owner=%s)\n",
                   net_http_client_name(client),
                   result,
                   net_http_client_name(s_owner_client));
    }

    if (client == NET_HTTP_CLIENT_XIAOZHI)
    {
        net_http_set_xiaozhi_active_internal(false);
    }

    s_owner_client = NET_HTTP_CLIENT_GENERIC;
}

void net_http_set_xiaozhi_active(bool active)
{
    net_http_set_xiaozhi_active_internal(active);
}

bool net_http_xiaozhi_active(void)
{
    return (s_xiaozhi_active != 0U);
}

bool net_http_should_defer_generic(void)
{
    return net_http_xiaozhi_active();
}
