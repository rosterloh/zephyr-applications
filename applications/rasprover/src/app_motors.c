#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_motors, LOG_LEVEL_INF);

#include <zephyr/actuator/actuator.h>
#include <zephyr/kernel.h>

#include "app_motors.h"

static const struct device *const left_motor = DEVICE_DT_GET(DT_ALIAS(left_motor));
static const struct device *const right_motor = DEVICE_DT_GET(DT_ALIAS(right_motor));

static struct k_work_delayable cmd_watchdog;
static bool ready;

static void cmd_watchdog_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	(void)actuator_set_velocity(left_motor, 0.0f);
	(void)actuator_set_velocity(right_motor, 0.0f);
	/* COAST is best-effort: backends without ACTUATOR_CAP_DRIVE_MODE reject it. */
	(void)actuator_set_drive_mode(left_motor, ACTUATOR_DRIVE_MODE_COAST);
	(void)actuator_set_drive_mode(right_motor, ACTUATOR_DRIVE_MODE_COAST);
	LOG_WRN("cmd_vel timeout, motors stopped");
}

bool app_motors_init(void)
{
	if (!device_is_ready(left_motor) || !device_is_ready(right_motor)) {
		LOG_ERR("motor devices not ready");
		return false;
	}

	if (actuator_enable(left_motor) != 0 || actuator_enable(right_motor) != 0) {
		LOG_ERR("motor enable failed");
		return false;
	}

	k_work_init_delayable(&cmd_watchdog, cmd_watchdog_handler);
	ready = true;
	LOG_INF("motors ready");
	return true;
}

void app_motors_cmd_vel(float linear_x, float angular_z)
{
	if (!ready) {
		return;
	}

	const float half_track_m = (float)CONFIG_APP_MOTORS_WHEEL_SEPARATION_MM / 2000.0f;
	const float max_speed_m_s = (float)CONFIG_APP_MOTORS_MAX_SPEED_MM_S / 1000.0f;
	float v_left = linear_x - angular_z * half_track_m;
	float v_right = linear_x + angular_z * half_track_m;

	/* Open loop: the hbridge backend interprets the velocity setpoint as
	 * normalised duty in -1.0..1.0.
	 */
	(void)actuator_set_velocity(left_motor, CLAMP(v_left / max_speed_m_s, -1.0f, 1.0f));
	(void)actuator_set_velocity(right_motor, CLAMP(v_right / max_speed_m_s, -1.0f, 1.0f));

	k_work_reschedule(&cmd_watchdog, K_MSEC(CONFIG_APP_MOTORS_CMD_TIMEOUT_MS));
}
