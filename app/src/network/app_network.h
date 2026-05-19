#ifndef APP_NETWORK_APP_NETWORK_H
#define APP_NETWORK_APP_NETWORK_H

#include <stdbool.h>
#include <time.h>
#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    APP_NETWORK_MODE_BT = 0,
    APP_NETWORK_MODE_4G,
    APP_NETWORK_MODE_NONE,
} app_network_mode_t;

typedef enum
{
    APP_NETWORK_LINK_NONE = 0,
    APP_NETWORK_LINK_BT_PAN,
    APP_NETWORK_LINK_4G_CAT1,
} app_network_link_t;

typedef enum
{
    APP_NETWORK_CLIENT_GENERIC = 0,
    APP_NETWORK_CLIENT_AI,
    APP_NETWORK_CLIENT_WEATHER,
    APP_NETWORK_CLIENT_OTA,
} app_network_client_t;

typedef enum
{
    APP_NETWORK_STATE_OFFLINE = 0,
    APP_NETWORK_STATE_RADIO_READY,
    APP_NETWORK_STATE_LINK_READY,
    APP_NETWORK_STATE_DNS_READY,
    APP_NETWORK_STATE_INTERNET_READY,
} app_network_state_t;

typedef struct
{
    app_network_mode_t desired_mode;
    app_network_mode_t runtime_mode;
    app_network_link_t active_link;
    app_network_state_t state;
    bool can_use_network;
    bool radios_suspended;
} app_network_snapshot_t;

void app_network_get_snapshot(app_network_snapshot_t *snapshot);
bool app_network_can_run(app_network_client_t client);
bool app_network_check_internet(app_network_client_t client, const char *hostname);
rt_err_t app_network_request_mode(app_network_mode_t mode);
rt_err_t app_network_http_begin(app_network_client_t client, rt_int32_t timeout_ms);
void app_network_http_end(app_network_client_t client);
const char *app_network_state_text(app_network_state_t state);
const char *app_network_link_text(app_network_link_t link);
bool app_network_get_network_time(time_t *utc_time);

#ifdef __cplusplus
}
#endif

#endif
