#ifndef __APP_SENSORS_H__
#define __APP_SENSORS_H__

#include <stdint.h>
#include <golioth/client.h>

void app_sensors_set_client(struct golioth_client *sensors_client);
void app_sensors_read_and_stream(void);
void app_sensors_init(void);

#endif /* __APP_SENSORS_H__ */