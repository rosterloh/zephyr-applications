#ifndef APP_ZENOH_H
#define APP_ZENOH_H

#include <stdbool.h>

#if IS_ENABLED(CONFIG_APP_ZENOH)
bool app_zenoh_init(void);
void app_zenoh_publish_power(double voltage, double current, double power);
#else
static inline bool app_zenoh_init(void)                              { return false; }
static inline void app_zenoh_publish_power(double v, double i, double p) { }
#endif

#endif /* APP_ZENOH_H */
