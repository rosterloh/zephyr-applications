#include <string.h>

#include <zephyr/device.h>
#include <zephyr/input/input_analog_axis.h>
#include <zephyr/input/input_analog_axis_settings.h>
#include <zephyr/shell/shell.h>

#include "teleop.h"

/* Boards may split axes across several analog-axis nodes (the ESP32 ADC
 * cannot read more than one channel per sequence without DMA).
 */
#define STICK_DEV(node) DEVICE_DT_GET(node),
static const struct device *const sticks[] = {DT_FOREACH_STATUS_OKAY(analog_axis, STICK_DEV)};

#define MAX_AXES_PER_DEV 3
/* Each side of rest must travel this far beyond the deadzone. */
#define MIN_SPAN         100

static struct {
	bool seen;
	int16_t rest, min, max;
} cal[ARRAY_SIZE(sticks)][MAX_AXES_PER_DEV];
static bool running;

/* Runs on the analog-axis threads; the first sample after start is rest. */
static void raw_cb(const struct device *dev, int channel, int16_t raw)
{
	for (size_t d = 0; d < ARRAY_SIZE(sticks); d++) {
		if (sticks[d] != dev || channel < 0 || channel >= MAX_AXES_PER_DEV) {
			continue;
		}
		if (!cal[d][channel].seen) {
			cal[d][channel].seen = true;
			cal[d][channel].rest = cal[d][channel].min = cal[d][channel].max = raw;
			return;
		}
		cal[d][channel].min = MIN(cal[d][channel].min, raw);
		cal[d][channel].max = MAX(cal[d][channel].max, raw);
		return;
	}
}

static int num_axes(size_t d)
{
	return MIN(analog_axis_num_axes(sticks[d]), MAX_AXES_PER_DEV);
}

static int cmd_start(const struct shell *sh, size_t argc, char **argv)
{
	memset(cal, 0, sizeof(cal));
	running = true;
	for (size_t d = 0; d < ARRAY_SIZE(sticks); d++) {
		analog_axis_set_raw_data_cb(sticks[d], raw_cb);
	}
	shell_print(sh, "Leave the stick at rest, then move every axis to both limits and run "
			"'teleop cal stop'");
	return 0;
}

static int cmd_stop(const struct shell *sh, size_t argc, char **argv)
{
	struct analog_axis_calibration c[ARRAY_SIZE(sticks)][MAX_AXES_PER_DEV];

	if (!running) {
		shell_error(sh, "run 'teleop cal start' first");
		return -EINVAL;
	}
	for (size_t d = 0; d < ARRAY_SIZE(sticks); d++) {
		analog_axis_set_raw_data_cb(sticks[d], NULL);
	}
	running = false;

	/* Validate everything before touching any stored calibration. */
	for (size_t d = 0; d < ARRAY_SIZE(sticks); d++) {
		for (int i = 0; i < num_axes(d); i++) {
			analog_axis_calibration_get(sticks[d], i, &c[d][i]);
			if (!cal[d][i].seen ||
			    !teleop_cal_symmetric(cal[d][i].rest, cal[d][i].min, cal[d][i].max,
						  c[d][i].in_deadzone + MIN_SPAN, &c[d][i].in_min,
						  &c[d][i].in_max)) {
				shell_error(sh,
					    "%s axis %d: rest %d seen %d..%d, move it to both "
					    "limits; nothing saved",
					    sticks[d]->name, i, cal[d][i].rest, cal[d][i].min,
					    cal[d][i].max);
				return -EINVAL;
			}
		}
	}

	for (size_t d = 0; d < ARRAY_SIZE(sticks); d++) {
		for (int i = 0; i < num_axes(d); i++) {
			analog_axis_calibration_set(sticks[d], i, &c[d][i]);
			shell_print(sh, "%s axis %d: rest %d -> in %d..%d", sticks[d]->name, i,
				    cal[d][i].rest, c[d][i].in_min, c[d][i].in_max);
		}
		int err = analog_axis_calibration_save(sticks[d]);

		if (err) {
			shell_error(sh, "%s: save failed (%d)", sticks[d]->name, err);
			return err;
		}
	}
	return 0;
}

static int cmd_show(const struct shell *sh, size_t argc, char **argv)
{
	for (size_t d = 0; d < ARRAY_SIZE(sticks); d++) {
		for (int i = 0; i < analog_axis_num_axes(sticks[d]); i++) {
			struct analog_axis_calibration c;

			analog_axis_calibration_get(sticks[d], i, &c);
			shell_print(sh, "%s axis %d: in %d..%d deadzone %u", sticks[d]->name, i,
				    c.in_min, c.in_max, c.in_deadzone);
		}
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
