# RaspRover H-Bridge Motor Support Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Drive the Waveshare ROS Driver board's two H-bridge motor channels from the `rasprover` app via zenoh `cmd_vel`, with quadrature encoder feedback through the actuator subsystem.

**Architecture:** A new multi-unit ESP32 PCNT encoder sensor driver in `rosterloh-drivers` (one device per PCNT unit) feeds the existing `rosterloh,actuator-hbridge` driver's `encoder` phandle. The board DTS (also in rosterloh-drivers) gains LEDC channels, PCNT units, and two hbridge nodes. The app adds an `app_motors` module (differential mixing + command watchdog) and a zenoh CDR-Twist subscriber.

**Tech Stack:** Zephyr DT/Kconfig, ESP32 PCNT HAL (`hal/pcnt_ll.h`), rosterloh actuator subsystem, zenoh-pico subscriber.

**Spec:** `docs/superpowers/specs/2026-06-10-rasprover-hbridge-motors-design.md`

**Testing note:** The PCNT driver touches real ESP32 silicon and has no emulator; there is no test harness in rosterloh-drivers for it. Verification is: clean builds of both targets (DTS validation + full compile), native_sim runtime check for the app path, and an on-hardware smoke test by the user at the end. TDD is applied where a runnable target exists (native_sim).

**Two repos:**
- Module: `/home/rio/workspace/zephyr-applications/deps/modules/lib/rosterloh-drivers` (git repo `rosterloh/zephyr-drivers`, currently detached HEAD at `6278d08` = pinned main). All paths starting with `drivers/`, `dts/`, `boards/` below are relative to this directory.
- App: `/home/rio/workspace/zephyr-applications` (paths starting with `applications/` relative to this).
- All build commands run from `/home/rio/workspace/zephyr-applications` via `uv run poe …`.

---

### Task 1: Branch the module repo

**Files:** none (git only)

- [ ] **Step 1: Create a feature branch in rosterloh-drivers**

```bash
git -C deps/modules/lib/rosterloh-drivers checkout -b feat/rasprover-motors
```

Expected: `Switched to a new branch 'feat/rasprover-motors'` (from detached HEAD `6278d08`).

- [ ] **Step 2: Create a feature branch in the app repo**

```bash
git checkout -b feat/rasprover-motors
```

Expected: branch created from `main`. Note: `poe.toml` has pre-existing uncommitted changes — leave them alone, never `git add` it.

---

### Task 2: PCNT encoder DT binding (module repo)

**Files:**
- Create: `dts/bindings/sensor/rosterloh,esp32-pcnt.yaml`

- [ ] **Step 1: Write the binding**

```yaml
description: |
  ESP32 PCNT pulse-counter block exposing each counting unit as its own
  encoder sensor device.

  Unlike the upstream espressif,esp32-pcnt sensor driver (which only reports
  unit 0), every enabled unit child node here becomes an independent Zephyr
  device, so multiple encoders (e.g. one per wheel) can be read separately.
  Apply this compatible to the SoC's pcnt node in a board dts to replace the
  upstream driver.

  Each unit uses PCNT channel 0 only: the signal input counts edges, the
  control input optionally gates/inverts the count direction. The vendor
  half-quad scheme (ESP32Encoder::attachHalfQuad) is sig-pos-mode = 2,
  sig-neg-mode = 1, ctrl-h-mode = 0, ctrl-l-mode = 1.

  The 16-bit hardware counter wraps at +/-30000 via high/low limit events
  that the driver accumulates into a 64-bit software total. The accumulated
  count is reported on SENSOR_CHAN_ROTATION in degrees, scaled by
  counts-per-revolution.

compatible: "rosterloh,esp32-pcnt"

include: [base.yaml, pinctrl-device.yaml]

child-binding:
  description: PCNT unit configured as an encoder input.

  properties:
    reg:
      type: int
      required: true
      description: PCNT unit index (0..7 on ESP32).

    counts-per-revolution:
      type: int
      required: true
      description: |
        Hardware counts per revolution of the measured shaft, including any
        decode multiplier and gearbox ratio. Used to scale counts to degrees
        for SENSOR_CHAN_ROTATION.

    filter:
      type: int
      default: 0
      description: |
        Glitch filter threshold in APB clock cycles (80 MHz: 800 = 10 us).
        0 disables the filter. Hardware maximum is 1023.

    sig-pos-mode:
      type: int
      default: 1
      enum: [0, 1, 2]
      description: |
        Action on positive edge of the signal input.
        0 = hold, 1 = increment, 2 = decrement.

    sig-neg-mode:
      type: int
      default: 0
      enum: [0, 1, 2]
      description: |
        Action on negative edge of the signal input.
        0 = hold, 1 = increment, 2 = decrement.

    ctrl-h-mode:
      type: int
      default: 0
      enum: [0, 1, 2]
      description: |
        Modifier while the control input is high.
        0 = keep, 1 = invert, 2 = hold.

    ctrl-l-mode:
      type: int
      default: 0
      enum: [0, 1, 2]
      description: |
        Modifier while the control input is low.
        0 = keep, 1 = invert, 2 = hold.
```

