#include <app_event_manager.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/pm/device.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#define MODULE main
#include <caf/events/module_state_event.h>
#include <caf/events/button_event.h>

#include "mqtt.h"
#include "dns_resolve.h"
#include "openthread.h"


#define RETRY_DELAY_SECONDS			10


static struct ha_switch switch1 = {
	.name = "Switch",
	.device_class = "switch",
};


static void register_switch_retry(struct ha_switch *switch)
{
	int ret;

retry:
	ret = ha_register_switch(switch);
	if (ret < 0) {
		LOG_WRN("Could not register switch, retrying");
		k_sleep(K_SECONDS(RETRY_DELAY_SECONDS));
		goto retry;
	}
}

static void set_online_retry(void)
{
	int ret;

retry:
	ret = ha_set_online();
	if (ret < 0) {
		LOG_WRN("Could not set online, retrying");
		k_sleep(K_SECONDS(RETRY_DELAY_SECONDS));
		goto retry;
	}
}

int main(void)
{
	const struct device *wdt = DEVICE_DT_GET(DT_NODELABEL(wdt0));
	const struct device *cons = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

	int main_wdt_chan_id = -1, mqtt_wdt_chan_id = -1;
	uint32_t reset_cause;


	init_watchdog(wdt, &main_wdt_chan_id, &mqtt_wdt_chan_id);

	LOG_INF("\n\n🚀 MAIN START (%s) 🚀\n", APP_VERSION_FULL);

	reset_cause = show_reset_cause();
	clear_reset_cause();

	if (is_reset_cause_watchdog(reset_cause)
	    || is_reset_cause_button(reset_cause)) {
		ret = openthread_erase_persistent_info();
		if (ret < 0) {
			LOG_WRN("Could not erase openthread info");
		}
	}

	ret = openthread_my_start();
	if (ret < 0) {
		LOG_ERR("Could not start openthread");
		return ret;
	}

	ret = uid_init(&temphum24, &hvac);
	if (ret < 0) {
		LOG_ERR("Could not init uid module");
		return ret;
	}

	if (app_event_manager_init()) {
		LOG_ERR("Event manager not initialized");
	} else {
		module_set_state(MODULE_STATE_READY);
	}

	LOG_INF("💤 waiting for openthread to be ready");
	openthread_wait_for_ready();

	mqtt_watchdog_init(wdt, mqtt_wdt_chan_id);
	ha_start(uid_get_device_id());

	register_switch_retry(&switch1);

	// We set the device online a little after sensor registrations
	// so HA gets time to process the sensor registrations first before
	// setting the entities online
	LOG_INF("💤 waiting for HA to process registration");
	k_sleep(K_SECONDS(5));

	set_online_retry();

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

	return 0;
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
