#ifndef INIT_H_
#define INIT_H_

int init_watchdog(const struct device *wdt,
		  int *main_channel_id, int *mqtt_channel_id);

#endif /* INIT_H_ */