- [ ] **Step 2: Commit (module repo)**

```bash
git -C deps/modules/lib/rosterloh-drivers add dts/bindings/sensor/rosterloh,esp32-pcnt.yaml
git -C deps/modules/lib/rosterloh-drivers commit -m "dts: bindings: add rosterloh,esp32-pcnt multi-unit encoder binding"
```

---

### Task 3: PCNT encoder driver (module repo)

**Files:**
- Create: `drivers/sensor/esp32_pcnt_encoder/Kconfig`
- Create: `drivers/sensor/esp32_pcnt_encoder/CMakeLists.txt`
- Create: `drivers/sensor/esp32_pcnt_encoder/esp32_pcnt_encoder.c`
- Modify: `drivers/sensor/Kconfig` (add rsource line)
- Modify: `drivers/sensor/CMakeLists.txt` (add subdirectory line)

Reference for the HAL API: upstream driver `deps/zephyr/drivers/sensor/espressif/pcnt_esp32/pcnt_esp32.c` and `deps/modules/hal/espressif/components/esp_hal_pcnt/esp32/include/hal/pcnt_ll.h`. Key facts:
- `pcnt_ll_get_count()` returns the live signed 16-bit counter.
- Raw unit status bits (from `pcnt_ll_get_unit_status`): bit2 = thres1, bit3 = thres0, bit4 = low limit, bit5 = high limit. (`pcnt_ll_get_event_status` is the same value `>> 2`.)
- Interrupt status from `pcnt_ll_get_intr_status` is one bit per unit (`BIT(unit)`).
- The hardware counter resets to 0 automatically when it reaches the high/low limit, so the ISR adds the limit value to the software accumulator.
- Edge actions: 0 = hold, 1 = increase, 2 = decrease. Level actions: 0 = keep, 1 = inverse, 2 = hold. These match the binding's property encodings directly.

- [ ] **Step 1: Write `drivers/sensor/esp32_pcnt_encoder/Kconfig`**

```kconfig
config ESP32_PCNT_ENCODER
	bool "ESP32 PCNT multi-unit encoder"
	default y
	depends on DT_HAS_ROSTERLOH_ESP32_PCNT_ENABLED
	depends on PINCTRL
	help
	  Encoder driver for the ESP32 pulse counter (PCNT) block. Each unit
	  child node becomes its own sensor device reporting accumulated
	  position on SENSOR_CHAN_ROTATION (degrees), with 16-bit hardware
	  overflow folded into a 64-bit software accumulator via limit-event
	  interrupts.

if ESP32_PCNT_ENCODER
module = ESP32_PCNT_ENCODER
module-str = esp32_pcnt_encoder
source "subsys/logging/Kconfig.template.log_config"
endif
```

- [ ] **Step 2: Write `drivers/sensor/esp32_pcnt_encoder/CMakeLists.txt`**

```cmake
zephyr_library()
zephyr_library_sources_ifdef(CONFIG_ESP32_PCNT_ENCODER esp32_pcnt_encoder.c)
```

- [ ] **Step 3: Wire into `drivers/sensor/Kconfig` and `drivers/sensor/CMakeLists.txt`**

In `drivers/sensor/Kconfig`, add inside the `if SENSOR` block:

```kconfig
rsource "esp32_pcnt_encoder/Kconfig"
```

In `drivers/sensor/CMakeLists.txt`, add:

```cmake
add_subdirectory_ifdef(CONFIG_ESP32_PCNT_ENCODER esp32_pcnt_encoder)
```

- [ ] **Step 4: Write `drivers/sensor/esp32_pcnt_encoder/esp32_pcnt_encoder.c`**

