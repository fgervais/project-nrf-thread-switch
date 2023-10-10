#include <stdio.h>
#include <zephyr/drivers/hwinfo.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(uid, LOG_LEVEL_DBG);

#include "ha.h"
#include "uid.h"


#define DEVICE_ID_BYTE_SIZE	8


static char device_id_hex_string[DEVICE_ID_BYTE_SIZE * 2 + 1];

// static char hdc302x_serial_string[UID_UNIQUE_ID_STRING_SIZE];
// static char scd4x_serial_string[UID_UNIQUE_ID_STRING_SIZE];
// #ifdef CONFIG_APP_ENABLE_SPS30
// static char sps30_serial_string[HVAC_SPS30_MAX_SERIAL_LEN];
// #endif


static int get_device_id_as_string(char *id_string, size_t id_string_len)
{
	uint8_t dev_id[DEVICE_ID_BYTE_SIZE];
	ssize_t length;

	length = hwinfo_get_device_id(dev_id, sizeof(dev_id));

	if (length == -ENOTSUP) {
		LOG_ERR("Not supported by hardware");
		return -ENOTSUP;
	} else if (length < 0) {
		LOG_ERR("Error: %zd", length);
		return length;
	}

	bin2hex(dev_id, ARRAY_SIZE(dev_id), id_string, id_string_len);

	LOG_INF("CPU device id: %s", id_string);

	return 0;
}

// static int get_hdc302x_serial_as_string(temphum24_t *temphum_ctx,
// 					char *sn_buf, size_t sn_buf_size)
// {
// 	int ret;
// 	uint16_t serial_number_words[3];

// 	ret = temphum24_get_serial_number(temphum_ctx, serial_number_words);
// 	if (ret < 0) {
// 		LOG_ERR("temphum24: could not read hdc302x serial number");
// 		return ret;
// 	}

// 	ret = snprintf(sn_buf, sn_buf_size,
// 		       "%04x%04x%04x",
// 		       serial_number_words[0],
// 		       serial_number_words[1],
// 		       serial_number_words[2]);
// 	if (ret < 0 && ret >= sn_buf_size) {
// 		LOG_ERR("Could not set sn_buf");
// 		return -ENOMEM;
// 	}

// 	LOG_INF("HDC302x serial number: %s", sn_buf);
// 	return 0;
// }

// static int get_scd4x_serial_as_string(hvac_t *hvac_ctx,
// 				      char *sn_buf, size_t sn_buf_size)
// {
// 	int ret;
// 	uint16_t scd4x_serial_words[3];

// 	ret = hvac_scd40_get_serial_number(hvac_ctx, scd4x_serial_words);
// 	if (ret < 0) {
// 		LOG_ERR("hvac: could not read scd4x serial number");
// 		return ret;
// 	}

// 	ret = snprintf(sn_buf, sn_buf_size,
// 		       "%04x%04x%04x",
// 		       scd4x_serial_words[0],
// 		       scd4x_serial_words[1],
// 		       scd4x_serial_words[2]);
// 	if (ret < 0 && ret >= sn_buf_size) {
// 		LOG_ERR("Could not set sn_buf");
// 		return -ENOMEM;
// 	}

// 	LOG_INF("SCD4x serial number: %s", sn_buf);
// 	return 0;
// }

// #ifdef CONFIG_APP_ENABLE_SPS30
// static int get_sps30_serial_as_string(hvac_t *hvac_ctx,
// 				      char *sn_buf, size_t sn_buf_size)
// {
// 	int ret;

// 	ret = hvac_sps30_get_serial_number(hvac_ctx, sn_buf, sn_buf_size);
// 	if (ret < 0) {
// 		LOG_ERR("hvac: could not read sps30 serial number");
// 		return ret;
// 	}

// 	LOG_INF("SPS30 serial number: %s", sn_buf);
// 	return 0;
// }
// #endif

int uid_generate_unique_id(char *uid_buf, size_t uid_buf_size,
			   const char *part_number,
			   const char *sensor_name,
			   const char *serial_number)
{
	int ret;

	ret = snprintf(uid_buf, uid_buf_size,
		       "%s_%s_%s",
		       part_number, serial_number, sensor_name);
	if (ret < 0 && ret >= uid_buf_size) {
		LOG_ERR("Could not set uid_buf");
		return -ENOMEM;
	}

	LOG_INF("📇 unique id: %s", uid_buf);
	return 0;
}

char * uid_get_device_id(void)
{
	return device_id_hex_string;
}

// char * uid_get_hdc302x_serial(void)
// {
// 	return hdc302x_serial_string;
// }

