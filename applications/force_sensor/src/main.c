#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <app_version.h>
#include <drivers/sensor/singletact.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main);

const struct device *force_sensor = DEVICE_DT_GET(DT_NODELABEL(singletact));

int main(void)
{
	struct sensor_value val;
	uint16_t raw, baseline;
	float force;
	int err;

	LOG_INF("Force Sensor %s", APP_VERSION_STRING);

	if (!device_is_ready(force_sensor)) {
		LOG_ERR("Device %s is not ready.", force_sensor->name);
		return -ENODEV;
	}

	while (1) {
		/* Read all sensors */
		err = sensor_sample_fetch(force_sensor);
		if (err) {
			LOG_ERR("Force sensor fetch failed: %d", err);
		}

		sensor_channel_get(force_sensor, SENSOR_CHAN_RAW, &val);
		raw = val.val1;
		baseline = val.val2;

		sensor_channel_get(force_sensor, SENSOR_CHAN_FORCE, &val);
		force = sensor_value_to_float(&val);

		LOG_RAW("%d,%d,%.6f\n", raw, baseline, (double)force);

		k_sleep(K_MSEC(50));
	}

	return 0;
}