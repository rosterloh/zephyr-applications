#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_gimbal, LOG_LEVEL_INF);

#include <zephyr/actuator/actuator.h>
#include <zephyr/device.h>
#ifdef CONFIG_BUS_SERVO
#include <zephyr/drivers/uart.h>
#include <drivers/bus_servo.h>
#endif

#include "app_gimbal.h"

#ifdef CONFIG_BUS_SERVO
#define SERVO_BUS_NODE DT_NODELABEL(gimbal_servo_bus)
#endif

static const struct device *const pan = DEVICE_DT_GET(DT_ALIAS(gimbal_pan));
static const struct device *const tilt = DEVICE_DT_GET(DT_ALIAS(gimbal_tilt));
static bool ready;

static bool read_joint(const struct device *dev, const char *name,
		       struct app_gimbal_joint_state *joint)
{
	struct actuator_feedback fb;

	if (actuator_read_feedback(dev, &fb) != 0 || !(fb.valid_mask & ACTUATOR_FB_POSITION)) {
		return false;
	}

	*joint = (struct app_gimbal_joint_state){
		.name = name,
		.position_rad = fb.position,
		.velocity_rad_s = (fb.valid_mask & ACTUATOR_FB_VELOCITY) ? fb.velocity : 0.0,
	};
	return true;
}

bool app_gimbal_init(void)
{
#ifdef CONFIG_BUS_SERVO
	/* The bus device has no init hook: the application configures its UART. */
	struct bus_servo_iface_param param = {
		.rx_timeout_us = 50000,
		.serial =
			{
				.baud = DT_PROP(DT_PARENT(SERVO_BUS_NODE), current_speed),
				.parity = UART_CFG_PARITY_NONE,
			},
	};
	int iface = bus_servo_iface_get_by_name(DEVICE_DT_NAME(SERVO_BUS_NODE));

	if (iface < 0 || bus_servo_init(iface, param) != 0) {
		LOG_ERR("gimbal servo bus init failed");
		return false;
	}
#endif

	if (!device_is_ready(pan) || !device_is_ready(tilt)) {
		LOG_ERR("gimbal devices not ready");
		return false;
	}

	if (actuator_enable(pan) != 0 || actuator_enable(tilt) != 0) {
		LOG_ERR("gimbal enable failed");
		return false;
	}

	ready = true;
	LOG_INF("gimbal ready");
	return true;
}

void app_gimbal_set_positions(float pan_rad, float tilt_rad)
{
	if (!ready) {
		return;
	}

	if (actuator_set_position(pan, pan_rad) != 0) {
		LOG_WRN("pan_joint setpoint failed");
	}
	if (actuator_set_position(tilt, tilt_rad) != 0) {
		LOG_WRN("tilt_joint setpoint failed");
	}
}

bool app_gimbal_read_joint_state(struct app_gimbal_joint_state joints[APP_GIMBAL_JOINT_COUNT])
{
	if (!ready || joints == NULL) {
		return false;
	}

	return read_joint(pan, "pan_joint", &joints[0]) &&
	       read_joint(tilt, "tilt_joint", &joints[1]);
}
