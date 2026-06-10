# RaspRover H-Bridge Motor Support — Design

**Date:** 2026-06-10
**Status:** Approved
**Repos touched:** `rosterloh/zephyr-drivers` (module PR), `zephyr-applications` (this repo)

## Goal

Drive the two H-bridge motor channels on the Waveshare ROS Driver board from the
`rasprover` app, commanded over zenoh via ROS 2 `geometry_msgs/Twist` (`cmd_vel`),
with per-wheel encoder feedback wired through the actuator subsystem.

## Hardware facts (from rasprover-rs firmware, `firmware/src/bin/main.rs`)

| Signal | GPIO | Notes |
|---|---|---|
| Left  AIN1 | 21 | direction input 1 |
| Left  AIN2 | 17 | direction input 2 |
| Left  PWMA | 25 | PWM, 20 kHz (LEDC LS ch2 sigmap available) |
| Right BIN1 | 22 | direction input 1 |
| Right BIN2 | 23 | direction input 2 |
| Right PWMB | 26 | PWM, 20 kHz (LEDC LS ch3 sigmap available) |
| Left encoder | 35 | single-channel, input-only pin, PCNT unit 0 |
| Right encoder | 16 | single-channel, PCNT unit 1 |

- TB6612-style PWM + IN1/IN2 signalling; no STBY pin wired.
- Encoders are single-channel: **no direction information**. Ticks accumulate on
  both edges regardless of rotation direction (same limitation as the Rust
  firmware). 10 µs glitch filter.
- Channel A = left, channel B = right (matches `motors.set_velocity(left, right)`).

## Existing building blocks

- `rosterloh,actuator-hbridge` driver in rosterloh-drivers already implements
  PWM+IN1/IN2 signalling, BRAKE/COAST drive modes, and an optional `encoder`
  phandle read via `SENSOR_CHAN_ROTATION` (degrees). Its `set_setpoint` passes
  velocity through as normalised duty (−1.0..1.0) — open-loop, equivalent to the
  Rust firmware's −100..100.
- The `ros_driver` board definition lives in rosterloh-drivers
  (`boards/waveshare/ros_driver/`). `&ledc0` uses LS timers 0/1 + channels 0/1
  for PWM LEDs; LS timers/channels 2/3 are free.
- Upstream Zephyr `espressif,esp32-pcnt` sensor driver is **single-instance and
  only reports unit 0** (`channel_get` hardcodes `unit_config[0]`), so it cannot
  serve two wheels. This motivates the new driver below.
- rasprover's zenoh layer speaks CDR-encoded ROS 2 messages to a zenoh-ros2dds
  bridge under key prefix `CONFIG_APP_ZENOH_KEY_PREFIX` (default `rt/rasprover`).

## Part 1 — Multi-unit PCNT encoder driver (rosterloh-drivers)

New sensor driver, compatible `rosterloh,esp32-pcnt`, at
`drivers/sensor/pcnt_esp32/` in rosterloh-drivers (name may follow that repo's
conventions). The board DTS overrides the `&pcnt` node's compatible so the
upstream driver stays out of the build.

- **Per-unit devices:** each unit child node (`unit0@0`, `unit1@1`, …) is
  defined as its own Zephyr sensor device, so each hbridge instance gets its own
  `encoder` phandle.
