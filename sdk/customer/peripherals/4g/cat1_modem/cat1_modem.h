#ifndef SDK_CUSTOMER_PERIPHERALS_4G_CAT1_MODEM_H
#define SDK_CUSTOMER_PERIPHERALS_4G_CAT1_MODEM_H

#include <stdbool.h>
#include <time.h>
#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

rt_err_t cat1_modem_init(void);
rt_err_t cat1_modem_request_online(void);
rt_err_t cat1_modem_request_offline(void);
bool cat1_modem_is_ready(void);
void cat1_modem_get_status_text(char *buffer, rt_size_t buffer_size);
bool cat1_modem_get_network_time(time_t *timestamp);

#ifdef __cplusplus
}
#endif

#endif
