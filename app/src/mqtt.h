#ifndef MQTT_H_
#define MQTT_H_

#include <net/net_ip.h>


extern char mqtt_server_addr[NET_IPV6_ADDR_LEN];

int mqtt_publisher(void);

#endif /* MQTT_H_ */