#include <openthread/thread.h>
#include <zephyr/net/openthread.h>
#include <zephyr/net/dns_resolve.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(dns_resolve, LOG_LEVEL_DBG);


#define DNS_TIMEOUT (2 * MSEC_PER_SEC)

char dns_resolve_last_resolve_addr[NET_IPV6_ADDR_LEN];
bool dns_resolve_finished;
bool dns_resolve_success;


static void dns_result_cb(enum dns_resolve_status status,
		   struct dns_addrinfo *info,
		   void *user_data)
{
	char *hr_family;
	void *addr;

	otInstance *instance = openthread_get_default_instance();


	k_sleep(K_MSEC(50));
	otLinkSetPollPeriod(instance, 0);

	switch (status) {
	case DNS_EAI_CANCELED:
		LOG_INF("DNS query was canceled");
		goto out;
	case DNS_EAI_FAIL:
		LOG_INF("DNS resolve failed");
		goto out;
	case DNS_EAI_NODATA:
		LOG_INF("Cannot resolve address");
		goto out;
	case DNS_EAI_ALLDONE:
		LOG_INF("DNS resolving finished");
		goto out;
	case DNS_EAI_INPROGRESS:
		break;
	default:
		LOG_INF("DNS resolving error (%d)", status);
		goto out;
	}

	if (!info) {
		goto out;
	}

	if (info->ai_family == AF_INET) {
		hr_family = "IPv4";
		addr = &net_sin(&info->ai_addr)->sin_addr;
		LOG_ERR("We need an ipv6 address but received ipv4");
	} else if (info->ai_family == AF_INET6) {
		hr_family = "IPv6";
		addr = (char *)&net_sin6(&info->ai_addr)->sin6_addr;
	} else {
		LOG_ERR("Invalid IP address family %d", info->ai_family);
		goto out;
	}

	LOG_INF("%s %s address: %s", user_data ? (char *)user_data : "<null>",
		hr_family,
		net_addr_ntop(info->ai_family, addr,
			dns_resolve_last_resolve_addr,
			sizeof(dns_resolve_last_resolve_addr)));

	dns_resolve_success = true;
out:
	dns_resolve_finished = true;
}

void dns_resolve_do_ipv6_lookup(void)
{
	static const char *query = "home.home.arpa";
	static uint16_t dns_id;
	int ret;

	otInstance *instance = openthread_get_default_instance();

	dns_resolve_finished = false;
	dns_resolve_success = false;

	otLinkSetPollPeriod(instance, 10);

	ret = dns_get_addr_info(query,
				DNS_QUERY_TYPE_AAAA,
				&dns_id,
				dns_result_cb,
				(void *)query,
				DNS_TIMEOUT);
	if (ret < 0) {
		LOG_ERR("Cannot resolve IPv6 address (%d)", ret);
		dns_resolve_finished = true;
		otLinkSetPollPeriod(instance, 0);
		return;
	}

	LOG_DBG("DNS id %u", dns_id);
}