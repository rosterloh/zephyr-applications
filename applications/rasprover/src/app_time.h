#ifndef APP_TIME_H
#define APP_TIME_H

#include <stdbool.h>
#include <zephyr/kernel.h>

#include "app_ros_cdr.h"

#if IS_ENABLED(CONFIG_APP_TIME_SYNC)
void app_time_start(void);
bool app_time_synced(void);
struct app_ros_time app_time_now(void);
#else
static inline void app_time_start(void)
{
}

static inline bool app_time_synced(void)
{
	return false;
}

static inline struct app_ros_time app_time_now(void)
{
	return (struct app_ros_time){0};
}
#endif

#endif /* APP_TIME_H */
