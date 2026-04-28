#ifndef APP_ZENOH_H
#define APP_ZENOH_H

#include <stdbool.h>

bool app_zenoh_init(void);
void app_zenoh_publish_power(double voltage, double current, double power);

#endif /* APP_ZENOH_H */
