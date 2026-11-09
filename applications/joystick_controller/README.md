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

The app boots through MCUboot, so build with `--sysbuild` to get the
bootloader too; `flash` then writes both.

```bash
mise run app joystick_controller --sysbuild                                              # QT Py ESP32-S3 + Joy FeatherWing
mise run app joystick_controller --sysbuild --board esp32p4_wifi6_poe_eth/esp32p4/hpcore # ESP32-P4 PoE + R400B stick
mise run flash joystick_controller
```

With more than one serial device attached, name the port:
`mise x -- west flash --build-dir builds/joystick_controller --esp-device /dev/cu.usbmodemXXXX`.

### Pointing it at your ROS host

The defaults (router `tcp/192.168.1.10:7447`, `ROS_DOMAIN_ID` 0, SNTP
`pool.ntp.org`, arm target) rarely match a real setup. Put the overrides in a
fragment outside the repo, e.g. `builds/joystick_host.conf` (gitignored):

```
CONFIG_APP_PICOROS_LOCATOR="tcp/<host-ip>:7447"
CONFIG_APP_PICOROS_DOMAIN_ID=0
CONFIG_APP_TIME_SNTP_SERVER="<the host's NTP server>"
# CONFIG_APP_TELEOP_TARGET_BASE=y   # mobile base on /cmd_vel instead of an arm
```

The `app` task takes no extra CMake arguments, so build with `west` directly.
Under sysbuild the option needs the image-name prefix and an absolute path:

```bash
mise x -- west build -b esp32p4_wifi6_poe_eth/esp32p4/hpcore -p always --sysbuild \
  --build-dir builds/joystick_controller applications/joystick_controller -- \
  -Djoystick_controller_EXTRA_CONF_FILE=$PWD/builds/joystick_host.conf
mise run flash joystick_controller
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
lugs: daisy-chain their outer lugs onto the handle pot's red and white. The
handle pot's wiper is its black wire (red to white is the full 10 kOhm).

| Signal | GPIO | Header pin | Wire |
|---|---|---|---|
| X wiper | 20 (ADC1 ch4) | 13 | X pot middle lug |
| Y wiper | 21 (ADC1 ch5) | 11 | Y pot middle lug |
| Z wiper | 22 (ADC1 ch6) | 12 | handle black |
| Pot supply | 3V3 | 1 | handle red + X/Y outer lugs |
| Pot return | GND | 6 | handle white + X/Y other outer lugs |
| Grasp | 23 | 7 | handle blue (other blue → GND, e.g. pin 9) |
| Enable | 6 | 15 | rocker tab (other tab → pin 16, GPIO5 held low as ground) |
| Mod | 48 | 33 | RAFI terminal, normally closed (other terminal → GND pin 34) |

Do not use the BOOT button: GPIO35 is RMII TXD1 while Ethernet is up.
Address comes from DHCP.

## Host

Run the router whose zenoh version matches the `zenoh` pin in `west.yml`, on
an address the controller can reach (the locator above), and use the same
domain as `APP_PICOROS_DOMAIN_ID`:

```bash
ros2 run rmw_zenoh_cpp rmw_zenohd
export RMW_IMPLEMENTATION=rmw_zenoh_cpp ROS_DOMAIN_ID=0
ros2 topic echo /servo_node/delta_twist_cmds   # arm; /cmd_vel for a base build
```

MoveIt Servo starts out expecting joint jog commands and ignores twists
until switched (a teleop UI may do this for you):

```bash
ros2 service call /servo_node/switch_command_type moveit_msgs/srv/ServoCommandType "{command_type: 1}"
```

To check an arm build without a robot, `host/cube.py` moves a cube in RViz
from the TwistStamped stream and logs the rate and stamp age once a second
(Servo drops stamps older than 0.1 s by default). Run both from this
directory in a ROS 2 env on the controller's domain and router. The camera
sits behind the robot, so stick forward moves the cube up the screen:

```bash
python host/cube.py &
rviz2 -d host/cube.rviz
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

For bench bring-up add `debug.conf` to the extra conf files
(`-Djoystick_controller_EXTRA_CONF_FILE="<abs>/debug.conf;<abs>/joystick_host.conf"`).
It adds `input dump on` for stick events, `gpio get gpio0 6` etc. for the
switches and `net iface` for the link and DHCP. `gpio get` prints the logical
value (the overlay's active-low already applied), not the raw pin level.

Arm (default), with `ros2 topic echo /servo_node/delta_twist_cmds` and
`ros2 topic hz` on it; base builds publish the same on `/cmd_vel` as Twist:

- [ ] boot log shows `time synced via SNTP` before the stick is used
- [ ] enable off, stick moved → nothing published
- [ ] enable on with the stick pushed → nothing until it is centred
- [ ] push forward → `linear.x > 0` at ~20 Hz, stamp within 0.1 s of
      `ros2 topic echo --field header` on the host's clock
- [ ] let go → exactly one all-zero message, then silence
- [ ] push left → `linear.y > 0`; twist counter-clockwise (P4) → `angular.z > 0`.
      Fix a wrong sign with `invert-output` on that axis in the overlay, then
      recalibrate.
- [ ] hold mod mid-move → one all-zero message, nothing until centred, then
      forward → `linear.z > 0`
- [ ] grasp pressed / released → `/grasp` `true` / `false`, only while enabled
- [ ] enable left on, power-cycled → nothing until it is switched off and on
- [ ] stop `rmw_zenohd` while moving, restart it → nothing until enable is
      switched off and on
