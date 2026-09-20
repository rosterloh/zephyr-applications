# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed

- **Breaking (host side):** the firmware is now a Pico-ROS node speaking
  `rmw_zenoh` conventions instead of a bare zenoh-pico publisher feeding
  `zenoh-bridge-ros2dds`. Topics keep their ROS names, but the zenoh keys,
  the discovery mechanism and the host stack all change: run ROS 2 with
  `RMW_IMPLEMENTATION=rmw_zenoh_cpp` and `rmw_zenohd` in place of `zenohd`
  plus the DDS bridge. A host still running the old bridge sees nothing.
- `CONFIG_APP_ZENOH*` is replaced by `CONFIG_APP_PICOROS*` (application
  topics and node identity) and `CONFIG_PICOROS_*`
  (`applications/common/picoros`, zenoh-pico transport tunables). The zenoh
  transport lease now defaults to 60 s with a x2 expire factor to match
  `rmw_zenohd`.
- Message encoding moved from the hand-written `app_ros_cdr.c` to Pico-ROS's
  `picoserdes` over Micro-CDR, driven by `src/rasprover_types.h`. The wire
  bytes are unchanged -- `tests/picoserdes` asserts the same golden arrays
  the old encoder was tested against.

### Added

- Differential-drive motor control for the two onboard h-bridges via the
  actuator subsystem, with quadrature PCNT encoder feedback.
- Zenoh `cmd_vel` subscriber (CDR `geometry_msgs/Twist`) with a
  stop-on-silence watchdog.
- Zenoh `/joint_states` publisher (CDR `sensor_msgs/JointState`) from wheel
  actuator feedback.
- Waveshare gimbal support with bus-servo pan/tilt actuator feedback and
  `/rasprover/gimbal_cmd` JointState position commands over zenoh.
- SNTP time synchronization for ROS header timestamps, with zero timestamps
  used until the first successful sync.

## [v1.0.0] - 2024-xx-xx

### Changed

-
