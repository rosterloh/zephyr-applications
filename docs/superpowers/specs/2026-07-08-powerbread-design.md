# PowerBread — Zephyr port design

**Date:** 2026-07-08
**App:** `applications/powerbread`
**Board:** `adafruit_qt_py_esp32c3` (out-of-tree board in `boards/adafruit/qt_py_esp32c3`)
**Upstream:** https://github.com/nicho810/XIAO-powerbread (Arduino + FreeRTOS + LVGL 8.3)

## Overview

A standalone breadboard power monitor: an INA3221 measures voltage/current on two
supply channels, an ST7735 0.96" LCD (80×160) shows an LVGL UI with three modes
(dashboard, line chart, statistics), and a resistor-ladder dial wheel navigates the
UI. Measurements can also be streamed as CSV over USB serial and controlled via
shell commands. No networking, no sysbuild/MCUboot.

## Hardware wiring

Wiring follows the XIAO PowerBread schematic, adapted to the QT Py ESP32-C3:

| Function          | QT Py pin | ESP32-C3 GPIO | Zephyr resource                     |
|-------------------|-----------|---------------|-------------------------------------|
| INA3221 SDA       | SDA       | GPIO5         | `i2c0` (Stemma QT), addr 0x40       |
| INA3221 SCL       | SCL       | GPIO6         | `i2c0`                              |
| LCD DIN (MOSI)    | MO        | GPIO7         | `spi2` MOSI (existing pinmux)       |
| LCD CLK           | SCK       | GPIO10        | `spi2` SCLK (existing pinmux)       |
| LCD DC            | MI        | GPIO8         | plain GPIO output (MISO un-muxed)   |
| LCD RST           | A3        | GPIO0         | plain GPIO output                   |
| LCD CS            | —         | —             | tied low on schematic; no GPIO      |
| LCD backlight     | —         | —             | hardwired on                        |
| Dial wheel ladder | A2 / D2   | GPIO1         | ADC1 channel 1                      |

- INA3221: channels 1 and 2 enabled (channel 3 disabled), **50 mΩ shunts**
  (upstream default: 0.8 mA resolution, 3.6 A max). Conversion times 140 µs
  (shunt and bus), averaging ×4 → full 2-channel cycle ≈ 2.24 ms, comfortably
  supporting 100 Hz polling.
- GPIO8 is pinmuxed as `SPIM2_MISO` by the board; the app overlay defines a
  replacement pinctrl group (MOSI + SCLK only) and points `&spi2` at it so
  GPIO8 becomes the DC line. The display is write-only, so MISO is not needed.
- A0/GPIO4 and A1/GPIO3 remain unused.

## Devicetree overlay (`boards/adafruit_qt_py_esp32c3.overlay`)

- `&pinctrl`: new `spim2_lcd` group (MOSI GPIO7, SCLK GPIO10); `&spi2` switched
  to it.
- MIPI-DBI SPI wrapper (`zephyr,mipi-dbi-spi`) on `spi2` with
  `dc-gpios = <&gpio0 8 GPIO_ACTIVE_HIGH>`,
  `reset-gpios = <&gpio0 0 GPIO_ACTIVE_LOW>`, no CS. Child node: `st7735r`
  display, 80×160 (0.96" panel variant with the required column/row offsets and
  init parameters), RGB565. `chosen { zephyr,display = ... }`.
- `ina3221@40` on `&i2c0`: `enable-channel = <1 1 0>`,
  `shunt-resistors = <50 50 50>` (mΩ), `conv-time-shunt = <0>`,
  `conv-time-bus = <0>`, `avg-mode = <1>`.
