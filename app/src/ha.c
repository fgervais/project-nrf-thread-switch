// MQTT device data
// -----------------------------------------------------------------------------
// ~ = CONFIG_APP_DEV_TYPE_* / device_id_hex_string (MQTT_BASE_PATH_FORMAT_STRING)
// availability_topic = ~/available
// state_topic = ~ / <sensor|binary_sensor|trigger> / sensor->unique_id /state

// MQTT Home Assistant config
// -----------------------------------------------------------------------------
// homeassistant / <sensor|binary_sensor|trigger> / conf->unique_id /config

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(home_assistant, LOG_LEVEL_DBG);

#include <zephyr/data/json.h>

#include <stdio.h>
#include <stdlib.h>
#include <app_version.h>

#include "ha.h"
#include "mqtt.h"


#define JSON_CONFIG_BUFFER_SIZE		1024
#define UNIQUE_ID_BUFFER_SIZE		64

#if defined(CONFIG_APP_DEV_TYPE_AIR_QUALITY)
#define MQTT_BASE_PATH_FORMAT_STRING "air_quality/%s"
#elif defined(CONFIG_APP_DEV_TYPE_ACTION_BUTTON)
#define MQTT_BASE_PATH_FORMAT_STRING "action_button/%s"
#else
#error "No device type defined"
#endif

#define LAST_WILL_TOPIC_FORMAT_STRING MQTT_BASE_PATH_FORMAT_STRING "/available"
#if defined(CONFIG_APP_USE_TEST_DISCOVERY_TOPIC)
#define DISCOVERY_TOPIC_FORMAT_STRING		"test/%s/%s/config"
#define DISCOVERY_TOPIC_TRIGGER_FORMAT_STRING	"test/%s/%s/%s_%s/config"
#else
#define DISCOVERY_TOPIC_FORMAT_STRING		"homeassistant/%s/%s/config"
#define DISCOVERY_TOPIC_TRIGGER_FORMAT_STRING	"homeassistant/%s/%s/%s_%s/config"
#endif

#define DEVICE_CONFIG {						\
	.identifiers = device_id_hex_string,				\
	.name = CONFIG_APP_DEVICE_NAME " - " CONFIG_APP_DEVICE_NICKNAME,	\
	.sw_version = APP_VERSION_FULL,					\
	.hw_version = "rev1",						\
	.model = "Gold",						\
	.manufacturer = "François Gervais",				\
}

struct ha_device {
	const char *identifiers;
	const char *name;
	const char *sw_version;
	const char *hw_version;
	const char *model;
	const char *manufacturer;
};

struct ha_sensor_config {
	const char *base_path;
	const char *name;
	const char *unique_id;
	const char *object_id;
	const char *device_class;
	const char *state_class;
	const char *unit_of_measurement;
	int suggested_display_precision;
	const char *availability_topic;
	const char *state_topic;
	struct ha_device dev;
};

struct ha_trigger_config {
	const char *automation_type;
	const char *payload;
	const char *topic;
	const char *type;
	const char *subtype;
	struct ha_device dev;
};


static const char *device_id_hex_string;
static char mqtt_base_path[HA_TOPIC_BUFFER_SIZE];

static char last_will_topic[HA_TOPIC_BUFFER_SIZE];
static const char *last_will_message = "offline";

static bool inhibit_discovery_mode;


static const struct json_obj_descr device_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct ha_device, identifiers,	JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct ha_device, name,	 	JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct ha_device, sw_version,	JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct ha_device, hw_version,	JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct ha_device, model,	 	JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct ha_device, manufacturer, 	JSON_TOK_STRING),
};

static const struct json_obj_descr sensor_config_descr[] = {
	JSON_OBJ_DESCR_PRIM_NAMED(struct ha_sensor_config, "~", base_path,	JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct ha_sensor_config, name,			JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct ha_sensor_config, unique_id,			JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct ha_sensor_config, object_id,			JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct ha_sensor_config, device_class,		JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct ha_sensor_config, state_class,		JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct ha_sensor_config, unit_of_measurement,	JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct ha_sensor_config, suggested_display_precision, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct ha_sensor_config, availability_topic,	JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct ha_sensor_config, state_topic,		JSON_TOK_STRING),
	JSON_OBJ_DESCR_OBJECT(struct ha_sensor_config, dev, device_descr),
};

static const struct json_obj_descr binary_sensor_config_descr[] = {
	JSON_OBJ_DESCR_PRIM_NAMED(struct ha_sensor_config, "~", base_path,	JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct ha_sensor_config, name,			JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct ha_sensor_config, unique_id,			JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct ha_sensor_config, object_id,			JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct ha_sensor_config, device_class,		JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct ha_sensor_config, availability_topic,	JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct ha_sensor_config, state_topic,		JSON_TOK_STRING),
	JSON_OBJ_DESCR_OBJECT(struct ha_sensor_config, dev, device_descr),
};

static const struct json_obj_descr trigger_config_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct ha_trigger_config, automation_type,		JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct ha_trigger_config, payload,			JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct ha_trigger_config, topic,			JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct ha_trigger_config, type,			JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct ha_trigger_config, subtype,			JSON_TOK_STRING),
	JSON_OBJ_DESCR_OBJECT(struct ha_trigger_config, dev, device_descr),
};

