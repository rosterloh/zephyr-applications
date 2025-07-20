#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_sensors, LOG_LEVEL_DBG);

#include <golioth/stream.h>
#include <zcbor_encode.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>

#include "app_sensors.h"

static struct golioth_client *client;

const struct device *current_sensor = DEVICE_DT_GET(DT_NODELABEL(ina219));

/* Callback for LightDB Stream */
static void async_error_handler(struct golioth_client *client, enum golioth_status status,
				const struct golioth_coap_rsp_code *coap_rsp_code, const char *path,
				void *arg)
{
	if (status != GOLIOTH_OK) {
		LOG_ERR("Async task failed: %d", status);
		return;
	}
}

void app_sensors_read_and_stream(void)
{
	struct sensor_value v_bus, power, current;
	int err;

	/* Read all sensors */
	err = sensor_sample_fetch(current_sensor);
	if (err) {
		LOG_ERR("Current sensor fetch failed: %d", err);
	}

	sensor_channel_get(current_sensor, SENSOR_CHAN_VOLTAGE, &v_bus);
	sensor_channel_get(current_sensor, SENSOR_CHAN_POWER, &power);
	sensor_channel_get(current_sensor, SENSOR_CHAN_CURRENT, &current);
	LOG_INF("v=%d.%d [V], i=%d.%d [A], p=%d.%d [W]", v_bus.val1, v_bus.val2, current.val2,
		current.val2, power.val1, power.val2);

	/* Only stream sensor data if connected */
	if (golioth_client_is_connected(client)) {
		/* Encode sensor data using CBOR serialization */
		uint8_t cbor_buf[13];

		ZCBOR_STATE_E(zse, 1, cbor_buf, sizeof(cbor_buf), 1);

		bool ok = zcbor_map_start_encode(zse, 1) && zcbor_tstr_put_lit(zse, "current") &&
			  zcbor_float32_put(zse, sensor_value_to_double(&current)) &&
			  zcbor_map_end_encode(zse, 1);

		if (!ok) {
			LOG_ERR("Failed to encode CBOR.");
			return;
		}

		size_t cbor_size = zse->payload - cbor_buf;

		/* Stream data to Golioth */
		err = golioth_stream_set_async(client, "sensor", GOLIOTH_CONTENT_TYPE_CBOR,
					       cbor_buf, cbor_size, async_error_handler, NULL);
		if (err) {
			LOG_ERR("Failed to send sensor data to Golioth: %d", err);
		}
	} else {
		LOG_DBG("No connection available, skipping streaming data");
	}
}

void app_sensors_set_client(struct golioth_client *sensors_client)
{
	client = sensors_client;
}

void app_sensors_init(void)
{
	if (!device_is_ready(current_sensor)) {
		LOG_ERR("Device %s is not ready.", current_sensor->name);
	}
}