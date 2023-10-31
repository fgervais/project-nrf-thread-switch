#ifndef MQTT_H_
#define MQTT_H_

#include <zephyr/device.h>
#include <zephyr/net/net_ip.h>


struct mqtt_subscription {
	const char *topic;
	void (*callback)(const char *);
};

int mqtt_publish_to_topic(const char *topic, char *payload, bool retain);
int mqtt_subscribe_to_topic(const struct mqtt_subscription *subs,
			    size_t nb_of_subs);
int mqtt_watchdog_init(const struct device *watchdog, int channel_id);
int mqtt_init(const char *dev_id,
	      const char *last_will_topic_string,
	      const char *last_will_message_string);

#endif /* MQTT_H_ */