```c
/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Multi-unit encoder driver for the ESP32 PCNT block. Each enabled unit
 * child node of the rosterloh,esp32-pcnt controller is its own sensor
 * device. The 16-bit hardware counter is extended to 64 bits by
 * accumulating high/low-limit events in an ISR (the hardware clears the
 * counter to zero whenever a limit is hit).
 */

#define DT_DRV_COMPAT rosterloh_esp32_pcnt

/* Include esp-idf headers first to avoid redefining BIT() macro */
#include <hal/pcnt_hal.h>
#include <hal/pcnt_ll.h>

#include <soc.h>
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/interrupt_controller/intc_esp32.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(esp32_pcnt_encoder, CONFIG_ESP32_PCNT_ENCODER_LOG_LEVEL);

#define PCNT_ENC_LIMIT 30000

/* Raw unit status bits (pcnt_ll_get_unit_status) */
#define PCNT_ENC_STATUS_LOW_LIMIT  BIT(4)
#define PCNT_ENC_STATUS_HIGH_LIMIT BIT(5)

#define PCNT_ENC_MAX_UNITS 8

struct pcnt_enc_unit_data {
	int64_t acc;   /* limit-event accumulator, ISR-updated */
	int64_t total; /* snapshot taken by sample_fetch */
};

struct pcnt_enc_unit_config {
	uint8_t idx;
	uint16_t filter;
	uint32_t counts_per_rev;
	uint8_t sig_pos_mode;
	uint8_t sig_neg_mode;
	uint8_t ctrl_h_mode;
	uint8_t ctrl_l_mode;
};

/* Controller-level state shared by all unit devices (single PCNT block). */
static struct {
	pcnt_hal_context_t hal;
	bool initialized;
	struct pcnt_enc_unit_data *units[PCNT_ENC_MAX_UNITS];
} pcnt_enc_shared = {
	.hal = {.dev = (pcnt_dev_t *)DT_INST_REG_ADDR(0)},
};

PINCTRL_DT_INST_DEFINE(0);
static const struct pinctrl_dev_config *pcnt_enc_pincfg = PINCTRL_DT_INST_DEV_CONFIG_GET(0);

static void IRAM_ATTR pcnt_enc_isr(void *arg)
{
	ARG_UNUSED(arg);
	pcnt_dev_t *hw = pcnt_enc_shared.hal.dev;
	uint32_t intr_status = pcnt_ll_get_intr_status(hw);

	pcnt_ll_clear_intr_status(hw, intr_status);

	for (int i = 0; i < PCNT_ENC_MAX_UNITS; i++) {
		if (!(intr_status & BIT(i)) || pcnt_enc_shared.units[i] == NULL) {
			continue;
		}
		uint32_t st = pcnt_ll_get_unit_status(hw, i);

		if (st & PCNT_ENC_STATUS_HIGH_LIMIT) {
			pcnt_enc_shared.units[i]->acc += PCNT_ENC_LIMIT;
		} else if (st & PCNT_ENC_STATUS_LOW_LIMIT) {
			pcnt_enc_shared.units[i]->acc -= PCNT_ENC_LIMIT;
		}
	}
}

static int pcnt_enc_global_init(void)
{
	int err;
	const struct device *clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(0));

	err = clock_control_on(clock_dev, (clock_control_subsys_t)DT_INST_CLOCKS_CELL(0, offset));
	if (err < 0) {
		LOG_ERR("clock enable failed (%d)", err);
		return err;
	}

	err = pinctrl_apply_state(pcnt_enc_pincfg, PINCTRL_STATE_DEFAULT);
	if (err < 0) {
		LOG_ERR("pinctrl apply failed (%d)", err);
		return err;
	}

	pcnt_hal_init(&pcnt_enc_shared.hal, 0);

	err = esp_intr_alloc(DT_INST_IRQ_BY_IDX(0, 0, irq),
			     ESP_PRIO_TO_FLAGS(DT_INST_IRQ_BY_IDX(0, 0, priority)) |
				     ESP_INT_FLAGS_CHECK(DT_INST_IRQ_BY_IDX(0, 0, flags)) |
				     ESP_INTR_FLAG_IRAM,
			     pcnt_enc_isr, NULL, NULL);
	if (err != 0) {
		LOG_ERR("isr alloc failed (%d)", err);
		return err;
	}

	pcnt_enc_shared.initialized = true;
	return 0;
}

static int pcnt_enc_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	const struct pcnt_enc_unit_config *cfg = dev->config;
	struct pcnt_enc_unit_data *data = dev->data;

	if (chan != SENSOR_CHAN_ALL && chan != SENSOR_CHAN_ROTATION) {
		return -ENOTSUP;
	}

	unsigned int key = irq_lock();

	data->total = data->acc + pcnt_ll_get_count(pcnt_enc_shared.hal.dev, cfg->idx);
	irq_unlock(key);

	return 0;
}

static int pcnt_enc_channel_get(const struct device *dev, enum sensor_channel chan,
				struct sensor_value *val)
{
	const struct pcnt_enc_unit_config *cfg = dev->config;
	struct pcnt_enc_unit_data *data = dev->data;

	if (chan != SENSOR_CHAN_ROTATION) {
		return -ENOTSUP;
	}

	/* counts -> micro-degrees, then split into sensor_value */
	int64_t udeg = data->total * 360LL * 1000000LL / cfg->counts_per_rev;

	val->val1 = (int32_t)(udeg / 1000000LL);
	val->val2 = (int32_t)(udeg % 1000000LL);
	return 0;
}

static int pcnt_enc_unit_init(const struct device *dev)
{
	const struct pcnt_enc_unit_config *cfg = dev->config;
	struct pcnt_enc_unit_data *data = dev->data;
	pcnt_dev_t *hw;
	int err;

	if (!pcnt_enc_shared.initialized) {
		err = pcnt_enc_global_init();
		if (err < 0) {
			return err;
		}
	}
	hw = pcnt_enc_shared.hal.dev;

	pcnt_enc_shared.units[cfg->idx] = data;

	pcnt_ll_stop_count(hw, cfg->idx);
	pcnt_ll_disable_all_events(hw, cfg->idx);

	/* Channel 0 carries the encoder; channel 1 must not count. */
	pcnt_ll_set_edge_action(hw, cfg->idx, 0, cfg->sig_pos_mode, cfg->sig_neg_mode);
	pcnt_ll_set_level_action(hw, cfg->idx, 0, cfg->ctrl_h_mode, cfg->ctrl_l_mode);
	pcnt_ll_set_edge_action(hw, cfg->idx, 1, PCNT_CHANNEL_EDGE_ACTION_HOLD,
				PCNT_CHANNEL_EDGE_ACTION_HOLD);
	pcnt_ll_set_level_action(hw, cfg->idx, 1, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
				 PCNT_CHANNEL_LEVEL_ACTION_KEEP);

	pcnt_ll_set_high_limit_value(hw, cfg->idx, PCNT_ENC_LIMIT);
	pcnt_ll_set_low_limit_value(hw, cfg->idx, -PCNT_ENC_LIMIT);
	pcnt_ll_enable_high_limit_event(hw, cfg->idx, true);
	pcnt_ll_enable_low_limit_event(hw, cfg->idx, true);

	pcnt_ll_set_glitch_filter_thres(hw, cfg->idx, cfg->filter);
	pcnt_ll_enable_glitch_filter(hw, cfg->idx, cfg->filter != 0);

	pcnt_ll_clear_count(hw, cfg->idx);
	pcnt_ll_enable_intr(hw, BIT(cfg->idx), true);
	pcnt_ll_start_count(hw, cfg->idx);

	return 0;
}

static DEVICE_API(sensor, pcnt_enc_api) = {
	.sample_fetch = pcnt_enc_sample_fetch,
	.channel_get = pcnt_enc_channel_get,
};

#define PCNT_ENC_UNIT_DEFINE(node_id)                                                              \
	static struct pcnt_enc_unit_data pcnt_enc_data_##node_id;                                  \
	static const struct pcnt_enc_unit_config pcnt_enc_cfg_##node_id = {                        \
		.idx = DT_REG_ADDR(node_id),                                                       \
		.filter = MIN(DT_PROP(node_id, filter), PCNT_LL_MAX_GLITCH_WIDTH),                 \
		.counts_per_rev = DT_PROP(node_id, counts_per_revolution),                         \
		.sig_pos_mode = DT_PROP(node_id, sig_pos_mode),                                    \
		.sig_neg_mode = DT_PROP(node_id, sig_neg_mode),                                    \
		.ctrl_h_mode = DT_PROP(node_id, ctrl_h_mode),                                      \
		.ctrl_l_mode = DT_PROP(node_id, ctrl_l_mode),                                      \
	};                                                                                         \
	SENSOR_DEVICE_DT_DEFINE(node_id, pcnt_enc_unit_init, NULL, &pcnt_enc_data_##node_id,       \
				&pcnt_enc_cfg_##node_id, POST_KERNEL,                              \
				CONFIG_SENSOR_INIT_PRIORITY, &pcnt_enc_api);

DT_INST_FOREACH_CHILD_STATUS_OKAY(0, PCNT_ENC_UNIT_DEFINE)
```

