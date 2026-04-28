#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_zenoh, LOG_LEVEL_INF);

#include <stdio.h>
#include <zenoh-pico.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include "app_zenoh.h"

#define ZENOH_CONNECT_FMT "serial/%s?baudrate=%d"
#define POWER_KEY         CONFIG_APP_ZENOH_KEY_PREFIX "/power"

static z_owned_session_t _session;
static z_owned_publisher_t _pub_power;
static bool _ready;

bool app_zenoh_init(void)
{
	const struct device *uart = device_get_binding(CONFIG_APP_ZENOH_UART_DEVICE);

	if (uart == NULL) {
		LOG_ERR("zenoh UART '%s' not found", CONFIG_APP_ZENOH_UART_DEVICE);
		return false;
	}

	char locator[64];
	snprintf(locator, sizeof(locator), ZENOH_CONNECT_FMT,
		 uart->name, CONFIG_APP_ZENOH_SERIAL_BAUDRATE);
	LOG_INF("zenoh connecting via %s", locator);

	z_owned_config_t cfg;
	z_config_default(&cfg);
	zp_config_insert(z_loan_mut(cfg), Z_CONFIG_MODE_KEY, "client");
	zp_config_insert(z_loan_mut(cfg), Z_CONFIG_CONNECT_KEY, locator);

	if (z_open(&_session, z_move(cfg), NULL) < 0) {
		LOG_ERR("zenoh session open failed");
		return false;
	}

	zp_start_read_task(z_loan_mut(_session), NULL);
	zp_start_lease_task(z_loan_mut(_session), NULL);

	z_view_keyexpr_t ke;
	z_view_keyexpr_from_str_unchecked(&ke, POWER_KEY);

	if (z_declare_publisher(z_loan(_session), &_pub_power, z_loan(ke), NULL) < 0) {
		LOG_ERR("zenoh publisher declare failed for '%s'", POWER_KEY);
		z_drop(z_move(_session));
		return false;
	}

	LOG_INF("zenoh ready, publishing to '%s'", POWER_KEY);
	_ready = true;
	return true;
}

void app_zenoh_publish_power(double voltage, double current, double power)
{
	if (!_ready) {
		return;
	}

	char buf[64];
	int len = snprintf(buf, sizeof(buf),
			   "{\"v\":%.3f,\"i\":%.3f,\"p\":%.3f}",
			   voltage, current, power);

	z_owned_bytes_t payload;
	z_bytes_copy_from_buf(&payload, (const uint8_t *)buf, len);

	if (z_publisher_put(z_loan(_pub_power), z_move(payload), NULL) < 0) {
		LOG_WRN("zenoh publish failed");
	}
}
