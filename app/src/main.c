#include <app_event_manager.h>
#include <openthread/thread.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/pm/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/openthread.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#define MODULE main
#include <caf/events/module_state_event.h>
#include <caf/events/button_event.h>

#include "mqtt.h"
#include "dns_resolve.h"
#include "openthread.h"


#define CSL_PERIOD_MS 500


void main(void)
{
	otError otErr;
	otInstance *instance = openthread_get_default_instance();
	const struct device *cons = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

	if (app_event_manager_init()) {
		LOG_ERR("Event manager not initialized");
	} else {
		module_set_state(MODULE_STATE_READY);
	}

	openthread_enable_ready_flag();

	while (!openthread_ready)
		k_sleep(K_MSEC(100));
	otErr = otLinkCslSetPeriod(instance,
			CSL_PERIOD_MS * 1000 / OT_US_PER_TEN_SYMBOLS);
	// Something else is not ready, not sure what
	k_sleep(K_MSEC(100));

	while (1) {
		dns_resolve_finished = false;
		dns_resolve_do_ipv6_lookup();

		while (!dns_resolve_finished)
			k_sleep(K_MSEC(100));

		if (dns_resolve_success) {
			strncpy(mqtt_server_addr, dns_resolve_last_resolve_addr,
				sizeof(mqtt_server_addr));
			break;
		}
	}

	LOG_INF("****************************************");
	LOG_INF("MAIN DONE");
	LOG_INF("****************************************");

	k_sleep(K_SECONDS(3));
	pm_device_action_run(cons, PM_DEVICE_ACTION_SUSPEND);

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
