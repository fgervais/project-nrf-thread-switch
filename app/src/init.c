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
		.window.max = (3 * 60 + 40) * MSEC_PER_SEC,
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

static void scd4x_reset(hvac_t *hvac)
{
	hvac_scd40_send_cmd(hvac, HVAC_STOP_PERIODIC_MEASUREMENT);
	k_sleep(K_MSEC(500));
	hvac_scd40_send_cmd(hvac, HVAC_REINIT);
	k_sleep(K_MSEC(30));
}

int init_temphum24_click(temphum24_t *temphum24)
{
	int ret;
	temphum24_cfg_t temphum24_cfg;

	temphum24_cfg_setup(&temphum24_cfg);
	temphum24->i2c.dev = DEVICE_DT_GET(DT_NODELABEL(i2c0));
	temphum24->rst.port = DEVICE_DT_GET(DT_NODELABEL(gpio0));
	temphum24->alert.port = DEVICE_DT_GET(DT_NODELABEL(gpio0));
	temphum24_cfg.rst = 5;
	temphum24_cfg.alert = 29;
	ret = temphum24_init(temphum24, &temphum24_cfg);
	if (ret < 0) {
		LOG_ERR("Could not initialize hdc302x");
		return ret;
	}

	return 0;
}

int init_hvac_click(hvac_t *hvac)
{
	int ret;
	hvac_cfg_t hvac_cfg;

	hvac_cfg_setup(&hvac_cfg);
	hvac->i2c.dev = DEVICE_DT_GET(DT_NODELABEL(i2c0));
	ret = hvac_init(hvac, &hvac_cfg);
	if (ret < 0) {
		LOG_ERR("Could not initialize hvac");
		return ret;
	}

	scd4x_reset(hvac);

	return 0;
}
