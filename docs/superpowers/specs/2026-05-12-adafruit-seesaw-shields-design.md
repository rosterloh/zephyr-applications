# Adafruit Seesaw shields — design

**Status:** approved (pending written review)
**Owner:** rio
**Date:** 2026-05-12
**Related:** `docs/superpowers/plans/2026-05-11-seesaw-zephyr-apis.md`

## Goal

Ship out-of-tree Zephyr shield definitions for the Adafruit Seesaw breakouts
that the `adafruit,seesaw-mfd` driver in `rosterloh-drivers` already supports,
so applications can compose them with `west build --shield <name>` instead of
hand-rolling the same DT nodes in every per-board overlay.

## Scope

Three breakouts, matching what existing apps in this workspace already use:

| Shield | Adafruit PID | I2C addr | Seesaw children |
|---|---|---|---|
| `adafruit_neoslider` | 5295 | 0x30 | `adafruit,seesaw-adc` + `adafruit,seesaw-neopixel` |
| `adafruit_rotary_encoder` | 4991 | 0x36 | `adafruit,seesaw-encoder` |
| `adafruit_neokey_1x4` | 4980 | 0x30 | `adafruit,seesaw-gpio` (16 pins) + `adafruit,seesaw-neopixel` |

All shields use the breakouts' default strap address. Users wanting a
non-default address write their own overlay; in-scope apps only ever use one of
each breakout, so no address-variant shields.

Out of scope: NeoTrellis, STEMMA Soil, ANO Rotary, BFF variants, anything not
currently in use by this workspace's apps.

## Architecture

### Where the shields live

Shields ship in **`rosterloh-drivers`**, alongside the MFD driver they depend
on. The module's `zephyr/module.yml` already declares `board_root: .`, so any
`boards/shields/<name>/` directory there is auto-discovered by west — no
workspace-side wiring is needed.

```
deps/modules/lib/rosterloh-drivers/
  boards/shields/
    adafruit_neoslider/
      adafruit_neoslider.overlay
      Kconfig.shield
      shield.yml
    adafruit_rotary_encoder/
      adafruit_rotary_encoder.overlay
      Kconfig.shield
      shield.yml
    adafruit_neokey_1x4/
      adafruit_neokey_1x4.overlay
      Kconfig.shield
      shield.yml
```

Keeping shields in the driver module means: any consumer that pulls
`rosterloh-drivers` via west gets the shields automatically; the shield
definitions track the MFD/child bindings they depend on in a single repo; no
divergence between driver capability and shield availability.

### Shield contents

Each `shield.yml` follows Zephyr's upstream pattern (`adafruit_aw9523` is the
nearest analogue):

```yaml
shield:
  name: adafruit_neoslider
  full_name: Adafruit I2C QT Slide Potentiometer with NeoPixel
  vendor: adafruit
  supported_features:
    - mfd
    - adc
    - led_strip
```

`Kconfig.shield` is a one-liner:

```kconfig
config SHIELD_ADAFRUIT_NEOSLIDER
	def_bool $(shields_list_contains,adafruit_neoslider)
```

No `Kconfig.defconfig` — the MFD parent and children are pulled in
automatically by `DT_HAS_*` selects in `rosterloh-drivers`' existing
`drivers/.../Kconfig.adafruit_seesaw` files.

### Overlay shape

Each overlay hooks `&zephyr_i2c` and instantiates the MFD parent at its
default address with `read-delay-us = <10000>` (10 ms — comfortable margin
above the 250 µs binding default, while ~10× faster than the 100 ms
currently used by in-tree apps). Concrete children mirror the chips' physical
layout. Example (`adafruit_neoslider.overlay`):

