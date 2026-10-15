# joystick_controller Pico-ROS Teleop Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rewrite `applications/joystick_controller` as a deadman-button teleop that publishes `geometry_msgs/Twist` over Pico-ROS, on the QT Py ESP32-S3 + Joy FeatherWing (Wi-Fi) and the Waveshare ESP32-P4-WIFI6-POE-ETH + APEM 3-axis stick (Ethernet).

**Architecture:** Per-board devicetree maps the stick to Zephyr's `analog-axis` input driver (`INPUT_ABS_X/Y/RZ`, scaled to ±1000) and the deadman to `gpio-keys` (`INPUT_BTN_0`). A pure, board-agnostic `teleop.c` turns (button, axes) into "publish this Twist" / "stay silent". `main.c` runs one loop: (re)connect Pico-ROS, sample inputs, step teleop, publish.

**Tech Stack:** Zephyr 4.5 (input subsystem, settings/ZMS, shell, conn_mgr), Pico-ROS + zenoh-pico via `applications/common/picoros`, rosterloh-drivers seesaw MFD/ADC/GPIO, ztest on `qemu_cortex_m3`.

**Spec:** `docs/superpowers/specs/2026-10-15-joystick-teleop-design.md`

## Global Constraints

- All commands through `mise run` / `mise x --` (never bare `west`/`python`). Build dirs under `builds/`.
- Boards: `adafruit_qt_py_esp32s3/esp32s3/procpu` (default) and `esp32p4_wifi6_poe_eth/esp32p4/hpcore`.
- Mapping: stick Y → `linear.x`, stick X → `linear.y`, twist Z → `angular.z`. FeatherWing: linear only (`angular.z` = 0).
- Deadman: publish only while held; exactly one zero Twist on release; silent otherwise.
- After a reconnect, nothing is published until the button has been seen released (no replay, no stale command).
- Kconfig defaults: locator `tcp/192.168.1.10:7447`, node `joystick_controller`, domain 0, topic `cmd_vel`, 20 Hz, max linear 0.5 m/s, max angular 1.0 rad/s.
- C formatted with in-tree `.clang-format`: `mise x -- clang-format --dry-run --Werror <files>`.
- Do not change `west.yml`, rasprover, or `applications/common/picoros`.
- Commit messages: imperative subject < 50 chars, end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. **Button held across a link drop/reconnect** → must stay silent until released and re-pressed. (Task 1 test `test_reset_blocks_until_release`.)
2. **Stick fully deflected beyond calibrated range** (axis value > 1000 or < −1000) → clamp to max speed, never exceed. (Task 1 test `test_clamps_out_of_range`.)
3. **Release on the same tick as a big stick input** → the release tick publishes zero, not the stick value. (Task 1 test `test_release_publishes_zero_once`.)
4. **Calibration where rest is off-centre** (pot not mid-scale) → resulting `in_min/in_max` are symmetric about rest so the deadzone sits at rest. (Task 1 test `test_cal_symmetric_about_rest`.)
5. **Publish fails while connected (router died)** → loop drops to reconnect and calls `teleop_reset()`; does not spin publishing. (Task 2, verified on hardware in Task 5 by stopping `rmw_zenohd`.)

---

## File Structure

```
applications/joystick_controller/
  CMakeLists.txt            rewrite: picoros include, src list
  Kconfig                   rewrite: APP_TELEOP_*, APP_PICOROS_*
  prj.conf                  rewrite: common config
  README.md                 rewrite: wiring, provisioning, calibration
  tests.yaml                add P4 platform
  boards/adafruit_qt_py_esp32s3_esp32s3_procpu.{overlay,conf}   rewrite (Joy FeatherWing, Wi-Fi)
  boards/esp32p4_wifi6_poe_eth_esp32p4_hpcore.{overlay,conf}    new (APEM, Ethernet)
  src/teleop.{c,h}          new: pure deadman/scaling/calibration math
  src/teleop_types.h        new: picoserdes MSG_LIST (Vector3, Twist)
  src/calibrate.c           new: `teleop cal` shell
  src/main.c                rewrite: input callback, connect/publish loop
  tests/teleop/             new: ztest for teleop.c
  src/common/, src/modules/ delete
mise.toml                   joystick_controller allowed boards
```

---

### Task 1: Pure teleop logic + tests

**Files:**
- Create: `applications/joystick_controller/src/teleop.h`, `src/teleop.c`
- Test: `applications/joystick_controller/tests/teleop/{CMakeLists.txt,prj.conf,testcase.yaml,src/main.c}`

**Interfaces:**
- Produces (in `teleop.h`):

