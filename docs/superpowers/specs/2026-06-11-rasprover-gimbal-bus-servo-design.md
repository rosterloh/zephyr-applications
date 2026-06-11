# RaspRover Gimbal Bus Servo Support - Design

**Date:** 2026-06-11
**Status:** Approved
**Repos touched:** `rosterloh/zephyr-drivers` (module PR), `zephyr-applications` (this repo)

## Goal

Add support for the Waveshare gimbal module on the `rasprover` firmware.

The implementation has two layers:

1. A reusable serial bus-servo driver in `rosterloh/zephyr-drivers`.
2. `applications/rasprover` configuration and application code that exposes the
   gimbal as ROS 2 joint state feedback and a ROS 2 joint-position command
   topic over zenoh.

The command topic uses `sensor_msgs/msg/JointState` with `name` entries
`pan_joint` and `tilt_joint`. Commanded positions are radians. Servo protocol
details and raw ticks stay below the ROS boundary.

## Hardware facts

The Waveshare reference firmware under `waveshareteam/ugv_base_ros`,
`ROS_Driver`, provides the current hardware map and control behavior:

| Item | Vendor value | Notes |
|---|---:|---|
| Servo UART RX | GPIO18 | `S_RXD` / `RoArmM2_Servo_RXD` |
| Servo UART TX | GPIO19 | `S_TXD` / `RoArmM2_Servo_TXD` |
| Pan servo ID | 2 | `GIMBAL_PAN_ID` |
| Tilt servo ID | 1 | `GIMBAL_TILT_ID` |
| Servo center tick | 2047 | Reference conversion uses center around 2047 |
| Position range | 0..4095 | 4096 ticks per 360 degrees |
| Pan limit | -180..180 deg | Vendor clamps command input |
| Tilt limit | -30..90 deg for simple/move | Vendor also uses -45..90 in user/steady helpers |

The existing `ros_driver/esp32/procpu` board definition already routes UART1 to
GPIO19 TX and GPIO18 RX. The same physical UART is currently aliased as
`zenoh-serial`, but rasprover's zenoh transport is TCP/WiFi, not serial. The
gimbal design reuses UART1 for the servo bus.

The vendor Arduino code includes `SCServo.h`, calls `SyncWritePosEx()`, reads
feedback fields via `FeedBack()`, and exposes gimbal commands as JSON IDs
133-137 and 141. We do not copy that JSON interface into rasprover; it is used
only as protocol and behavior reference.

## Existing building blocks

- `rosterloh-drivers` already owns the reusable actuator subsystem and hardware
  backends. Wheel support in rasprover goes through `actuator_set_velocity()`
  and `actuator_read_feedback()`.
- `rosterloh-drivers` already has a Dynamixel serial protocol driver and
  `rosterloh,actuator-dxl` backend. It is useful as a structural reference, but
  it should not be reused directly for SCServo/Feetech-compatible bus servos.
  The frame header, checksum/CRC, instruction set, and register model differ.
- `applications/rasprover` already publishes CDR-encoded
  `sensor_msgs/msg/JointState` to `rt/joint_states`, and subscribes to
  CDR-encoded `geometry_msgs/msg/Twist` for `cmd_vel`.
- `applications/rasprover` has native-sim coverage through fake actuator nodes.
  Gimbal support should preserve that pattern so the app builds and the command
  path can be exercised without hardware.

## Part 1 - Bus servo protocol driver in `rosterloh-drivers`

Add a serial bus driver for the Waveshare/SCServo-style protocol. Use these
module names unless implementation uncovers a protocol-level reason to rename
them before the first commit:

- `include/drivers/bus_servo.h`
- `drivers/bus_servo/`
- `dts/bindings/waveshare,bus-servo.yaml`

The bus node is a UART child, like the existing `robotis,dynamixel` binding.
It owns half-duplex serial framing and optional transmit-enable GPIO support if
future boards need it.

Required protocol operations for this feature:

- ping or presence check for a servo ID
- torque enable/disable
- write goal position with optional speed and acceleration
- read present position
- read present speed
- read present load/current/voltage/temperature when the register map supports
  it
- sync write position for pan and tilt on the same bus

The protocol API should return normal negative errno values for transport or
validation failures. Device-reported error/status bytes should be translated
into either errno or actuator fault flags at the actuator layer.

The driver should include protocol-level tests in `rosterloh-drivers` using a
fake UART/bus harness modelled after the Dynamixel tests. The minimum useful
tests are:

- generated frame bytes for write-position and torque commands
- checksum validation
- read-response parsing for position and speed
- timeout or malformed response handling
- sync write frame generation for two IDs

## Part 2 - Bus servo actuator backend in `rosterloh-drivers`