- **Accumulation:** 16-bit hardware counter with high/low-limit interrupts
  folding overflow into a software `int64` total (same scheme as the Rust
  firmware's `LIMIT = 30_000` handling).
- **Counting mode:** per-unit channel config; for this board, channel 0 signal
  input with both edges incrementing, control input unused (Keep).
- **Filter:** per-unit glitch filter property (APB ticks, as upstream), set to
  ~10 µs for this board.
- **Scaling:** required per-unit `counts-per-revolution` property converts the
  accumulated count to degrees for `SENSOR_CHAN_ROTATION`
  (val1 = whole degrees, val2 = micro-degrees), which is exactly what
  `actuator_hbridge.c`'s feedback path consumes.
- **Direction:** none available from single-channel encoders; rotation is
  monotonically increasing. Documented in the binding.

## Part 2 — Board DTS additions (rosterloh-drivers, same branch)

In `ros_driver_procpu.dts` / `ros_driver-pinctrl.dtsi`:

- **Pinctrl:** `LEDC_CH2_GPIO25`, `LEDC_CH3_GPIO26` added to the ledc group;
  new `pcnt_default` group with `PCNT0_CH0SIG_GPIO35`, `PCNT1_CH0SIG_GPIO16`
  (bias-pull-up, matching the Rust firmware's pull-ups).
- **`&ledc0`:** add `channel2@2` (timer 2) and `channel3@3` (timer 3).
- **`&pcnt`:** `compatible = "rosterloh,esp32-pcnt"`, `status = "okay"`,
  `unit0@0` (left) and `unit1@1` (right), each with ch0 both-edge increment,
  10 µs filter, placeholder `counts-per-revolution` (see Calibration).
- **Two hbridge nodes** (root-level, e.g. under a `motors` container):
  - `left_motor`: `pwms = <&ledc0 2 …>`, `in1-gpios = <&gpio0 21 …>`,
    `in2-gpios = <&gpio0 17 …>`, `encoder = <&pcnt_unit0>`.
  - `right_motor`: `pwms = <&ledc0 3 …>`, `in1-gpios = <&gpio0 22 …>`,
    `in2-gpios = <&gpio0 23 …>`, `encoder = <&pcnt_unit1>`.
  - `pwm-period-ns` left at the binding default 50000 (20 kHz). No `stby-gpios`.
- **Aliases:** `left-motor`, `right-motor`.

## Part 3 — rasprover app (this repo)

### `src/app_motors.c` / `app_motors.h`

- Resolves both actuators via the `left-motor` / `right-motor` aliases;
  `actuator_enable()` on init; logs and degrades gracefully (returns false) if
  devices are missing/not ready.
- `void app_motors_cmd_vel(float linear_x_m_s, float angular_z_rad_s)`:
  differential mix `v_l = linear − angular·track/2`, `v_r = linear +
  angular·track/2`, each normalised to ±1.0 duty by the max wheel speed, then
  `actuator_set_velocity()` per wheel.
- **Command watchdog:** `k_work_delayable` rescheduled on every command; on
  expiry (500 ms) both motors get `actuator_set_velocity(0)` +
  `ACTUATOR_DRIVE_MODE_COAST`. Mirrors the Rust firmware's stop-on-silence
  behaviour.
- Kconfig (app `Kconfig`): `APP_MOTORS_WHEEL_SEPARATION_MM`,
  `APP_MOTORS_MAX_SPEED_MM_S`, `APP_MOTORS_CMD_TIMEOUT_MS` (default 500).

### `src/app_zenoh.c`

- Declare a subscriber on `CONFIG_APP_ZENOH_KEY_PREFIX "/cmd_vel"`.
- Decode CDR-LE `geometry_msgs/Twist`: 4-byte CDR header + 6×float64
  (linear.xyz, angular.xyz) = 52 bytes; validate length and header, extract
  `linear.x` and `angular.z`, forward to `app_motors_cmd_vel()`.

### Config / build

- `prj.conf`: `CONFIG_ACTUATOR=y`, `CONFIG_ACTUATOR_HBRIDGE=y`,
  `CONFIG_ACTUATOR_HBRIDGE_ENCODER=y`, `CONFIG_PWM=y`, `CONFIG_SENSOR=y`.
- `main.c`: call `app_motors_init()` alongside the other init calls.
- **native_sim:** `boards/native_sim_native_64.overlay` gains two
  `rosterloh,actuator-fake` nodes under the same aliases so the
  cmd_vel → mixing → actuator path builds and runs in simulation
  (`CONFIG_ACTUATOR_FAKE=y` in the board conf; hbridge/encoder configs stay off).

## Calibration defaults (placeholders, flagged for tuning on hardware)

- `counts-per-revolution`: 1320 (typical GB37-520-class gearmotor, 1:30 gear,
  both-edge single-channel counting) — set in board DTS, override per robot.
- `APP_MOTORS_WHEEL_SEPARATION_MM`: 150.
- `APP_MOTORS_MAX_SPEED_MM_S`: 500.

These only affect feedback scaling and twist mixing, not signal correctness.

## Error handling

- Missing/not-ready actuator devices: `app_motors_init()` logs an error and the
  zenoh callback becomes a no-op (rover still runs sensors/display).
- Malformed cmd_vel payload (wrong length/header): dropped with a rate-limited
  warning.
- Encoder device not ready: hbridge `init` already fails the device; covered by
  the missing-device path above.

## Testing / verification

1. rosterloh-drivers branch: build in-place in the module checkout (west owns
   the path; commits go to that repo, PR against `rosterloh/zephyr-drivers`).
2. `uv run poe agent-build rasprover --sysbuild` (hardware target,
   ros_driver/esp32/procpu) — must build clean.
3. `uv run poe app rasprover --board native_sim/native/64` — sim path builds;
   run the binary and confirm init logs (fake actuators enabled, no crash).
4. clang-format (`uv run clang-format --dry-run --Werror`) on all touched C
   files in both repos.
5. On-hardware smoke test (manual, user): publish a Twist via zenoh, verify
   wheels respond and stop ~500 ms after publishing ceases.

## Out of scope / follow-ups

- Odometry/JointState publishing from encoder feedback (natural next step; the
  feedback plumbing this design adds makes it cheap).
- Closed-loop wheel velocity control (needs direction-aware encoders or
  commanded-direction sign injection).
- After the module PR merges: `uv run poe west-update` and drop any temporary
  local module state.
