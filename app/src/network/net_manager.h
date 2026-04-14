#ifndef APP_NETWORK_NET_MANAGER_H
#define APP_NETWORK_NET_MANAGER_H

#include <stdbool.h>
#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    NET_MANAGER_LINK_NONE = 0,
    NET_MANAGER_LINK_BT_PAN,
    NET_MANAGER_LINK_4G_CAT1,
} net_manager_link_t;

typedef enum
{
    NET_MANAGER_SERVICE_OFFLINE = 0,
    NET_MANAGER_SERVICE_RADIO_READY,
    NET_MANAGER_SERVICE_LINK_READY,
    NET_MANAGER_SERVICE_DNS_READY,
    NET_MANAGER_SERVICE_INTERNET_READY,
} net_manager_service_state_t;

typedef enum
{
    NET_MANAGER_MODE_NONE = 0,
    NET_MANAGER_MODE_BT,
    NET_MANAGER_MODE_4G,
    NET_MANAGER_MODE_SLEEP,
} net_manager_mode_t;

typedef struct
{
    net_manager_mode_t desired_mode;
    net_manager_mode_t runtime_mode;
    net_manager_link_t active_link;
    bool network_ready;
    bool bt_stack_ready;
    bool bt_enabled;
    bool bt_connected;
    bool pan_ready;
    bool net_4g_enabled;
    bool cat1_ready;
    bool radios_suspended;
} net_manager_snapshot_t;

rt_err_t net_manager_init(void);
void net_manager_request_bt_mode(void);
void net_manager_request_4g_mode(void);
void net_manager_request_none_mode(void);
void net_manager_suspend_for_sleep(void);
void net_manager_resume_after_wake(void);
void net_manager_notify_bt_stack_ready(bool ready);
void net_manager_notify_bt_acl(bool connected);
void net_manager_notify_pan_ready(bool ready);
void net_manager_notify_cat1_ready(bool ready);

bool net_manager_bt_enabled(void);
bool net_manager_bt_connected(void);
bool net_manager_4g_enabled(void);
net_manager_service_state_t net_manager_get_service_state(void);
bool net_manager_service_ready(void);
bool net_manager_dns_ready(void);
bool net_manager_internet_ready(void);
bool net_manager_can_run_weather(void);
bool net_manager_can_run_ai(void);
bool net_manager_network_ready(void);
bool net_manager_is_4g_active(void);
net_manager_link_t net_manager_get_active_link(void);
net_manager_mode_t net_manager_get_desired_mode(void);
void net_manager_get_snapshot(net_manager_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

#endif
