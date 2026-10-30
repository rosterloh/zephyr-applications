#ifndef __APP_SENSORS_H__
#define __APP_SENSORS_H__

#include <stdbool.h>
#include <stdint.h>

struct app_power {
	int32_t voltage_mv;
	int32_t current_ma;
	int32_t power_mw;
	int64_t uptime_ms; /* when it was read */
};

void app_sensors_read_and_stream(void);
void app_sensors_init(void);
/* Copy of the last power reading; false before the first one. */
bool app_sensors_last_power(struct app_power *out);

#endif /* __APP_SENSORS_H__ */
