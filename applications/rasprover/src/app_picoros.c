#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_picoros, LOG_LEVEL_INF);

#include <math.h>
#include <string.h>
#include <zephyr/kernel.h>

#include <picoros.h>
#include <picoserdes.h>

#include "app_picoros.h"
#include "app_time.h"

#ifdef CONFIG_APP_GIMBAL
#include "app_gimbal.h"
#endif

#ifdef CONFIG_APP_MOTORS
#include "app_motors.h"
#endif

#define BATTERY_STATE_TOPIC CONFIG_APP_PICOROS_BATTERY_STATE_TOPIC
#define GIMBAL_CMD_TOPIC    CONFIG_APP_PICOROS_GIMBAL_CMD_TOPIC

#ifdef CONFIG_APP_MOTORS
#define CMD_VEL_TOPIC           CONFIG_APP_PICOROS_CMD_VEL_TOPIC
#define JOINT_STATE_TOPIC       CONFIG_APP_PICOROS_JOINT_STATE_TOPIC
#define JOINT_STATE_INTERVAL_MS (1000 / CONFIG_APP_PICOROS_JOINT_STATE_PUBLISH_HZ)
#ifdef CONFIG_APP_GIMBAL
#define APP_JOINT_COUNT (APP_MOTORS_JOINT_COUNT + APP_GIMBAL_JOINT_COUNT)
#else
#define APP_JOINT_COUNT APP_MOTORS_JOINT_COUNT
#endif
#endif /* CONFIG_APP_MOTORS */

/* Serialization scratch.
 *
 * picoros_publish() hands the buffer to zenoh-pico with
 * z_bytes_from_static_buf(), i.e. without copying, and z_publisher_put() does
 * not return until the batch has been written to the socket -- so a buffer
 * that outlives the call is enough. The two publishers run on different
 * threads (main for battery, the joint-state workqueue), so they get one
 * buffer each rather than sharing one.
 *
 * ps_serialize() writes the 4-byte CDR encapsulation header as a uint32_t
 * store, so the buffer must be 4-aligned; an array of uint8_t is not
 * guaranteed to be, and an unaligned 32-bit store faults on Xtensa.
 *
 * Sizes are worst cases, not guesses, because ps_serialize() cannot report
 * overflow: Micro-CDR raises a flag on its writer and stops advancing, while
 * ps_serialize() returns the truncated length and throws the writer away. A
 * buffer that is too small therefore publishes a silently truncated message.
 *
 * BatteryState with empty frame_id, location and serial_number and no cell
 * arrays is exactly 73 bytes (see tests/picoserdes for the byte-by-byte
 * derivation).
 *
 * Both buffers are zeroed before every encode. Micro-CDR's ucdr_align_to()
 * skips CDR padding by advancing the iterator rather than writing it, so the
 * pad bytes of each message would otherwise carry whatever the previous
 * message left there -- leaking fragments of old payloads onto the network for
 * no benefit. (The old hand-rolled encoder zero-filled its padding, so this
 * also keeps the wire bytes identical to what the golden-byte tests pin.)
 */
#define BATTERY_STATE_BUF_SIZE 128
static uint8_t _battery_buf[BATTERY_STATE_BUF_SIZE] __aligned(4);

static picoros_node_t _node = {
	.name = CONFIG_APP_PICOROS_NODE_NAME,
	.domain_id = CONFIG_APP_PICOROS_DOMAIN_ID,
};

static picoros_publisher_t _pub_battery = {
	.topic =
		{
			.name = BATTERY_STATE_TOPIC,
			.type = ROSTYPE_NAME(ros_BatteryState),
			.rihs_hash = ROSTYPE_HASH(ros_BatteryState),
		},
};

#ifdef CONFIG_APP_MOTORS
/* JointState with the four joint names this firmware publishes
 * (left_wheel_joint, right_wheel_joint, pan_joint, tilt_joint), a stamp, an
 * empty frame_id, and position and velocity sequences but no effort, is 184
 * bytes. 320 leaves room for a fifth joint or longer names.
 */
#define JOINT_STATE_BUF_SIZE 320
static uint8_t _joint_state_buf[JOINT_STATE_BUF_SIZE] __aligned(4);

static picoros_publisher_t _pub_joint_state = {
	.topic =
		{
			.name = JOINT_STATE_TOPIC,
			.type = ROSTYPE_NAME(ros_JointState),
			.rihs_hash = ROSTYPE_HASH(ros_JointState),
		},
};

static struct k_work_delayable _joint_state_work;
static bool _joint_state_ready;
#endif

static bool _ready;

/* -------------------------------------------------------------------------
 * Subscribers
 *
 * picoros invokes these from the zenoh-pico read task with a heap buffer it
 * frees on return, and picoserdes deserializes rstring fields in place --
 * pointers into that buffer. Nothing here may outlive the callback.
 * -------------------------------------------------------------------------
 */

#ifdef CONFIG_APP_GIMBAL
/* Longest JointState command we will look at. A publisher that sends more
 * joints than this still deserializes: ps_deserialize reports n_deserialized
 * up to the capacity in n_elements and drops the rest.
 */
