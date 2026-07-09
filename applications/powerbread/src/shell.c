/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sampler.h"

#include <stdlib.h>
#include <string.h>

#include <zephyr/shell/shell.h>

/* "12.34" style formatting without float printf: value scaled by 100.
 * The explicit sign prefix keeps -0.99..-0.01 from printing as positive
 * (integer division truncates -50/100 to 0).
 */
static void fmt_x100(char *buf, size_t len, int32_t x100)
{
	snprintf(buf, len, "%s%d.%02d", (x100 < 0 && x100 > -100) ? "-" : "", x100 / 100,
		 abs(x100 % 100));
}

static int cmd_read(const struct shell *sh, size_t argc, char **argv)
{
	struct pb_snapshot snap;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	sampler_get(&snap);
	if (!snap.sensor_ok) {
		shell_error(sh, "sensor not available");
		return -EIO;
	}
	for (int i = 0; i < PB_NUM_CH; i++) {
		char v[16], ma[16], mw[16];

		fmt_x100(v, sizeof(v), (int32_t)(snap.ch[i].v * 100.0f));
		fmt_x100(ma, sizeof(ma), (int32_t)(snap.ch[i].ma * 100.0f));
		fmt_x100(mw, sizeof(mw), (int32_t)(snap.ch[i].mw * 100.0f));
		shell_print(sh, "CH%d: %s V  %s mA  %s mW", i + 1, v, ma, mw);
	}
	return 0;
}

static int cmd_reset(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	sampler_reset_stats();
	shell_print(sh, "stats reset");
	return 0;
}

static int cmd_stream(const struct shell *sh, size_t argc, char **argv)
{
	if (strcmp(argv[1], "on") == 0) {
		sampler_stream_set(true);
	} else if (strcmp(argv[1], "off") == 0) {
		sampler_stream_set(false);
	} else {
		shell_error(sh, "usage: powerbread stream <on|off>");
		return -EINVAL;
	}
	shell_print(sh, "stream %s", sampler_stream_get() ? "on" : "off");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_powerbread, SHELL_CMD(read, NULL, "One-shot readings for both channels", cmd_read),
	SHELL_CMD(reset, NULL, "Reset stats and energy counters", cmd_reset),
	SHELL_CMD_ARG(stream, NULL, "CSV streaming: stream <on|off>", cmd_stream, 2, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(powerbread, &sub_powerbread, "PowerBread power monitor", NULL);
