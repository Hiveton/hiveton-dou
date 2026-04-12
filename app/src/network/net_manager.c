#include "network/net_manager.h"

#include <string.h>

#include "ui/ui_dispatch.h"
#include "ui/ui_helpers.h"
#include "xiaozhi/weather/weather.h"
#include "cat1_modem.h"

#define NET_MANAGER_THREAD_STACK_SIZE 3072
#define NET_MANAGER_THREAD_PRIORITY   17
#define NET_MANAGER_THREAD_TICK       10
#define NET_MANAGER_EVAL_INTERVAL_MS  1000U
#define NET_MANAGER_4G_FALLBACK_DELAY_MS 5000U

static struct rt_thread s_net_manager_thread;
static rt_uint8_t s_net_manager_stack[NET_MANAGER_THREAD_STACK_SIZE];
static volatile rt_uint8_t s_bt_connected = 0U;
static volatile rt_uint8_t s_pan_ready = 0U;
static volatile rt_uint8_t s_cat1_ready = 0U;
static volatile rt_uint8_t s_initialized = 0U;
static rt_tick_t s_bt_last_loss_tick = 0U;
static net_manager_link_t s_active_link = NET_MANAGER_LINK_NONE;

static void net_manager_refresh_side_effects(net_manager_link_t link)
{
    ui_dispatch_request_status_refresh();
    ui_dispatch_request_time_refresh();

    if (link != NET_MANAGER_LINK_NONE)
    {
        xiaozhi_weather_request_refresh();
    }
}

static net_manager_link_t net_manager_resolve_active_link(void)
{
    if (s_pan_ready)
    {
        return NET_MANAGER_LINK_BT_PAN;
    }

    if (s_cat1_ready)
    {
        return NET_MANAGER_LINK_4G_CAT1;
    }

    return NET_MANAGER_LINK_NONE;
}

static void net_manager_update_link_state(void)
{
    net_manager_link_t link = net_manager_resolve_active_link();

    if (link != s_active_link)
    {
        s_active_link = link;
        rt_kprintf("net: active link -> %d\n", (int)link);
        net_manager_refresh_side_effects(link);
    }
    else
    {
        ui_dispatch_request_status_refresh();
    }
}

static void net_manager_thread_entry(void *parameter)
{
    rt_tick_t fallback_ticks;

    (void)parameter;

    fallback_ticks = rt_tick_from_millisecond(NET_MANAGER_4G_FALLBACK_DELAY_MS);
    s_bt_last_loss_tick = rt_tick_get();

    while (1)
    {
        rt_tick_t now = rt_tick_get();
        s_cat1_ready = cat1_modem_is_ready() ? 1U : 0U;

        if (s_pan_ready)
        {
            (void)cat1_modem_request_offline();
        }
        else if ((now - s_bt_last_loss_tick) >= fallback_ticks)
        {
            (void)cat1_modem_request_online();
        }

        net_manager_update_link_state();
        rt_thread_mdelay(NET_MANAGER_EVAL_INTERVAL_MS);
    }
}

rt_err_t net_manager_init(void)
{
    rt_err_t result;

    if (s_initialized)
    {
        return RT_EOK;
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
    s_initialized = 1U;
    return RT_EOK;
}

void net_manager_notify_bt_acl(bool connected)
{
    s_bt_connected = connected ? 1U : 0U;
    if (!connected)
    {
        s_bt_last_loss_tick = rt_tick_get();
    }
    ui_dispatch_request_status_refresh();
}

void net_manager_notify_pan_ready(bool ready)
{
    s_pan_ready = ready ? 1U : 0U;
    if (!ready)
    {
        s_bt_last_loss_tick = rt_tick_get();
    }
    net_manager_update_link_state();
}

void net_manager_notify_cat1_ready(bool ready)
{
    s_cat1_ready = ready ? 1U : 0U;
    net_manager_update_link_state();
}

bool net_manager_bt_connected(void)
{
    return s_bt_connected != 0U;
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