#define GIMBAL_CMD_MAX_JOINTS 8

static void gimbal_cmd_handler(uint8_t *rx_data, size_t data_len)
{
	rstring names[GIMBAL_CMD_MAX_JOINTS];
	double positions[GIMBAL_CMD_MAX_JOINTS];
	double velocities[GIMBAL_CMD_MAX_JOINTS];
	double efforts[GIMBAL_CMD_MAX_JOINTS];
	ros_JointState cmd = {
		.name = {.data = names, .n_elements = ARRAY_SIZE(names)},
		.position = {.data = positions, .n_elements = ARRAY_SIZE(positions)},
		.velocity = {.data = velocities, .n_elements = ARRAY_SIZE(velocities)},
		.effort = {.data = efforts, .n_elements = ARRAY_SIZE(efforts)},
	};

	if (!ps_deserialize(rx_data, &cmd, data_len)) {
		LOG_WRN("gimbal_cmd: bad JointState payload (len %zu)", data_len);
		return;
	}

	/* Positions are matched to joints by name, not by index: a ROS
	 * publisher is free to order or omit them.
	 */
	bool have_pan = false, have_tilt = false;
	float pan = 0.0f, tilt = 0.0f;
	uint32_t count = MIN(cmd.name.n_deserialized, cmd.position.n_deserialized);

	for (uint32_t i = 0; i < count; i++) {
		if (cmd.name.data[i] == NULL) {
			continue;
		}
		if (strcmp(cmd.name.data[i], "pan_joint") == 0) {
			pan = (float)cmd.position.data[i];
			have_pan = true;
		} else if (strcmp(cmd.name.data[i], "tilt_joint") == 0) {
			tilt = (float)cmd.position.data[i];
			have_tilt = true;
		}
	}

	/* Both joints required, as before the pico-ros switch: a partial
	 * command has no obvious meaning for a pan/tilt head, and the previous
	 * decoder rejected it outright rather than inventing the missing axis.
	 */
	if (!have_pan || !have_tilt) {
		LOG_WRN("gimbal_cmd: need both pan_joint and tilt_joint (%u name(s))", count);
		return;
	}

	app_gimbal_set_positions(pan, tilt);
}

static picoros_subscriber_t _sub_gimbal_cmd = {
	.topic =
		{
			.name = GIMBAL_CMD_TOPIC,
			.type = ROSTYPE_NAME(ros_JointState),
			.rihs_hash = ROSTYPE_HASH(ros_JointState),
		},
	.user_callback = gimbal_cmd_handler,
};
#endif /* CONFIG_APP_GIMBAL */

#ifdef CONFIG_APP_MOTORS
static void cmd_vel_handler(uint8_t *rx_data, size_t data_len)
{
	ros_Twist twist;

	if (!ps_deserialize(rx_data, &twist, data_len)) {
		LOG_WRN("cmd_vel: bad Twist payload (len %zu)", data_len);
		return;
	}

	app_motors_cmd_vel((float)twist.linear.x, (float)twist.angular.z);
}

static picoros_subscriber_t _sub_cmd_vel = {
	.topic =
		{
			.name = CMD_VEL_TOPIC,
			.type = ROSTYPE_NAME(ros_Twist),
			.rihs_hash = ROSTYPE_HASH(ros_Twist),
		},
	.user_callback = cmd_vel_handler,
};

static void joint_state_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (!_ready || !_joint_state_ready) {
		goto reschedule;
	}

	struct app_motors_joint_state motor_joints[APP_MOTORS_JOINT_COUNT];

	if (!app_motors_read_joint_state(motor_joints)) {
		goto reschedule;
	}

	/* rstring is char*, but picoserdes only ever reads a serialized string.
	 * The joint names are string literals owned by app_motors/app_gimbal.
	 */
	rstring names[APP_JOINT_COUNT];
	double positions[APP_JOINT_COUNT];
	double velocities[APP_JOINT_COUNT];
	uint32_t joint_count = 0;

	for (size_t i = 0; i < ARRAY_SIZE(motor_joints); i++) {
		names[joint_count] = (rstring)motor_joints[i].name;
		positions[joint_count] = motor_joints[i].position_rad;
		velocities[joint_count] = motor_joints[i].velocity_rad_s;
		joint_count++;
	}

#ifdef CONFIG_APP_GIMBAL
	struct app_gimbal_joint_state gimbal_joints[APP_GIMBAL_JOINT_COUNT];

	if (!app_gimbal_read_joint_state(gimbal_joints)) {
		LOG_WRN("gimbal joint feedback unavailable");
		goto reschedule;
	}
	for (size_t i = 0; i < ARRAY_SIZE(gimbal_joints); i++) {
		names[joint_count] = (rstring)gimbal_joints[i].name;
		positions[joint_count] = gimbal_joints[i].position_rad;
		velocities[joint_count] = gimbal_joints[i].velocity_rad_s;
		joint_count++;
	}
