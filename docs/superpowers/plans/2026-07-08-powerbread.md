# PowerBread Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** New Zephyr app `applications/powerbread` — a dual-channel breadboard power monitor (INA3221 + ST7735 LCD + ADC dial wheel) on the Adafruit QT Py ESP32-C3, ported from XIAO-powerbread.

**Architecture:** A 100 Hz sampler thread reads the INA3221 and maintains stats/energy/chart ring buffers behind a mutex-protected snapshot API. An LVGL 9.6 UI (dashboard / chart / stats screens) refreshes at 10 Hz from snapshots; mode/channel changes are requested via atomics so shell and input contexts never touch LVGL directly. A resistor-ladder dial on ADC1_CH1 arrives through Zephyr's in-tree `adc-keys` + `zephyr,input-longpress` drivers as input events.

**Tech Stack:** Zephyr (main), in-tree drivers `ti,ina3221`, `sitronix,st7735r` (via `zephyr,mipi-dbi-spi`), `adc-keys`, `zephyr,input-longpress`; LVGL 9.6; out-of-tree board `adafruit_qt_py_esp32c3`.

**Spec:** `docs/superpowers/specs/2026-07-08-powerbread-design.md`

## Global Constraints

- All commands run from the workspace root through `uv`: `uv run poe agent-build powerbread` to build; never bare `west`/`python`. Build dir is `builds/powerbread/` (the poe task handles this).
- C code follows the in-tree `.clang-format`; verify with `uv run clang-format --dry-run --Werror <files>`.
- No networking, BT, sysbuild, or flash settings — this is a standalone instrument.
- Board target is exactly `adafruit_qt_py_esp32c3` (out-of-tree, `boards/adafruit/qt_py_esp32c3`).
- Float printf is NOT enabled (`CONFIG_CBPRINTF_FP_SUPPORT` stays off; deviation from the spec's config outline for footprint). All user-visible numbers are formatted with integer math (e.g. `x10 = (int32_t)(f * 10.0f)`).
- The build machine has no attached hardware: "verify" for each task means the build succeeds and clang-format passes. Hardware checks are collected in the final validation checklist for the user.
- Commit after every task with the trailer `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`.

---

### Task 1: App scaffold + poe integration

**Files:**
- Create: `applications/powerbread/CMakeLists.txt`
- Create: `applications/powerbread/Kconfig`
- Create: `applications/powerbread/VERSION`
- Create: `applications/powerbread/prj.conf`
- Create: `applications/powerbread/src/main.c`
- Create: `applications/powerbread/boards/adafruit_qt_py_esp32c3.overlay`
- Create: `applications/powerbread/README.md`
- Modify: `poe.toml` (the `app` task `case` block, ~line 277, and the `args` help string ~line 300)

**Interfaces:**
- Consumes: nothing.
- Produces: a building app skeleton; `main.c` is replaced in Task 4. Later tasks append to `prj.conf`, the overlay, and `CMakeLists.txt` exactly as shown in those tasks.

- [ ] **Step 1: Create the app skeleton**

`applications/powerbread/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(powerbread)

target_sources(app PRIVATE
  src/main.c
)
```

`applications/powerbread/Kconfig`:

```
mainmenu "Application options"

source "Kconfig.zephyr"
```

`applications/powerbread/VERSION`:

```
VERSION_MAJOR = 1
VERSION_MINOR = 0
PATCHLEVEL = 0
VERSION_TWEAK = 0
EXTRAVERSION =
```

`applications/powerbread/prj.conf`:

```
# Logging
CONFIG_LOG=y

# Shell over USB serial (board chosen: zephyr,shell-uart = &usb_serial)
CONFIG_SHELL=y

CONFIG_MAIN_STACK_SIZE=4096
```

`applications/powerbread/src/main.c`:

```c
/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <app_version.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("PowerBread %s", APP_VERSION_STRING);
	return 0;
}
```

`applications/powerbread/boards/adafruit_qt_py_esp32c3.overlay` (placeholder comment only for now; filled in Tasks 2/4/6):

```dts
/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 */
```

`applications/powerbread/README.md`:

```markdown
# PowerBread

Dual-channel breadboard power monitor, ported from
[XIAO-powerbread](https://github.com/nicho810/XIAO-powerbread).
Wiring and usage docs are completed alongside the implementation.

Build: `uv run poe app powerbread`
```

- [ ] **Step 2: Register the app in poe.toml**

In `poe.toml`, inside the `app` task `case` block, add after the `force_sensor` line:

```
    powerbread)           ALLOWED="adafruit_qt_py_esp32c3"; DEFAULT="adafruit_qt_py_esp32c3" ;;
```

In the same file's `args` list for the `app` task, extend the positional arg help string to include `powerbread` (append it to the app-name list).

- [ ] **Step 3: Build**

Run: `uv run poe agent-build powerbread` (background it; takes a few minutes)
Expected: success, "last 5 lines" tail shows memory usage table.

- [ ] **Step 4: Commit**

```bash
git add applications/powerbread poe.toml
git commit -m "powerbread: scaffold new app (QT Py ESP32-C3)"
```

---

### Task 2: INA3221 devicetree + sampler unit

**Files:**
- Modify: `applications/powerbread/boards/adafruit_qt_py_esp32c3.overlay`
- Modify: `applications/powerbread/prj.conf`
- Modify: `applications/powerbread/CMakeLists.txt`
- Create: `applications/powerbread/src/sampler.h`
- Create: `applications/powerbread/src/sampler.c`

**Interfaces:**
- Consumes: Task 1 skeleton.
- Produces (used by Tasks 3–6):
  - `struct pb_channel_stats` / `struct pb_snapshot` (see header below)
  - `void sampler_get(struct pb_snapshot *out);`
  - `void sampler_reset_stats(void);`
  - `void sampler_stream_set(bool enable);` / `bool sampler_stream_get(void);`
  - `PB_NUM_CH` (2), `PB_CHART_POINTS` (160). Chart arrays are `int32_t` mA values, oldest first, updated at 20 Hz (~8 s history).

- [ ] **Step 1: Add the INA3221 to the overlay**

Append to `applications/powerbread/boards/adafruit_qt_py_esp32c3.overlay`:

```dts
&i2c0 {
	status = "okay";

	ina3221: ina3221@40 {
		compatible = "ti,ina3221";
		reg = <0x40>;
		/* PowerBread schematic: 50 mOhm shunts on channels 1+2 */
		shunt-resistors = <50 50 50>;
		enable-channel = <1 1 0>;
		/* 140 us conversions, x4 averaging: full 2-ch cycle ~2.24 ms */
		conv-time-shunt = <0>;
		conv-time-bus = <0>;
		avg-mode = <1>;
	};
};
```

- [ ] **Step 2: Enable sensor support in prj.conf**

Append to `applications/powerbread/prj.conf`:

```
# INA3221 current/voltage monitor
CONFIG_I2C=y
CONFIG_SENSOR=y
```

- [ ] **Step 3: Write the sampler**

`applications/powerbread/src/sampler.h`:

```c
/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_SAMPLER_H_
#define APP_SAMPLER_H_

#include <stdbool.h>
#include <stdint.h>

#define PB_NUM_CH       2
#define PB_CHART_POINTS 160

struct pb_channel_stats {
	float v;  /* latest bus voltage [V] */
	float ma; /* latest current [mA] */
	float mw; /* latest power [mW] */
	float v_min, v_max;
	float ma_min, ma_max;
	float ma_avg;
	float mah; /* accumulated charge [mAh] since reset */
	float mwh; /* accumulated energy [mWh] since reset */
	int32_t chart[PB_CHART_POINTS]; /* current [mA], oldest first, 20 Hz */
};

struct pb_snapshot {
	bool sensor_ok;
	uint32_t t_ms;
	struct pb_channel_stats ch[PB_NUM_CH];
};

void sampler_get(struct pb_snapshot *out);
void sampler_reset_stats(void);
void sampler_stream_set(bool enable);
bool sampler_stream_get(void);

#endif /* APP_SAMPLER_H_ */
```

`applications/powerbread/src/sampler.c`:

```c
/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sampler.h"

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

LOG_MODULE_REGISTER(sampler, LOG_LEVEL_INF);

/*
 * Private attribute of the in-tree INA3221 driver
 * (deps/zephyr/drivers/sensor/ti/ina3221/ina3221.h). Selects which of the
 * three hardware channels channel_get() reports; val1 is 1-based.
 */
#define SENSOR_ATTR_INA3221_SELECTED_CHANNEL (SENSOR_ATTR_PRIV_START + 1)

#define SAMPLE_PERIOD_MS 10
#define CHART_DECIMATION 5 /* 100 Hz / 5 = 20 Hz chart rate */

struct channel_acc {
	float v, ma, mw;
	float v_min, v_max;
	float ma_min, ma_max;
	double ma_sum;
	uint32_t n;
	double mah, mwh;
	int32_t chart[PB_CHART_POINTS];
	uint16_t head; /* next write index == oldest sample */
};

static const struct device *const ina = DEVICE_DT_GET_ONE(ti_ina3221);
static struct channel_acc acc[PB_NUM_CH];
static bool sensor_ok;
static bool stream_on;
static K_MUTEX_DEFINE(lock);

static void reset_channel(struct channel_acc *c)
{
	c->v_min = 1e9f;
	c->ma_min = 1e9f;
	c->v_max = -1e9f;
	c->ma_max = -1e9f;
	c->ma_sum = 0.0;
	c->n = 0;
	c->mah = 0.0;
	c->mwh = 0.0;
}

void sampler_reset_stats(void)
{
	k_mutex_lock(&lock, K_FOREVER);
	for (int i = 0; i < PB_NUM_CH; i++) {
		reset_channel(&acc[i]);
	}
	k_mutex_unlock(&lock);
}

void sampler_stream_set(bool enable)
{
	stream_on = enable;
}

bool sampler_stream_get(void)
{
	return stream_on;
}

void sampler_get(struct pb_snapshot *out)
{
	k_mutex_lock(&lock, K_FOREVER);
	out->sensor_ok = sensor_ok;
	out->t_ms = k_uptime_get_32();
	for (int i = 0; i < PB_NUM_CH; i++) {
		const struct channel_acc *c = &acc[i];
		struct pb_channel_stats *o = &out->ch[i];

		o->v = c->v;
		o->ma = c->ma;
		o->mw = c->mw;
		o->v_min = (c->n > 0) ? c->v_min : 0.0f;
		o->v_max = (c->n > 0) ? c->v_max : 0.0f;
		o->ma_min = (c->n > 0) ? c->ma_min : 0.0f;
		o->ma_max = (c->n > 0) ? c->ma_max : 0.0f;
		o->ma_avg = (c->n > 0) ? (float)(c->ma_sum / c->n) : 0.0f;
		o->mah = (float)c->mah;
		o->mwh = (float)c->mwh;
		for (int p = 0; p < PB_CHART_POINTS; p++) {
			o->chart[p] = c->chart[(c->head + p) % PB_CHART_POINTS];
		}
	}
	k_mutex_unlock(&lock);
}

static int read_channels(float *v, float *ma, float *mw)
{
	struct sensor_value val;
	int ret;

	ret = sensor_sample_fetch(ina);
	if (ret != 0) {
		return ret;
	}

	for (int i = 0; i < PB_NUM_CH; i++) {
		struct sensor_value sel = {.val1 = i + 1, .val2 = 0};

		ret = sensor_attr_set(ina, SENSOR_CHAN_ALL,
				      SENSOR_ATTR_INA3221_SELECTED_CHANNEL, &sel);
		if (ret != 0) {
			return ret;
		}
		ret = sensor_channel_get(ina, SENSOR_CHAN_VOLTAGE, &val);
		if (ret != 0) {
			return ret;
		}
		v[i] = sensor_value_to_float(&val);
		ret = sensor_channel_get(ina, SENSOR_CHAN_CURRENT, &val);
		if (ret != 0) {
			return ret;
		}
		ma[i] = sensor_value_to_float(&val) * 1000.0f;
		ret = sensor_channel_get(ina, SENSOR_CHAN_POWER, &val);
		if (ret != 0) {
			return ret;
		}
		mw[i] = sensor_value_to_float(&val) * 1000.0f;
	}
	return 0;
}

static void stream_csv(uint32_t t_ms, const float *v, const float *ma, const float *mw)
{
	for (int i = 0; i < PB_NUM_CH; i++) {
		int32_t mv = (int32_t)(v[i] * 1000.0f);
		int32_t ma_x10 = (int32_t)(ma[i] * 10.0f);
		int32_t mw_x10 = (int32_t)(mw[i] * 10.0f);

		printk("%u,%d,%d,%d.%d,%d.%d\n", t_ms, i + 1, mv, ma_x10 / 10,
		       abs(ma_x10 % 10), mw_x10 / 10, abs(mw_x10 % 10));
	}
}

static void sampler_thread(void *a, void *b, void *c)
{
	float v[PB_NUM_CH], ma[PB_NUM_CH], mw[PB_NUM_CH];
	unsigned int decim = 0;
	int64_t last_t = 0;
	unsigned int errs = 0;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	sampler_reset_stats();

	if (!device_is_ready(ina)) {
		LOG_ERR("INA3221 not ready");
		return;
	}

	last_t = k_uptime_get();

	while (true) {
		int64_t t0 = k_uptime_get();
		int ret = read_channels(v, ma, mw);

		if (ret == 0) {
			int64_t now = k_uptime_get();
			double dt_h = (double)(now - last_t) / 3600000.0;
			bool push;

			last_t = now;
			decim++;
			push = (decim >= CHART_DECIMATION);
			if (push) {
				decim = 0;
			}

			k_mutex_lock(&lock, K_FOREVER);
			sensor_ok = true;
			for (int i = 0; i < PB_NUM_CH; i++) {
				struct channel_acc *ch = &acc[i];

				ch->v = v[i];
				ch->ma = ma[i];
				ch->mw = mw[i];
				ch->v_min = MIN(ch->v_min, v[i]);
				ch->v_max = MAX(ch->v_max, v[i]);
				ch->ma_min = MIN(ch->ma_min, ma[i]);
				ch->ma_max = MAX(ch->ma_max, ma[i]);
				ch->ma_sum += ma[i];
				ch->n++;
				ch->mah += ma[i] * dt_h;
				ch->mwh += mw[i] * dt_h;
				if (push) {
					ch->chart[ch->head] = (int32_t)ma[i];
					ch->head = (ch->head + 1) % PB_CHART_POINTS;
				}
			}
			k_mutex_unlock(&lock);

			if (stream_on) {
				stream_csv((uint32_t)now, v, ma, mw);
			}
			errs = 0;
		} else {
			k_mutex_lock(&lock, K_FOREVER);
			sensor_ok = false;
			k_mutex_unlock(&lock);
			errs++;
			if (errs == 1 || (errs % 500) == 0) {
				LOG_WRN("INA3221 read failed: %d", ret);
			}
		}

		int64_t elapsed = k_uptime_get() - t0;

		if (elapsed < SAMPLE_PERIOD_MS) {
			k_msleep(SAMPLE_PERIOD_MS - (int32_t)elapsed);
		}
	}
}

K_THREAD_DEFINE(pb_sampler, 2048, sampler_thread, NULL, NULL, NULL, 5, 0, 0);
```

Add `src/sampler.c` to `target_sources` in `applications/powerbread/CMakeLists.txt`:

```cmake
target_sources(app PRIVATE
  src/main.c
  src/sampler.c
)
```

- [ ] **Step 4: Build and format-check**

Run: `uv run poe agent-build powerbread` (background)
Expected: success.
Run: `uv run clang-format --dry-run --Werror applications/powerbread/src/sampler.c applications/powerbread/src/sampler.h`
Expected: no output, exit 0. Fix formatting if not.

- [ ] **Step 5: Commit**

```bash
git add applications/powerbread
git commit -m "powerbread: add INA3221 sampler (100 Hz, stats, energy, chart ring)"
```

---

### Task 3: Shell commands (read / reset / stream)

**Files:**
- Create: `applications/powerbread/src/shell.c`
- Modify: `applications/powerbread/CMakeLists.txt`

**Interfaces:**
- Consumes: `sampler_get()`, `sampler_reset_stats()`, `sampler_stream_set()/get()` from Task 2.
- Produces: `powerbread read|reset|stream` shell commands. Task 6 appends `mode`, `channel`, `dial` subcommands to the same `SHELL_STATIC_SUBCMD_SET_CREATE` block.

- [ ] **Step 1: Write the shell unit**

`applications/powerbread/src/shell.c`:

```c
/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sampler.h"

#include <stdlib.h>
#include <string.h>

#include <zephyr/shell/shell.h>

/* "12.34" style formatting without float printf: value scaled by 100 */
static void fmt_x100(char *buf, size_t len, int32_t x100)
{
	snprintf(buf, len, "%d.%02d", x100 / 100, abs(x100 % 100));
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
	sub_powerbread,
	SHELL_CMD(read, NULL, "One-shot readings for both channels", cmd_read),
	SHELL_CMD(reset, NULL, "Reset stats and energy counters", cmd_reset),
	SHELL_CMD_ARG(stream, NULL, "CSV streaming: stream <on|off>", cmd_stream, 2, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(powerbread, &sub_powerbread, "PowerBread power monitor", NULL);
```

Add `src/shell.c` to `target_sources` in `applications/powerbread/CMakeLists.txt`:

```cmake
target_sources(app PRIVATE
  src/main.c
  src/sampler.c
  src/shell.c
)
```

- [ ] **Step 2: Build and format-check**

Run: `uv run poe agent-build powerbread` (background)
Expected: success.
Run: `uv run clang-format --dry-run --Werror applications/powerbread/src/shell.c`
Expected: exit 0.

- [ ] **Step 3: Commit**

```bash
git add applications/powerbread
git commit -m "powerbread: add shell commands (read, reset, stream)"
```

---

### Task 4: Display devicetree + LVGL dashboard

**Files:**
- Modify: `applications/powerbread/boards/adafruit_qt_py_esp32c3.overlay`
- Modify: `applications/powerbread/prj.conf`
- Modify: `applications/powerbread/src/main.c` (full replacement shown)
- Create: `applications/powerbread/src/ui.h`
- Create: `applications/powerbread/src/ui.c`
- Modify: `applications/powerbread/CMakeLists.txt`

**Interfaces:**
- Consumes: `sampler_get()` and `struct pb_snapshot` from Task 2.
- Produces (used by Tasks 5–6):
  - `enum pb_mode { PB_MODE_DASH, PB_MODE_CHART, PB_MODE_STATS, PB_MODE_COUNT };`
  - `int pb_ui_init(void);` — 0 on success, negative errno if display unavailable
  - `void pb_ui_set_mode(enum pb_mode mode);` / `enum pb_mode pb_ui_get_mode(void);`
  - `void pb_ui_step_mode(int dir);` — dir is +1/-1, wraps
  - `void pb_ui_set_channel(uint8_t ch);` (0-based) / `uint8_t pb_ui_get_channel(void);`
  - All five setters/getters are safe from any thread (atomics; the LVGL refresh timer applies them).
  - In this task the chart and stats screens are created as empty containers; Task 5 fills them.

- [ ] **Step 1: Add the display to the overlay**

Append to `applications/powerbread/boards/adafruit_qt_py_esp32c3.overlay`:

```dts
/*
 * ST7735 0.96" 80x160 LCD, wired per the XIAO PowerBread schematic:
 * DIN=GPIO7 (SPI2 MOSI), CLK=GPIO10 (SPI2 SCLK), DC=GPIO8, RST=GPIO0,
 * CS tied low on the module (no CS GPIO), backlight hardwired on.
 * GPIO8 is SPIM2_MISO on the base board; re-pinmux SPI2 without MISO so
 * GPIO8 is free to drive the DC line (display is write-only).
 */

&pinctrl {
	spim2_lcd: spim2_lcd {
		group1 {
			pinmux = <SPIM2_SCLK_GPIO10>;
		};
		group2 {
			pinmux = <SPIM2_MOSI_GPIO7>;
			output-low;
		};
	};
};

&spi2 {
	pinctrl-0 = <&spim2_lcd>;
	pinctrl-names = "default";
};

/ {
	chosen {
		zephyr,display = &st7735r;
	};

	mipi_dbi {
		compatible = "zephyr,mipi-dbi-spi";
		spi-dev = <&spi2>;
		dc-gpios = <&gpio0 8 GPIO_ACTIVE_HIGH>;
		reset-gpios = <&gpio0 0 GPIO_ACTIVE_LOW>;
		write-only;
		#address-cells = <1>;
		#size-cells = <0>;

		/* Panel init values from the in-tree heltec_wireless_tracker
		 * (same 0.96" 160x80 ST7735 panel), rotated to portrait.
		 */
		st7735r: st7735r@0 {
			compatible = "sitronix,st7735r";
			reg = <0>;
			mipi-max-frequency = <20000000>;
			mipi-mode = "MIPI_DBI_MODE_SPI_4WIRE";
			width = <80>;
			height = <160>;
			inversion-on;
			x-offset = <26>;
			y-offset = <1>;
			madctl = <0xc8>;
			colmod = <0x05>;
			invctr = <7>;
			vmctr1 = <0x0e>;
			pwctr1 = [a2 02 84];
			pwctr2 = [c1];
			pwctr3 = [0a 00];
			pwctr4 = [8a 2a];
			pwctr5 = [8a ee];
			frmctr1 = [01 26 2e];
			frmctr2 = [01 26 2e];
			frmctr3 = [01 26 2e 01 26 2e];
			gamctrp1 = [0f 1a 0f 18 2f 28 20 22 1f 1b 23 37 00 07 02 10];
			gamctrn1 = [0f 1b 0f 17 33 2c 29 2e 30 2e 30 3b 00 07 03 10];
		};
	};
};
```

Hardware calibration note (not a build concern): if the panel shows mirrored or
offset output, adjust `madctl`/`x-offset`/`y-offset` — the proven landscape
variant is `madctl = <0xa8>`, `width = <160>`, `height = <80>`,
`x-offset = <1>`, `y-offset = <26>`.

- [ ] **Step 2: Enable display + LVGL in prj.conf**

Append to `applications/powerbread/prj.conf`:

```
# ST7735 display via MIPI-DBI SPI
CONFIG_SPI=y
CONFIG_MIPI_DBI=y
CONFIG_DISPLAY=y

# LVGL
CONFIG_LVGL=y
CONFIG_LV_COLOR_DEPTH_16=y
CONFIG_LV_Z_MEM_POOL_SIZE=16384
CONFIG_LV_Z_VDB_SIZE=100
CONFIG_LV_USE_CHART=y
CONFIG_LV_FONT_MONTSERRAT_12=y
CONFIG_LV_FONT_MONTSERRAT_16=y
CONFIG_LV_FONT_DEFAULT_MONTSERRAT_12=y
```

Also change the existing `CONFIG_MAIN_STACK_SIZE=4096` line to
`CONFIG_MAIN_STACK_SIZE=8192` (LVGL rendering runs in main).

- [ ] **Step 3: Write the UI unit (dashboard + mode framework)**

`applications/powerbread/src/ui.h`:

```c
/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_UI_H_
#define APP_UI_H_

#include <stdint.h>

enum pb_mode {
	PB_MODE_DASH,
	PB_MODE_CHART,
	PB_MODE_STATS,
	PB_MODE_COUNT,
};

int pb_ui_init(void);
void pb_ui_set_mode(enum pb_mode mode);
enum pb_mode pb_ui_get_mode(void);
void pb_ui_step_mode(int dir);
void pb_ui_set_channel(uint8_t ch);
uint8_t pb_ui_get_channel(void);

#endif /* APP_UI_H_ */
```

`applications/powerbread/src/ui.c` (Task 5 replaces the two `create_*` stubs
and extends `refresh_cb`; everything else stays):

```c
/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ui.h"
#include "sampler.h"

#include <stdio.h>
#include <stdlib.h>

#include <lvgl.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(ui, LOG_LEVEL_INF);

#define REFRESH_PERIOD_MS 100

static atomic_t req_mode = ATOMIC_INIT(PB_MODE_DASH);
static atomic_t req_channel = ATOMIC_INIT(0);

static lv_obj_t *header;
static lv_obj_t *pages[PB_MODE_COUNT];
static lv_obj_t *dash_label[PB_NUM_CH];
static enum pb_mode shown_mode = PB_MODE_COUNT; /* force first update */
static struct pb_snapshot snap;

static const char *const mode_names[PB_MODE_COUNT] = {"dash", "chart", "stats"};

void pb_ui_set_mode(enum pb_mode mode)
{
	if (mode < PB_MODE_COUNT) {
		atomic_set(&req_mode, mode);
	}
}

enum pb_mode pb_ui_get_mode(void)
{
	return (enum pb_mode)atomic_get(&req_mode);
}

void pb_ui_step_mode(int dir)
{
	int m = ((int)atomic_get(&req_mode) + dir + PB_MODE_COUNT) % PB_MODE_COUNT;

	atomic_set(&req_mode, m);
}

void pb_ui_set_channel(uint8_t ch)
{
	if (ch < PB_NUM_CH) {
		atomic_set(&req_channel, ch);
	}
}

uint8_t pb_ui_get_channel(void)
{
	return (uint8_t)atomic_get(&req_channel);
}

/* "12.34" style formatting without float printf: value scaled by 100 */
static void fmt_x100(char *buf, size_t len, int32_t x100)
{
	snprintf(buf, len, "%d.%02d", x100 / 100, abs(x100 % 100));
}

static void create_dash(lv_obj_t *parent)
{
	for (int i = 0; i < PB_NUM_CH; i++) {
		lv_obj_t *panel = lv_obj_create(parent);

		/* page is 144 px tall: two 68 px panels with a 8 px gap */
		lv_obj_set_size(panel, lv_pct(100), 68);
		lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, (i == 0) ? 0 : 76);
		lv_obj_set_style_pad_all(panel, 2, 0);

		lv_obj_t *title = lv_label_create(panel);

		lv_label_set_text_fmt(title, "CH%d", i + 1);
		lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

		dash_label[i] = lv_label_create(panel);
		lv_label_set_text(dash_label[i], "--");
		lv_obj_align(dash_label[i], LV_ALIGN_LEFT_MID, 0, 6);
	}
}

/* Filled in by the chart/stats task */
static void create_chart(lv_obj_t *parent)
{
	ARG_UNUSED(parent);
}

static void create_stats(lv_obj_t *parent)
{
	ARG_UNUSED(parent);
}

static void update_dash(void)
{
	for (int i = 0; i < PB_NUM_CH; i++) {
		char v[16], ma[16], mw[16];

		fmt_x100(v, sizeof(v), (int32_t)(snap.ch[i].v * 100.0f));
		fmt_x100(ma, sizeof(ma), (int32_t)(snap.ch[i].ma * 100.0f));
		fmt_x100(mw, sizeof(mw), (int32_t)(snap.ch[i].mw * 100.0f));
		lv_label_set_text_fmt(dash_label[i], "%sV\n%smA\n%smW", v, ma, mw);
	}
}

static void update_chart(void)
{
}

static void update_stats(void)
{
}

static void refresh_cb(lv_timer_t *timer)
{
	enum pb_mode mode = (enum pb_mode)atomic_get(&req_mode);
	uint8_t ch = (uint8_t)atomic_get(&req_channel);

	ARG_UNUSED(timer);

	sampler_get(&snap);

	if (mode != shown_mode) {
		for (int i = 0; i < PB_MODE_COUNT; i++) {
			if (i == mode) {
				lv_obj_remove_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
			} else {
				lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
			}
		}
		shown_mode = mode;
	}

	lv_label_set_text_fmt(header, "CH%d %s%s", ch + 1, mode_names[mode],
			      snap.sensor_ok ? "" : " !SNS");

	switch (mode) {
	case PB_MODE_DASH:
		update_dash();
		break;
	case PB_MODE_CHART:
		update_chart();
		break;
	case PB_MODE_STATS:
		update_stats();
		break;
	default:
		break;
	}
}

int pb_ui_init(void)
{
	const struct device *disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

	if (!device_is_ready(disp)) {
		LOG_ERR("display not ready");
		return -ENODEV;
	}

	lv_obj_t *scr = lv_screen_active();

	lv_obj_set_style_pad_all(scr, 0, 0);

	header = lv_label_create(scr);
	lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 1);
	lv_label_set_text(header, "");

	for (int i = 0; i < PB_MODE_COUNT; i++) {
		pages[i] = lv_obj_create(scr);
		lv_obj_set_size(pages[i], lv_pct(100), 144);
		lv_obj_align(pages[i], LV_ALIGN_BOTTOM_MID, 0, 0);
		lv_obj_set_style_pad_all(pages[i], 1, 0);
		lv_obj_set_style_border_width(pages[i], 0, 0);
		lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
	}

	create_dash(pages[PB_MODE_DASH]);
	create_chart(pages[PB_MODE_CHART]);
	create_stats(pages[PB_MODE_STATS]);

	lv_timer_create(refresh_cb, REFRESH_PERIOD_MS, NULL);

	display_blanking_off(disp);

	return 0;
}
```

Replace `applications/powerbread/src/main.c` entirely with:

```c
/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ui.h"

#include <lvgl.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <app_version.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("PowerBread %s", APP_VERSION_STRING);

	if (pb_ui_init() != 0) {
		LOG_ERR("UI disabled; sampler and shell remain active");
		k_sleep(K_FOREVER);
	}

	while (true) {
		uint32_t delay = lv_timer_handler();

		k_msleep(MIN(delay, REFRESH_LOOP_CAP_MS));
	}
	return 0;
}
```

with this define above `main()`:

```c
#define REFRESH_LOOP_CAP_MS 50
```

Add `src/ui.c` to `target_sources` in `applications/powerbread/CMakeLists.txt`:

```cmake
target_sources(app PRIVATE
  src/main.c
  src/sampler.c
  src/shell.c
  src/ui.c
)
```

- [ ] **Step 4: Build and format-check**

Run: `uv run poe agent-build powerbread` (background)
Expected: success. Watch RAM usage in the tail — the LVGL pool + full-frame
VDB (~25 KB) must fit; if the link fails on RAM, reduce `CONFIG_LV_Z_VDB_SIZE`
to `50`.
Run: `uv run clang-format --dry-run --Werror applications/powerbread/src/ui.c applications/powerbread/src/ui.h applications/powerbread/src/main.c`
Expected: exit 0.

- [ ] **Step 5: Commit**

```bash
git add applications/powerbread
git commit -m "powerbread: add ST7735 display and LVGL dashboard"
```

---

### Task 5: Chart and stats screens

**Files:**
- Modify: `applications/powerbread/src/ui.c`

**Interfaces:**
- Consumes: `struct pb_snapshot.ch[].chart` (int32_t mA, oldest first, `PB_CHART_POINTS`), stats fields from Task 2; page containers and `refresh_cb` structure from Task 4.
- Produces: fully functional chart and stats modes; no API changes.

- [ ] **Step 1: Implement the chart screen**

In `applications/powerbread/src/ui.c`, add file-scope statics near the other widgets:

```c
static lv_obj_t *chart;
static lv_chart_series_t *chart_ser;
static lv_obj_t *chart_label;
static int32_t chart_buf[PB_CHART_POINTS];
static lv_obj_t *stats_label;
```

Replace the `create_chart()` stub with:

```c
static void create_chart(lv_obj_t *parent)
{
	chart = lv_chart_create(parent);
	lv_obj_set_size(chart, lv_pct(100), 110);
	lv_obj_align(chart, LV_ALIGN_TOP_MID, 0, 0);
	lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
	lv_chart_set_point_count(chart, PB_CHART_POINTS);
	lv_chart_set_div_line_count(chart, 4, 4);
	chart_ser = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_YELLOW),
					LV_CHART_AXIS_PRIMARY_Y);
	lv_chart_set_series_ext_y_array(chart, chart_ser, chart_buf);

	chart_label = lv_label_create(parent);
	lv_obj_align(chart_label, LV_ALIGN_BOTTOM_MID, 0, 0);
	lv_label_set_text(chart_label, "--");
}
```

Replace the `update_chart()` stub with:

```c
static void update_chart(void)
{
	uint8_t ch = pb_ui_get_channel();
	const struct pb_channel_stats *c = &snap.ch[ch];
	int32_t lo = INT32_MAX, hi = INT32_MIN;
	char ma[16];

	for (int p = 0; p < PB_CHART_POINTS; p++) {
		chart_buf[p] = c->chart[p];
		lo = MIN(lo, chart_buf[p]);
		hi = MAX(hi, chart_buf[p]);
	}
	/* pad the range so a flat line is not glued to an edge */
	if (hi - lo < 10) {
		hi = lo + 10;
	}
	lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, lo, hi);
	lv_chart_refresh(chart);

	fmt_x100(ma, sizeof(ma), (int32_t)(c->ma * 100.0f));
	lv_label_set_text_fmt(chart_label, "%s mA", ma);
}
```

- [ ] **Step 2: Implement the stats screen**

Replace the `create_stats()` stub with:

```c
static void create_stats(lv_obj_t *parent)
{
	stats_label = lv_label_create(parent);
	lv_obj_align(stats_label, LV_ALIGN_TOP_LEFT, 0, 0);
	lv_label_set_text(stats_label, "--");
}
```

Replace the `update_stats()` stub with:

```c
static void update_stats(void)
{
	const struct pb_channel_stats *c = &snap.ch[pb_ui_get_channel()];
	char lo[16], hi[16], avg[16], v[16], mah[16], mwh[16];

	fmt_x100(lo, sizeof(lo), (int32_t)(c->ma_min * 100.0f));
	fmt_x100(hi, sizeof(hi), (int32_t)(c->ma_max * 100.0f));
	fmt_x100(avg, sizeof(avg), (int32_t)(c->ma_avg * 100.0f));
	fmt_x100(v, sizeof(v), (int32_t)(c->v * 100.0f));
	fmt_x100(mah, sizeof(mah), (int32_t)(c->mah * 100.0f));
	fmt_x100(mwh, sizeof(mwh), (int32_t)(c->mwh * 100.0f));
	lv_label_set_text_fmt(stats_label,
			      "V %s\nmin %s\nmax %s\navg %s\nmAh %s\nmWh %s", v,
			      lo, hi, avg, mah, mwh);
}
```

- [ ] **Step 3: Build and format-check**

Run: `uv run poe agent-build powerbread` (background)
Expected: success.
Run: `uv run clang-format --dry-run --Werror applications/powerbread/src/ui.c`
Expected: exit 0.

- [ ] **Step 4: Commit**

```bash
git add applications/powerbread
git commit -m "powerbread: add chart and stats screens"
```

---

### Task 6: Dial wheel input + remaining shell commands

**Files:**
- Modify: `applications/powerbread/boards/adafruit_qt_py_esp32c3.overlay`
- Modify: `applications/powerbread/prj.conf`
- Create: `applications/powerbread/src/input.c`
- Modify: `applications/powerbread/src/shell.c`
- Modify: `applications/powerbread/CMakeLists.txt`

**Interfaces:**
- Consumes: `pb_ui_step_mode()`, `pb_ui_set_mode()`, `pb_ui_set_channel()`, `pb_ui_get_channel()` (Task 4), `sampler_reset_stats()` (Task 2).
- Produces: dial events driving the UI; `powerbread mode|channel|dial` shell commands.
- Event code contract: the `adc-keys` node emits `INPUT_KEY_UP` / `INPUT_KEY_DOWN` / `INPUT_KEY_ENTER`; the longpress node consumes `INPUT_KEY_ENTER` and emits `INPUT_KEY_A` (short) / `INPUT_KEY_B` (long). `input.c` handles UP/DOWN/A/B and ignores raw ENTER.

- [ ] **Step 1: Add the dial to the overlay**

Append to `applications/powerbread/boards/adafruit_qt_py_esp32c3.overlay`
(add the two `#include` lines at the very top of the file, above the first
node, keeping the copyright header first):

```dts
#include <zephyr/dt-bindings/adc/adc.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
```

```dts
/*
 * PowerBread dial wheel: resistor ladder on D2/A2 = GPIO1 = ADC1 channel 1.
 * Idle sits near 0 V; press/down/up produce distinct levels. Thresholds
 * derived from upstream's 10-bit values (XIAO ESP32-C3 build); calibrate
 * with `powerbread dial` and adjust here if the module differs.
 */

&adc0 {
	status = "okay";
	#address-cells = <1>;
	#size-cells = <0>;

	channel@1 {
		reg = <1>;
		zephyr,gain = "ADC_GAIN_1_4";
		zephyr,reference = "ADC_REF_INTERNAL";
		zephyr,acquisition-time = <ADC_ACQ_TIME_DEFAULT>;
		zephyr,resolution = <12>;
	};
};

/ {
	dial: dial {
		compatible = "adc-keys";
		io-channels = <&adc0 1>;
		keyup-threshold-mv = <0>;
		sample-period-ms = <20>;

		dial_press {
			press-thresholds-mv = <510>;
			zephyr,code = <INPUT_KEY_ENTER>;
		};
		dial_down {
			press-thresholds-mv = <910>;
			zephyr,code = <INPUT_KEY_DOWN>;
		};
		dial_up {
			press-thresholds-mv = <1580>;
			zephyr,code = <INPUT_KEY_UP>;
		};
	};

	longpress {
		compatible = "zephyr,input-longpress";
		input = <&dial>;
		input-codes = <INPUT_KEY_ENTER>;
		short-codes = <INPUT_KEY_A>;
		long-codes = <INPUT_KEY_B>;
		long-delay-ms = <1000>;
	};
};
```

- [ ] **Step 2: Enable ADC + input in prj.conf**

Append to `applications/powerbread/prj.conf`:

```
# Dial wheel (adc-keys resistor ladder + longpress)
CONFIG_ADC=y
CONFIG_INPUT=y
```

- [ ] **Step 3: Write the input handler**

`applications/powerbread/src/input.c`:

```c
/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sampler.h"
#include "ui.h"

#include <zephyr/input/input.h>

static void dial_cb(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);

	if (evt->type != INPUT_EV_KEY || evt->value != 1) {
		return;
	}

	switch (evt->code) {
	case INPUT_KEY_UP:
		pb_ui_step_mode(1);
		break;
	case INPUT_KEY_DOWN:
		pb_ui_step_mode(-1);
		break;
	case INPUT_KEY_A: /* dial short press */
		pb_ui_set_channel((pb_ui_get_channel() + 1) % PB_NUM_CH);
		break;
	case INPUT_KEY_B: /* dial long press */
		sampler_reset_stats();
		break;
	default:
		break;
	}
}

INPUT_CALLBACK_DEFINE(NULL, dial_cb, NULL);
```

- [ ] **Step 4: Add mode / channel / dial shell commands**

In `applications/powerbread/src/shell.c`, add these includes after the
existing ones:

```c
#include "ui.h"

#include <zephyr/drivers/adc.h>
```

Add these command handlers above the `SHELL_STATIC_SUBCMD_SET_CREATE` block:

```c
static int cmd_mode(const struct shell *sh, size_t argc, char **argv)
{
	if (strcmp(argv[1], "dash") == 0) {
		pb_ui_set_mode(PB_MODE_DASH);
	} else if (strcmp(argv[1], "chart") == 0) {
		pb_ui_set_mode(PB_MODE_CHART);
	} else if (strcmp(argv[1], "stats") == 0) {
		pb_ui_set_mode(PB_MODE_STATS);
	} else {
		shell_error(sh, "usage: powerbread mode <dash|chart|stats>");
		return -EINVAL;
	}
	shell_print(sh, "mode %s", argv[1]);
	return 0;
}

static int cmd_channel(const struct shell *sh, size_t argc, char **argv)
{
	int ch = atoi(argv[1]);

	if (ch < 1 || ch > PB_NUM_CH) {
		shell_error(sh, "usage: powerbread channel <1|2>");
		return -EINVAL;
	}
	pb_ui_set_channel((uint8_t)(ch - 1));
	shell_print(sh, "channel %d", ch);
	return 0;
}

static int cmd_dial(const struct shell *sh, size_t argc, char **argv)
{
	static const struct adc_dt_spec dial_adc =
		ADC_DT_SPEC_GET_BY_IDX(DT_NODELABEL(dial), 0);
	int16_t raw;
	struct adc_sequence seq = {
		.buffer = &raw,
		.buffer_size = sizeof(raw),
	};
	int32_t mv;
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = adc_channel_setup_dt(&dial_adc);
	if (ret == 0) {
		ret = adc_sequence_init_dt(&dial_adc, &seq);
	}
	if (ret == 0) {
		ret = adc_read_dt(&dial_adc, &seq);
	}
	if (ret != 0) {
		shell_error(sh, "adc read failed: %d", ret);
		return ret;
	}
	mv = raw;
	ret = adc_raw_to_millivolts_dt(&dial_adc, &mv);
	if (ret != 0) {
		shell_error(sh, "mv conversion failed: %d", ret);
		return ret;
	}
	shell_print(sh, "dial: %d mV (raw %d)", mv, raw);
	return 0;
}
```

Extend the subcommand set (replace the existing
`SHELL_STATIC_SUBCMD_SET_CREATE` block) with:

```c
SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_powerbread,
	SHELL_CMD(read, NULL, "One-shot readings for both channels", cmd_read),
	SHELL_CMD(reset, NULL, "Reset stats and energy counters", cmd_reset),
	SHELL_CMD_ARG(stream, NULL, "CSV streaming: stream <on|off>", cmd_stream, 2, 0),
	SHELL_CMD_ARG(mode, NULL, "Set UI mode: mode <dash|chart|stats>", cmd_mode, 2, 0),
	SHELL_CMD_ARG(channel, NULL, "Set focused channel: channel <1|2>", cmd_channel, 2, 0),
	SHELL_CMD(dial, NULL, "Read dial ADC level (calibration aid)", cmd_dial),
	SHELL_SUBCMD_SET_END);
```

Add `src/input.c` to `target_sources` in `applications/powerbread/CMakeLists.txt`:

```cmake
target_sources(app PRIVATE
  src/main.c
  src/sampler.c
  src/shell.c
  src/ui.c
  src/input.c
)
```

- [ ] **Step 5: Build and format-check**

Run: `uv run poe agent-build powerbread` (background)
Expected: success.
Run: `uv run clang-format --dry-run --Werror applications/powerbread/src/input.c applications/powerbread/src/shell.c`
Expected: exit 0.

- [ ] **Step 6: Commit**

```bash
git add applications/powerbread
git commit -m "powerbread: add dial wheel input and mode/channel/dial shell commands"
```

---

### Task 7: README, final format sweep, hardware checklist

**Files:**
- Modify: `applications/powerbread/README.md`

**Interfaces:**
- Consumes: everything above.
- Produces: final documentation; no code changes.

- [ ] **Step 1: Write the README**

Replace `applications/powerbread/README.md` with:

```markdown
# PowerBread

Dual-channel breadboard power monitor on the Adafruit QT Py ESP32-C3, ported
from [XIAO-powerbread](https://github.com/nicho810/XIAO-powerbread): an
INA3221 measures voltage/current/power on two supply rails, a 0.96" ST7735
LCD shows an LVGL UI (dashboard, line chart, statistics), and a resistor-ladder
dial wheel navigates it. Measurements stream as CSV over USB serial on demand.

## Wiring

| Function          | QT Py pin | GPIO   | Notes                          |
|-------------------|-----------|--------|--------------------------------|
| INA3221 SDA/SCL   | SDA/SCL   | 5 / 6  | Stemma QT, addr 0x40, 50 mR shunts |
| LCD DIN (MOSI)    | MO        | 7      | SPI2                           |
| LCD CLK           | SCK       | 10     | SPI2                           |
| LCD DC            | MI        | 8      | re-pinmuxed from SPI2 MISO     |
| LCD RST           | A3        | 0      |                                |
| LCD CS            | —         | —      | tied low on the module         |
| Dial wheel ladder | A2 / D2   | 1      | ADC1 channel 1                 |

## Controls

- Dial up/down: cycle mode (dashboard → chart → stats)
- Dial short press: switch focused channel (1 ↔ 2)
- Dial long press (≥1 s): reset stats and energy counters

Shell (USB serial), root command `powerbread`:
`read`, `reset`, `stream <on|off>` (CSV: `t_ms,ch,mV,mA,mW`),
`mode <dash|chart|stats>`, `channel <1|2>`, `dial` (raw dial mV, for
calibrating the `adc-keys` thresholds in the board overlay).

## Build & flash

    uv run poe app powerbread
    uv run poe flash powerbread
```

- [ ] **Step 2: Final verification sweep**

Run: `uv run clang-format --dry-run --Werror applications/powerbread/src/*.c applications/powerbread/src/*.h`
Expected: exit 0.
Run: `uv run poe agent-build powerbread` (background)
Expected: success.

- [ ] **Step 3: Commit**

```bash
git add applications/powerbread
git commit -m "powerbread: add README with wiring and usage"
```

- [ ] **Step 4: Report the hardware validation checklist**

Implementation cannot verify hardware; report this checklist to the user at
the end:

1. Flash: `uv run poe flash powerbread`; connect USB serial console.
2. Boot log shows `PowerBread 1.0.0`, no INA3221/display errors.
3. `powerbread read` shows plausible V on both channels (e.g. ~5.0 V input).
4. Dashboard shows both channels updating; put a load on one channel and
   confirm mA/mW move.
5. Dial up/down cycles the three screens; short press flips CH1/CH2 in the
   header; long press zeroes the stats screen.
6. If the dial misbehaves: run `powerbread dial` while holding each position
   and adjust `press-thresholds-mv` in the overlay to the observed values.
7. If the panel image is offset/mirrored: tune `madctl`/`x-offset`/`y-offset`
   in the overlay (landscape variant documented in the overlay comment).
8. `powerbread stream on` emits ~200 CSV lines/s; `stream off` stops.
```
