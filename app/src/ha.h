#ifndef HA_H_
#define HA_H_

#include <uid.h>

#define HA_TOPIC_BUFFER_SIZE		128

#define HA_SENSOR_TYPE				"sensor"
#define HA_BINARY_SENSOR_TYPE		"binary_sensor"

struct ha_sensor {
	// Set by user
	const char *type;
	const char *name;
	char unique_id[UID_UNIQUE_ID_STRING_SIZE];
	const char *device_class;
	const char *state_class;
	const char *unit_of_measurement;
	int suggested_display_precision;
	bool retain;

	// Internal use
	double total_value;
	int number_of_values;
	bool binary_state;

	char full_state_topic[HA_TOPIC_BUFFER_SIZE];
};

struct ha_trigger {
	// Set by user
	const char *type;
	const char *subtype;

	char full_topic[HA_TOPIC_BUFFER_SIZE];
};

int ha_start(const char *device_id, bool inhibit_discovery);
int ha_set_online();

int ha_init_sensor(struct ha_sensor *);
int ha_init_binary_sensor(struct ha_sensor *);

int ha_register_sensor(struct ha_sensor *);

int ha_add_sensor_reading(struct ha_sensor *, double value);
int ha_set_binary_sensor_state(struct ha_sensor *, bool state);

bool ha_get_binary_sensor_state(struct ha_sensor *);

int ha_send_sensor_value(struct ha_sensor *);
int ha_send_binary_sensor_state(struct ha_sensor *);

int ha_register_trigger(struct ha_trigger *);
int ha_send_trigger_event(struct ha_trigger *);

#endif /* HA_H_ */