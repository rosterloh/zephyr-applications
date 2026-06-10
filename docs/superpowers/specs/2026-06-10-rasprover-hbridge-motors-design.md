# RaspRover H-Bridge Motor Support — Design

**Date:** 2026-06-10
**Status:** Approved
**Repos touched:** `rosterloh/zephyr-drivers` (module PR), `zephyr-applications` (this repo)

## Goal

Drive the two H-bridge motor channels on the Waveshare ROS Driver board from the
`rasprover` app, commanded over zenoh via ROS 2 `geometry_msgs/Twist` (`cmd_vel`),
with per-wheel encoder feedback wired through the actuator subsystem.

## Hardware facts

Cross-checked between rasprover-rs (`firmware/src/bin/main.rs`) and the vendor
firmware (`waveshareteam/ugv_base_ros`, `ROS_Driver/ugv_config.h` +
`movtion_module.h`), which is authoritative for this board.

| Signal | GPIO | Notes |
|---|---|---|
| Left  AIN1 | 21 | direction input 1 |
| Left  AIN2 | 17 | direction input 2 |
| Left  PWMA | 25 | PWM (LEDC LS ch2 sigmap available) |
| Right BIN1 | 22 | direction input 1 |
| Right BIN2 | 23 | direction input 2 |
| Right PWMB | 26 | PWM (LEDC LS ch3 sigmap available) |
| Left encoder A (AENCA) | 35 | PCNT unit 0 signal input (input-only pin) |
| Left encoder B (AENCB) | 34 | PCNT unit 0 control input (input-only pin) |
| Right encoder A (BENCA) | 27 | PCNT unit 1 signal input |
| Right encoder B (BENCB) | 16 | PCNT unit 1 control input |

- TB6612-style PWM + IN1/IN2 signalling; no STBY pin wired.
- **Encoders are quadrature.** The vendor firmware uses
  `ESP32Encoder::attachHalfQuad(ENCA, ENCB)` — half-quad decode (both edges of
  ENCA, direction from ENCB). rasprover-rs only wired ENCA per wheel and lost
  direction; we use both channels, so feedback is signed.
- **Forward polarity**: vendor positive drive is IN1 low / IN2 high (both
  channels). Our hbridge driver asserts in1 for forward, so the DT swaps the
  assignment (`in1-gpios` = AIN2/BIN2 pin, `in2-gpios` = AIN1/BIN1 pin), which
  preserves brake = both-high / coast = both-low semantics.
- **PWM**: vendor runs 100 kHz, 8-bit (rasprover-rs used 20 kHz). Use the
  vendor-proven 100 kHz → `pwm-period-ns = <10000>`.
- Channel A = left, channel B = right (both firmwares agree).
- Vendor RaspRover constants (`mainType:01`): wheel Ø 0.080 m,
  `ONE_CIRCLE_PLUSES` 2100 (half-quad counts/wheel-rev), track width 0.125 m,
  no direction inversion. Vendor stops motors after a 3 s command heartbeat
  timeout and runs a speed PID (kp=20, ki=2000, kd=0, ±255) — we stay
  open-loop this pass.

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
- **Counting mode:** per-unit channel config (sig pos/neg edge actions, ctrl
  high/low actions, as in the upstream child binding). For this board: half-quad
  decode — channel 0 signal input counts both edges, control input inverts the
  count direction (matches vendor `attachHalfQuad`), giving **signed** counts.
- **Filter:** per-unit glitch filter property (APB ticks, as upstream), set to
  ~10 µs for this board.
- **Scaling:** required per-unit `counts-per-revolution` property converts the
  accumulated count to degrees for `SENSOR_CHAN_ROTATION`
  (val1 = whole degrees, val2 = micro-degrees), which is exactly what
  `actuator_hbridge.c`'s feedback path consumes.

## Part 2 — Board DTS additions (rosterloh-drivers, same branch)

In `ros_driver_procpu.dts` / `ros_driver-pinctrl.dtsi`:

- **Pinctrl:** `LEDC_CH2_GPIO25`, `LEDC_CH3_GPIO26` added to the ledc group;
  new `pcnt_default` group with `PCNT0_CH0SIG_GPIO35`, `PCNT0_CH0CTRL_GPIO34`,
  `PCNT1_CH0SIG_GPIO27`, `PCNT1_CH0CTRL_GPIO16` (bias-pull-up; all four macros
  verified present in `esp32-pinctrl.h`).
- **`&ledc0`:** add `channel2@2` (timer 2) and `channel3@3` (timer 3).
- **`&pcnt`:** `compatible = "rosterloh,esp32-pcnt"`, `status = "okay"`,
  `unit0@0` (left) and `unit1@1` (right), each in half-quad mode (ch0 sig both
  edges, ctrl inverts), 10 µs filter, `counts-per-revolution = <2100>` (vendor
  value for RaspRover).
- **Two hbridge nodes** (root-level, e.g. under a `motors` container), with
  in1/in2 swapped relative to the silkscreen names so that the driver's
  forward = vendor's forward (see polarity note above):
  - `left_motor`: `pwms = <&ledc0 2 …>`, `in1-gpios = <&gpio0 17 …>` (AIN2),
    `in2-gpios = <&gpio0 21 …>` (AIN1), `encoder = <&pcnt_unit0>`.
  - `right_motor`: `pwms = <&ledc0 3 …>`, `in1-gpios = <&gpio0 23 …>` (BIN2),
    `in2-gpios = <&gpio0 22 …>` (BIN1), `encoder = <&pcnt_unit1>`.
  - `pwm-period-ns = <10000>` (vendor's 100 kHz). No `stby-gpios`.
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

## Robot constants (vendor values for RaspRover, `mainType:01`)

- `counts-per-revolution = <2100>` (half-quad counts per wheel revolution) —
  board DTS.
- `APP_MOTORS_WHEEL_SEPARATION_MM`: 125 (vendor `TRACK_WIDTH` 0.125 m).
- Wheel diameter 80 mm (vendor `WHEEL_D`) — recorded here for the odometry
  follow-up; not needed by this pass.
- `APP_MOTORS_MAX_SPEED_MM_S`: 500 (estimate; only scales twist→duty mixing,
  tune on hardware).

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
  feedback plumbing this design adds makes it cheap, and quadrature decode
  makes it signed).
- Closed-loop wheel velocity control (quadrature feedback makes this feasible;
  vendor reference gains kp=20, ki=2000, kd=0 on ±255 duty).
- After the module PR merges: `uv run poe west-update` and drop any temporary
  local module state.