- [ ] **Step 5: clang-format check**

```bash
uv run clang-format --dry-run --Werror deps/modules/lib/rosterloh-drivers/drivers/sensor/esp32_pcnt_encoder/esp32_pcnt_encoder.c
```

Expected: no output (clean). Fix any complaints with `uv run clang-format -i <file>`.

- [ ] **Step 6: Commit (module repo)**

```bash
git -C deps/modules/lib/rosterloh-drivers add drivers/sensor/esp32_pcnt_encoder drivers/sensor/Kconfig drivers/sensor/CMakeLists.txt
git -C deps/modules/lib/rosterloh-drivers commit -m "drivers: sensor: add ESP32 PCNT multi-unit encoder driver"
```

Note: compile verification happens in Task 4 Step 4, once DT nodes exist to instantiate the driver.

---

### Task 4: Board DTS — pinctrl, LEDC channels, PCNT units, hbridge nodes (module repo)

**Files:**
- Modify: `boards/waveshare/ros_driver/ros_driver-pinctrl.dtsi`
- Modify: `boards/waveshare/ros_driver/ros_driver_procpu.dts`

- [ ] **Step 1: Add motor PWM pins and PCNT pins to `ros_driver-pinctrl.dtsi`**

Change the `ledc0_default` group and append a `pcnt_default` group (inside the existing `&pinctrl { … }` block):

