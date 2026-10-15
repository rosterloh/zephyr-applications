# Joystick Controller (Pico-ROS teleop)

A physical `teleop_twist_joy`: publishes `geometry_msgs/Twist` on `/cmd_vel`
straight onto a ROS 2 graph through Pico-ROS (rmw_zenoh wire format).

- Hold the deadman button → Twist at 20 Hz scaled from the stick.
- Release → exactly one all-zero Twist, then silence (other `cmd_vel`
  sources such as nav2 behind a twist_mux are not blocked).
- After a link drop the controller publishes nothing until the button has
  been released and pressed again.

| Input | Twist |
|---|---|
| stick forward/back (Y) | `linear.x` (max 0.5 m/s) |
| stick left/right (X) | `linear.y` (max 0.5 m/s) |
| knob twist (Z, P4 only) | `angular.z` (max 1.0 rad/s) |

Limits, rate, topic, node name, domain and router locator are Kconfig options
(`APP_TELEOP_*`, `APP_PICOROS_*`).

**Safety backstop:** if the controller loses power or its link it cannot send
a stop. The robot side must time out stale commands (twist_mux timeouts, or
e.g. rasprover's 500 ms `APP_MOTORS_CMD_TIMEOUT_MS`).

## Boards

```bash
mise run app joystick_controller                                              # QT Py ESP32-S3 + Joy FeatherWing
mise run app joystick_controller --board esp32p4_wifi6_poe_eth/esp32p4/hpcore # ESP32-P4 PoE + APEM stick
```

### Adafruit QT Py ESP32-S3 + Joy FeatherWing (Wi-Fi, linear only)

Joy FeatherWing seesaw (0x49) on the STEMMA QT I2C bus. Deadman = button A.
Stick on seesaw ADC pins 2/3. Check the axis order, signs and button pin on
first use (see the overlay).

Provision Wi-Fi once over the shell:

```
wifi cred add -s <ssid> -k 1 -p <psk>
```

### Waveshare ESP32-P4-WIFI6-POE-ETH + APEM M-series (Ethernet/PoE)

| Signal | GPIO | Header pin | APEM wire |
|---|---|---|---|
| X wiper | 20 (ADC1 ch4) | 13 | white (X pot) |
| Y wiper | 21 (ADC1 ch5) | 11 | white (Y pot) |
| Z wiper | 22 (ADC1 ch6) | 12 | white (Z pot) |
| Pot supply | 3V3 | 1 | red |
| Pot return | GND | 6 | blue |
| Deadman | 23 | 7 | orange (other orange → GND) |

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

With `ros2 topic echo /cmd_vel` and `ros2 topic hz /cmd_vel` on the host:

- [ ] idle → nothing published
- [ ] hold + push forward → `linear.x > 0` at ~20 Hz
- [ ] release → exactly one all-zero message
- [ ] push left → `linear.y > 0`; twist counter-clockwise (P4) → `angular.z > 0`.
      Fix a wrong sign with `invert-output` on that axis in the overlay.
- [ ] stop `rmw_zenohd` while holding, restart it → nothing until release and re-press
