#ifndef APP_TIME_H
#define APP_TIME_H

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>

/* builtin_interfaces/msg/Time, in the shape app_time keeps it: seconds and
 * nanoseconds since the UNIX epoch, or {0, 0} before the first SNTP sync.
 * Deliberately not picoserdes's ros_Time -- app_time only reads the clock and
 * should not drag the serialization layer into every translation unit that
 * wants a timestamp.
 */
struct app_ros_time {
	uint32_t sec;
	uint32_t nanosec;
};

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