```c
#define TELEOP_AXIS_MAX 1000 /* analog-axis out-max; out-min is -TELEOP_AXIS_MAX */

struct teleop_limits { double max_linear; double max_angular; }; /* m/s, rad/s */
struct teleop_input { bool enable; int16_t x, y, rz; };
struct teleop_twist { double linear_x, linear_y, angular_z; };
struct teleop_state { bool active; bool blocked; };

/* Returns true when *out must be published this tick. */
bool teleop_step(struct teleop_state *s, const struct teleop_input *in,
		 const struct teleop_limits *lim, struct teleop_twist *out);
/* Link lost: go inactive and ignore enable until it is seen released. */
void teleop_reset(struct teleop_state *s);
/* Largest span symmetric about rest that fits inside [seen_min, seen_max]. */
void teleop_cal_symmetric(int16_t rest, int16_t seen_min, int16_t seen_max,
			  int16_t *in_min, int16_t *in_max);
```

Semantics of `teleop_step`:
- `blocked`: if `enable` → return false; else clear `blocked`, return false.
- `enable` → `active = true`; `out` = y→`linear_x`, x→`linear_y`, rz→`angular_z`, each `clamp(v, ±1000) * max / 1000`; return true.
- `!enable && active` → `active = false`; `out` = all zero; return true.
- otherwise return false.
- A zero-initialised `teleop_state` is the boot state (not blocked).

- [ ] **Step 1: Write the test suite** `tests/teleop/src/main.c` (`ZTEST_SUITE(teleop, ...)`), with `lim = {0.5, 1.0}`:
  - `test_idle_is_silent`: enable=false, axes=(500,500,500) → returns false.
  - `test_held_scales_holonomic`: enable, x=1000, y=−500, rz=250 → true; `linear_x == -0.25`, `linear_y == 0.5`, `angular_z == 0.25` (`zassert_within(..., 1e-9)`).
  - `test_release_publishes_zero_once`: held tick, then enable=false with y=1000 → true and all zero; next tick → false.
  - `test_clamps_out_of_range`: enable, y=1500, x=−1500 → `linear_x == 0.5`, `linear_y == -0.5`.
  - `test_reset_blocks_until_release`: held tick; `teleop_reset`; enable still held → false (twice); enable=false → false; enable=true → true.
  - `test_cal_symmetric_about_rest`: (rest 1800, min 900, max 3000) → (900, 2700); (rest 2048, min 100, max 4000) → (100, 3996).
- [ ] **Step 2: Test scaffolding.** `CMakeLists.txt` builds `src/main.c` + `../../src/teleop.c` with `../../src` included; `prj.conf`: `CONFIG_ZTEST=y`; `testcase.yaml` copies rasprover's picoserdes pattern (`platform_allow: [qemu_cortex_m3, native_sim]`, `integration_platforms: [qemu_cortex_m3]`), test id `joystick_controller.teleop`.
- [ ] **Step 3: Run, expect link/compile failure**
  Run: `mise x -- west twister -T applications/joystick_controller/tests/teleop -p qemu_cortex_m3 --outdir builds/twister-teleop`
  Expected: build error, `teleop.c` missing / undefined `teleop_step`.
- [ ] **Step 4: Implement `teleop.c`** per the semantics above.
- [ ] **Step 5: Run again** — Expected: `6 of 6 executed test cases passed`.
- [ ] **Step 6: clang-format check and commit** (`Add pure teleop deadman logic`).

---

### Task 2: Rewrite the app on S3 + Joy FeatherWing

**Files:**
- Delete: `src/common/`, `src/modules/`
- Rewrite: `CMakeLists.txt`, `Kconfig`, `prj.conf`, `src/main.c`, `boards/adafruit_qt_py_esp32s3_esp32s3_procpu.{overlay,conf}`
- Create: `src/teleop_types.h`

**Interfaces:**
- Consumes: Task 1 `teleop.h`.
- Produces: Kconfig symbols `APP_PICOROS` (menuconfig, `select PICOROS`, default y), `APP_PICOROS_LOCATOR`, `APP_PICOROS_NODE_NAME`, `APP_PICOROS_DOMAIN_ID`, `APP_PICOROS_CMD_VEL_TOPIC` (default `"cmd_vel"`), `APP_TELEOP_PUBLISH_HZ` (int, 20, range 1–100), `APP_TELEOP_MAX_LINEAR_MM_S` (int, 500), `APP_TELEOP_MAX_ANGULAR_MRAD_S` (int, 1000). Devicetree contract for every board: one `analog-axis` node with `out-min = <-1000>; out-max = <1000>;`, children reporting `INPUT_ABS_X`, `INPUT_ABS_Y` (and `INPUT_ABS_RZ` if present); one `gpio-keys` child with `zephyr,code = <INPUT_BTN_0>`.