```dts
	ledc0_default: ledc0_default {
		group1 {
			pinmux = <LEDC_CH0_GPIO4>,
				 <LEDC_CH1_GPIO5>,
				 <LEDC_CH2_GPIO25>,
				 <LEDC_CH3_GPIO26>;
			output-enable;
		};
	};

	pcnt_default: pcnt_default {
		group1 {
			pinmux = <PCNT0_CH0SIG_GPIO35>,
				 <PCNT0_CH0CTRL_GPIO34>,
				 <PCNT1_CH0SIG_GPIO27>,
				 <PCNT1_CH0CTRL_GPIO16>;
			bias-pull-up;
		};
	};
```

(The `ledc0_default` edit replaces the existing two-entry pinmux list; everything else in the file stays untouched.)

- [ ] **Step 2: Add LEDC channels, PCNT units, hbridge nodes and aliases to `ros_driver_procpu.dts`**

In the existing `aliases` node, append:

```dts
		left-motor = &left_motor;
		right-motor = &right_motor;
```

In the existing `&ledc0` node, append after `channel1@1`:

```dts
	channel2@2 {
		reg = <0x2>;
		timer = <2>;
	};
	channel3@3 {
		reg = <0x3>;
		timer = <3>;
	};
```

Append these new top-level node overrides at the end of the file:

```dts
&pcnt {
	compatible = "rosterloh,esp32-pcnt";
	pinctrl-0 = <&pcnt_default>;
	pinctrl-names = "default";
	status = "okay";
	#address-cells = <1>;
	#size-cells = <0>;

	/* Half-quad decode (vendor ESP32Encoder::attachHalfQuad):
	 * both edges of the signal input count, control input inverts.
	 * 2100 counts per wheel revolution (Waveshare RaspRover).
	 */
	pcnt_unit0: unit0@0 {
		reg = <0>;
		counts-per-revolution = <2100>;
		filter = <800>; /* 10 us at 80 MHz APB */
		sig-pos-mode = <2>;
		sig-neg-mode = <1>;
		ctrl-h-mode = <0>;
		ctrl-l-mode = <1>;
	};

	pcnt_unit1: unit1@1 {
		reg = <1>;
		counts-per-revolution = <2100>;
		filter = <800>;
		sig-pos-mode = <2>;
		sig-neg-mode = <1>;
		ctrl-h-mode = <0>;
		ctrl-l-mode = <1>;
	};
};
```

And inside the root node (`/ { … }`, after the `buttons` block):

```dts
	/* TB6612-style dual H-bridge. Vendor-positive drive is IN1 low /
	 * IN2 high, so in1/in2 are swapped relative to the AIN1/AIN2
	 * silkscreen names to make the driver's forward match the
	 * vendor's forward while keeping brake = both-high.
	 */
	left_motor: motor_left {
		compatible = "rosterloh,actuator-hbridge";
		pwms = <&ledc0 2 PWM_HZ(100000) PWM_POLARITY_NORMAL>;
		in1-gpios = <&gpio0 17 GPIO_ACTIVE_HIGH>; /* AIN2 */
		in2-gpios = <&gpio0 21 GPIO_ACTIVE_HIGH>; /* AIN1 */
		encoder = <&pcnt_unit0>;
		pwm-period-ns = <10000>; /* 100 kHz, vendor-proven */
		default-mode = "velocity";
		label = "Left motor";
	};

	right_motor: motor_right {
		compatible = "rosterloh,actuator-hbridge";
		pwms = <&ledc0 3 PWM_HZ(100000) PWM_POLARITY_NORMAL>;
		in1-gpios = <&gpio0 23 GPIO_ACTIVE_HIGH>; /* BIN2 */
		in2-gpios = <&gpio0 22 GPIO_ACTIVE_HIGH>; /* BIN1 */
		encoder = <&pcnt_unit1>;
		pwm-period-ns = <10000>;
		default-mode = "velocity";
		label = "Right motor";
	};
```

