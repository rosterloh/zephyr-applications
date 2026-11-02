# Joystick Controller (Pico-ROS teleop)

A physical teleop joystick that publishes straight onto a ROS 2 graph through
Pico-ROS (rmw_zenoh wire format). It drives either a robot arm through MoveIt
Servo (the default) or a mobile base on `/cmd_vel`, picked at build time with
`APP_TELEOP_TARGET_ARM` / `APP_TELEOP_TARGET_BASE`.

The ESP32-P4 build lives in a printed box with the stick, an enable rocker and
a mod button:
[mechanical-parts/joystick-p4-box](https://github.com/rosterloh/mechanical-parts/tree/main/joystick-p4-box).

- **Enable** (latching rocker on the P4, held button A on the QT Py) gates
  everything. Switching it on with the stick deflected does nothing until the
  stick has been centred.
- While enabled, a deflected stick publishes at 20 Hz; centring it, switching
  enable off or changing mod sends exactly one all-zero twist, then silence,
  so other command sources (nav2 behind a twist_mux) aren't blocked.
- After boot or a link drop nothing is sent until enable has been seen off,
  so a rocker left on doesn't start the robot.
- **Mod** (arm only) shifts the stick to lift/pitch/roll; the stick must
  centre again after each change.
- **Grasp** (arm only, the stick's own button) publishes `std_msgs/Bool` on
  `/grasp` when it changes while enabled.

| Input | Arm: TwistStamped | Arm, mod held | Base: Twist |
|---|---|---|---|
| stick forward/back (Y) | `linear.x` | `linear.z` | `linear.x` |
| stick left/right (X) | `linear.y` | `angular.y` | `linear.y` |
| handle twist (Z, P4 only) | `angular.z` | `angular.x` | `angular.z` |

Linear axes reach 0.5 m/s and angular ones 1.0 rad/s at full deflection.
Limits, rate, topics, frame, node name, domain and router locator are Kconfig
options (`APP_TELEOP_*`, `APP_PICOROS_*`).

**Arm needs synced time.** MoveIt Servo drops a `TwistStamped` older than its
`incoming_command_timeout` (0.1 s by default) by the ROS clock, so the
controller's SNTP clock (`APP_TIME_SNTP_SERVER`, resynced every 60 s) and the
ROS host must agree to well under that: point both at the same NTP server,
ideally the host itself. Until the first sync the stamp is zero and Servo
ignores every command.

**Safety backstop:** if the controller loses power or its link it cannot send
a stop. The robot side must time out stale commands (Servo's
`incoming_command_timeout`, twist_mux timeouts, or e.g. rasprover's 500 ms
`APP_MOTORS_CMD_TIMEOUT_MS`).

## Boards

```bash
mise run app joystick_controller                                              # QT Py ESP32-S3 + Joy FeatherWing
mise run app joystick_controller --board esp32p4_wifi6_poe_eth/esp32p4/hpcore # ESP32-P4 PoE + R400B stick
```

For a mobile base instead of an arm (the `app` task takes no extra CMake
arguments):

```bash
mise x -- west build -b esp32p4_wifi6_poe_eth/esp32p4/hpcore -p always --build-dir builds/joystick_controller \
  applications/joystick_controller -- -DCONFIG_APP_TELEOP_TARGET_BASE=y
```

### Adafruit QT Py ESP32-S3 + Joy FeatherWing (Wi-Fi, 2-axis)

Joy FeatherWing seesaw (0x49) on the STEMMA QT I2C bus. Enable = button A,
held; mod = B; grasp = X. Stick on seesaw ADC pins 2/3. Check the axis order,
signs and button pins on first use (see the overlay).

Provision Wi-Fi once over the shell:

```
wifi cred add -s <ssid> -k 1 -p <psk>
```

### Waveshare ESP32-P4-WIFI6-POE-ETH + HZQS R400B-M4 (Ethernet/PoE)

In [joystick-p4-box](https://github.com/rosterloh/mechanical-parts/tree/main/joystick-p4-box).
R400B-M4: 10 kOhm pots with a 50° electrical angle, matching the stick's ±25°
travel. The header has the Raspberry Pi layout (GND on 6, 9, 14, 20, 25, 30, 34,
39). The handle pot comes wired; the X and Y pots need wires soldered to their
lugs: daisy-chain their outer lugs onto the handle pot's red and black.

| Signal | GPIO | Header pin | Wire |
|---|---|---|---|
| X wiper | 20 (ADC1 ch4) | 13 | X pot middle lug |
| Y wiper | 21 (ADC1 ch5) | 11 | Y pot middle lug |
| Z wiper | 22 (ADC1 ch6) | 12 | handle white |
| Pot supply | 3V3 | 1 | handle red + X/Y outer lugs |
| Pot return | GND | 6 | handle black + X/Y other outer lugs |
| Grasp | 23 | 7 | handle blue (other blue → GND, e.g. pin 9) |
| Enable | 6 | 15 | rocker tab (other tab → GND pin 14) |
| Mod | 48 | 33 | RAFI terminal (other terminal → GND pin 34) |

Do not use the BOOT button: GPIO35 is RMII TXD1 while Ethernet is up.
Address comes from DHCP.

## Host

Run the router whose zenoh version matches the `zenoh` pin in `west.yml`:

```bash
ros2 run rmw_zenoh_cpp rmw_zenohd
RMW_IMPLEMENTATION=rmw_zenoh_cpp ros2 topic echo /cmd_vel
```

## Calibration

The pots cover only part of the ADC range, so calibrate each new stick. The
result is stored in settings and survives reboots.

```
teleop cal start   # stick at rest
                   # move every axis to both limits
teleop cal stop    # stores a range symmetric about the rest position
teleop cal show
```

## Hardware check

Arm (default), with `ros2 topic echo /servo_node/delta_twist_cmds` and
`ros2 topic hz` on it; base builds publish the same on `/cmd_vel` as Twist:

- [ ] boot log shows `time synced via SNTP` before the stick is used
- [ ] enable off, stick moved → nothing published
- [ ] enable on with the stick pushed → nothing until it is centred
- [ ] push forward → `linear.x > 0` at ~20 Hz, stamp within 0.1 s of
      `ros2 topic echo --field header` on the host's clock
- [ ] let go → exactly one all-zero message, then silence
- [ ] push left → `linear.y > 0`; twist counter-clockwise (P4) → `angular.z > 0`.
      Fix a wrong sign with `invert-output` on that axis in the overlay.
- [ ] hold mod mid-move → one all-zero message, nothing until centred, then
      forward → `linear.z > 0`
- [ ] grasp pressed / released → `/grasp` `true` / `false`, only while enabled
- [ ] enable left on, power-cycled → nothing until it is switched off and on
- [ ] stop `rmw_zenohd` while moving, restart it → nothing until enable is
      switched off and on