- `&adc0` channel 1 (GPIO1) configured for full-scale input (attenuation/gain
  chosen at implementation to cover the ladder's ~0–2 V range).
- `adc-keys` node on that channel with three keys — `INPUT_KEY_UP`,
  `INPUT_KEY_DOWN`, `INPUT_KEY_ENTER` — thresholds derived from upstream's
  10-bit values (idle < ~320 mV, press ≈ 320–640 mV, down ≈ 640–1130 mV,
  up ≈ 1130–1940 mV). Exact per-key `press-thresholds-mv` semantics follow the
  Zephyr `adc-keys` driver; values are calibration-adjustable in the overlay.
- `zephyr,input-longpress` node on `INPUT_KEY_ENTER` emitting distinct short /
  long codes (long ≥ 1 s).

## Software architecture

Three units plus `main.c` (init + thread start). Follows `force_sensor` layout:
`prj.conf`, `CMakeLists.txt`, `boards/*.overlay`, `src/`, `README.md`, `VERSION`.

### 1. Sampler (`src/sampler.c`, own thread, 100 Hz)

- Polls the INA3221 every 10 ms: selects channel via
  `SENSOR_ATTR_INA3221_SELECTED_CHANNEL`, fetches bus voltage, current, power
  for channels 1 and 2.
- Per channel maintains:
  - latest sample (V, mA, mW);
  - running stats: min/max/avg current and voltage since last reset;
  - energy integration: mAh and mWh accumulators;
  - a chart ring buffer: current values decimated to 20 Hz, 160 points
    (≈ 8 s of history).
- Snapshot API guarded by a mutex: `sampler_get(struct pb_snapshot *)`,
  `sampler_reset_stats()`.
- CSV streaming: when enabled, prints `t_ms,ch,mV,mA,mW` lines to the console
  (one line per channel per sample; ~100 Hz × 2). Toggled from shell; off by
  default.
- If the sensor is missing/not ready: log an error, UI shows a sensor-error
  message, shell stays functional.

### 2. UI (`src/ui.c`)

LVGL on the ST7735, portrait 80×160 like upstream. A `pb_ui_set_mode()` /
`pb_ui_set_channel()` API shared by dial and shell. Refresh timer ~10 Hz reads
the sampler snapshot. Three screens:

- **Dashboard** — both channels stacked: voltage, current, power each.
- **Chart** — LVGL line chart of the focused channel's current from the ring
  buffer, auto-scaled Y axis, current value overlaid.
- **Stats** — focused channel: min/max/avg current, voltage, and accumulated
  mAh / mWh since reset.

If the display isn't ready, UI unit disables itself with a log error; the rest
of the app keeps running.

### 3. Control (`src/input.c`, `src/shell.c`)

- Input callback (input subsystem) on the dial events:
  - **up / down** — cycle UI mode (dashboard → chart → stats → …);
  - **short press** — toggle focused channel (1 ↔ 2);
  - **long press** — reset stats/energy counters.
- Shell commands (root `powerbread`):
  - `mode <dash|chart|stats>`, `channel <1|2>`
  - `stream <on|off>` — CSV streaming
  - `reset` — clear stats/energy
  - `read` — one-shot readings for both channels
  - `dial` — print the live dial ADC reading in mV (threshold calibration aid)

## Configuration (`prj.conf` outline)

Sensor + ADC + input (`CONFIG_SENSOR`, `CONFIG_ADC`, `CONFIG_INPUT`), display +
LVGL (`CONFIG_DISPLAY`, `CONFIG_LVGL`, chart widget, 16-bit color), shell + log,
`CONFIG_CBPRINTF_FP_SUPPORT` for float formatting. Heap sized for the LVGL
buffers (80×160×2 ≈ 25 KB full framebuffer). No networking, BT, flash/settings.

## Repo integration

- `poe.toml`: add `powerbread) ALLOWED="adafruit_qt_py_esp32c3"; DEFAULT="adafruit_qt_py_esp32c3" ;;`
  to the `app` task case.
- Build dir `builds/powerbread/` via existing tasks.

## Verification

1. `uv run poe agent-build powerbread` passes.
2. C sources pass `uv run clang-format --dry-run --Werror`.
3. On hardware (user): dashboard shows plausible V/I on both channels; dial
   navigates modes/channels; `powerbread read`/`stream`/`dial` work over USB
   serial; stats accumulate and reset.

## Out of scope

- Upstream's XPB binary protocol and web-serial browser UI (CSV instead).
- Other boards (XIAO RP2040/ESP32-*), native_sim emulation.
- sysbuild / MCUboot, OTA, networking.
- Persisting config (chart interval, default channel) to flash — fixed
  defaults; can be added later with Settings if wanted.
