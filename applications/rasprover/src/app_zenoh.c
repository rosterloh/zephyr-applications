#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_zenoh, LOG_LEVEL_INF);

#include <string.h>
#include <zenoh-pico.h>
#include <zephyr/kernel.h>

#include "app_ros_cdr.h"
#include "app_time.h"
#include "app_zenoh.h"

#ifdef CONFIG_APP_MOTORS
#include "app_motors.h"
#endif

#define BATTERY_STATE_KEY          CONFIG_APP_ZENOH_KEY_PREFIX "/battery_state"
#define GIMBAL_CMD_KEY             CONFIG_APP_ZENOH_GIMBAL_CMD_KEY
#define CDR_BATTERY_STATE_MAX_SIZE 80

#ifdef CONFIG_APP_MOTORS
#define CMD_VEL_KEY              CONFIG_APP_ZENOH_KEY_PREFIX "/cmd_vel"
#define JOINT_STATE_KEY          CONFIG_APP_ZENOH_JOINT_STATE_KEY
#define CDR_JOINT_STATE_MAX_SIZE 160
#define JOINT_STATE_INTERVAL_MS  (1000 / CONFIG_APP_ZENOH_JOINT_STATE_PUBLISH_HZ)
#endif

static z_owned_session_t _session;
static z_owned_publisher_t _pub_battery;
#ifdef CONFIG_APP_MOTORS
static z_owned_publisher_t _pub_joint_state;
static z_owned_subscriber_t _sub_cmd_vel;
static struct k_work_delayable _joint_state_work;
static bool _joint_state_ready;
#endif
static bool _ready;

static bool declare_cdr_publisher(z_owned_publisher_t *pub, const char *key)
{
	z_view_keyexpr_t ke;
	z_view_keyexpr_from_str_unchecked(&ke, key);

	z_publisher_options_t pub_opts;
	z_publisher_options_default(&pub_opts);

	z_owned_encoding_t enc;
	z_encoding_from_str(&enc, "application/cdr");
	pub_opts.encoding = z_move(enc);

	if (z_declare_publisher(z_loan(_session), pub, z_loan(ke), &pub_opts) < 0) {
		LOG_ERR("zenoh publisher declare failed for '%s'", key);
		return false;
	}

	return true;
}

#ifdef CONFIG_APP_MOTORS
/*
 * CDR Little-Endian geometry_msgs/msg/Twist as produced by the
 * zenoh-ros2dds bridge:
 *
 *  [0]  CDR LE header  4 B  { 0x00, 0x01, 0x00, 0x00 }
 *  [4]  linear.x       8 B  f64 LE
 *  [12] linear.y       8 B  f64 LE
 *  [20] linear.z       8 B  f64 LE
 *  [28] angular.x      8 B  f64 LE
 *  [36] angular.y      8 B  f64 LE
 *  [44] angular.z      8 B  f64 LE
 *  Total: 52 bytes
 */
#define CDR_TWIST_SIZE 52

static void cmd_vel_handler(z_loaned_sample_t *sample, void *arg)
{
	ARG_UNUSED(arg);

	z_owned_slice_t slice;

	if (z_bytes_to_slice(z_sample_payload(sample), &slice) < 0) {
		return;
	}

	const uint8_t *buf = z_slice_data(z_loan(slice));
	size_t len = z_slice_len(z_loan(slice));

	if (len < CDR_TWIST_SIZE || buf[1] != 0x01) {
		LOG_WRN("cmd_vel: bad payload (len %zu)", len);
		z_drop(z_move(slice));
		return;
	}

	/* ESP32 is little-endian; CDR LE doubles can be memcpy'd directly. */
	double linear_x, angular_z;

	memcpy(&linear_x, buf + 4, sizeof(linear_x));
	memcpy(&angular_z, buf + 44, sizeof(angular_z));
	z_drop(z_move(slice));

	app_motors_cmd_vel((float)linear_x, (float)angular_z);
}

static bool declare_cmd_vel_subscriber(void)
{
	z_view_keyexpr_t ke;
	z_view_keyexpr_from_str_unchecked(&ke, CMD_VEL_KEY);

	z_owned_closure_sample_t callback;
	z_closure(&callback, cmd_vel_handler, NULL, NULL);

	if (z_declare_subscriber(z_loan(_session), &_sub_cmd_vel, z_loan(ke), z_move(callback),
				 NULL) < 0) {
		LOG_ERR("zenoh subscriber declare failed for '%s'", CMD_VEL_KEY);
		return false;
	}

	LOG_INF("zenoh subscribed to '%s'", CMD_VEL_KEY);
	return true;
}