// <discovery_prefix>/<component>/[<node_id>/]<object_id>/config
//
// Best practice for entities with a unique_id is to set <object_id> to
// unique_id and omit the <node_id>.
// https://www.home-assistant.io/integrations/mqtt/#discovery-topic
static int ha_send_sensor_discovery(const char *sensor_type,
			     struct ha_sensor_config *conf)
{
	int ret;
	char json_config[JSON_CONFIG_BUFFER_SIZE];
	char discovery_topic[HA_TOPIC_BUFFER_SIZE];

	snprintf(discovery_topic, sizeof(discovery_topic),
		 DISCOVERY_TOPIC_FORMAT_STRING,
		 sensor_type, conf->unique_id);

	LOG_DBG("discovery topic: %s", discovery_topic);

	if (strcmp(sensor_type, "sensor") == 0) {
		ret = json_obj_encode_buf(
			sensor_config_descr, ARRAY_SIZE(sensor_config_descr),
			conf, json_config, sizeof(json_config));
		if (ret < 0) {
			LOG_ERR("Could not encode JSON (%d)", ret);
			return ret;
		}
	}
	else if (strcmp(sensor_type, "binary_sensor") == 0) {
		ret = json_obj_encode_buf(
			binary_sensor_config_descr,
			ARRAY_SIZE(binary_sensor_config_descr),
			conf, json_config, sizeof(json_config));
		if (ret < 0) {
			LOG_ERR("Could not encode JSON (%d)", ret);
			return ret;
		}
	}

	LOG_DBG("payload: %s", json_config);

	ret = mqtt_publish_to_topic(discovery_topic, json_config, true);
	if (ret < 0) {
		LOG_ERR("Count not publish to topic");
		return ret;
	}

	return 0;
}

static int ha_send_trigger_discovery(struct ha_trigger_config *conf)
{
	int ret;
	char json_config[JSON_CONFIG_BUFFER_SIZE];
	char discovery_topic[HA_TOPIC_BUFFER_SIZE];

	snprintf(discovery_topic, sizeof(discovery_topic),
		 DISCOVERY_TOPIC_TRIGGER_FORMAT_STRING,
		 "device_automation", device_id_hex_string,
		 conf->type, conf->subtype);

	LOG_DBG("discovery topic: %s", discovery_topic);

	ret = json_obj_encode_buf(
		trigger_config_descr, ARRAY_SIZE(trigger_config_descr),
		conf, json_config, sizeof(json_config));
	if (ret < 0) {
		LOG_ERR("Could not encode JSON (%d)", ret);
		return ret;
	}

	LOG_DBG("payload: %s", json_config);

	ret = mqtt_publish_to_topic(discovery_topic, json_config, true);
	if (ret < 0) {
		LOG_ERR("Count not publish to topic");
		return ret;
	}

	return 0;
}

int ha_start(const char *device_id, bool inhibit_discovery)
{
	int ret;

	device_id_hex_string = device_id;
	inhibit_discovery_mode = inhibit_discovery;

	ret = snprintf(mqtt_base_path, sizeof(mqtt_base_path),
		 MQTT_BASE_PATH_FORMAT_STRING, device_id_hex_string);
	if (ret < 0 && ret >= sizeof(mqtt_base_path)) {
		LOG_ERR("Could not set mqtt_base_path");
		return -ENOMEM;
	}

	ret = snprintf(last_will_topic, sizeof(last_will_topic),
		 LAST_WILL_TOPIC_FORMAT_STRING, device_id_hex_string);
	if (ret < 0 && ret >= sizeof(last_will_topic)) {
		LOG_ERR("Could not set last_will_topic");
		return -ENOMEM;
	}

	ret = mqtt_init(device_id_hex_string, last_will_topic, last_will_message);
	if (ret < 0) {
		LOG_ERR("could initialize MQTT");
		return ret;
	}

	return 0;
}

int ha_set_online()
{
	int ret;

	ret = mqtt_publish_to_topic(last_will_topic, "online", true);
	if (ret < 0) {
		LOG_ERR("Count not publish to topic");
		return ret;
	}

	return 0;
}