```dts
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

The encoder and neokey overlays follow the same pattern, swapping address,
children, and the neopixel pin/chain-length to match each chip.

### Bus binding: `&zephyr_i2c`

Shields reference `&zephyr_i2c` — the standard Zephyr shield convention. Board
status:

- `adafruit_qt_py_esp32s3/esp32s3/procpu`: upstream board already aliases
  `zephyr_i2c: &i2c1`. Works as-is.
- `adafruit_qt_py_esp32c3`: workspace-owned board, already aliases
  `zephyr_i2c: &i2c0`. Works as-is. (Not used by any of the three shields in
  scope, but covered for completeness.)
- `robotis_openrb_150`: workspace-owned board, **does not** expose
  `zephyr_i2c` — only `arduino_mkr_i2c` (an alias of `sercom0`). Fix this in
  the same change set: append a one-line alias to
  `boards/robotis/openrb_150/robotis_openrb_150.dts`:

  ```dts
  zephyr_i2c: &arduino_mkr_i2c {};
  ```

  This is additive: existing overlays that reference `&arduino_mkr_i2c`
  continue to work unchanged, and any Zephyr-shield-conformant breakout is now
  composable on OpenRB. The alias lives in the workspace because the board
  does — `rosterloh-drivers` does not own `robotis_openrb_150`.

## Demo sample

Add a single sample at
`rosterloh-drivers/samples/drivers/seesaw_shield_demo/` that proves each
shield boots end-to-end against real silicon (or in DT compile against
QEMU/native_sim-style targets where applicable).

### Structure

```
samples/drivers/seesaw_shield_demo/
  CMakeLists.txt
  prj.conf
  sample.yaml
  README.rst
  src/main.c
```

`src/main.c` uses `#if DT_HAS_COMPAT_STATUS_OKAY(...)` blocks to enable
per-child demo code:

- `adafruit_seesaw_adc` → log slider channels at 10 Hz.
- `adafruit_seesaw_encoder` → log encoder delta + button on input events.
- `adafruit_seesaw_gpio` → log key down/up events via the input subsystem
  (`adafruit_seesaw_gamepad` already maps the gpio child to input).
- `adafruit_seesaw_neopixel` → cycle the strip through a few colors so the
  user can see the demo is alive.

The same binary builds for any combination of the three shields; whichever
the user passes via `--shield`, the matching code blocks light up.

### Sample.yaml

Three test scenarios, one per shield, all integration-platform'd to
`adafruit_qt_py_esp32s3/esp32s3/procpu` (only workspace board with `zephyr_i2c`
out of the box at design time — OpenRB joins once the alias lands):

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

`prj.conf` enables everything the union of children needs: `CONFIG_LOG=y`,
`CONFIG_I2C=y`, `CONFIG_MFD=y`, `CONFIG_ADC=y`, `CONFIG_GPIO=y`,
`CONFIG_INPUT=y`, `CONFIG_LED_STRIP=y`, plus the per-driver `_ADAFRUIT_SEESAW`
switches (`MFD_ADAFRUIT_SEESAW`, `GPIO_ADAFRUIT_SEESAW`,
`ADC_ADAFRUIT_SEESAW`, `LED_STRIP_ADAFRUIT_SEESAW`, etc.). Drivers whose
compatible isn't enabled in DT just don't link in.

## Verification

1. **DT compile check, per shield, against `hello_world`**:
   ```bash
   uv run west build -p always -b adafruit_qt_py_esp32s3/esp32s3/procpu \
     --shield adafruit_neokey_1x4 deps/zephyr/samples/hello_world
   ```
   Repeat for `adafruit_neoslider` and `adafruit_rotary_encoder`. Validates
   that the overlay merges and the MFD + children Kconfigs resolve.
2. **Same check on OpenRB** (after the `zephyr_i2c` alias lands) for
   `adafruit_neoslider` and `adafruit_rotary_encoder`. Confirms the alias
   works and the shields apply on a non-Adafruit board.
3. **Demo sample**: `west build` each `sample.yaml` test scenario. Twister
   runs them all in one shot.
4. **Regression**: existing apps still build unchanged.
   ```bash
   uv run poe agent-build motor_controller
   uv run poe agent-build joystick_controller
   ```

## Non-goals

- Migrating `motor_controller`/`joystick_controller` to use these shields.
  They keep their inline overlays for now. (Can be a follow-up.)
- Changing `poe.toml` to forward `--shield`. Not needed by current apps.
- Address-variant shields (`adafruit_neoslider_31`, etc.). Add only if/when
  someone needs two of the same breakout on one bus.
- Per-shield `doc/` directories. README under each shield is fine if anything;
  not blocking.

## Git flow

Per `CLAUDE.md`'s out-of-tree-module convention:

1. Branch in `deps/modules/lib/rosterloh-drivers` (e.g. `feat/seesaw-shields`).
   Commit shields + sample there. Open a PR against `rosterloh/zephyr-drivers`.
2. Branch in `zephyr-applications` (e.g. `feat/seesaw-shields-support`).
   Commit the OpenRB `zephyr_i2c` alias and this spec there. PR against
   `rosterloh/zephyr-applications`.
3. After both merge, `uv run poe west-update` to advance the pinned
   `rosterloh-drivers` main ref locally.

## Open questions

None at design time.
