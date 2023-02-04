#ifndef DNS_RESOLVE_H_
#define DNS_RESOLVE_H_

extern char *dns_resolve_addr;
extern bool dns_resolve_finished;

void dns_resolve_do_ipv6_lookup(void);

#endif /* DNS_RESOLVE_H_ */