#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_zenoh, LOG_LEVEL_INF);

#include <string.h>
#include <zenoh-pico.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include "app_zenoh.h"

#define ZENOH_CONNECT_FMT  "serial/%s?baudrate=%d"
#define BATTERY_STATE_KEY  CONFIG_APP_ZENOH_KEY_PREFIX "/battery_state"

/*
 * CDR Little-Endian encoding for sensor_msgs/msg/BatteryState.
 *
 * This is the wire format expected by the zenoh-ros2dds bridge for the ROS2
 * topic that maps to key 'rt/<ns>/battery_state'.
 *
 * Fixed layout (all fields, empty arrays, empty strings):
 *
 *  [0]   CDR LE header          4 B   { 0x00, 0x01, 0x00, 0x00 }
 *  [4]   header.stamp.sec       4 B   u32 LE = 0
 *  [8]   header.stamp.nanosec   4 B   u32 LE = 0
 *  [12]  header.frame_id len    4 B   u32 LE = 1  (empty string: length incl. '\0')
 *  [16]  header.frame_id data   1 B   '\0'
 *  [17]  padding                3 B
 *  [20]  voltage                4 B   f32 LE
 *  [24]  temperature            4 B   f32 LE = NaN
 *  [28]  current                4 B   f32 LE
 *  [32]  charge                 4 B   f32 LE = NaN
 *  [36]  capacity               4 B   f32 LE = NaN
 *  [40]  design_capacity        4 B   f32 LE = NaN
 *  [44]  percentage             4 B   f32 LE = NaN
 *  [48]  power_supply_status    1 B   2 = DISCHARGING
 *  [49]  power_supply_health    1 B   2 = GOOD
 *  [50]  power_supply_technology 1 B  0 = UNKNOWN
 *  [51]  present                1 B   1 = true
 *  [52]  cell_voltage count     4 B   u32 LE = 0
 *  [56]  cell_temperature count 4 B   u32 LE = 0
 *  [60]  location len           4 B   u32 LE = 1
 *  [64]  location data          1 B   '\0'
 *  [65]  padding                3 B
 *  [68]  serial_number len      4 B   u32 LE = 1
 *  [72]  serial_number data     1 B   '\0'
 *  Total: 73 bytes
 */
#define CDR_BATTERY_STATE_SIZE 73

/* IEEE 754 quiet NaN for float32 (little-endian) */
static const uint8_t F32_NAN[4] = {0x00, 0x00, 0xC0, 0x7F};

static void write_u32_le(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v);
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static size_t encode_battery_state(uint8_t *buf, float voltage, float current)
{
	memset(buf, 0, CDR_BATTERY_STATE_SIZE);

	/* CDR LE header */
	buf[0] = 0x00; buf[1] = 0x01; buf[2] = 0x00; buf[3] = 0x00;

	/* Header.stamp = {0, 0} — already zero */

	/* Header.frame_id = "" */
	write_u32_le(buf + 12, 1);    /* length = 1 (null terminator only) */
	/* buf[16] = '\0' — already zero; buf[17..19] padding — already zero */

	/* float32 fields */
	memcpy(buf + 20, &voltage, 4);
	memcpy(buf + 24, F32_NAN, 4); /* temperature */
	memcpy(buf + 28, &current, 4);
	memcpy(buf + 32, F32_NAN, 4); /* charge */
	memcpy(buf + 36, F32_NAN, 4); /* capacity */
	memcpy(buf + 40, F32_NAN, 4); /* design_capacity */
	memcpy(buf + 44, F32_NAN, 4); /* percentage */

	/* status bytes */
	buf[48] = 2; /* POWER_SUPPLY_STATUS_DISCHARGING */
	buf[49] = 2; /* POWER_SUPPLY_HEALTH_GOOD */
	buf[50] = 0; /* POWER_SUPPLY_TECHNOLOGY_UNKNOWN */
	buf[51] = 1; /* present = true */

	/* cell_voltage / cell_temperature counts = 0 — already zero */

	/* location = "" */
	write_u32_le(buf + 60, 1);
	/* buf[64] = '\0'; buf[65..67] padding — already zero */

	/* serial_number = "" */
	write_u32_le(buf + 68, 1);
	/* buf[72] = '\0' — already zero */

	return CDR_BATTERY_STATE_SIZE;
}

static z_owned_session_t _session;
static z_owned_publisher_t _pub_battery;
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

	/* Declare publisher with CDR encoding for zenoh-ros2dds bridge */
	z_view_keyexpr_t ke;
	z_view_keyexpr_from_str_unchecked(&ke, BATTERY_STATE_KEY);

	z_publisher_options_t pub_opts;
	z_publisher_options_default(&pub_opts);

	z_owned_encoding_t enc;
	z_encoding_from_str(&enc, "application/cdr");
	pub_opts.encoding = z_move(enc);

	if (z_declare_publisher(z_loan(_session), &_pub_battery, z_loan(ke), &pub_opts) < 0) {
		LOG_ERR("zenoh publisher declare failed for '%s'", BATTERY_STATE_KEY);
		z_drop(z_move(_session));
		return false;
	}

	LOG_INF("zenoh ready, publishing sensor_msgs/BatteryState to '%s'",
		BATTERY_STATE_KEY);
	_ready = true;
	return true;
}

void app_zenoh_publish_power(double voltage, double current, double power)
{
	if (!_ready) {
		return;
	}

	uint8_t buf[CDR_BATTERY_STATE_SIZE];
	size_t len = encode_battery_state(buf, (float)voltage, (float)current);

	z_owned_bytes_t payload;
	z_bytes_copy_from_buf(&payload, buf, len);

	if (z_publisher_put(z_loan(_pub_battery), z_move(payload), NULL) < 0) {
		LOG_WRN("zenoh publish failed");
	}
}
