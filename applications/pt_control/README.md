# pt_control

Pan/tilt gimbal controller for the Waveshare `ros_driver` (ESP32) board. It
exposes the two bus-servo joints to a remote host running `ros2_control` by
bridging `sensor_msgs/JointState` over zenoh-pico / WiFi:

- **publishes** joint feedback (position + velocity) as `sensor_msgs/JointState`
  on `rt/pt_control/joint_states` → ROS 2 `/pt_control/joint_states`
- **subscribes** to position commands as `sensor_msgs/JointState` on
  `rt/pt_control/joint_commands` → ROS 2 `/pt_control/joint_commands`

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

Run a zenoh router and the ROS 2 <-> zenoh bridge, then a controller_manager
with a topic-based hardware interface bound to the two topics above:

```bash
zenohd -l tcp/0.0.0.0:7447
ros2 run zenoh_bridge_ros2dds zenoh_bridge_ros2dds -e tcp/localhost:7447
```

Configure a `topic_based_ros2_control/TopicBasedSystem` hardware component with
`joint_states_topic: /pt_control/joint_states` and
`joint_commands_topic: /pt_control/joint_commands`, and joints `pan_joint` /
`tilt_joint`. A `JointStateBroadcaster` plus a
`JointGroupPositionController` (or `JointTrajectoryController`) then drive the
gimbal from the host.