// `object_id` = `unique id`
//
// `object_id` is set to `unique id` in order to maintain full `name` flexibility
// as by default, the `entity_id` is generated from the `name` if defined and
// `entity_id` has strict character requirements.
//
// Setting the `object_id` allows HA to use it instead of the name to
// generate the `entity_id` thus allowing name to use characters unallowed
// in an `entity_id`.
//
// https://github.com/home-assistant/core/issues/4628#:~:text=Description%20of%20problem%3A%20In%20case%20device%20has%20no%20English%20characters%20in%20the%20name%20HA%20will%20generate%20an%20empty%20entity_id%20and%20it%20will%20not%20be%20possible%20to%20access%20the%20device%20from%20HA.
// 
// object_id: Used instead of name for automatic generation of entity_id
//   https://www.home-assistant.io/integrations/sensor.mqtt/#object_id
//
// Best practice for entities with a unique_id is to set <object_id> to unique_id
//   https://www.home-assistant.io/integrations/mqtt/#discovery-messages
//
// MQTT sensor example:
// https://community.home-assistant.io/t/unique-id-with-mqtt-sensor-not-working/564315/8
//
// Other usefull links:
// https://community.home-assistant.io/t/unique-id-and-object-id-are-being-ignored-in-my-mqtt-sensor/397368/14
// https://community.home-assistant.io/t/wth-are-there-unique-id-and-entity-id/467623/9
int ha_register_sensor(struct ha_sensor *sensor)
{
	int ret;
	char brief_state_topic[HA_TOPIC_BUFFER_SIZE];
	struct ha_sensor_config ha_sensor_config = {
		.base_path = mqtt_base_path,
		.name = sensor->name,
		.unique_id = sensor->unique_id,
		.object_id = sensor->unique_id,
		.device_class = sensor->device_class,
		.state_class = sensor->state_class,
		.unit_of_measurement = sensor->unit_of_measurement,
		.suggested_display_precision = sensor->suggested_display_precision,
		.availability_topic = "~/available",
		.state_topic = brief_state_topic,
		.dev = DEVICE_CONFIG,
	};

	LOG_INF("📝 registering sensor: %s", sensor->unique_id);

	ret = snprintf(brief_state_topic, sizeof(brief_state_topic),
		       "~/%s/%s/state",
		       sensor->type, sensor->unique_id);
	if (ret < 0 && ret >= sizeof(brief_state_topic)) {
		LOG_ERR("Could not set brief_state_topic");
		return -ENOMEM;
	}

	ret = snprintf(sensor->full_state_topic, sizeof(sensor->full_state_topic),
		 "%s%s",
		 mqtt_base_path,
		 brief_state_topic + 1);
	if (ret < 0 && ret >= sizeof(brief_state_topic)) {
		LOG_ERR("Could not set full_state_topic");
		return -ENOMEM;
	}

	if (!inhibit_discovery_mode) {
		LOG_INF("📖 send discovery");
		ret = ha_send_sensor_discovery(sensor->type, &ha_sensor_config);
		if (ret < 0) {
			LOG_ERR("Could not send discovery");
			return ret;
		}
	}

	return 0;
}

int ha_add_sensor_reading(struct ha_sensor *sensor, double value)
{
	sensor->total_value += value;
	sensor->number_of_values += 1;

	return 0;
}

int ha_set_binary_sensor_state(struct ha_sensor *sensor, bool state)
{
	sensor->binary_state = state;

	return 0;
}

bool ha_get_binary_sensor_state(struct ha_sensor *sensor)
{
	return sensor->binary_state;
}

int ha_send_sensor_value(struct ha_sensor *sensor)
{
	int ret;
	char value_string[16];

	if (sensor->number_of_values == 0) {
		goto out;
	}

	ret = snprintf(value_string, sizeof(value_string),
		       "%g",
		       sensor->total_value / sensor->number_of_values);
	if (ret < 0 && ret >= sizeof(value_string)) {
		LOG_ERR("Could not set value_string");
		return -ENOMEM;
	}

	ret = mqtt_publish_to_topic(sensor->full_state_topic,
		value_string, sensor->retain);
	if (ret < 0) {
		LOG_ERR("Count not publish to topic");
		return ret;
	}

	sensor->total_value = 0;
	sensor->number_of_values = 0;

out:
	return 0;
}

int ha_send_binary_sensor_state(struct ha_sensor *sensor)
{
	int ret;

	ret = mqtt_publish_to_topic(sensor->full_state_topic,
		sensor->binary_state ? "ON" : "OFF", sensor->retain);
	if (ret < 0) {
		LOG_ERR("Count not publish to topic");
		return ret;
	}

	return 0;
}

int ha_register_trigger(struct ha_trigger *trigger)
{
	int ret;
	struct ha_trigger_config ha_trigger_config = {
		.automation_type = "trigger",
		.payload = "PRESS",
		.topic = trigger->full_topic,
		.type = trigger->type,
		.subtype = trigger->subtype,
		.dev = DEVICE_CONFIG,
	};

	LOG_INF("📝 registering trigger: %s", trigger->type);

	ret = snprintf(trigger->full_topic, sizeof(trigger->full_topic),
		 "%s/%s/%s_%s/action",
		 mqtt_base_path,
		 "trigger", trigger->type, trigger->subtype);
	if (ret < 0 && ret >= sizeof(trigger->full_topic)) {
		LOG_ERR("Could not set full_topic");
		return -ENOMEM;
	}

	if (!inhibit_discovery_mode) {
		LOG_INF("📖 send discovery");
		ret = ha_send_trigger_discovery(&ha_trigger_config);
		if (ret < 0) {
			LOG_ERR("Could not send discovery");
			return ret;
		}
	}

	return 0;
}

int ha_send_trigger_event(struct ha_trigger *trigger)
{
	int ret;

	ret = mqtt_publish_to_topic(trigger->full_topic,
		"PRESS", false);
	if (ret < 0) {
		LOG_ERR("Count not publish to topic");
		return ret;
	}

	return 0;
}
