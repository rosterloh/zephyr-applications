# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.3.0] - 2026-11-19

### Added

- Terminal in the web page: the device shell over a WebSocket at `/shell`,
  behind the same admin login as the API. ESP32 boards only; costs about 8 KB
  of DRAM (the AI-Thinker goes from 92% to 96% DRAM).

### Changed

- Redesigned web page: status header, snapshot with stat tiles, settings in
  tabs, status refreshed every 10 s, dark and light themes.

## [0.2.0] - 2026-11-19

Ports the applicable changes from Prusa-Firmware-ESP32-Cam v1.1.3.

### Added

- HD (1280x720) and FHD (1920x1080) resolutions. The web page, the shell and
  `POST /api/config` offer only the sizes the sensor reports as JPEG, and
  `GET /api/config` lists them in a new `resolutions` array. The OV3660
  (DFR1154) gets both; the OV2640 (AI-Thinker) stays at QVGA..UXGA.
- `prusa factory_reset` shell command: erases all settings (config, WiFi
  networks, Home Assistant key, hawkBit settings) and reboots.

### Changed

- WiFi retries back off from 10 s to 5 minutes while the network is missing
  or rejects the password, instead of retrying every 10 s.

### Fixed

- A frame that is not a whole JPEG (SOI..EOI) is dropped and the next one is
  taken, up to 3 tries, instead of being uploaded or streamed.

## [0.1.0] - 2026-11-18

First release: Prusa Connect camera for the AI-Thinker ESP32-CAM and the
DFRobot ESP32-S3 AI Camera (DFR1154).

### Added

- Periodic JPEG upload to the Prusa Connect camera service, with camera info,
  backoff on network and server errors, and a `prusa` shell.
- Config page and JSON API behind basic auth: name, token, interval,
  resolution, quality, flips, rotation (also written to Exif), flash, sensor
  tuning, static IPv4.
- mDNS (`prusa-zcam.local`), MJPEG stream on port 81, `/snapshot.jpg`.
- hawkBit OTA through MCUboot, with the image confirmed only after a camera
  frame and an IPv4 address.
- Home Assistant via the ESPHome native API: camera entity, status and
  controls.
- Core dumps to flash and a build ID in the image.
- native_sim integration tests and on-board HIL tests (`mise run hil prusa_cam`).

