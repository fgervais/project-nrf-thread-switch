#ifndef DNS_RESOLVE_H_
#define DNS_RESOLVE_H_

#include <zephyr/net/net_ip.h>


extern char dns_resolve_last_resolve_addr[NET_IPV6_ADDR_LEN];
extern bool dns_resolve_finished;
extern bool dns_resolve_success;

void dns_resolve_do_ipv6_lookup(void);

#endif /* DNS_RESOLVE_H_ */