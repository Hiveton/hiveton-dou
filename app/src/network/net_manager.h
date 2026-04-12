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

rt_err_t net_manager_init(void);
void net_manager_request_bt_mode(void);
void net_manager_request_4g_mode(void);
void net_manager_notify_bt_acl(bool connected);
void net_manager_notify_pan_ready(bool ready);
void net_manager_notify_cat1_ready(bool ready);

bool net_manager_bt_enabled(void);
bool net_manager_bt_connected(void);
bool net_manager_4g_enabled(void);
bool net_manager_network_ready(void);
bool net_manager_is_4g_active(void);
net_manager_link_t net_manager_get_active_link(void);

#ifdef __cplusplus
}
#endif

#endif
