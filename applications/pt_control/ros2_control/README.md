# pt_control host example (topic_based_ros2_control)

Drives the `pt_control` firmware from a host `ros2_control` stack. The firmware
speaks `sensor_msgs/JointState` over zenoh-pico; a `zenoh-bridge-ros2dds`
turns those into ROS 2 topics, and
[`topic_based_ros2_control`](https://github.com/PickNikRobotics/topic_based_ros2_control)
`TopicBasedSystem` exposes them as a `ros2_control` hardware component.

```
pt_control (ESP32, zenoh-pico client)
   │  rt/robot_joint_states     (firmware -> host, position + velocity)
   │  rt/robot_joint_commands   (host -> firmware, position)
   ▼
zenoh-bridge-ros2dds  ──►  /robot_joint_states  /robot_joint_commands  (ROS 2)
   ▼
TopicBasedSystem (ros2_control hardware)  ──►  pan_tilt_controller + joint_state_broadcaster
```

Joints: `pan_joint`, `tilt_joint`; command interface `position`; state
interfaces `position`, `velocity`. Firmware key defaults already map to
`/robot_joint_states` and `/robot_joint_commands`, which are also the
`TopicBasedSystem` defaults — no topic params required.

## Files

| File | Purpose |
|---|---|
| `pt_control.urdf.xacro` | pan/tilt description + `TopicBasedSystem` hardware block |
| `controllers.yaml` | `joint_state_broadcaster` + `pan_tilt_controller` (JTC) |
| `zenoh-bridge-ros2dds.json5` | bridge listening on `tcp/0.0.0.0:7447` |

## Prerequisites

```bash
sudo apt install ros-$ROS_DISTRO-topic-based-ros2-control \
                 ros-$ROS_DISTRO-joint-trajectory-controller \
                 ros-$ROS_DISTRO-joint-state-broadcaster
# zenoh-bridge-ros2dds: install from the eclipse-zenoh/zenoh-plugin-ros2dds
# releases (standalone binary) or the matching ROS package.
```

## Run

1. **Point the firmware at this host** — set `CONFIG_APP_ZENOH_LOCATOR` to
   `tcp/<this-host-ip>:7447` in `../prj.conf`, rebuild and flash. Confirm the
   OLED/console shows an IP.

2. **Start the bridge** (the firmware client connects to it):
   ```bash
   zenoh-bridge-ros2dds -c zenoh-bridge-ros2dds.json5
   ```

3. **Bring up ros2_control** (commands below are for a recent distro; exact
   invocation varies slightly by ROS 2 version):
   ```bash
   xacro pt_control.urdf.xacro > /tmp/pt_control.urdf
   ros2 run robot_state_publisher robot_state_publisher /tmp/pt_control.urdf &
   ros2 run controller_manager ros2_control_node --ros-args --params-file controllers.yaml &
   ros2 run controller_manager spawner joint_state_broadcaster
   ros2 run controller_manager spawner pan_tilt_controller
   ```

## Verify / drive

```bash
ros2 control list_hardware_interfaces          # pan/tilt command+state, "available"
ros2 topic echo /robot_joint_states            # live feedback from the firmware
ros2 topic echo /joint_states                  # broadcaster output

# move the gimbal (pan 0.3 rad, tilt -0.2 rad over 1 s)
ros2 topic pub -1 /pan_tilt_controller/joint_trajectory trajectory_msgs/msg/JointTrajectory \
  "{ joint_names: [pan_joint, tilt_joint],
     points: [{ positions: [0.3, -0.2], time_from_start: { sec: 1 } }] }"
```

## Notes

- **Rate:** the UART servo bus, not WiFi, is the ceiling. `update_rate` is 50 Hz
  to match the firmware; higher just floods the bus/bridge.
- **Weak WiFi** adds latency/jitter — fine for streaming position setpoints, less
  so for tight closed-loop control.
- **Command interface is position only.** The firmware ignores velocity/effort
  commands; declare a position command interface (as the URDF does).