- [ ] **Step 3: Commit (module repo)**

```bash
git -C deps/modules/lib/rosterloh-drivers add boards/waveshare/ros_driver
git -C deps/modules/lib/rosterloh-drivers commit -m "boards: ros_driver: add h-bridge motors, PCNT encoders, LEDC channels"
```

- [ ] **Step 4: Verify the DTS + driver compile against the unmodified app**

The app doesn't enable `CONFIG_ACTUATOR` yet, so the hbridge nodes are inert, but the PCNT driver compiles (`CONFIG_SENSOR=y` is already set and the new compatible defaults the driver on) and all DT references are validated:

```bash
uv run poe agent-build rasprover --sysbuild
```

Expected: success, log tail printed; full log at `logs/rasprover-build.log`. If DT errors appear, fix the dts/binding and amend the module commit.

---

### Task 5: App Kconfig + app_motors module (app repo)

**Files:**
- Create: `applications/rasprover/src/app_motors.h`
- Create: `applications/rasprover/src/app_motors.c`
- Modify: `applications/rasprover/Kconfig` (append before `source "Kconfig.zephyr"`)
- Modify: `applications/rasprover/CMakeLists.txt` (add conditional source)
- Modify: `applications/rasprover/prj.conf` (enable actuator/PWM configs)
- Modify: `applications/rasprover/src/main.c` (init call)

- [ ] **Step 1: Add APP_MOTORS Kconfig options**

In `applications/rasprover/Kconfig`, insert before `source "Kconfig.zephyr"`:

```kconfig
menuconfig APP_MOTORS
	bool "Enable differential drive motor control"
	default y
	depends on ACTUATOR
	help
	  Drives the two h-bridge wheel motors from cmd_vel twist commands
	  with open-loop duty mixing and a stop-on-silence watchdog.

if APP_MOTORS

config APP_MOTORS_WHEEL_SEPARATION_MM
	int "Wheel separation (track width) in mm"
	default 125
	help
	  Distance between the left and right wheel contact points.
	  Vendor value for the Waveshare RaspRover is 125 mm.

config APP_MOTORS_MAX_SPEED_MM_S
	int "Wheel speed mapped to 100% duty, in mm/s"
	default 500
	help
	  Open-loop scaling: a commanded wheel speed of this magnitude
	  produces full PWM duty. Tune on hardware.

config APP_MOTORS_CMD_TIMEOUT_MS
	int "Stop motors after this many ms without a cmd_vel"
	default 500

endif # APP_MOTORS
```

- [ ] **Step 2: Write `applications/rasprover/src/app_motors.h`**

```c
#ifndef __APP_MOTORS_H__
#define __APP_MOTORS_H__

#include <stdbool.h>

bool app_motors_init(void);

/* Differential-drive a geometry_msgs/Twist: linear.x (m/s), angular.z (rad/s). */
void app_motors_cmd_vel(float linear_x, float angular_z);

#endif /* __APP_MOTORS_H__ */
```

- [ ] **Step 3: Write `applications/rasprover/src/app_motors.c`**

```c
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
```

- [ ] **Step 4: Wire into the build and main()**

`applications/rasprover/CMakeLists.txt` — after the existing `target_sources_ifdef` lines:

```cmake
target_sources_ifdef(CONFIG_APP_MOTORS app PRIVATE src/app_motors.c)
```

`applications/rasprover/src/main.c` — add the include below the existing app includes:

```c
#include "app_motors.h"
```

and in `main()` after `app_sensors_init();`:

```c
#ifdef CONFIG_APP_MOTORS
	app_motors_init();
#endif
```

(`app_motors.h` has no config-dependent content, so the unconditional include is safe.)

- [ ] **Step 5: Enable configs in `applications/rasprover/prj.conf`**

Append under the `# Drivers` section:

```
# Motors: h-bridge actuators + PCNT encoders
CONFIG_ACTUATOR=y
CONFIG_ACTUATOR_HBRIDGE=y
CONFIG_ACTUATOR_HBRIDGE_ENCODER=y
CONFIG_PWM=y
```

- [ ] **Step 6: Build for hardware**

```bash
uv run poe agent-build rasprover --sysbuild
```

Expected: success. The hbridge driver now instantiates both motors with encoder phandles; failures here are usually missing Kconfig deps (check `logs/rasprover-build.log`).

- [ ] **Step 7: clang-format check + commit (app repo)**

