#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <drivers/sensor/singletact.h>

LOG_MODULE_REGISTER(test_shell, LOG_LEVEL_INF);

#define TEST_FORCE_START_HELP                                                                      \
	SHELL_HELP("Start reading from force sensor.\n"                                            \
		   "Read interval is optional. Rate is 50ms when no interval provided.",           \
		   "[<read interval in ms>]")

static struct {
	const struct shell *sh;
	const struct device *dev;
	bool reading;
	struct k_timer read_timer;
} test_shell_context;

static void force_read_fn(struct k_timer *timer)
{
	struct sensor_value val;
	float force;
	uint16_t raw, baseline;
	int err;

	// struct test_shell_context *ctx = CONTAINER_OF(timer, struct test_shell_context, timer);

	if (test_shell_context.reading && test_shell_context.dev) {
		/* Read all sensors */
		err = sensor_sample_fetch(test_shell_context.dev);
		if (err) {
			LOG_ERR("Force sensor fetch failed: %d", err);
		}

		sensor_channel_get(test_shell_context.dev, SENSOR_CHAN_RAW, &val);
		raw = val.val1;
		baseline = val.val2;

		sensor_channel_get(test_shell_context.dev, SENSOR_CHAN_FORCE, &val);
		force = sensor_value_to_float(&val);

		LOG_RAW("%d,%d,%.6f\n", raw, baseline, (double)force);
	}
}

static int cmd_force_tare(const struct shell *sh, size_t argc, char *argv[])
{
	return 0;
}

static int cmd_force_set_address(const struct shell *sh, size_t argc, char *argv[])
{
	return 0;
}

static int cmd_force_start(const struct shell *sh, size_t argc, char *argv[])
{
	test_shell_context.sh = sh;
	if (test_shell_context.reading) {
		shell_error(sh, "Force sensor reading is already active.");
		return -ENOEXEC;
	}

	if (argc > 1) {
		int interval = atoi(argv[1]);
		if (interval <= 0) {
			shell_error(sh, "Invalid read interval: %s", argv[1]);
			return -EINVAL;
		}
		test_shell_context.reading = true;
		k_timer_start(&test_shell_context.read_timer, K_NO_WAIT, K_MSEC(interval));
	} else {
		test_shell_context.reading = true;
		k_timer_start(&test_shell_context.read_timer, K_NO_WAIT, K_MSEC(50));
	}

	return 0;
}

static int cmd_force_stop(const struct shell *sh, size_t argc, char *argv[])
{
	test_shell_context.sh = sh;
	if (test_shell_context.reading) {
		k_timer_stop(&test_shell_context.read_timer);
		test_shell_context.reading = false;
		shell_info(sh, "Force sensor reading stopped.");
	} else {
		shell_warn(sh, "Force sensor reading is not active.");
	}

	return 0;
}

SHELL_SUBCMD_SET_CREATE(test_commands, (test));

SHELL_STATIC_SUBCMD_SET_CREATE(
	test_cmd_force,
	SHELL_CMD_ARG(tare, NULL,
		      "Reset the baseline value of the force sensor to current value.\n",
		      cmd_force_tare, 0, 0),
	SHELL_CMD_ARG(set_address, NULL, "Set the address of the force sensor.\n",
		      cmd_force_set_address, 0, 0),
	SHELL_CMD_ARG(start, NULL, TEST_FORCE_START_HELP, cmd_force_start, 0, 1),
	SHELL_CMD_ARG(stop, NULL, "Stop reading from force sensors.\n", cmd_force_stop, 0, 0),
	SHELL_SUBCMD_SET_END);

SHELL_SUBCMD_ADD((test), force, &test_cmd_force, "Force sensor testing.\n", NULL, 0, 0);

SHELL_CMD_REGISTER(test, &test_commands, "Test commands", NULL);

static int test_shell_init(void)
{
	test_shell_context.sh = NULL;
	test_shell_context.dev = DEVICE_DT_GET_ONE(pps_singletact);
	test_shell_context.reading = false;

	if (!device_is_ready(test_shell_context.dev)) {
		LOG_ERR("Force sensor device %s is not ready.", test_shell_context.dev->name);
		test_shell_context.dev = NULL;
	} else {
		LOG_DBG("Force sensor device %s is ready.", test_shell_context.dev->name);
	}

	k_timer_init(&test_shell_context.read_timer, force_read_fn, NULL);

	return 0;
}

SYS_INIT(test_shell_init, APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