Expose each bus servo as an actuator device so rasprover can treat the gimbal
like other joints.

Proposed binding:

```dts
servo_bus: bus-servo {
	compatible = "waveshare,bus-servo";

	pan_servo: servo@2 {
		compatible = "rosterloh,actuator-bus-servo";
		reg = <2>;
		label = "Gimbal pan";
		default-mode = "position";
		ticks-per-rev = <4096>;
		rad-zero-tick = <2047>;
		position-min-rad-milli = <-3142>;
		position-max-rad-milli = <3142>;
	};

	tilt_servo: servo@1 {
		compatible = "rosterloh,actuator-bus-servo";
		reg = <1>;
		label = "Gimbal tilt";
		default-mode = "position";
		ticks-per-rev = <4096>;
		rad-zero-tick = <2047>;
		position-min-rad-milli = <-524>;
		position-max-rad-milli = <1571>;
		invert-position;
	};
};
```

The design intent is pan +/- pi rad and tilt -30..90 degrees.

Backend behavior:

- `actuator_enable()` enables torque and starts feedback polling.
- `actuator_disable()` disables torque and stops feedback polling.
- `actuator_set_position()` clamps to the configured min/max and writes the raw
  goal tick.
- `actuator_set_velocity()` and `actuator_set_effort()` return `-ENOTSUP` for
  this first pass.
- `actuator_read_feedback()` returns position and velocity when available. It
  may include effort/current, voltage, and temperature if those map cleanly to
  existing `struct actuator_feedback` fields.
- group setpoint support should use the bus sync-write operation when all
  actuators are on the same bus. That gives pan and tilt coordinated updates
  from rasprover without baking gimbal-specific sync logic into the app.

Conversion rules:

- Position setpoint in radians maps to servo ticks by
  `rad-zero-tick + rad * ticks-per-rev / (2*pi)`, with optional inversion.
- Feedback maps raw ticks back to radians using the inverse conversion.
- Raw servo position is not exposed in ROS messages.

## Part 3 - Board DTS and Kconfig in `rosterloh-drivers`

Update the Waveshare `ros_driver/esp32/procpu` board definition:

- Keep UART1 on GPIO19 TX / GPIO18 RX.
- Add a bus-servo node on UART1.
- Add child actuator nodes for servo IDs 2 and 1.
- Add aliases:
  - `gimbal-pan = &pan_servo`
  - `gimbal-tilt = &tilt_servo`

Enablement remains application-controlled through Kconfig. The board should
describe the hardware; rasprover decides whether to turn on the driver.

New Kconfig symbols in `rosterloh-drivers`:

- `BUS_SERVO`
- `ACTUATOR_BUS_SERVO`
- log-level symbols following the existing driver style

The rasprover hardware config will select/enable:

- `CONFIG_ACTUATOR=y`
- `CONFIG_BUS_SERVO=y`
- `CONFIG_ACTUATOR_BUS_SERVO=y`
- UART interrupt support if the bus driver needs it

Native simulation should not instantiate the real bus driver. It will use fake
actuators for the same `gimbal-pan` and `gimbal-tilt` aliases.

## Part 4 - `rasprover` application module

Add `src/app_gimbal.c` and `src/app_gimbal.h`, gated by `CONFIG_APP_GIMBAL`.

Responsibilities:

- Resolve `DT_ALIAS(gimbal_pan)` and `DT_ALIAS(gimbal_tilt)`.
- Enable both actuators on startup.
- Provide a small command API:

```c
bool app_gimbal_init(void);
void app_gimbal_set_positions(float pan_rad, float tilt_rad);
bool app_gimbal_read_joint_state(struct app_gimbal_joint_state joints[APP_GIMBAL_JOINT_COUNT]);
```

Joint names are fixed:

- `pan_joint`
- `tilt_joint`

`app_gimbal_set_positions()` should clamp through the actuator backend limits,
log setpoint failures, and hold the last commanded position. There is no command
watchdog for this first pass because the ROS command is a position setpoint, not
a velocity stream.

Kconfig in `applications/rasprover/Kconfig`:

- `APP_GIMBAL`, default `y` when `ACTUATOR`
- `APP_ZENOH_GIMBAL_CMD_KEY`, default
  `CONFIG_APP_ZENOH_KEY_PREFIX "/gimbal_cmd"`

The app should continue to boot if gimbal devices are missing or not ready:
log the failure, leave the gimbal disabled, keep battery/motor ROS behavior
running.

## Part 5 - ROS CDR changes in `rasprover`

### JointState publication

When `CONFIG_APP_GIMBAL=y`, `rt/joint_states` should include four joints:

1. `left_wheel_joint`
2. `right_wheel_joint`
3. `pan_joint`
4. `tilt_joint`

The existing encoder already handles arbitrary joint counts. Increase the local
buffer size in `app_zenoh.c` enough for four names and four float64 positions
and velocities. Keep effort empty.

If wheel feedback is available but gimbal feedback is temporarily unavailable,
the initial implementation may skip the whole publish cycle and warn. A later
improvement can publish partial joint state, but that complicates consumers
because `JointState` arrays must stay internally consistent.

### JointState command subscription

Declare a subscriber on `CONFIG_APP_ZENOH_GIMBAL_CMD_KEY`, defaulting to:

| Zenoh key | ROS 2 topic | Message |
|---|---|---|
| `rt/rasprover/gimbal_cmd` | `/rasprover/gimbal_cmd` | `sensor_msgs/msg/JointState` |

Add a CDR little-endian decoder for the subset of `sensor_msgs/msg/JointState`
that rasprover needs:

- validate the CDR little-endian header
- skip the ROS header fields
- read the `name` string sequence
- read the `position` float64 sequence
- ignore `velocity` and `effort`

Command handling rules:

- Names are authoritative; order is not.
- Apply only `pan_joint` and `tilt_joint`.
- Require both target joints and both position values in a command before
  applying a setpoint.
- Ignore unknown names.
- Drop malformed payloads with a warning.
- Drop payloads with missing target names, missing positions, or mismatched
  name/position lengths with a warning.
- Positions are radians.

This gives the user an array-of-positions command while keeping the message
self-describing and consistent with `/joint_states` feedback.

## Part 6 - Native simulation

Update `applications/rasprover/boards/native_sim_native_64.overlay`:

- add fake actuator nodes for `gimbal-pan` and `gimbal-tilt`
- keep names and aliases identical to hardware DTS

Update `applications/rasprover/boards/native_sim_native_64.conf`:

- ensure the real bus-servo protocol driver is off
- keep `CONFIG_ACTUATOR_FAKE=y`
- allow `CONFIG_APP_GIMBAL=y` so the app-level code builds in sim

Native sim cannot validate SCServo wire traffic. It should validate:

- `app_gimbal` init path with actuator devices present
- JointState command decode path
- setpoint dispatch into fake actuators
- expanded JointState publication build path

## Error handling

- Missing gimbal aliases: compile-time failure when `APP_GIMBAL` is enabled for
  a board that does not describe the hardware. Native sim and hardware DTS must
  both provide aliases.
- Device not ready or enable failure: runtime log, gimbal disabled, rest of app
  continues.
- Servo bus timeout/read error: actuator feedback call fails; rasprover skips
  the affected publish cycle and logs a warning.
- Malformed command payload: subscriber drops the command and leaves current
  servo setpoints unchanged.
- Out-of-range command: actuator backend clamps to DT min/max.

## Testing and verification

Driver module:

1. Add protocol tests in `deps/modules/lib/rosterloh-drivers/tests/drivers/`.
2. Add actuator backend tests with fake bus responses.
3. Run relevant `twister` suites from this workspace using `uv run west` or the
   module's existing test command through `uv run`.
4. Commit driver changes inside `deps/modules/lib/rosterloh-drivers`, not this
   repo.

Application:

1. Add Python/C host tests for CDR JointState command decoding.
2. Extend topic/default tests to cover `APP_ZENOH_GIMBAL_CMD_KEY` and joint
   names.
3. Build native sim:
   `uv run poe app rasprover --board native_sim/native/64`
4. Build hardware target:
   `uv run poe agent-build rasprover --sysbuild`
5. Run clang-format dry-run on touched C files:
   `uv run clang-format --dry-run --Werror <files>`

Hardware smoke test:

1. Flash or OTA the rasprover firmware.
2. Start `zenohd` and `zenoh-ros2dds` on the host.
3. Confirm `/joint_states` includes `pan_joint` and `tilt_joint`.
4. Publish a `sensor_msgs/msg/JointState` to `/rasprover/gimbal_cmd` with:
   - `name: ["pan_joint", "tilt_joint"]`
   - `position: [0.0, 0.0]`
5. Publish small nonzero pan/tilt commands and confirm motion direction and
   limits.
6. Confirm that a one-shot command holds position until the next command.

## Out of scope / follow-ups

- Full vendor JSON command compatibility.
- Gimbal steady/stabilization mode using IMU pitch compensation.
- Velocity/acceleration command fields over ROS.
- Raw servo diagnostics topic.
- Closed-loop smoothing or trajectory interpolation.
- Supporting RoArm-M2 with the same bus-servo driver. The driver should be
  reusable enough for that later, but this implementation only configures the
  two gimbal servos.