#endif

	struct app_ros_time stamp = app_time_now();
	ros_JointState msg = {
		.header =
			{
				.stamp = {.sec = (int32_t)stamp.sec, .nanosec = stamp.nanosec},
				.frame_id = "",
			},
		.name = {.data = names, .n_elements = joint_count},
		.position = {.data = positions, .n_elements = joint_count},
		.velocity = {.data = velocities, .n_elements = joint_count},
		.effort = {.data = NULL, .n_elements = 0},
	};

	memset(_joint_state_buf, 0, sizeof(_joint_state_buf));

	size_t len = ps_serialize(_joint_state_buf, &msg, sizeof(_joint_state_buf));

	if (len == 0) {
		LOG_WRN("joint_states encode failed");
	} else if (picoros_publish(&_pub_joint_state, _joint_state_buf, len) != PICOROS_OK) {
		LOG_WRN("joint_states publish failed");
	}

reschedule:
	k_work_reschedule(&_joint_state_work, K_MSEC(JOINT_STATE_INTERVAL_MS));
}
#endif /* CONFIG_APP_MOTORS */

bool app_picoros_init(void)
{
	picoros_interface_t ifx = {
		.mode = "client",
		.locator = CONFIG_APP_PICOROS_LOCATOR,
	};

	LOG_INF("pico-ros connecting via %s", ifx.locator);

	/* One attempt, not upstream's retry-until-ready loop: main() runs under
	 * the boot watchdog channel, and app_net_connect() has already
	 * established that the network is up. A router that is not listening
	 * yet is better surfaced as a failed boot than as a silent spin.
	 */
	if (picoros_interface_init(&ifx) != PICOROS_OK) {
		LOG_ERR("pico-ros interface init failed");
		return false;
	}

	if (picoros_node_init(&_node) != PICOROS_OK) {
		LOG_ERR("pico-ros node init failed");
		picoros_interface_close();
		return false;
	}

	if (picoros_publisher_declare(&_node, &_pub_battery) != PICOROS_OK) {
		LOG_ERR("pico-ros publisher declare failed for '%s'", BATTERY_STATE_TOPIC);
		picoros_node_drop(&_node);
		picoros_interface_close();
		return false;
	}

#ifdef CONFIG_APP_MOTORS
	if (picoros_publisher_declare(&_node, &_pub_joint_state) == PICOROS_OK) {
		_joint_state_ready = true;
		k_work_init_delayable(&_joint_state_work, joint_state_work_handler);
	} else {
		LOG_WRN("continuing without joint_states publisher");
	}

	if (picoros_subscriber_declare(&_node, &_sub_cmd_vel) != PICOROS_OK) {
		LOG_WRN("continuing without cmd_vel subscription");
	}
#endif
#ifdef CONFIG_APP_GIMBAL
	if (picoros_subscriber_declare(&_node, &_sub_gimbal_cmd) != PICOROS_OK) {
		LOG_WRN("continuing without gimbal command subscription");
	}
#endif

	_ready = true;
#ifdef CONFIG_APP_MOTORS
	if (_joint_state_ready) {
		k_work_schedule(&_joint_state_work, K_NO_WAIT);
	}
	LOG_INF("pico-ros node '%s' up on domain %u: /%s, /%s", _node.name, _node.domain_id,
		BATTERY_STATE_TOPIC, JOINT_STATE_TOPIC);
#else
	LOG_INF("pico-ros node '%s' up on domain %u: /%s", _node.name, _node.domain_id,
		BATTERY_STATE_TOPIC);
#endif
	return true;
}

void app_picoros_publish_power(double voltage, double current, double power)
{
	ARG_UNUSED(power);

	if (!_ready) {
		return;
	}

	struct app_ros_time stamp = app_time_now();
	ros_BatteryState msg = {
		.header =
			{
				.stamp = {.sec = (int32_t)stamp.sec, .nanosec = stamp.nanosec},
				.frame_id = "",
			},
		.voltage = (float)voltage,
		.temperature = NAN,
		.current = (float)current,
		.charge = NAN,
		.capacity = NAN,
		.design_capacity = NAN,
		.percentage = NAN,
		.power_supply_status = 2,     /* POWER_SUPPLY_STATUS_DISCHARGING */
		.power_supply_health = 1,     /* POWER_SUPPLY_HEALTH_GOOD */
		.power_supply_technology = 0, /* POWER_SUPPLY_TECHNOLOGY_UNKNOWN */
		.present = true,
		.cell_voltage = {.data = NULL, .n_elements = 0},
		.cell_temperature = {.data = NULL, .n_elements = 0},
		.location = "",
		.serial_number = "",
	};

	memset(_battery_buf, 0, sizeof(_battery_buf));

	size_t len = ps_serialize(_battery_buf, &msg, sizeof(_battery_buf));

	if (len == 0) {
		LOG_WRN("battery_state encode failed");
		return;
	}

	if (picoros_publish(&_pub_battery, _battery_buf, len) != PICOROS_OK) {
		LOG_WRN("battery_state publish failed");
	}
}
