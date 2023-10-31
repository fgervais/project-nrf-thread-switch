#include <stdio.h>
#include <zephyr/drivers/hwinfo.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(uid, LOG_LEVEL_DBG);

#include "ha.h"
#include "uid.h"


#define DEVICE_ID_BYTE_SIZE	8


static char device_id_hex_string[DEVICE_ID_BYTE_SIZE * 2 + 1];


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

	return 0;
}
