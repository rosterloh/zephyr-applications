#ifndef APP_PICOROS_H
#define APP_PICOROS_H

#include <stdbool.h>

#if IS_ENABLED(CONFIG_APP_PICOROS)
bool app_picoros_init(void);
void app_picoros_publish_power(double voltage, double current, double power);
#else
static inline bool app_picoros_init(void)
{
	return false;
}
static inline void app_picoros_publish_power(double v, double i, double p)
{
}
#endif

#endif /* APP_PICOROS_H */
