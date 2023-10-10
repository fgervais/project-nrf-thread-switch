#include <app_event_manager.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/pm/device.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#define MODULE main
#include <caf/events/module_state_event.h>
#include <caf/events/button_event.h>

#include <app_version.h>

#include "dns_resolve.h"
#include "ha.h"
#include "init.h"
#include "mqtt.h"
#include "openthread.h"
#include "reset.h"
#include "uid.h"


#define RETRY_DELAY_SECONDS			10

#define MAIN_LOOP_PERIOD_SECONDS		1 * 60
#define NUMBER_OF_LOOP_RESET_WATCHDOG_SENSOR	(5 * 60 / MAIN_LOOP_PERIOD_SECONDS)



static struct ha_switch switch1 = {
	.name = "Switch",
	.device_class = "switch",
};

static struct ha_sensor watchdog_triggered_sensor = {
	.type = HA_BINARY_SENSOR_TYPE,
	.name = "Watchdog",
	.device_class = "problem",
	.retain = true,
};


static void register_switch_retry(struct ha_switch *sw)
{
	int ret;

retry:
	ret = ha_register_switch(sw);
	if (ret < 0) {
		LOG_WRN("Could not register switch, retrying");
		k_sleep(K_SECONDS(RETRY_DELAY_SECONDS));
		goto retry;
	}
}

static void register_sensor_retry(struct ha_sensor *sensor)
{
	int ret;

retry:
	ret = ha_register_sensor(sensor);
	if (ret < 0) {
		LOG_WRN("Could not register sensor, retrying");
		k_sleep(K_SECONDS(RETRY_DELAY_SECONDS));
		goto retry;
	}
}

static void send_binary_sensor_retry(struct ha_sensor *sensor)
{
	int ret;

retry:
	ret = ha_send_binary_sensor_state(sensor);
	if (ret < 0) {
		LOG_WRN("Could not send binary sensor, retrying");
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
	// const struct device *cons = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

	int ret;
	int main_wdt_chan_id = -1, mqtt_wdt_chan_id = -1;
	uint32_t reset_cause;

	uint32_t main_loop_counter = 0;


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

	ret = uid_init();
	if (ret < 0) {
		LOG_ERR("Could not init uid module");
		return ret;
	}

	ret = uid_generate_unique_id(watchdog_triggered_sensor.unique_id,
				     sizeof(watchdog_triggered_sensor.unique_id),
				     "nrf52840", "wdt",
				     uid_get_device_id());
	if (ret < 0) {
		LOG_ERR("Could not generate hdc302x temperature unique id");
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

	register_sensor_retry(&watchdog_triggered_sensor);
	register_switch_retry(&switch1);

	// We set the device online a little after sensor registrations
	// so HA gets time to process the sensor registrations first before
	// setting the entities online
	LOG_INF("💤 waiting for HA to process registration");
	k_sleep(K_SECONDS(5));

	set_online_retry();

	ha_set_binary_sensor_state(&watchdog_triggered_sensor,
				   is_reset_cause_watchdog(reset_cause));
	send_binary_sensor_retry(&watchdog_triggered_sensor);

	LOG_INF("🎉 init done 🎉");

	// while (1) {
	// 	dns_resolve_finished = false;
	// 	dns_resolve_do_ipv6_lookup();

	// 	while (!dns_resolve_finished)
	// 		k_sleep(K_MSEC(100));

	// 	if (dns_resolve_success) {
	// 		strncpy(mqtt_server_addr, dns_resolve_last_resolve_addr,
	// 			sizeof(mqtt_server_addr));
	// 		break;
	// 	}
	// }


	LOG_INF("****************************************");
	LOG_INF("MAIN DONE");
	LOG_INF("****************************************");

	// k_sleep(K_SECONDS(3));
	// pm_device_action_run(cons, PM_DEVICE_ACTION_SUSPEND);

	// LOG_INF("PM_DEVICE_ACTION_SUSPEND");

	while(1) {
		k_sleep(K_SECONDS(1 * 60));

		if (main_loop_counter >= NUMBER_OF_LOOP_RESET_WATCHDOG_SENSOR &&
		    ha_get_binary_sensor_state(&watchdog_triggered_sensor) == true) {
			ha_set_binary_sensor_state(&watchdog_triggered_sensor, false);
			send_binary_sensor_retry(&watchdog_triggered_sensor);
		}

		// Epilogue

		main_loop_counter += 1;

		LOG_INF("🦴 feed watchdog");
		wdt_feed(wdt, main_wdt_chan_id);

		LOG_INF("💤 end of main loop");
		k_sleep(K_SECONDS(MAIN_LOOP_PERIOD_SECONDS));
	}

	return 0;
}

static bool event_handler(const struct app_event_header *eh)
{
	int ret;
	const struct button_event *evt;

	if (is_button_event(eh)) {
		evt = cast_button_event(eh);

		if (evt->pressed) {
			// mqtt_publisher();
			ha_toggle_switch_state(&switch1);
			ret = ha_send_switch_state(&switch1);
			if (ret < 0) {
				LOG_WRN("⚠️ could not send switch state");
// WHAT TO DO HERE?
			}
		}
	}

	return true;
}

APP_EVENT_LISTENER(MODULE, event_handler);
APP_EVENT_SUBSCRIBE(MODULE, button_event);
