# Adafruit Seesaw Shields Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship out-of-tree Zephyr shield definitions for three Adafruit Seesaw breakouts (NeoSlider, I2C Rotary Encoder, NeoKey 1x4) inside the `rosterloh-drivers` module, plus a `seesaw_shield_demo` sample, plus a one-line `zephyr_i2c` alias on `robotis_openrb_150` to make the shield ecosystem usable on that board.

**Architecture:** Shields live in `rosterloh-drivers/boards/shields/` (module's `module.yml` already declares `board_root: .`, so west auto-discovers them). Each shield is a directory with `shield.yml`, `Kconfig.shield`, and a single `<name>.overlay` that hooks `&zephyr_i2c`. The demo sample uses `#if DT_HAS_COMPAT_STATUS_OKAY(...)` so the same binary lights up whichever shield child is enabled. Workspace-side changes are scoped to the OpenRB board DTS and this plan.

**Tech Stack:** Zephyr 4.x devicetree + Kconfig, `west build`, twister, the `adafruit,seesaw-mfd` driver in `rosterloh-drivers`, `uv run poe` task runner.

**Spec:** `docs/superpowers/specs/2026-05-12-adafruit-seesaw-shields-design.md`

**Prerequisite reading:**

- `CLAUDE.md` — required workflow rules. Every Python invocation goes through `uv run`. Build via `uv run poe`, never bare `west`. Out-of-tree-module work is committed inside `deps/modules/lib/rosterloh-drivers/`, PR'd against `rosterloh/zephyr-drivers`, and **not** vendored into this repo.
- `deps/modules/lib/rosterloh-drivers/dts/bindings/mfd/adafruit,seesaw-mfd.yaml` — MFD parent binding. `read-delay-us` defaults to 250 µs; we ship 10 ms in the shields.
- `deps/modules/lib/rosterloh-drivers/dts/bindings/{adc,gpio,led_strip,sensor}/adafruit,seesaw-*.yaml` — child bindings the overlays compose.
- `deps/zephyr/boards/shields/adafruit_aw9523/` — closest upstream analogue for shield-layout reference.
- Existing per-app overlays whose inline nodes we're cloning into shields:
  - `applications/motor_controller/boards/robotis_openrb_150.overlay` (NeoSlider + Rotary Encoder)
  - `applications/joystick_controller/boards/adafruit_qt_py_esp32s3_esp32s3_procpu.overlay` (NeoKey 1x4, currently commented out)

**Out of scope (do not touch):**

- `applications/motor_controller/` and `applications/joystick_controller/` source/overlays. The spec explicitly leaves apps alone.
- `poe.toml` — no shield forwarding needed in this round.
- Any address-variant shields, doc directories, or other Seesaw breakouts not listed.

**Git flow:** Two branches, two PRs.

| Repo | Branch | Contains | PR target |
|---|---|---|---|
| `rosterloh-drivers` (at `deps/modules/lib/rosterloh-drivers/`) | `feat/seesaw-shields` | All 3 shields + sample | `rosterloh/zephyr-drivers` |
| `zephyr-applications` (workspace root) | `feat/seesaw-shields-support` | OpenRB `zephyr_i2c` alias + this spec/plan | `rosterloh/zephyr-applications` |

Use `cd deps/modules/lib/rosterloh-drivers && git ...` for module-side commits; workspace-root `git ...` for workspace-side commits. All `west`/`poe` commands run from the workspace root regardless.

**Definition of done:**

1. The three shield directories exist in `rosterloh-drivers/boards/shields/`, each with `shield.yml`, `Kconfig.shield`, `<name>.overlay`.
2. The demo sample exists at `rosterloh-drivers/samples/drivers/seesaw_shield_demo/` and builds for all three shields on `adafruit_qt_py_esp32s3/esp32s3/procpu`.
3. `west build` with `--shield adafruit_neoslider` (and the other two) succeeds against `deps/zephyr/samples/hello_world` on the boards listed in the spec.
4. The OpenRB-150 DTS exposes `zephyr_i2c` as an alias of `arduino_mkr_i2c`.
5. `uv run poe agent-build motor_controller` and `uv run poe agent-build joystick_controller` still succeed (regression check).
6. Two PRs are open and pass CI.

---

## File Structure

```
deps/modules/lib/rosterloh-drivers/                   # MODULE-SIDE (branch: feat/seesaw-shields)
  boards/shields/
    adafruit_neoslider/
      shield.yml
      Kconfig.shield
      adafruit_neoslider.overlay
    adafruit_rotary_encoder/
      shield.yml
      Kconfig.shield
      adafruit_rotary_encoder.overlay
    adafruit_neokey_1x4/
      shield.yml
      Kconfig.shield
      adafruit_neokey_1x4.overlay
  samples/drivers/seesaw_shield_demo/
    CMakeLists.txt
    Kconfig
    prj.conf
    sample.yaml
    README.rst
    src/main.c

boards/robotis/openrb_150/                            # WORKSPACE-SIDE (branch: feat/seesaw-shields-support)
  robotis_openrb_150.dts                              # MODIFIED: add zephyr_i2c alias
```

| File | Responsibility |
|---|---|
| `boards/shields/<name>/shield.yml` | Declares shield name, vendor, features for `west boards --shield` discovery. |
| `boards/shields/<name>/Kconfig.shield` | One-line `def_bool $(shields_list_contains,<name>)` so `CONFIG_SHIELD_*` is set when the shield is selected. |
| `boards/shields/<name>/<name>.overlay` | Hooks `&zephyr_i2c`, instantiates `seesaw-mfd` at the breakout's default address, declares the children that breakout supports. |
| `samples/.../seesaw_shield_demo/src/main.c` | Single source with `#if DT_HAS_COMPAT_STATUS_OKAY(...)` blocks. ADC slider polling, encoder sensor reads, GPIO keypad polling, NeoPixel color cycle. |
| `samples/.../seesaw_shield_demo/sample.yaml` | Three twister scenarios, one per shield, each passing the shield name via `extra_args: SHIELD=...`. |
| `boards/robotis/openrb_150/robotis_openrb_150.dts` | Workspace-owned board DTS. Append `zephyr_i2c: &arduino_mkr_i2c {};` at the bottom; everything else untouched. |

---

## Task 1: Set up module branch and verify clean baseline

**Files:** none (state-setup task)

This is the only task that must run first; everything else under "module side" depends on this branch.

- [ ] **Step 1: Confirm workspace is clean and on `main`**

```bash
cd /home/rio/src/github/rosterloh/zephyr-applications
git status --short
git rev-parse --abbrev-ref HEAD
```

Expected: `git status` shows only the spec+plan docs (untracked or on a workspace branch you intend to use). Current branch is whichever workspace branch we'll commit the OpenRB alias on (typically `main` or a fresh `feat/seesaw-shields-support`).

- [ ] **Step 2: Create the module-side feature branch**

```bash
cd /home/rio/src/github/rosterloh/zephyr-applications/deps/modules/lib/rosterloh-drivers
git status --short
git checkout -b feat/seesaw-shields main
git rev-parse --abbrev-ref HEAD
```

Expected: `git status` shows clean tree (no uncommitted edits). New branch `feat/seesaw-shields` is checked out, based on `main`.

If `git status` shows uncommitted edits in the module, stop and surface them — they likely belong to an earlier in-flight branch and shouldn't be folded into this one.

- [ ] **Step 3: Verify the module's `module.yml` declares `board_root: .`**

```bash
cat /home/rio/src/github/rosterloh/zephyr-applications/deps/modules/lib/rosterloh-drivers/zephyr/module.yml
```

Expected output contains:

```yaml
build:
  ...
  settings:
    board_root: .
    dts_root: .
```

If `board_root` is missing or different, stop — the plan's auto-discovery assumption is wrong and the shield directory layout needs to change.

- [ ] **Step 4: Confirm no shield directory exists yet**

```bash
ls /home/rio/src/github/rosterloh/zephyr-applications/deps/modules/lib/rosterloh-drivers/boards/shields/ 2>&1
```

Expected: `ls: cannot access ... No such file or directory` (or an empty listing). Anything else means there's prior work to reconcile.

---

## Task 2: Create the `adafruit_neoslider` shield

**Files:**

- Create: `deps/modules/lib/rosterloh-drivers/boards/shields/adafruit_neoslider/shield.yml`
- Create: `deps/modules/lib/rosterloh-drivers/boards/shields/adafruit_neoslider/Kconfig.shield`
- Create: `deps/modules/lib/rosterloh-drivers/boards/shields/adafruit_neoslider/adafruit_neoslider.overlay`

Mirrors the NeoSlider node currently in `applications/motor_controller/boards/robotis_openrb_150.overlay` lines 67–84, but bound to `&zephyr_i2c` and with `read-delay-us = <10000>` (10 ms instead of the 100 ms used in-app).

- [ ] **Step 1: Create `shield.yml`**

Write `deps/modules/lib/rosterloh-drivers/boards/shields/adafruit_neoslider/shield.yml`:

```yaml
shield:
  name: adafruit_neoslider
  full_name: Adafruit I2C QT Slide Potentiometer with NeoPixel (PID 5295)
  vendor: adafruit
  supported_features:
    - mfd
    - adc
    - led_strip
```

- [ ] **Step 2: Create `Kconfig.shield`**

Write `deps/modules/lib/rosterloh-drivers/boards/shields/adafruit_neoslider/Kconfig.shield`:

```kconfig
# Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
# SPDX-License-Identifier: Apache-2.0

config SHIELD_ADAFRUIT_NEOSLIDER
	def_bool $(shields_list_contains,adafruit_neoslider)
```

- [ ] **Step 3: Create the overlay**

Write `deps/modules/lib/rosterloh-drivers/boards/shields/adafruit_neoslider/adafruit_neoslider.overlay`:

```dts
/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Adafruit I2C QT Slide Potentiometer with NeoPixel (PID 5295).
 * Seesaw firmware exposes the slider on the ADC child (channel 18 on
 * the SAMD09 variant; see Adafruit Learn) and 4 NeoPixels on pin 14.
 */

#include <zephyr/dt-bindings/led/led.h>

&zephyr_i2c {
	status = "okay";

	adafruit_neoslider_mfd: seesaw@30 {
		status = "okay";
		compatible = "adafruit,seesaw-mfd";
		reg = <0x30>;
		read-delay-us = <10000>;

		adafruit_neoslider_adc: adc {
			compatible = "adafruit,seesaw-adc";
			#io-channel-cells = <1>;
		};

		adafruit_neoslider_leds: neopixel {
			compatible = "adafruit,seesaw-neopixel";
			chain-length = <4>;
			color-mapping = <LED_COLOR_ID_GREEN LED_COLOR_ID_RED LED_COLOR_ID_BLUE>;
			seesaw-pin = <14>;
			frequency = <800000>;
		};
	};
};
```

- [ ] **Step 4: Verify the shield is discoverable**

Run:

```bash
cd /home/rio/src/github/rosterloh/zephyr-applications
uv run west boards --shield 2>/dev/null | grep adafruit_neoslider
```

Expected: a line containing `adafruit_neoslider`. If grep returns nothing, the shield isn't being picked up — recheck `module.yml`'s `board_root` and the directory layout.

- [ ] **Step 5: DT compile check on QT Py ESP32S3**

Run:

```bash
cd /home/rio/src/github/rosterloh/zephyr-applications
uv run west build -p always \
  -b adafruit_qt_py_esp32s3/esp32s3/procpu \
  --shield adafruit_neoslider \
  --build-dir builds/shield_check_neoslider \
  deps/zephyr/samples/hello_world
```

Expected: build succeeds. Inspect `builds/shield_check_neoslider/zephyr/zephyr.dts` and confirm:

```bash
grep -A 2 "adafruit_neoslider_mfd:" builds/shield_check_neoslider/zephyr/zephyr.dts
```

Should show the `seesaw@30` node with `compatible = "adafruit,seesaw-mfd"`.

- [ ] **Step 6: Clean the build dir**

```bash
rm -rf builds/shield_check_neoslider
```

Keeps `builds/` tidy; this dir was scratch space only.

- [ ] **Step 7: Commit**

```bash
cd /home/rio/src/github/rosterloh/zephyr-applications/deps/modules/lib/rosterloh-drivers
git add boards/shields/adafruit_neoslider/
git commit -m "$(cat <<'EOF'
boards: shields: add adafruit_neoslider

Shield wraps the NeoSlider breakout (PID 5295) as a single --shield
argument. Pulls in the seesaw-mfd parent at 0x30 plus the adc and
neopixel children that match the chip's physical layout (4-pixel chain
on seesaw pin 14).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: Create the `adafruit_rotary_encoder` shield

**Files:**

- Create: `deps/modules/lib/rosterloh-drivers/boards/shields/adafruit_rotary_encoder/shield.yml`
- Create: `deps/modules/lib/rosterloh-drivers/boards/shields/adafruit_rotary_encoder/Kconfig.shield`
- Create: `deps/modules/lib/rosterloh-drivers/boards/shields/adafruit_rotary_encoder/adafruit_rotary_encoder.overlay`

Mirrors the rotary encoder node in `applications/motor_controller/boards/robotis_openrb_150.overlay` lines 87–96.

- [ ] **Step 1: Create `shield.yml`**

Write `deps/modules/lib/rosterloh-drivers/boards/shields/adafruit_rotary_encoder/shield.yml`:

```yaml
shield:
  name: adafruit_rotary_encoder
  full_name: Adafruit I2C Rotary Encoder Breakout (PID 4991)
  vendor: adafruit
  supported_features:
    - mfd
    - sensor
```

- [ ] **Step 2: Create `Kconfig.shield`**

Write `deps/modules/lib/rosterloh-drivers/boards/shields/adafruit_rotary_encoder/Kconfig.shield`:

```kconfig
# Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
# SPDX-License-Identifier: Apache-2.0

config SHIELD_ADAFRUIT_ROTARY_ENCODER
	def_bool $(shields_list_contains,adafruit_rotary_encoder)
```

- [ ] **Step 3: Create the overlay**

Write `deps/modules/lib/rosterloh-drivers/boards/shields/adafruit_rotary_encoder/adafruit_rotary_encoder.overlay`:

```dts
/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Adafruit I2C Rotary Encoder Breakout (PID 4991).
 * Position is reported via the encoder sensor child as
 * SENSOR_CHAN_ROTATION (signed step count from SEESAW_ENCODER_POSITION).
 */

&zephyr_i2c {
	status = "okay";

	adafruit_rotary_encoder_mfd: seesaw@36 {
		status = "okay";
		compatible = "adafruit,seesaw-mfd";
		reg = <0x36>;
		read-delay-us = <10000>;

		adafruit_rotary_encoder: encoder {
			compatible = "adafruit,seesaw-encoder";
		};
	};
};
```

- [ ] **Step 4: Verify discoverable**

```bash
cd /home/rio/src/github/rosterloh/zephyr-applications
uv run west boards --shield 2>/dev/null | grep adafruit_rotary_encoder
```

Expected: a line containing `adafruit_rotary_encoder`.

- [ ] **Step 5: DT compile check on QT Py ESP32S3**

```bash
uv run west build -p always \
  -b adafruit_qt_py_esp32s3/esp32s3/procpu \
  --shield adafruit_rotary_encoder \
  --build-dir builds/shield_check_rotary_encoder \
  deps/zephyr/samples/hello_world
grep -A 2 "adafruit_rotary_encoder_mfd:" builds/shield_check_rotary_encoder/zephyr/zephyr.dts
```

Expected: build succeeds, grep shows the `seesaw@36` node.

- [ ] **Step 6: Clean and commit**

```bash
rm -rf builds/shield_check_rotary_encoder
cd /home/rio/src/github/rosterloh/zephyr-applications/deps/modules/lib/rosterloh-drivers
git add boards/shields/adafruit_rotary_encoder/
git commit -m "$(cat <<'EOF'
boards: shields: add adafruit_rotary_encoder

Shield wraps the I2C Rotary Encoder breakout (PID 4991). MFD parent at
0x36 plus the encoder sensor child; reports position as
SENSOR_CHAN_ROTATION.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: Create the `adafruit_neokey_1x4` shield

**Files:**

- Create: `deps/modules/lib/rosterloh-drivers/boards/shields/adafruit_neokey_1x4/shield.yml`
- Create: `deps/modules/lib/rosterloh-drivers/boards/shields/adafruit_neokey_1x4/Kconfig.shield`
- Create: `deps/modules/lib/rosterloh-drivers/boards/shields/adafruit_neokey_1x4/adafruit_neokey_1x4.overlay`

Mirrors the (currently commented-out) NeoKey node in `applications/joystick_controller/boards/adafruit_qt_py_esp32s3_esp32s3_procpu.overlay` lines 14–34.

- [ ] **Step 1: Create `shield.yml`**

Write `deps/modules/lib/rosterloh-drivers/boards/shields/adafruit_neokey_1x4/shield.yml`:

```yaml
shield:
  name: adafruit_neokey_1x4
  full_name: Adafruit NeoKey 1x4 QT I2C Keypad (PID 4980)
  vendor: adafruit
  supported_features:
    - mfd
    - gpio
    - led_strip
```

- [ ] **Step 2: Create `Kconfig.shield`**

Write `deps/modules/lib/rosterloh-drivers/boards/shields/adafruit_neokey_1x4/Kconfig.shield`:

```kconfig
# Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
# SPDX-License-Identifier: Apache-2.0

config SHIELD_ADAFRUIT_NEOKEY_1X4
	def_bool $(shields_list_contains,adafruit_neokey_1x4)
```

- [ ] **Step 3: Create the overlay**

Write `deps/modules/lib/rosterloh-drivers/boards/shields/adafruit_neokey_1x4/adafruit_neokey_1x4.overlay`:

```dts
/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Adafruit NeoKey 1x4 QT I2C Keypad (PID 4980).
 * 4 keys are wired to seesaw GPIO pins 4,5,6,7 (active-low). 4 NeoPixels
 * are chained on seesaw pin 3.
 */

#include <zephyr/dt-bindings/led/led.h>

&zephyr_i2c {
	status = "okay";

	adafruit_neokey_1x4_mfd: seesaw@30 {
		status = "okay";
		compatible = "adafruit,seesaw-mfd";
		reg = <0x30>;
		read-delay-us = <10000>;

		adafruit_neokey_1x4_gpio: gpio {
			status = "okay";
			compatible = "adafruit,seesaw-gpio";
			gpio-controller;
			#gpio-cells = <2>;
			ngpios = <16>;
		};

		adafruit_neokey_1x4_leds: neopixel {
			compatible = "adafruit,seesaw-neopixel";
			chain-length = <4>;
			color-mapping = <LED_COLOR_ID_GREEN LED_COLOR_ID_RED LED_COLOR_ID_BLUE>;
			seesaw-pin = <3>;
			frequency = <800000>;
		};
	};
};
```

- [ ] **Step 4: Verify discoverable**

```bash
cd /home/rio/src/github/rosterloh/zephyr-applications
uv run west boards --shield 2>/dev/null | grep adafruit_neokey_1x4
```

Expected: a line containing `adafruit_neokey_1x4`.

- [ ] **Step 5: DT compile check on QT Py ESP32S3**

```bash
uv run west build -p always \
  -b adafruit_qt_py_esp32s3/esp32s3/procpu \
  --shield adafruit_neokey_1x4 \
  --build-dir builds/shield_check_neokey \
  deps/zephyr/samples/hello_world
grep -A 2 "adafruit_neokey_1x4_mfd:" builds/shield_check_neokey/zephyr/zephyr.dts
```

Expected: build succeeds, grep shows the `seesaw@30` node.

- [ ] **Step 6: Clean and commit**

```bash
rm -rf builds/shield_check_neokey
cd /home/rio/src/github/rosterloh/zephyr-applications/deps/modules/lib/rosterloh-drivers
git add boards/shields/adafruit_neokey_1x4/
git commit -m "$(cat <<'EOF'
boards: shields: add adafruit_neokey_1x4

Shield wraps the NeoKey 1x4 keypad (PID 4980). MFD parent at 0x30 plus
the gpio (16 pins, keys on 4-7) and neopixel (4-pixel chain on pin 3)
children.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: Scaffold the `seesaw_shield_demo` sample

**Files:**

- Create: `deps/modules/lib/rosterloh-drivers/samples/drivers/seesaw_shield_demo/CMakeLists.txt`
- Create: `deps/modules/lib/rosterloh-drivers/samples/drivers/seesaw_shield_demo/Kconfig`
- Create: `deps/modules/lib/rosterloh-drivers/samples/drivers/seesaw_shield_demo/prj.conf`
- Create: `deps/modules/lib/rosterloh-drivers/samples/drivers/seesaw_shield_demo/README.rst`

This task creates the build scaffolding only. The actual demo logic lands in Task 6 so the source file can fail-fast on missing `prj.conf` symbols.

- [ ] **Step 1: Create `CMakeLists.txt`**

Write `deps/modules/lib/rosterloh-drivers/samples/drivers/seesaw_shield_demo/CMakeLists.txt`:

```cmake
# Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(seesaw_shield_demo)

target_sources(app PRIVATE src/main.c)
```

- [ ] **Step 2: Create `Kconfig`**

Write `deps/modules/lib/rosterloh-drivers/samples/drivers/seesaw_shield_demo/Kconfig`:

```kconfig
# Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
# SPDX-License-Identifier: Apache-2.0

source "Kconfig.zephyr"
```

- [ ] **Step 3: Create `prj.conf`**

Write `deps/modules/lib/rosterloh-drivers/samples/drivers/seesaw_shield_demo/prj.conf`:

```kconfig
# Logging
CONFIG_LOG=y
CONFIG_LOG_DEFAULT_LEVEL=3

# I2C (every shield uses I2C)
CONFIG_I2C=y

# MFD + child drivers. Each is default-y on its DT_HAS_* guard, so the
# ones whose compatibles are not present in the active overlay simply
# don't link in.
CONFIG_MFD=y
CONFIG_MFD_ADAFRUIT_SEESAW=y

CONFIG_ADC=y
CONFIG_ADC_ADAFRUIT_SEESAW=y

CONFIG_GPIO=y
CONFIG_GPIO_ADAFRUIT_SEESAW=y

CONFIG_LED_STRIP=y
CONFIG_LED_STRIP_ADAFRUIT_SEESAW=y

CONFIG_SENSOR=y
CONFIG_ADAFRUIT_SEESAW_ENCODER=y

# Stack space for the cooperative ADC acquisition + INT worker threads.
CONFIG_MAIN_STACK_SIZE=2048
```

- [ ] **Step 4: Create the README**

Write `deps/modules/lib/rosterloh-drivers/samples/drivers/seesaw_shield_demo/README.rst`:

```rst
.. zephyr:code-sample:: seesaw_shield_demo
   :name: Adafruit Seesaw shield demo

   Exercise the rosterloh-drivers Seesaw MFD shields end-to-end.

Overview
********

Builds against any of the ``adafruit_neoslider``,
``adafruit_rotary_encoder``, or ``adafruit_neokey_1x4`` shields and
demonstrates the corresponding driver:

* NeoSlider: reads all 4 ADC channels and logs the values at 5 Hz.
* Rotary Encoder: logs ``SENSOR_CHAN_ROTATION`` deltas at 5 Hz.
* NeoKey 1x4: polls GPIO pins 4-7 and logs key down/up events.

If the active shield ships NeoPixels, the demo also cycles the strip
through red/green/blue.

Building and Running
********************

.. code-block:: console

   west build -p always \
     -b adafruit_qt_py_esp32s3/esp32s3/procpu \
     --shield adafruit_neoslider \
     samples/drivers/seesaw_shield_demo
   west flash
```

- [ ] **Step 5: Prepare the `src/` directory**

```bash
cd /home/rio/src/github/rosterloh/zephyr-applications/deps/modules/lib/rosterloh-drivers
mkdir -p samples/drivers/seesaw_shield_demo/src
ls samples/drivers/seesaw_shield_demo/
```

Expected: `CMakeLists.txt`, `Kconfig`, `prj.conf`, `README.rst`, and an empty `src/`. Do **not** commit here — the sample isn't buildable until Task 6 adds `main.c` and `sample.yaml`. A single commit covers both tasks at the end of Task 6.

---

## Task 6: Implement the demo `main.c` and `sample.yaml`

**Files:**

- Create: `deps/modules/lib/rosterloh-drivers/samples/drivers/seesaw_shield_demo/src/main.c`
- Create: `deps/modules/lib/rosterloh-drivers/samples/drivers/seesaw_shield_demo/sample.yaml`

- [ ] **Step 1: Write `src/main.c`**

Write `deps/modules/lib/rosterloh-drivers/samples/drivers/seesaw_shield_demo/src/main.c`:

```c
/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Adafruit Seesaw shield demo.
 *
 * Selects per-child demo code at compile time based on which seesaw
 * children the active shield enables. Build with --shield
 * adafruit_neoslider / adafruit_rotary_encoder / adafruit_neokey_1x4.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(seesaw_shield_demo, LOG_LEVEL_INF);

#define POLL_INTERVAL K_MSEC(200)

/* ---------- ADC (NeoSlider) ---------- */

#if DT_HAS_COMPAT_STATUS_OKAY(adafruit_seesaw_adc)

#define ADC_NODE     DT_COMPAT_GET_ANY_STATUS_OKAY(adafruit_seesaw_adc)
#define ADC_CHANNELS 4

static const struct device *const adc_dev = DEVICE_DT_GET(ADC_NODE);

static int adc_demo_init(void)
{
	if (!device_is_ready(adc_dev)) {
		LOG_ERR("ADC device not ready");
		return -ENODEV;
	}

	for (uint8_t ch = 0; ch < ADC_CHANNELS; ch++) {
		struct adc_channel_cfg cfg = {
			.gain = ADC_GAIN_1,
			.reference = ADC_REF_INTERNAL,
			.acquisition_time = ADC_ACQ_TIME_DEFAULT,
			.channel_id = ch,
			.differential = 0,
		};

		int err = adc_channel_setup(adc_dev, &cfg);

		if (err) {
			LOG_ERR("adc_channel_setup(%u) failed: %d", ch, err);
			return err;
		}
	}

	LOG_INF("ADC demo ready (%d channels)", ADC_CHANNELS);
	return 0;
}

static void adc_demo_tick(void)
{
	int16_t samples[ADC_CHANNELS];
	struct adc_sequence seq = {
		.channels = BIT_MASK(ADC_CHANNELS),
		.buffer = samples,
		.buffer_size = sizeof(samples),
		.resolution = 12,
	};

	int err = adc_read(adc_dev, &seq);

	if (err) {
		LOG_ERR("adc_read failed: %d", err);
		return;
	}

	LOG_INF("ADC: ch0=%d ch1=%d ch2=%d ch3=%d",
		samples[0], samples[1], samples[2], samples[3]);
}

#else
static int adc_demo_init(void) { return 0; }
static void adc_demo_tick(void) { }
#endif

/* ---------- Encoder ---------- */

#if DT_HAS_COMPAT_STATUS_OKAY(adafruit_seesaw_encoder)

#define ENCODER_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(adafruit_seesaw_encoder)

static const struct device *const encoder_dev = DEVICE_DT_GET(ENCODER_NODE);
static int32_t encoder_last;

static int encoder_demo_init(void)
{
	if (!device_is_ready(encoder_dev)) {
		LOG_ERR("Encoder device not ready");
		return -ENODEV;
	}
	encoder_last = 0;
	LOG_INF("Encoder demo ready");
	return 0;
}

static void encoder_demo_tick(void)
{
	struct sensor_value val;
	int err = sensor_sample_fetch(encoder_dev);

	if (err) {
		LOG_ERR("encoder fetch failed: %d", err);
		return;
	}

	err = sensor_channel_get(encoder_dev, SENSOR_CHAN_ROTATION, &val);
	if (err) {
		LOG_ERR("encoder get failed: %d", err);
		return;
	}

	if (val.val1 != encoder_last) {
		LOG_INF("Encoder: pos=%d (delta=%d)",
			val.val1, val.val1 - encoder_last);
		encoder_last = val.val1;
	}
}

#else
static int encoder_demo_init(void) { return 0; }
static void encoder_demo_tick(void) { }
#endif

/* ---------- GPIO keypad (NeoKey 1x4) ---------- */

#if DT_HAS_COMPAT_STATUS_OKAY(adafruit_seesaw_gpio)

#define GPIO_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(adafruit_seesaw_gpio)
#define KEYPAD_PINS 4
static const uint8_t keypad_pin_map[KEYPAD_PINS] = {4, 5, 6, 7};

static const struct device *const keypad_dev = DEVICE_DT_GET(GPIO_NODE);
static uint8_t keypad_last_state[KEYPAD_PINS];

static int keypad_demo_init(void)
{
	if (!device_is_ready(keypad_dev)) {
		LOG_ERR("Keypad GPIO not ready");
		return -ENODEV;
	}

	for (size_t i = 0; i < KEYPAD_PINS; i++) {
		int err = gpio_pin_configure(keypad_dev, keypad_pin_map[i],
					     GPIO_INPUT | GPIO_PULL_UP);

		if (err) {
			LOG_ERR("keypad configure pin %u: %d",
				keypad_pin_map[i], err);
			return err;
		}
		keypad_last_state[i] = 1; /* pull-up, idle high */
	}

	LOG_INF("Keypad demo ready (%d keys)", KEYPAD_PINS);
	return 0;
}

static void keypad_demo_tick(void)
{
	for (size_t i = 0; i < KEYPAD_PINS; i++) {
		int level = gpio_pin_get(keypad_dev, keypad_pin_map[i]);

		if (level < 0) {
			LOG_ERR("keypad get pin %u: %d",
				keypad_pin_map[i], level);
			continue;
		}

		if ((uint8_t)level != keypad_last_state[i]) {
			LOG_INF("Key %u %s", (unsigned int)i,
				level == 0 ? "DOWN" : "UP");
			keypad_last_state[i] = (uint8_t)level;
		}
	}
}

#else
static int keypad_demo_init(void) { return 0; }
static void keypad_demo_tick(void) { }
#endif

/* ---------- NeoPixel cycle ---------- */

#if DT_HAS_COMPAT_STATUS_OKAY(adafruit_seesaw_neopixel)

#define STRIP_NODE   DT_COMPAT_GET_ANY_STATUS_OKAY(adafruit_seesaw_neopixel)
#define STRIP_LEN    DT_PROP(STRIP_NODE, chain_length)

static const struct device *const strip_dev = DEVICE_DT_GET(STRIP_NODE);
static struct led_rgb strip_buf[STRIP_LEN];
static uint8_t strip_phase;

static const struct led_rgb strip_colors[3] = {
	{ .r = 32, .g = 0,  .b = 0  },
	{ .r = 0,  .g = 32, .b = 0  },
	{ .r = 0,  .g = 0,  .b = 32 },
};

static int strip_demo_init(void)
{
	if (!device_is_ready(strip_dev)) {
		LOG_ERR("LED strip not ready");
		return -ENODEV;
	}
	LOG_INF("NeoPixel demo ready (%d pixels)", STRIP_LEN);
	return 0;
}

static void strip_demo_tick(void)
{
	const struct led_rgb color = strip_colors[strip_phase];

	for (size_t i = 0; i < STRIP_LEN; i++) {
		strip_buf[i] = color;
	}

	int err = led_strip_update_rgb(strip_dev, strip_buf, STRIP_LEN);

	if (err) {
		LOG_ERR("led_strip_update_rgb failed: %d", err);
		return;
	}

	strip_phase = (strip_phase + 1) % ARRAY_SIZE(strip_colors);
}

#else
static int strip_demo_init(void) { return 0; }
static void strip_demo_tick(void) { }
#endif

int main(void)
{
	LOG_INF("seesaw_shield_demo starting");

	(void)adc_demo_init();
	(void)encoder_demo_init();
	(void)keypad_demo_init();
	(void)strip_demo_init();

	while (1) {
		adc_demo_tick();
		encoder_demo_tick();
		keypad_demo_tick();
		strip_demo_tick();
		k_sleep(POLL_INTERVAL);
	}

	return 0;
}
```

Notes for the implementer:

- The pin numbers in `keypad_pin_map` (4,5,6,7) are the seesaw GPIO pin numbers the NeoKey 1x4 firmware wires the keys to (active-low with pull-up). They are physical to the breakout; same values used by Adafruit's CircuitPython library.
- `DT_COMPAT_GET_ANY_STATUS_OKAY` is fine here because each demo only ever sees one MFD child of each compatible. If two shields with the same compatible were stacked, the demo would only exercise one — that's acceptable for a demo.
- The `_demo_init`/`_demo_tick` stubs in the `#else` branches keep `main()` free of `#ifdef`s.

- [ ] **Step 2: Write `sample.yaml`**

Write `deps/modules/lib/rosterloh-drivers/samples/drivers/seesaw_shield_demo/sample.yaml`:

```yaml
sample:
  name: Seesaw shield demo
  description: |
    Demonstrates each adafruit_seesaw-mfd shield by enabling whichever
    children are present and dumping their input/output to the shell.

common:
  tags:
    - drivers
    - seesaw
    - samples
  harness: keyboard

tests:
  sample.drivers.seesaw_shield_demo.neoslider:
    extra_args: SHIELD=adafruit_neoslider
    integration_platforms:
      - adafruit_qt_py_esp32s3/esp32s3/procpu
    filter: dt_compat_enabled("adafruit,seesaw-adc")
  sample.drivers.seesaw_shield_demo.rotary_encoder:
    extra_args: SHIELD=adafruit_rotary_encoder
    integration_platforms:
      - adafruit_qt_py_esp32s3/esp32s3/procpu
    filter: dt_compat_enabled("adafruit,seesaw-encoder")
  sample.drivers.seesaw_shield_demo.neokey_1x4:
    extra_args: SHIELD=adafruit_neokey_1x4
    integration_platforms:
      - adafruit_qt_py_esp32s3/esp32s3/procpu
    filter: dt_compat_enabled("adafruit,seesaw-gpio")
```

- [ ] **Step 3: Build the sample with `adafruit_neoslider`**

```bash
cd /home/rio/src/github/rosterloh/zephyr-applications
uv run west build -p always \
  -b adafruit_qt_py_esp32s3/esp32s3/procpu \
  --shield adafruit_neoslider \
  --build-dir builds/seesaw_demo_neoslider \
  deps/modules/lib/rosterloh-drivers/samples/drivers/seesaw_shield_demo
```

Expected: build succeeds. Inspect the final binary listing to confirm the ADC demo is in:

```bash
grep -c "ADC demo ready" builds/seesaw_demo_neoslider/zephyr/zephyr.map
```

Expected: a positive integer (the log-format string is in the build).

- [ ] **Step 4: Build with `adafruit_rotary_encoder`**

```bash
uv run west build -p always \
  -b adafruit_qt_py_esp32s3/esp32s3/procpu \
  --shield adafruit_rotary_encoder \
  --build-dir builds/seesaw_demo_rotary_encoder \
  deps/modules/lib/rosterloh-drivers/samples/drivers/seesaw_shield_demo
```

Expected: build succeeds.

- [ ] **Step 5: Build with `adafruit_neokey_1x4`**

```bash
uv run west build -p always \
  -b adafruit_qt_py_esp32s3/esp32s3/procpu \
  --shield adafruit_neokey_1x4 \
  --build-dir builds/seesaw_demo_neokey \
  deps/modules/lib/rosterloh-drivers/samples/drivers/seesaw_shield_demo
```

Expected: build succeeds.

- [ ] **Step 6: Clean build dirs**

```bash
rm -rf builds/seesaw_demo_neoslider builds/seesaw_demo_rotary_encoder builds/seesaw_demo_neokey
```

- [ ] **Step 7: Commit the sample**

```bash
cd /home/rio/src/github/rosterloh/zephyr-applications/deps/modules/lib/rosterloh-drivers
git add samples/drivers/seesaw_shield_demo/
git commit -m "$(cat <<'EOF'
samples: drivers: add seesaw_shield_demo

Single sample that adapts at compile time to whichever
adafruit_neoslider / adafruit_rotary_encoder / adafruit_neokey_1x4
shield is selected. Exercises adc / encoder / gpio children and cycles
NeoPixels on shields that have them. Three twister scenarios cover the
three shields against adafruit_qt_py_esp32s3/esp32s3/procpu.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: Run twister on the three sample scenarios

**Files:** none (verification task)

- [ ] **Step 1: Run twister against the three integration platforms**

```bash
cd /home/rio/src/github/rosterloh/zephyr-applications
uv run west twister -T deps/modules/lib/rosterloh-drivers/samples/drivers/seesaw_shield_demo \
  -p adafruit_qt_py_esp32s3/esp32s3/procpu --build-only -v
```

Expected: three scenarios PASS (build-only). If any FAIL, inspect the matching `twister-out/<scenario>/build.log` and fix in `main.c`, `prj.conf`, or the shield overlay as appropriate.

- [ ] **Step 2: Clean twister output**

```bash
rm -rf twister-out
```

(Twister leaves these directories in the workspace root. The module's `.gitignore` already excludes `twister-out*` from the module repo.)

- [ ] **Step 3: Push the module branch**

```bash
cd /home/rio/src/github/rosterloh/zephyr-applications/deps/modules/lib/rosterloh-drivers
git log --oneline main..HEAD
git push -u rosterloh feat/seesaw-shields
```

Expected: 4 commits (one shield-per-commit plus the sample) pushed to `rosterloh/zephyr-drivers`. If the remote name differs locally, run `git remote -v` to find it (likely `rosterloh` or `origin`).

- [ ] **Step 4: Open the module-side PR**

```bash
cd /home/rio/src/github/rosterloh/zephyr-applications/deps/modules/lib/rosterloh-drivers
gh pr create --title "shields: add Adafruit Seesaw breakouts + demo sample" \
  --body "$(cat <<'EOF'
## Summary

- Ship three Zephyr shields (`adafruit_neoslider`, `adafruit_rotary_encoder`, `adafruit_neokey_1x4`) that wrap the breakouts the existing seesaw MFD driver supports.
- Add a `seesaw_shield_demo` sample with three twister scenarios that exercise each shield against `adafruit_qt_py_esp32s3/esp32s3/procpu`.

## Test plan

- [ ] `west build -p always -b adafruit_qt_py_esp32s3/esp32s3/procpu --shield adafruit_neoslider samples/drivers/seesaw_shield_demo` succeeds.
- [ ] Same for `adafruit_rotary_encoder` and `adafruit_neokey_1x4`.
- [ ] `west twister -T samples/drivers/seesaw_shield_demo -p adafruit_qt_py_esp32s3/esp32s3/procpu --build-only` reports 3/3 passing.

Spec: rosterloh/zephyr-applications/docs/superpowers/specs/2026-05-12-adafruit-seesaw-shields-design.md

Generated with [Claude Code](https://claude.com/claude-code)
EOF
)"
```

If `gh pr create` errors, fix the inputs (e.g., wrong base branch, missing CI secrets) — do **not** push past a failing precondition.

---

## Task 8: Add `zephyr_i2c` alias to `robotis_openrb_150`

**Files:**

- Modify: `boards/robotis/openrb_150/robotis_openrb_150.dts`

Workspace-side change. From here on, operate from the workspace root, not the module checkout.

- [ ] **Step 1: Check out the workspace branch**

```bash
cd /home/rio/src/github/rosterloh/zephyr-applications
git status --short
git checkout -b feat/seesaw-shields-support main
```

Expected: clean working tree (the spec+plan docs are already committed as part of the brainstorming flow; if they're still untracked, see Task 10 for the docs commit). New branch checked out.

If the branch already exists (e.g. from a previous iteration), use `git checkout feat/seesaw-shields-support` instead.

- [ ] **Step 2: Read the current end of the OpenRB DTS**

```bash
tail -20 boards/robotis/openrb_150/robotis_openrb_150.dts
```

This is for orientation — pick the right insertion point (end of file is fine; the alias is a top-level statement).

- [ ] **Step 3: Append the alias**

Edit `boards/robotis/openrb_150/robotis_openrb_150.dts` and add at the end of the file:

```dts

/* Zephyr-shield convention: shields hook &zephyr_i2c. Alias the Arduino
 * MKR I2C bus so out-of-tree shields apply on OpenRB.
 */
zephyr_i2c: &arduino_mkr_i2c {};
```

Make sure the file ends with a single trailing newline (standard for DTS files in this repo).

- [ ] **Step 4: Verify existing apps still build (regression)**

```bash
uv run poe agent-build motor_controller
```

Expected: build succeeds, last 5 lines printed. The motor_controller's existing overlay uses `&arduino_mkr_i2c` directly, which still resolves — the alias is purely additive. If the build fails, inspect `logs/motor_controller-build.log` to see what changed.

```bash
uv run poe agent-build joystick_controller
```

Expected: build succeeds. (joystick_controller doesn't touch OpenRB but build it anyway to confirm no module-side commit broke an unrelated app.)

- [ ] **Step 5: Run the OpenRB shield smoke test**

```bash
uv run west build -p always \
  -b robotis_openrb_150 \
  --shield adafruit_neoslider \
  --build-dir builds/shield_check_neoslider_openrb \
  deps/zephyr/samples/hello_world
```

Expected: build succeeds. The `&zephyr_i2c` reference in the shield overlay now resolves through the new alias to `&sercom0`.

```bash
uv run west build -p always \
  -b robotis_openrb_150 \
  --shield adafruit_rotary_encoder \
  --build-dir builds/shield_check_rotary_encoder_openrb \
  deps/zephyr/samples/hello_world
```

Expected: build succeeds.

- [ ] **Step 6: Clean the build dirs**

```bash
rm -rf builds/shield_check_neoslider_openrb builds/shield_check_rotary_encoder_openrb
```

- [ ] **Step 7: Commit**

```bash
cd /home/rio/src/github/rosterloh/zephyr-applications
git add boards/robotis/openrb_150/robotis_openrb_150.dts
git commit -m "$(cat <<'EOF'
boards: robotis: openrb_150: alias zephyr_i2c to arduino_mkr_i2c

Adds a one-line alias so Zephyr-shield-convention overlays
(&zephyr_i2c { ... }) apply on this board. Existing in-tree overlays
that reference &arduino_mkr_i2c directly are unaffected.

Verified with --shield adafruit_neoslider and --shield
adafruit_rotary_encoder against samples/hello_world.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 9: Final cross-app regression check

**Files:** none (verification task)

- [ ] **Step 1: Rebuild every workspace app from scratch**

```bash
cd /home/rio/src/github/rosterloh/zephyr-applications
uv run poe agent-build motor_controller
uv run poe agent-build joystick_controller
```

Expected: both succeed. (Skip `embedded_vision`, `force_sensor`, `rasprover` — they don't touch the Seesaw path or OpenRB.)

If either fails, inspect the corresponding log under `logs/`. The likely cause of a failure here would be the `zephyr_i2c` alias colliding with some other label or with the existing `arduino_mkr_i2c` users — investigate before reverting.

- [ ] **Step 2: Verify shield discoverability from the workspace**

```bash
uv run west boards --shield 2>/dev/null | grep adafruit_
```

Expected: at least the three new shields appear in the list, alongside any other Adafruit shields the in-tree Zephyr brings.

---

## Task 10: Commit the spec + plan and push the workspace PR

**Files:**

- `docs/superpowers/specs/2026-05-12-adafruit-seesaw-shields-design.md` (already committed during brainstorming)
- `docs/superpowers/plans/2026-05-12-adafruit-seesaw-shields.md` (this file)

The spec was committed at the end of brainstorming. This task commits the plan and pushes everything.

- [ ] **Step 1: Stage the plan**

```bash
cd /home/rio/src/github/rosterloh/zephyr-applications
git status --short
```

Expected: shows `docs/superpowers/plans/2026-05-12-adafruit-seesaw-shields.md` as untracked (or modified, if you've been editing it). The spec is already committed.

```bash
git add docs/superpowers/plans/2026-05-12-adafruit-seesaw-shields.md
git commit -m "$(cat <<'EOF'
docs: implementation plan for Adafruit Seesaw shields

Companion to the 2026-05-12 design spec. Walks an engineer through
adding three shields + a demo sample in rosterloh-drivers and a
zephyr_i2c alias on robotis_openrb_150.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 2: Push the workspace branch**

```bash
git log --oneline main..HEAD
git push -u origin feat/seesaw-shields-support
```

Expected: at least two commits pushed (spec doc + plan doc + OpenRB alias). If the remote isn't `origin`, find it with `git remote -v`.

- [ ] **Step 3: Open the workspace PR**

```bash
gh pr create --title "boards: openrb_150 zephyr_i2c alias + seesaw shield design docs" \
  --body "$(cat <<'EOF'
## Summary

- Add `zephyr_i2c: &arduino_mkr_i2c {};` alias on `robotis_openrb_150` so out-of-tree Zephyr-shield-convention overlays apply on this board.
- Land the design spec and implementation plan for the companion shields in `rosterloh-drivers` (separate PR: <link to module PR>).

## Test plan

- [ ] `uv run poe agent-build motor_controller` still succeeds (no regression from the alias).
- [ ] `uv run poe agent-build joystick_controller` still succeeds.
- [ ] `west build -b robotis_openrb_150 --shield adafruit_neoslider deps/zephyr/samples/hello_world` succeeds once the companion module PR lands.

Companion module PR: <link>

Generated with [Claude Code](https://claude.com/claude-code)
EOF
)"
```

Update the body with the actual module-PR URL after both PRs exist.

- [ ] **Step 4: Mention in module PR**

Cross-link from the module PR description to the workspace PR for reviewer context (or vice versa). `gh pr edit --body` works for this.

---

## Post-merge follow-up (not part of this plan; documented for handoff)

After both PRs merge:

1. Run `uv run poe west-update` in the workspace to advance the pinned `rosterloh-drivers` ref past the shield commits.
2. Commit the resulting `west.yml.lock` (or whatever the workspace tracks for module pins).
3. The three shields are then usable from any app in this workspace via `west build ... --shield adafruit_neoslider` (or `_rotary_encoder` / `_neokey_1x4`).

App migrations of `motor_controller` and `joystick_controller` to use these shields instead of inline overlays are explicitly out of scope here.