- [ ] **Step 1: Build config.**
  - `CMakeLists.txt`: set `PICOROS_USER_TYPE_FILE` to `src/teleop_types.h`, `include(../common/picoros/picoros.cmake)` (pattern: `applications/rasprover/CMakeLists.txt`), sources `src/main.c src/teleop.c`.
  - `src/teleop_types.h`: `MSG_LIST` with only `ros_Vector3` and `ros_Twist`, names and hashes copied verbatim from `applications/rasprover/src/rasprover_types.h`; empty `SRV_LIST` if picoserdes requires one (check rasprover's file).
  - `Kconfig`: symbols above; help text style per `applications/rasprover/Kconfig`. Include `rsource "../common/picoros/Kconfig"` per CLAUDE.md if rasprover does.
  - `prj.conf`: keep MCUboot/settings/ZMS block; add `CONFIG_INPUT=y`, `CONFIG_ADC=y`, `CONFIG_SHELL=y`, networking (`NETWORKING`, `NET_IPV4`, `NET_TCP`, `NET_SOCKETS`, `NET_DHCPV4`, `NET_CONNECTION_MANAGER`, `NET_MGMT_EVENT`), `CONFIG_ZENOH_PICO_LINK_TCP=y`, `CONFIG_ZENOH_PICO_THREADS_NUM=2`, and the heap/net-buf sizing lines from `applications/rasprover/prj.conf` with their comments. Drop `ZBUS` and `LED_STRIP`.
  - S3 `.conf`: Wi-Fi block from rasprover `prj.conf` ("WiFi connection management" section, `CONFIG_WIFI` … `CONFIG_NET_L2_WIFI_SHELL`).
- [ ] **Step 2: S3 overlay** on `&i2c1` (STEMMA QT, `zephyr_i2c`): `adafruit,seesaw-mfd` at `0x49` with children `adafruit,seesaw-adc` (`#io-channel-cells = <1>`, channel@0 and channel@1 nodes with `zephyr,resolution = <10>`) and `adafruit,seesaw-gpio`. Root nodes: `analog-axis` (`poll-period-ms = <15>`; X = logical ch 0 / chip pin 2, Y = ch 1 / pin 3, `in-min = <0>`, `in-max = <1023>`, `in-deadzone = <40>`) and `gpio-keys` with `polling-mode;`, button A on seesaw pin 6, `GPIO_ACTIVE_LOW | GPIO_PULL_UP`. Comment that X/Y order, invert flags and the button pin must be checked on hardware (Adafruit's Joy FeatherWing guide: A=6, B=7, Y=9, X=10, Select=14).
- [ ] **Step 3: `src/main.c`.**
  - `INPUT_CALLBACK_DEFINE(NULL, input_cb, NULL)`: store `INPUT_ABS_X/Y/RZ` values and `INPUT_BTN_0` state in `atomic_t`s.
  - L4 connectivity flag from `NET_EVENT_L4_CONNECTED`/`DISCONNECTED` (same shape as `applications/rasprover/src/app_network.c`, without its blocking wait); call `conn_mgr_all_if_connect(true)` at boot.
  - Pico-ROS: static `picoros_node_t` and `picoros_publisher_t` for `ros_Twist` on `CONFIG_APP_PICOROS_CMD_VEL_TOPIC`; `static bool ros_connect(void)` does interface init → node init → publisher declare with the rasprover-style unwind on failure; `static void ros_disconnect(void)` does undeclare/drop/close.
  - `main()` loop, period `1000 / CONFIG_APP_TELEOP_PUBLISH_HZ` ms:
    - not connected → `teleop_reset(&state)`; if L4 up, try `ros_connect()`; on failure sleep 2 s; `continue`.
    - else read the atomics into a `teleop_input`; `teleop_step`; if true, serialise into a 4-aligned zeroed buffer (`ps_serialize`, see the buffer comment in rasprover `app_picoros.c`) and `picoros_publish`; on failure, or when L4 drops, `ros_disconnect()` and mark not connected.
- [ ] **Step 4: Build.** Run: `mise run agent-build joystick_controller`. Expected: success tail, `logs/joystick_controller-build.log` written. Same error twice after fixes → stop and report.
- [ ] **Step 5: Re-run Task 1 tests** (still pass); clang-format; commit (`Rewrite joystick_controller as Pico-ROS teleop`).

---

### Task 3: ESP32-P4 board with the APEM stick

**Files:**
- Create: `boards/esp32p4_wifi6_poe_eth_esp32p4_hpcore.overlay`, `.conf`
- Modify: `mise.toml` (`joystick_controller` case in `[tasks.app]`), `applications/joystick_controller/tests.yaml`

**Interfaces:** Consumes the Task 2 devicetree contract.

- [ ] **Step 1: Overlay.**
  - `&adc0` `status = "okay"`, channel nodes `channel@4/5/6` (ADC1 ch4 = GPIO20 header pin 13, ch5 = GPIO21 pin 11, ch6 = GPIO22 pin 12), `zephyr,gain = "ADC_GAIN_1_4"` (full 3.3 V range), `zephyr,reference = "ADC_REF_INTERNAL"`, `zephyr,acquisition-time = <ADC_ACQ_TIME_DEFAULT>`, `zephyr,resolution = <12>`.
  - `analog-axis` with X = ch4, Y = ch5, RZ = ch6 (channels increasing, as the binding requires), `in-min = <0>`, `in-max = <4095>`, `in-deadzone = <80>`, `out-min = <-1000>`, `out-max = <1000>`.
  - `gpio-keys` with button on GPIO23 (header pin 7, `&gpio0 23`), `GPIO_ACTIVE_LOW | GPIO_PULL_UP`, `INPUT_BTN_0`.
  - Comment block with the APEM wire colours: pots red = 3V3, blue = GND, white = wiper; button orange/orange between GPIO23 and GND. Warn that BOOT (GPIO35) is RMII TXD1 and is not the deadman.
- [ ] **Step 2: `.conf`**: Ethernet lines from `applications/data_collection/prj.conf` (`CONFIG_NET_L2_ETHERNET`, `CONFIG_ETH_ESP32`).
- [ ] **Step 3: mise + twister.** `ALLOWED="adafruit_qt_py_esp32s3/esp32s3/procpu esp32p4_wifi6_poe_eth/esp32p4/hpcore"`, default unchanged; add the P4 to `tests.yaml` `integration_platforms`.
- [ ] **Step 4: Build both.**
  Run: `mise run agent-build joystick_controller --board esp32p4_wifi6_poe_eth/esp32p4/hpcore`, then `mise run agent-build joystick_controller`. Expected: both succeed.
- [ ] **Step 5: Commit** (`Add ESP32-P4 PoE board to joystick_controller`).

---

### Task 4: Calibration shell

**Files:** Create `src/calibrate.c`; add it to `CMakeLists.txt`.

**Interfaces:** Consumes `teleop_cal_symmetric()` (Task 1), `analog_axis_set_raw_data_cb()`, `analog_axis_calibration_get/set()` (`<zephyr/input/input_analog_axis.h>`), `analog_axis_calibration_save()` (`<zephyr/input/input_analog_axis_settings.h>`). The `analog-axis` device comes from `DEVICE_DT_GET_ONE(analog_axis)`.

- [ ] **Step 1: Shell commands** under `SHELL_STATIC_SUBCMD_SET_CREATE` → `teleop cal`:
  - `teleop cal start`: stick at rest. Install the raw-data callback; the first raw sample per channel is `rest`; track `seen_min/seen_max`.
  - `teleop cal stop`: remove the callback; for each channel `teleop_cal_symmetric()`, keep the existing `in_deadzone`, `analog_axis_calibration_set()`, then `analog_axis_calibration_save()`; print per-axis rest/min/max.
  - `teleop cal show`: print the current calibration per channel.
  - Refuse `stop` without `start`, and refuse a channel whose span is < 100 counts (stick never moved). Error message on the shell, nothing saved.
- [ ] **Step 2: Build both boards** (commands as in Task 3 Step 4). Expected: success.
- [ ] **Step 3: Commit** (`Add teleop calibration shell`).

---

### Task 5: README + hardware verification

**Files:** Rewrite `applications/joystick_controller/README.md`.

- [ ] **Step 1: README**: what it does (deadman semantics, mapping, FeatherWing linear-only); both boards' wiring (tables from the overlays); Wi-Fi provisioning (`wifi cred add -s <ssid> -k 1 -p <psk>`); router (`ros2 run rmw_zenoh_cpp rmw_zenohd`, matching zenoh version pinned in `west.yml`); calibration procedure; the robot-side command-timeout backstop from the spec.
- [ ] **Step 2: Hardware check (human, per board)**, with `RMW_IMPLEMENTATION=rmw_zenoh_cpp ros2 topic echo /cmd_vel` on the host:
  - idle → nothing; hold + push forward → `linear.x > 0` at ~20 Hz (`ros2 topic hz /cmd_vel`); release → exactly one all-zero message.
  - push left → `linear.y > 0`; twist CCW (P4) → `angular.z > 0`. Fix sign with `invert-output` in the overlay.
  - stop `rmw_zenohd` while holding, restart it: no messages until release + re-press.
- [ ] **Step 3: Commit** (`Document joystick_controller teleop`).
