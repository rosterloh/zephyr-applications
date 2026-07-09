# pt_control

Pan/tilt gimbal controller for the Waveshare `ros_driver` (ESP32) board. It
exposes the two bus-servo joints to a remote host running `ros2_control` by
bridging `sensor_msgs/JointState` over zenoh-pico / WiFi:

- **publishes** joint feedback (position + velocity) as `sensor_msgs/JointState`
  on `rt/robot_joint_states` → ROS 2 `/robot_joint_states`
- **subscribes** to position commands as `sensor_msgs/JointState` on
  `rt/robot_joint_commands` → ROS 2 `/robot_joint_commands`

These key defaults match the
[`topic_based_ros2_control`](https://github.com/PickNikRobotics/topic_based_ros2_control)
`TopicBasedSystem` topic defaults, so the host pairs with no param overrides.
A ready-to-run host example (URDF, controllers, bridge config) lives in
[`ros2_control/`](ros2_control/).

Joint names are `pan_joint` and `tilt_joint`; positions are in radians.
Pan+tilt setpoints are issued as a single atomic bus sync-write.

The onboard SSD1306 128x32 OLED shows three rows — `pt_control`, the WiFi
state (`connecting` / `connected` / `WiFi down`), and the full IP address on
its own line once DHCP completes. The IP is also logged to the serial console.

The display uses the Character Frame Buffer (`CONFIG_CHARACTER_FRAMEBUFFER`),
not LVGL: LVGL's monochrome init/render path hangs this panel at boot (an
unresolved issue in the Zephyr LVGL glue, also hit by the rasprover firmware).
The text uses an 8x8 font (`src/cfb_font_unscii8.c`, generated from the
public-domain unscii-8 font) so a 15-char IPv4 address fits one line and three
rows fit the 32px height; the stock CFB fonts (10x16+) are too large. To
disable the OLED entirely, set `CONFIG_APP_DISPLAY=n` — the IP is still on the
console.

## Build & flash

```bash
uv run poe app pt_control --sysbuild   # board: ros_driver/esp32/procpu (+ MCUboot for OTA)
uv run poe flash pt_control
```

`--sysbuild` builds MCUboot plus an OTA-capable app image. A plain
`uv run poe app pt_control` still compiles but is not bootable via MCUboot.

## OTA update (MCUmgr over UDP, no Bluetooth)

Once the board has an IP (shown on the OLED), push a new signed image over WiFi:

```bash
mcumgr --conntype udp --connstring <board-ip>:1337 image upload \
    builds/pt_control/pt_control/zephyr/zephyr.signed.bin
mcumgr --conntype udp --connstring <board-ip>:1337 image list   # note the new hash
mcumgr --conntype udp --connstring <board-ip>:1337 image test <hash>
mcumgr --conntype udp --connstring <board-ip>:1337 reset
```

## Provision WiFi

Credentials are stored in the settings backend (persist across reboots). On the
serial shell:

```
wifi cred add -s <ssid> -k 1 -p <psk>
kernel reboot cold
```

Point the firmware at your zenoh router by setting `CONFIG_APP_ZENOH_LOCATOR`
(default `tcp/192.168.1.10:7447`) in `prj.conf` before building.

## Remote host (ros2_control)

The host runs `topic_based_ros2_control/TopicBasedSystem` behind a
`zenoh-bridge-ros2dds` that the firmware's zenoh-pico client connects to. A
complete, copy-paste example is in [`ros2_control/`](ros2_control/):

- `pt_control.urdf.xacro` — pan/tilt description + the `TopicBasedSystem`
  hardware block (position command, position+velocity state).
- `controllers.yaml` — `joint_state_broadcaster` + a `pan_tilt_controller`
  (`JointTrajectoryController`).
- `zenoh-bridge-ros2dds.json5` — bridge listening on `tcp/0.0.0.0:7447` for the
  firmware to connect to.

See `ros2_control/README.md` for the run steps. Because the firmware key
defaults map to `/robot_joint_states` and `/robot_joint_commands`, the
`TopicBasedSystem` defaults line up with no topic params.
