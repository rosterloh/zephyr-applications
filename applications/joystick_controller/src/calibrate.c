#include <string.h>

#include <zephyr/device.h>
#include <zephyr/input/input_analog_axis.h>
#include <zephyr/input/input_analog_axis_settings.h>
#include <zephyr/shell/shell.h>

#include "teleop.h"

#define MAX_AXES 3
/* A span smaller than this means the stick was never moved on that axis. */
#define MIN_SPAN 100

static const struct device *const joystick = DEVICE_DT_GET_ONE(analog_axis);

static struct {
	bool seen;
	int16_t rest, min, max;
} cal[MAX_AXES];
static bool running;

/* Runs on the analog-axis thread; the first sample after start is rest. */
static void raw_cb(const struct device *dev, int channel, int16_t raw)
{
	if (channel < 0 || channel >= MAX_AXES) {
		return;
	}
	if (!cal[channel].seen) {
		cal[channel].seen = true;
		cal[channel].rest = cal[channel].min = cal[channel].max = raw;
		return;
	}
	cal[channel].min = MIN(cal[channel].min, raw);
	cal[channel].max = MAX(cal[channel].max, raw);
}

static int cmd_start(const struct shell *sh, size_t argc, char **argv)
{
	memset(cal, 0, sizeof(cal));
	running = true;
	analog_axis_set_raw_data_cb(joystick, raw_cb);
	shell_print(sh, "Leave the stick at rest, then move every axis to its limits and run "
			"'teleop cal stop'");
	return 0;
}

static int cmd_stop(const struct shell *sh, size_t argc, char **argv)
{
	int n = MIN(analog_axis_num_axes(joystick), MAX_AXES);

	if (!running) {
		shell_error(sh, "run 'teleop cal start' first");
		return -EINVAL;
	}
	analog_axis_set_raw_data_cb(joystick, NULL);
	running = false;

	for (int i = 0; i < n; i++) {
		if (!cal[i].seen || cal[i].max - cal[i].min < MIN_SPAN) {
			shell_error(sh, "axis %d barely moved (%d..%d), nothing saved", i,
				    cal[i].min, cal[i].max);
			return -EINVAL;
		}
	}

	for (int i = 0; i < n; i++) {
		struct analog_axis_calibration c;

		analog_axis_calibration_get(joystick, i, &c);
		teleop_cal_symmetric(cal[i].rest, cal[i].min, cal[i].max, &c.in_min, &c.in_max);
		analog_axis_calibration_set(joystick, i, &c);
		shell_print(sh, "axis %d: rest %d seen %d..%d -> in %d..%d", i, cal[i].rest,
			    cal[i].min, cal[i].max, c.in_min, c.in_max);
	}

	return analog_axis_calibration_save(joystick);
}

static int cmd_show(const struct shell *sh, size_t argc, char **argv)
{
	for (int i = 0; i < analog_axis_num_axes(joystick); i++) {
		struct analog_axis_calibration c;

		analog_axis_calibration_get(joystick, i, &c);
		shell_print(sh, "axis %d: in %d..%d deadzone %u", i, c.in_min, c.in_max,
			    c.in_deadzone);
	}
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_cal,
			       SHELL_CMD(start, NULL, "Start recording (stick at rest)", cmd_start),
			       SHELL_CMD(stop, NULL, "Store the recorded range", cmd_stop),
			       SHELL_CMD(show, NULL, "Print the current calibration", cmd_show),
			       SHELL_SUBCMD_SET_END);
SHELL_STATIC_SUBCMD_SET_CREATE(sub_teleop, SHELL_CMD(cal, &sub_cal, "Joystick calibration", NULL),
			       SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(teleop, &sub_teleop, "Teleop controller", NULL);
