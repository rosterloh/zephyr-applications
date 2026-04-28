#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_sensors, LOG_LEVEL_DBG);

#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>

#include "app_sensors.h"
#include "app_zenoh.h"

const struct device *current_sensor = DEVICE_DT_GET(DT_NODELABEL(ina219));

void app_sensors_read_and_stream(void)
{
	struct sensor_value v_bus, power, current;
	int err;

	err = sensor_sample_fetch(current_sensor);
	if (err) {
		LOG_ERR("Current sensor fetch failed: %d", err);
		return;
	}

	sensor_channel_get(current_sensor, SENSOR_CHAN_VOLTAGE, &v_bus);
	sensor_channel_get(current_sensor, SENSOR_CHAN_POWER, &power);
	sensor_channel_get(current_sensor, SENSOR_CHAN_CURRENT, &current);
	LOG_INF("v=%d.%d [V], i=%d.%d [A], p=%d.%d [W]",
		v_bus.val1, v_bus.val2,
		current.val1, current.val2,
		power.val1, power.val2);

	app_zenoh_publish_power(
		sensor_value_to_double(&v_bus),
		sensor_value_to_double(&current),
		sensor_value_to_double(&power));
}

void app_sensors_init(void)
{
	if (!device_is_ready(current_sensor)) {
		LOG_ERR("Device %s is not ready.", current_sensor->name);
	}
}
