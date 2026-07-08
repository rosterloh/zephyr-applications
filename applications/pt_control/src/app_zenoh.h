#ifndef APP_ZENOH_H
#define APP_ZENOH_H

#include <stdbool.h>

#if IS_ENABLED(CONFIG_APP_ZENOH)
bool app_zenoh_init(void);
#else
static inline bool app_zenoh_init(void)
{
	return false;
}
#endif

#endif /* APP_ZENOH_H */
