#include <app_event_manager.h>
#include <device.h>
#include <devicetree.h>
#include <drivers/gpio.h>
#include <pm/device.h>
#include <zephyr.h>

#include <logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#define MODULE main
#include <caf/events/module_state_event.h>
#include <caf/events/button_event.h>

#include "mqtt.h"
#include "dns_resolve.h"


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
		dns_resolve_do_ipv6_lookup();

		while (!dns_resolve_finished)
			k_sleep(K_MSEC(100));

		if (dns_resolve_addr != NULL) {
			mqtt_server_addr = dns_resolve_addr;
			break;
		}
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
