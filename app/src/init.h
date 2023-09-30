#ifndef INIT_H_
#define INIT_H_

// #include "hvac.h"
// #include "temphum24.h"


int init_watchdog(const struct device *wdt,
		  int *main_channel_id, int *mqtt_channel_id);
// int init_temphum24_click(temphum24_t *temphum24);
// int init_hvac_click(hvac_t *hvac);

#endif /* INIT_H_ */