// char * uid_get_scd4x_serial(void)
// {
// 	return scd4x_serial_string;
// }

// #ifdef CONFIG_APP_ENABLE_SPS30
// char * uid_get_sps30_serial(void)
// {
// 	return sps30_serial_string;
// }
// #endif

// int uid_init(temphum24_t *temphum24, hvac_t *hvac)
int uid_init(void)
{
	int ret;

	ret = get_device_id_as_string(
		device_id_hex_string,
		ARRAY_SIZE(device_id_hex_string));
	if (ret < 0) {
		LOG_ERR("Could not get device ID");
		return ret;
	}

// 	ret = get_hdc302x_serial_as_string(temphum24,
// 					   hdc302x_serial_string,
// 					   sizeof(hdc302x_serial_string));
// 	if (ret < 0) {
// 		LOG_ERR("Could not get hdc302x serial number string");
// 		return ret;
// 	}

// 	ret = get_scd4x_serial_as_string(hvac, scd4x_serial_string,
// 					 sizeof(scd4x_serial_string));
// 	if (ret < 0) {
// 		LOG_ERR("Could not get scd4x serial number string");
// 		return ret;
// 	}

// #ifdef CONFIG_APP_ENABLE_SPS30
// 	ret = get_sps30_serial_as_string(hvac,
// 					 sps30_serial_string,
// 					 sizeof(sps30_serial_string));
// 	if (ret < 0) {
// 		LOG_ERR("Could not get sps30 serial number string");
// 		return ret;
// 	}
// #endif

	return 0;
}

// #ifdef CONFIG_APP_ENABLE_SPS30
// int uid_fill_unique_ids(struct ha_sensor *wdt,
// 			struct ha_sensor *temp,
// 			struct ha_sensor *hum,
// 			struct ha_sensor *co2,
// 			struct ha_sensor *pm1_sensor,
// 			struct ha_sensor *pm25_sensor,
// 			struct ha_sensor *pm10_sensor)
// #else
// int uid_fill_unique_ids(struct ha_sensor *wdt,
// 			struct ha_sensor *temp,
// 			struct ha_sensor *hum,
// 			struct ha_sensor *co2)
// #endif
// {
// 	int ret;

// 	ret = generate_unique_id(wdt->unique_id,
// 				 sizeof(wdt->unique_id),
// 				 "nrf52840", "wdt",
// 				 uid_get_device_id());
// 	if (ret < 0) {
// 		LOG_ERR("Could not generate hdc302x temperature unique id");
// 		return ret;
// 	}

// 	ret = generate_unique_id(temp->unique_id,
// 				 sizeof(temp->unique_id),
// 				 "hdc302x", "temp",
// 				 uid_get_hdc302x_serial());
// 	if (ret < 0) {
// 		LOG_ERR("Could not generate hdc302x temperature unique id");
// 		return ret;
// 	}

// 	ret = generate_unique_id(hum->unique_id,
// 				 sizeof(hum->unique_id),
// 				 "hdc302x", "hum",
// 				 uid_get_hdc302x_serial());
// 	if (ret < 0) {
// 		LOG_ERR("Could not generate hdc302x humidity unique id");
// 		return ret;
// 	}

// 	ret = generate_unique_id(co2->unique_id,
// 				 sizeof(co2->unique_id),
// 				 "scd4x", "co2",
// 				 uid_get_scd4x_serial());
// 	if (ret < 0) {
// 		LOG_ERR("Could not generate scd4x unique id");
// 		return ret;
// 	}

// #ifdef CONFIG_APP_ENABLE_SPS30
// 	ret = generate_unique_id(pm1_sensor->unique_id,
// 				 sizeof(pm1_sensor->unique_id),
// 				 "sps30", "pm1",
// 				 uid_get_sps30_serial());
// 	if (ret < 0) {
// 		LOG_ERR("Could not generate pm1 unique id");
// 		return ret;
// 	}

// 	ret = generate_unique_id(pm25_sensor->unique_id,
// 				 sizeof(pm25_sensor->unique_id),
// 				 "sps30", "pm25",
// 				 uid_get_sps30_serial());
// 	if (ret < 0) {
// 		LOG_ERR("Could not generate pm25 unique id");
// 		return ret;
// 	}

// 	ret = generate_unique_id(pm10_sensor->unique_id,
// 				 sizeof(pm10_sensor->unique_id),
// 				 "sps30", "pm10",
// 				 uid_get_sps30_serial());
// 	if (ret < 0) {
// 		LOG_ERR("Could not generate pm10 unique id");
// 		return ret;
// 	}
// #endif

// 	return 0;
// }