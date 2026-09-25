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
- **WiFi is actually enabled** (`CONFIG_WIFI=y` and `&wifi` in the board
  overlay); before this the firmware had no WiFi interface on hardware.
  To fit it, the Bluetooth SMP transport and the OLED display are dropped on
  hardware; OTA is over the shell UART. See "Memory budget" in the README for
  the heap, stack and pool sizes this needs.
- `CONFIG_LOG` is enabled; the application's log lines were previously
  compiled out.

### Fixed

- Boot waited for any L4 connectivity, which an IPv6 address satisfies before
  DHCP completes, so the one-shot Pico-ROS connect ran without IPv4. It now
  waits for IPv4.
- The gimbal never initialised on hardware: the servo bus's UART was never
  configured (`bus_servo_init()`), so every gimbal call failed and
  `/joint_states` was never published.

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
