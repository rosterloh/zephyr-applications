#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_sensors, LOG_LEVEL_DBG);

#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>

#include "app_display.h"
#include "app_sensors.h"
#include "app_zenoh.h"

#if DT_NODE_EXISTS(DT_NODELABEL(ina219))
static const struct device *current_sensor = DEVICE_DT_GET(DT_NODELABEL(ina219));
#endif

static void publish(double v, double i, double p)
{
	LOG_INF("v=%.3f [V], i=%.3f [A], p=%.3f [W]", v, i, p);

#if IS_ENABLED(CONFIG_APP_DISPLAY)
	app_display_update_power(v, i, p);
#endif
	app_zenoh_publish_power(v, i, p);
}

void app_sensors_read_and_stream(void)
{
#if DT_NODE_EXISTS(DT_NODELABEL(ina219))
	struct sensor_value v_bus, power, current;

	if (sensor_sample_fetch(current_sensor)) {
		LOG_ERR("INA219 fetch failed");
		return;
	}

	sensor_channel_get(current_sensor, SENSOR_CHAN_VOLTAGE, &v_bus);
	sensor_channel_get(current_sensor, SENSOR_CHAN_POWER, &power);
	sensor_channel_get(current_sensor, SENSOR_CHAN_CURRENT, &current);

	publish(sensor_value_to_double(&v_bus),
		sensor_value_to_double(&current),
		sensor_value_to_double(&power));
#else
	/* Simulated data for targets without INA219 (e.g. native_sim) */
	static uint32_t tick;
	double v = 12.0 + (double)(tick % 20) / 100.0;
	double i = 0.400 + (double)(tick % 10) / 1000.0;

	publish(v, i, v * i);
	tick++;
#endif
}

void app_sensors_init(void)
{
#if DT_NODE_EXISTS(DT_NODELABEL(ina219))
	if (!device_is_ready(current_sensor)) {
		LOG_ERR("INA219 not ready");
	}
#else
	LOG_INF("INA219 not present — using simulated sensor data");
#endif
}
