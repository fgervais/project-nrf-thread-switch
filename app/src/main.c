#include <hal/nrf_power.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/input/input.h>
#include <zephyr/pm/device.h>
// There is an include missing in thread_analyzer.h
// Workaround by including it lower.
#include <zephyr/debug/thread_analyzer.h>
#include <zephyr/sys/reboot.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#include <app_version.h>

#include "ha.h"
#include "init.h"
#include "mqtt.h"
#include "openthread.h"
#include "reset.h"
#include "uid.h"


#define RETRY_DELAY_SECONDS			10

#define MAIN_LOOP_PERIOD_SECONDS		(1 * 60)
#define NUMBER_OF_LOOP_RUN_ANALYSIS		(5 * 60 / MAIN_LOOP_PERIOD_SECONDS)
#define NUMBER_OF_LOOP_RESET_WATCHDOG_SENSOR	(5 * 60 / MAIN_LOOP_PERIOD_SECONDS)

#define ERROR_BOOT_TOKEN			(uint8_t)0x38


static const struct device *const buttons_dev = DEVICE_DT_GET(DT_NODELABEL(buttons));
static bool ready = false;

static struct ha_sensor watchdog_triggered_sensor = {
	.type = HA_BINARY_SENSOR_TYPE,
	.name = "Watchdog",
	.device_class = "problem",
	.retain = true,
};

static struct ha_trigger trigger1 = {
	.type = "button_short_press",
	.subtype = "button_1",
};


static void register_trigger_retry(struct ha_trigger *trigger)
{
	int ret;

retry:
	ret = ha_register_trigger(trigger);
	if (ret < 0) {
		LOG_WRN("Could not register trigger, retrying");
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
	bool fast_boot = false;

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
	else if (is_reset_cause_software(reset_cause)
		 && nrf_power_gpregret_get(NRF_POWER) == ERROR_BOOT_TOKEN) {
		LOG_INF("🔥 Fast boot!");
		fast_boot = true;
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
		LOG_ERR("Could not generate watchdog unique id");
		return ret;
	}

	LOG_INF("💤 waiting for openthread to be ready");
	openthread_wait_for_ready();
	// Something else is not ready, not sure what
	k_sleep(K_MSEC(100));

	mqtt_watchdog_init(wdt, mqtt_wdt_chan_id);
	ha_start(uid_get_device_id());

	register_sensor_retry(&watchdog_triggered_sensor);
	register_trigger_retry(&trigger1);

	// We set the device online a little after sensor registrations
	// so HA gets time to process the sensor registrations first before
	// setting the entities online
	LOG_INF("💤 waiting for HA to process registration");
	k_sleep(K_SECONDS(5));

	set_online_retry();

	ha_set_binary_sensor_state(&watchdog_triggered_sensor,
				   is_reset_cause_watchdog(reset_cause));
	send_binary_sensor_retry(&watchdog_triggered_sensor);

	ready = true;

	LOG_INF("🎉 init done 🎉");

	// k_sleep(K_SECONDS(3));
	// pm_device_action_run(cons, PM_DEVICE_ACTION_SUSPEND);

	// LOG_INF("PM_DEVICE_ACTION_SUSPEND");

	while(1) {
		LOG_INF("🗨️  main loop counter: %d", main_loop_counter);
		if (main_loop_counter % NUMBER_OF_LOOP_RUN_ANALYSIS == 0) {
			thread_analyzer_print();
		}

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

static void event_handler(struct input_event *evt)
{
	int ret;

	LOG_INF("GPIO_KEY %s pressed, zephyr_code=%u, value=%d",
		 evt->dev->name, evt->code, evt->value);

	if (!ready) {
		return;
	}

	ret = ha_send_trigger_event(&trigger1);
	if (ret < 0) {
		LOG_ERR("could not send button state");
		// modules/lib/matter/src/platform/nrfconnect/Reboot.cpp
		// zephyr/soc/arm/nordic_nrf/nrf52/soc.c
		sys_reboot(ERROR_BOOT_TOKEN);
	}
}

// Upcoming API: INPUT_CALLBACK_DEFINE()
INPUT_LISTENER_CB_DEFINE(buttons_dev, event_handler);