static void joint_state_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (_ready && _joint_state_ready) {
		struct app_motors_joint_state motor_joints[APP_MOTORS_JOINT_COUNT];

		if (app_motors_read_joint_state(motor_joints)) {
			struct app_ros_joint_sample joints[APP_MOTORS_JOINT_COUNT];
			uint8_t buf[CDR_JOINT_STATE_MAX_SIZE];

			for (size_t i = 0; i < ARRAY_SIZE(joints); i++) {
				joints[i] = (struct app_ros_joint_sample){
					.name = motor_joints[i].name,
					.position = motor_joints[i].position_rad,
					.velocity = motor_joints[i].velocity_rad_s,
				};
			}

			size_t len = app_ros_encode_joint_state(buf, sizeof(buf), app_time_now(),
								joints, ARRAY_SIZE(joints));
			if (len == 0) {
				LOG_WRN("joint_states encode failed");
			} else {
				z_owned_bytes_t payload;

				z_bytes_copy_from_buf(&payload, buf, len);
				if (z_publisher_put(z_loan(_pub_joint_state), z_move(payload),
						    NULL) < 0) {
					LOG_WRN("joint_states publish failed");
				}
			}
		}
	}

	k_work_reschedule(&_joint_state_work, K_MSEC(JOINT_STATE_INTERVAL_MS));
}
#endif /* CONFIG_APP_MOTORS */

bool app_zenoh_init(void)
{
	LOG_INF("zenoh connecting via %s", CONFIG_APP_ZENOH_LOCATOR);

	z_owned_config_t cfg;
	z_config_default(&cfg);
	zp_config_insert(z_loan_mut(cfg), Z_CONFIG_MODE_KEY, "client");
	zp_config_insert(z_loan_mut(cfg), Z_CONFIG_CONNECT_KEY, CONFIG_APP_ZENOH_LOCATOR);

	if (z_open(&_session, z_move(cfg), NULL) < 0) {
		LOG_ERR("zenoh session open failed");
		return false;
	}

	zp_start_read_task(z_loan_mut(_session), NULL);
	zp_start_lease_task(z_loan_mut(_session), NULL);

	if (!declare_cdr_publisher(&_pub_battery, BATTERY_STATE_KEY)) {
		z_drop(z_move(_session));
		return false;
	}

#ifdef CONFIG_APP_MOTORS
	if (declare_cdr_publisher(&_pub_joint_state, JOINT_STATE_KEY)) {
		_joint_state_ready = true;
		k_work_init_delayable(&_joint_state_work, joint_state_work_handler);
	} else {
		LOG_WRN("continuing without joint_states publisher");
	}

	if (!declare_cmd_vel_subscriber()) {
		LOG_WRN("continuing without cmd_vel subscription");
	}
#endif

	_ready = true;
#ifdef CONFIG_APP_MOTORS
	if (_joint_state_ready) {
		k_work_schedule(&_joint_state_work, K_NO_WAIT);
	}
	LOG_INF("zenoh ready, publishing BatteryState to '%s' and JointState to '%s'",
		BATTERY_STATE_KEY, JOINT_STATE_KEY);
#else
	LOG_INF("zenoh ready, publishing BatteryState to '%s'", BATTERY_STATE_KEY);
#endif
	return true;
}

void app_zenoh_publish_power(double voltage, double current, double power)
{
	ARG_UNUSED(power);

	if (!_ready) {
		return;
	}

	uint8_t buf[CDR_BATTERY_STATE_MAX_SIZE];
	size_t len = app_ros_encode_battery_state(buf, sizeof(buf), app_time_now(), (float)voltage,
						  (float)current);

	if (len == 0) {
		LOG_WRN("battery_state encode failed");
		return;
	}

	z_owned_bytes_t payload;
	z_bytes_copy_from_buf(&payload, buf, len);

	if (z_publisher_put(z_loan(_pub_battery), z_move(payload), NULL) < 0) {
		LOG_WRN("zenoh publish failed");
	}
}
