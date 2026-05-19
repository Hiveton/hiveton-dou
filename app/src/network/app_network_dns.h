#ifndef APP_NETWORK_APP_NETWORK_DNS_H
#define APP_NETWORK_APP_NETWORK_DNS_H

#include <lwip/dns.h>

#ifdef __cplusplus
extern "C" {
#endif

err_t app_network_dns_gethostbyname(const char *hostname, ip_addr_t *addr,
                                    dns_found_callback found, void *callback_arg);

#ifdef __cplusplus
}
#endif

#endif
