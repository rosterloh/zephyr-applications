#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_zenoh, LOG_LEVEL_INF);

#include <zenoh-pico.h>
#include <zephyr/kernel.h>

#include "app_gimbal.h"
#include "app_ros_cdr.h"
#include "app_time.h"
#include "app_zenoh.h"

#define JOINT_STATE_KEY          CONFIG_APP_ZENOH_JOINT_STATE_KEY
#define JOINT_CMD_KEY            CONFIG_APP_ZENOH_JOINT_CMD_KEY
#define CDR_JOINT_STATE_MAX_SIZE 256
#define JOINT_STATE_INTERVAL_MS  (1000 / CONFIG_APP_ZENOH_JOINT_STATE_PUBLISH_HZ)

static z_owned_session_t _session;
static z_owned_publisher_t _pub_joint_state;
static z_owned_subscriber_t _sub_joint_cmd;
static struct k_work_delayable _joint_state_work;
static bool _joint_state_ready;
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

static void joint_cmd_handler(z_loaned_sample_t *sample, void *arg)
{
	ARG_UNUSED(arg);

	z_owned_slice_t slice;

	if (z_bytes_to_slice(z_sample_payload(sample), &slice) < 0) {
		return;
	}

	const uint8_t *buf = z_slice_data(z_loan(slice));
	size_t len = z_slice_len(z_loan(slice));
	struct app_ros_joint_command cmd;

	if (!app_ros_decode_joint_command(buf, len, &cmd)) {
		LOG_WRN("joint_commands: bad JointState payload (len %zu)", len);
		z_drop(z_move(slice));
		return;
	}
	z_drop(z_move(slice));

	app_gimbal_set_positions((float)cmd.pan_position, (float)cmd.tilt_position);
}

static bool declare_joint_cmd_subscriber(void)
{
	z_view_keyexpr_t ke;
	z_view_keyexpr_from_str_unchecked(&ke, JOINT_CMD_KEY);

	z_owned_closure_sample_t callback;
	z_closure(&callback, joint_cmd_handler, NULL, NULL);

	if (z_declare_subscriber(z_loan(_session), &_sub_joint_cmd, z_loan(ke), z_move(callback),
				 NULL) < 0) {
		LOG_ERR("zenoh subscriber declare failed for '%s'", JOINT_CMD_KEY);
		return false;
	}

	LOG_INF("zenoh subscribed to '%s'", JOINT_CMD_KEY);
	return true;
}

static void joint_state_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (!_ready || !_joint_state_ready) {
		goto reschedule;
	}

	struct app_gimbal_joint_state gimbal_joints[APP_GIMBAL_JOINT_COUNT];

	if (!app_gimbal_read_joint_state(gimbal_joints)) {
		LOG_WRN("gimbal joint feedback unavailable");
		goto reschedule;
	}

	struct app_ros_joint_sample joints[APP_GIMBAL_JOINT_COUNT];
	uint8_t buf[CDR_JOINT_STATE_MAX_SIZE];

	for (size_t i = 0; i < ARRAY_SIZE(gimbal_joints); i++) {
		joints[i] = (struct app_ros_joint_sample){
			.name = gimbal_joints[i].name,
			.position = gimbal_joints[i].position_rad,
			.velocity = gimbal_joints[i].velocity_rad_s,
		};
	}

	size_t len = app_ros_encode_joint_state(buf, sizeof(buf), app_time_now(), joints,
						ARRAY_SIZE(joints));
	if (len == 0) {
		LOG_WRN("joint_states encode failed");
	} else {
		z_owned_bytes_t payload;

		z_bytes_copy_from_buf(&payload, buf, len);
		if (z_publisher_put(z_loan(_pub_joint_state), z_move(payload), NULL) < 0) {
			LOG_WRN("joint_states publish failed");
		}
	}

reschedule:
	k_work_reschedule(&_joint_state_work, K_MSEC(JOINT_STATE_INTERVAL_MS));
}

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

	if (declare_cdr_publisher(&_pub_joint_state, JOINT_STATE_KEY)) {
		_joint_state_ready = true;
		k_work_init_delayable(&_joint_state_work, joint_state_work_handler);
	} else {
		LOG_WRN("continuing without joint_states publisher");
	}

	if (!declare_joint_cmd_subscriber()) {
		LOG_WRN("continuing without joint_commands subscription");
	}

	_ready = true;
	if (_joint_state_ready) {
		k_work_schedule(&_joint_state_work, K_NO_WAIT);
	}
	LOG_INF("zenoh ready, publishing JointState to '%s', commands on '%s'", JOINT_STATE_KEY,
		JOINT_CMD_KEY);
	return true;
}
