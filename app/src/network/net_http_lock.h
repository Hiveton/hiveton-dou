#ifndef APP_NETWORK_NET_HTTP_LOCK_H
#define APP_NETWORK_NET_HTTP_LOCK_H

#include <stdbool.h>
#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    NET_HTTP_CLIENT_GENERIC = 0,
    NET_HTTP_CLIENT_XIAOZHI = 1,
} net_http_client_t;

rt_err_t net_http_lock_take(net_http_client_t client, rt_int32_t timeout_ms);
void net_http_lock_release(net_http_client_t client);
void net_http_set_xiaozhi_active(bool active);
bool net_http_xiaozhi_active(void);
bool net_http_should_defer_generic(void);

#ifdef __cplusplus
}
#endif

#endif
