#include <app_event_manager.h>
#include <device.h>
#include <devicetree.h>
#include <drivers/gpio.h>
#include <openthread/thread.h>
#include <pm/device.h>
#include <zephyr.h>
#include <zephyr/net/dns_resolve.h>
#include <zephyr/net/openthread.h>

#include <logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#define MODULE main
#include <caf/events/module_state_event.h>
#include <caf/events/button_event.h>

#include "mqtt.h"


#define DNS_TIMEOUT (2 * MSEC_PER_SEC)


bool dns_resolve_finished = false;


void dns_result_cb(enum dns_resolve_status status,
		   struct dns_addrinfo *info,
		   void *user_data)
{
	char hr_addr[NET_IPV6_ADDR_LEN];
	char *hr_family;

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
		// hr_family = "IPv4";
		// mqtt_server_addr = &net_sin(&info->ai_addr)->sin_addr;
		LOG_ERR("We need an ipv6 address but received ipv4");
	} else if (info->ai_family == AF_INET6) {
		hr_family = "IPv6";
		mqtt_server_addr = (char *)&net_sin6(&info->ai_addr)->sin6_addr;
	} else {
		LOG_ERR("Invalid IP address family %d", info->ai_family);
		goto out;
	}

	LOG_INF("%s %s address: %s", user_data ? (char *)user_data : "<null>",
		hr_family,
		net_addr_ntop(info->ai_family, mqtt_server_addr,
					 hr_addr, sizeof(hr_addr)));
out:
	dns_resolve_finished = true;
}

static void do_ipv6_lookup(void)
{
	static const char *query = "home.home.arpa";
	static uint16_t dns_id;
	int ret;

	otInstance *instance = openthread_get_default_instance();


	otLinkSetPollPeriod(instance, 10);

	ret = dns_get_addr_info(query,
				DNS_QUERY_TYPE_AAAA,
				&dns_id,
				dns_result_cb,
				(void *)query,
				DNS_TIMEOUT);
	if (ret < 0) {
		LOG_ERR("Cannot resolve IPv6 address (%d)", ret);
		return;
	}

	LOG_DBG("DNS id %u", dns_id);
}


void main(void)
{
	const struct device *cons = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

	if (app_event_manager_init()) {
		LOG_ERR("Event manager not initialized");
	} else {
		module_set_state(MODULE_STATE_READY);
	}

	// Wait a bit for Thread to initialize
	k_sleep(K_MSEC(100));

	while (1) {
		dns_resolve_finished = false;
		do_ipv6_lookup();

		while (!dns_resolve_finished)
			k_sleep(K_MSEC(100));

		if (mqtt_server_addr != NULL)
			break;
	}

	LOG_INF("****************************************");
	LOG_INF("MAIN DONE");
	LOG_INF("****************************************");

	k_sleep(K_SECONDS(3));
	// pm_device_action_run(cons, PM_DEVICE_ACTION_SUSPEND);

	LOG_INF("PM_DEVICE_ACTION_SUSPEND");
}

static bool event_handler(const struct app_event_header *eh)
{
	const struct button_event *evt;

	if (is_button_event(eh)) {
		evt = cast_button_event(eh);

		if (evt->pressed) {
			mqtt_publisher();
		}
	}

	return true;
}

APP_EVENT_LISTENER(MODULE, event_handler);
APP_EVENT_SUBSCRIBE(MODULE, button_event);
