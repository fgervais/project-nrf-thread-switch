#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(init, LOG_LEVEL_DBG);

#include "init.h"


static int watchdog_new_channel(const struct device *wdt)
{
	int ret;

	struct wdt_timeout_cfg wdt_config = {
		.window.min = 0,
		.window.max = CONFIG_APP_WATCHDOG_TIMEOUT_SEC * MSEC_PER_SEC,
		.callback = NULL,
		.flags = WDT_FLAG_RESET_SOC,
	};

	ret = wdt_install_timeout(wdt, &wdt_config);
	if (ret < 0) {
		LOG_ERR("watchdog install error");
	}

	return ret;
}

int init_watchdog(const struct device *wdt,
		  int *main_channel_id, int *mqtt_channel_id)
{
	int ret;

	if (!device_is_ready(wdt)) {
		LOG_ERR("%s: device not ready", wdt->name);
		return -ENODEV;
	}

	ret = watchdog_new_channel(wdt);
	if (ret < 0) {
		LOG_ERR("Could not create a new watchdog channel");
		return ret;
	}

	*main_channel_id = ret;
	LOG_INF("main watchdog channel id: %d", *main_channel_id);

	ret = watchdog_new_channel(wdt);
	if (ret < 0) {
		LOG_ERR("Could not create a new watchdog channel");
		return ret;
	}

	*mqtt_channel_id = ret;
	LOG_INF("mqtt watchdog channel id: %d", *mqtt_channel_id);

	ret = wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG);
	if (ret < 0) {
		LOG_ERR("watchdog setup error");
		return 0;
	}

	LOG_INF("🐶 watchdog started!");

	return 0;
}
