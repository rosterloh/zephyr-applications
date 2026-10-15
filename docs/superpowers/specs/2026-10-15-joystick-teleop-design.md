# joystick_controller: Pico-ROS teleop — design

## Intent

Turn `applications/joystick_controller` into a physical teleop controller that
behaves like ROS 2 `teleop_twist_joy`, but runs on the MCU and publishes
`geometry_msgs/Twist` straight onto the ROS 2 graph via Pico-ROS (rmw_zenoh
wire format) — no laptop, `joy_node` or bridge in the loop.

The existing app (zbus button/led/state demo) may be discarded entirely.

## Behaviour (agreed)

- The button is a **deadman/enable**. While held, publish Twist at a fixed rate
  scaled from stick position.
- On release, publish **one zero Twist**, then go silent. Stick motion without
  the button publishes nothing (so other publishers, e.g. nav2 via twist_mux,
  are not blocked).
- Centre deadband so a resting stick does not creep.
- Axis mapping (holonomic, first pass):

  | Input | Twist field |
  |---|---|
  | stick Y (forward/back) | `linear.x` |
  | stick X (left/right) | `linear.y` |
  | knob twist (Z) | `angular.z` |

  The FeatherWing build has no Z axis: **linear only** (`angular.z` always 0).

  The target robot is holonomic and lives outside this repo. rasprover (diff
  drive, ignores `linear.y`, listens on `rasprover/cmd_vel`) is not the target;
  driving it from the FeatherWing would only go forward/back. That is accepted.

## Targets (agreed)

| | P4 (new) | S3 (keep supported) |
|---|---|---|
| Board | `esp32p4_wifi6_poe_eth/esp32p4/hpcore` (out-of-tree, in rosterloh-drivers) | `adafruit_qt_py_esp32s3/esp32s3/procpu` |
| Stick | APEM M-series, handle 9: 3-axis, 1 pushbutton, 5 kΩ pots | Adafruit Joy FeatherWing (seesaw @ 0x49), 2-axis + 5 buttons |
| Network | Wired Ethernet (PoE) | Wi-Fi |

Hardware notes:
- APEM pots are ratiometric potentiometers on 3.3 V → three ESP32-P4 ADC
  channels; the pushbutton (orange/orange pair) is a plain switch → one GPIO with
  pull-up. X/Y travel ±28°, Z ±45°, all spring-centred. Depending on pot option
  (P = 220° track, R = 50° track) the usable ADC span is a fraction of full
  scale, so **per-axis calibration is required**, not optional.
- P4 BOOT button (GPIO35) is shared with RMII TXD1 — must not be used as the
  deadman when Ethernet is up.
- The Joy FeatherWing is **not currently wired** in the S3 overlay (only a
  commented-out NeoKey block). It needs adding. Seesaw MFD/GPIO/ADC drivers
  already exist in rosterloh-drivers (`adafruit,seesaw-*`).

## How (decided)

- **Inputs via Zephyr's input subsystem, configured in devicetree per board:**
  - `analog-axis` node (`zephyr,analog-axis` driver) with one child per axis
    pointing at `io-channels` (P4 on-chip ADC; S3 seesaw ADC). It provides
    deadzone, min/max scaling, invert, and — with `CONFIG_SETTINGS` —
    persisted calibration (`INPUT_ANALOG_AXIS_SETTINGS`). Zephyr ships no
    calibration shell for it, so the app adds a small `teleop cal` command.
    Emits `INPUT_ABS_X/Y/RZ`.
  - `gpio-keys` for the deadman button (`polling-mode` on the S3 seesaw GPIO if
    seesaw interrupts are not wired).
  - Application code is then board-agnostic: one `INPUT_CALLBACK_DEFINE` that
    stores the latest axis values and button state, plus a periodic publisher.
- **ROS via `applications/common/picoros`** (shared module, as rasprover uses),
  opted in with the `include(...)`/`rsource` pattern from `CLAUDE.md`.
  rasprover's `app_picoros.c` / `app_network.c` are the reference for
  interface init, node/publisher declaration and network bring-up.
- Build wiring: add the P4 board to the `joystick_controller` case in
  `mise.toml`'s `app` task (S3 stays default); add it to `tests.yaml`
  `integration_platforms`; per-board `boards/<board>.conf` + `.overlay`.
- Keep MCUboot/sysbuild as today.

## Decisions left to the builder (my calls — change freely)

- Kconfig (mirroring rasprover's `APP_PICOROS_*` naming): locator (default
  `tcp/192.168.1.10:7447`), node name `joystick_controller`, domain ID 0,
  topic `cmd_vel`, publish rate 20 Hz, max linear speed 0.5 m/s, max angular
  1.0 rad/s.
- Wi-Fi credentials on S3 via `wifi_credentials` + shell, as rasprover does;
  P4 uses DHCP on Ethernet.
- FeatherWing deadman: **button A**. Other FeatherWing buttons unused.
- P4 ADC pins / button GPIO: pick free ADC-capable pins on the 40-pin header,
  document them in the overlay and README.
- If the router is unreachable at boot, retry connect in the background; never
  publish stale stick data after a reconnect (button must be re-pressed).
  This is new code: rasprover's picoros init is a single attempt with no
  reconnect.
- Controller power/link loss: the **robot-side command timeout** is the backstop
  (the controller cannot send anything once its link is gone), e.g. twist_mux
  timeouts or rasprover's 500 ms `APP_MOTORS_CMD_TIMEOUT_MS`. The publish rate
  must stay well inside such timeouts (20 Hz = 50 ms). No queued zero is
  replayed after reconnect.
- Status LED / NeoKey support: dropped unless trivially retained.

## Non-goals

- Turbo/speed-scale buttons, multiple enable buttons, `sensor_msgs/Joy`
  publishing, `TwistStamped`.
- On-device display or configuration UI beyond the Zephyr shell.
- Changing pico-ros / zenoh pins in `west.yml`.

## Anti-goals (failure even if it "works")

- Robot keeps moving after the button is released — release must always send
  zero while the link is up (link/power loss is covered robot-side, see above).
- Continuous zero publishing while idle (blocks other cmd_vel sources).
- Robot creeping with the stick at rest (missing calibration/deadband).

## Verification

- Both boards build: `mise run agent-build joystick_controller` and
  `... --board esp32p4_wifi6_poe_eth/esp32p4/hpcore`.
- Pure mapping logic (raw axis → scaled Twist, deadman state machine) gets a
  small ztest or assert-based check runnable on `native_sim`.
- On hardware: `ros2 topic echo /cmd_vel` on a host running `rmw_zenohd`
  shows Twist only while held and a single zero on release.