```bash
uv run clang-format --dry-run --Werror applications/rasprover/src/app_motors.c applications/rasprover/src/app_motors.h applications/rasprover/src/main.c
git add applications/rasprover/src/app_motors.c applications/rasprover/src/app_motors.h applications/rasprover/src/main.c applications/rasprover/Kconfig applications/rasprover/CMakeLists.txt applications/rasprover/prj.conf
git commit -m "rasprover: add app_motors differential drive module"
```

---

### Task 6: zenoh cmd_vel subscriber (app repo)

**Files:**
- Modify: `applications/rasprover/src/app_zenoh.c`
- Modify: `applications/rasprover/Kconfig` (APP_ZENOH selects)
- Modify: `applications/rasprover/prj.conf`

- [ ] **Step 1: Enable subscription support**

In `applications/rasprover/Kconfig`, inside `menuconfig APP_ZENOH`'s select list (after `select ZENOH_PICO_PUBLICATION`):

```kconfig
	select ZENOH_PICO_SUBSCRIPTION
```

In `applications/rasprover/prj.conf`, after `CONFIG_ZENOH_PICO_PUBLICATION=y`:

```
CONFIG_ZENOH_PICO_SUBSCRIPTION=y
```

- [ ] **Step 2: Add the subscriber to `applications/rasprover/src/app_zenoh.c`**

Below `#define BATTERY_STATE_KEY …` add:

```c
#define CMD_VEL_KEY CONFIG_APP_ZENOH_KEY_PREFIX "/cmd_vel"
```

Below the existing includes add:

```c
#ifdef CONFIG_APP_MOTORS
#include "app_motors.h"
#endif
```

Next to the `_pub_battery` static add:

```c
#ifdef CONFIG_APP_MOTORS
static z_owned_subscriber_t _sub_cmd_vel;
#endif
```

Add the handler and a declare helper above `app_zenoh_init()`:

```c
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

	if (z_declare_subscriber(z_loan(_session), &_sub_cmd_vel, z_loan(ke),
				 z_move(callback), NULL) < 0) {
		LOG_ERR("zenoh subscriber declare failed for '%s'", CMD_VEL_KEY);
		return false;
	}

	LOG_INF("zenoh subscribed to '%s'", CMD_VEL_KEY);
	return true;
}
#endif /* CONFIG_APP_MOTORS */
```

In `app_zenoh_init()`, after the publisher declare succeeds (before `LOG_INF("zenoh ready…")`):

```c
#ifdef CONFIG_APP_MOTORS
	if (!declare_cmd_vel_subscriber()) {
		LOG_WRN("continuing without cmd_vel subscription");
	}
#endif
```

(A failed subscriber declare is non-fatal: telemetry still flows, watchdog keeps motors stopped.)

- [ ] **Step 3: Build for hardware**

```bash
uv run poe agent-build rasprover --sysbuild
```

Expected: success.

- [ ] **Step 4: clang-format check + commit (app repo)**

```bash
uv run clang-format --dry-run --Werror applications/rasprover/src/app_zenoh.c
git add applications/rasprover/src/app_zenoh.c applications/rasprover/Kconfig applications/rasprover/prj.conf
git commit -m "rasprover: subscribe to cmd_vel and drive motors"
```

---

### Task 7: native_sim support with fake actuators (app repo)

**Files:**
- Modify: `applications/rasprover/boards/native_sim_native_64.overlay`
- Modify: `applications/rasprover/boards/native_sim_native_64.conf`

- [ ] **Step 1: Add fake actuator nodes + aliases to the overlay**

Append to `applications/rasprover/boards/native_sim_native_64.overlay`:

```dts
/* Fake motors so the cmd_vel -> mixing -> actuator path runs in sim */
/ {
	aliases {
		left-motor = &fake_motor_left;
		right-motor = &fake_motor_right;
	};

	fake_motor_left: fake-motor-left {
		compatible = "rosterloh,actuator-fake";
	};

	fake_motor_right: fake-motor-right {
		compatible = "rosterloh,actuator-fake";
	};
};
```

- [ ] **Step 2: Adjust `applications/rasprover/boards/native_sim_native_64.conf`**

Append:

```
# Motors: fake actuators replace the h-bridge hardware
CONFIG_ACTUATOR_HBRIDGE=n
CONFIG_ACTUATOR_HBRIDGE_ENCODER=n
CONFIG_ACTUATOR_FAKE=y
CONFIG_PWM=n
```

- [ ] **Step 3: Build and run native_sim**

```bash
uv run poe app rasprover --board native_sim/native/64
timeout 5 ./builds/rasprover/zephyr/zephyr.exe || true
```

Expected: build succeeds; the run prints boot logs including `<inf> app_motors: motors ready` (zenoh/cmd_vel is disabled on native_sim, so no subscriber log). SDL display window may open — the `timeout` kills it after 5 s.

