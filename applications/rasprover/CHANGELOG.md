# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Differential-drive motor control for the two onboard h-bridges via the
  actuator subsystem, with quadrature PCNT encoder feedback.
- Zenoh `cmd_vel` subscriber (CDR `geometry_msgs/Twist`) with a
  stop-on-silence watchdog.
- Zenoh `/joint_states` publisher (CDR `sensor_msgs/JointState`) from wheel
  actuator feedback.
- SNTP time synchronization for ROS header timestamps, with zero timestamps
  used until the first successful sync.

## [v1.0.0] - 2024-xx-xx

### Changed

-