- [ ] **Step 4: Commit (app repo)**

```bash
git add applications/rasprover/boards/native_sim_native_64.overlay applications/rasprover/boards/native_sim_native_64.conf
git commit -m "rasprover: fake actuators for native_sim motor path"
```

---

### Task 8: CHANGELOG, final verification, PRs

**Files:**
- Modify: `applications/rasprover/CHANGELOG.md`

- [ ] **Step 1: Add a CHANGELOG entry**

Under the top of `applications/rasprover/CHANGELOG.md` (after the intro paragraphs, above `## [v1.0.0]`):

```markdown
## [Unreleased]

### Added

- Differential-drive motor control for the two onboard h-bridges via the
  actuator subsystem, with quadrature PCNT encoder feedback.
- Zenoh `cmd_vel` subscriber (CDR `geometry_msgs/Twist`) with a
  stop-on-silence watchdog.
```

```bash
git add applications/rasprover/CHANGELOG.md
git commit -m "rasprover: changelog for motor support"
```

- [ ] **Step 2: Final clean builds of both targets**

```bash
uv run poe agent-build rasprover --sysbuild
uv run poe agent-build rasprover --board native_sim/native/64
```

Expected: both succeed.

- [ ] **Step 3: Push and open the module PR (rosterloh/zephyr-drivers)**

```bash
git -C deps/modules/lib/rosterloh-drivers push -u origin feat/rasprover-motors
gh pr create --repo rosterloh/zephyr-drivers --head feat/rasprover-motors \
  --title "ESP32 PCNT multi-unit encoder driver + ros_driver motor nodes" \
  --body "$(cat <<'EOF'
Adds a multi-unit ESP32 PCNT encoder sensor driver (one device per PCNT
unit, 64-bit accumulation via limit-event ISR, SENSOR_CHAN_ROTATION in
degrees) and wires the Waveshare ROS Driver board's two h-bridge motors:
LEDC channels 2/3 (100 kHz), PCNT units 0/1 in half-quad mode (2100
counts/rev), rosterloh,actuator-hbridge nodes with vendor-correct
polarity, and left-motor/right-motor aliases.

Pin map and constants cross-checked against waveshareteam/ugv_base_ros.
Verified by building zephyr-applications' rasprover app against this
branch.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
EOF
)"
```

- [ ] **Step 4: Push and open the app PR (this repo)**

```bash
git push -u origin feat/rasprover-motors
gh pr create --title "rasprover: differential drive over zenoh cmd_vel" \
  --body "$(cat <<'EOF'
Adds app_motors (differential mixing, open-loop duty, stop-on-silence
watchdog) and a zenoh cmd_vel subscriber decoding CDR
geometry_msgs/Twist. native_sim builds with fake actuators.

Depends on rosterloh/zephyr-drivers PR "ESP32 PCNT multi-unit encoder
driver + ros_driver motor nodes" — CI for the hardware board will only
pass after that merges and west.yml's pinned main advances
(`uv run poe west-update`).

Spec: docs/superpowers/specs/2026-06-10-rasprover-hbridge-motors-design.md

🤖 Generated with [Claude Code](https://claude.com/claude-code)
EOF
)"
```

- [ ] **Step 5: Hand off to the user for the hardware smoke test**

Manual (user, rover on blocks): flash with `uv run poe flash rasprover`, publish a Twist via the zenoh-ros2dds bridge (e.g. `ros2 topic pub -r 10 /rasprover/cmd_vel geometry_msgs/msg/Twist '{linear: {x: 0.2}}'`), verify both wheels spin forward and stop ~500 ms after the publisher stops. Verify encoder feedback signs with `actuator_get_feedback` (or a follow-up odometry publisher).

---

## Self-review results

- **Spec coverage:** binding+driver (Tasks 2–3) ↔ spec Part 1; board DTS (Task 4) ↔ Part 2; app module/Kconfig (Task 5), zenoh subscriber (Task 6), native_sim (Task 7) ↔ Part 3; verification (Tasks 4/5/6/7/8) ↔ spec Testing. Robot constants 2100/125/500 and polarity/PWM decisions all appear in Tasks 4–5.
- **Known deviations:** none.
- **Type consistency:** `app_motors_cmd_vel(float, float)` matches between Tasks 5 and 6; `DT_ALIAS(left_motor)` matches the `left-motor` alias in Tasks 4 and 7; `CONFIG_ESP32_PCNT_ENCODER` name consistent across Kconfig/CMake; binding property names (`counts-per-revolution`, `sig-pos-mode`, …) match the `DT_PROP` accessors in the driver.
