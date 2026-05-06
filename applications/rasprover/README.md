# rasprover

Firmware for the [Waveshare RaspRover](https://www.waveshare.com/wiki/RaspRover) robot platform, running on the Waveshare ROS Driver board (ESP32).

## Hardware

| Component | Part | Interface |
|-----------|------|-----------|
| MCU | ESP32 (Xtensa LX6, 240 MHz) | — |
| Current/power monitor | INA219 | I2C @ 0x42 |
| OLED display | SSD1306 128×32 | I2C @ 0x3C |

Board: `ros_driver/esp32/procpu` (defined in [rosterloh-drivers](https://github.com/rosterloh/zephyr-drivers))

## Flash layout

MCUboot is the bootloader. The partition table is defined in the board DTS:

| Partition | Offset | Size | Purpose |
|-----------|--------|------|---------|
| `mcuboot` | 0x1000 | 60 KB | Bootloader |
| `slot0` | 0x10000 | 1 MB | Primary application image |
| `slot1` | 0x110000 | 1 MB | OTA update image |
| `storage` | 0x250000 | 24 KB | Zephyr settings (ZMS) |

MCUboot is configured in **swap-using-move** mode (the ESP32 default): on reboot after an OTA upload, MCUboot moves the new image from slot1 into slot0 before booting it.

## Building

Sysbuild is required — it builds MCUboot and the application together:

```shell
uv run poe build-rasprover
```

Or manually:

```shell
uv run west build -b ros_driver/esp32/procpu -p always --sysbuild \
  --build-dir builds/rasprover applications/rasprover
```

This produces two binaries:
- `builds/rasprover/mcuboot/zephyr/zephyr.bin` — bootloader (flash once)
- `builds/rasprover/rasprover/zephyr/zephyr.signed.bin` — application (OTA target)

## Flashing

Initial flash (bootloader + application):

```shell
uv run west flash --build-dir builds/rasprover
```

## OTA updates

OTA is handled via [MCUmgr](https://docs.zephyrproject.org/latest/services/device_mgmt/mcumgr.html) using the SMP protocol.
Two transports are enabled:

| Transport | How to use |
|-----------|------------|
| **Bluetooth LE** | Connect with nRF Connect for Mobile or `mcumgr` CLI |
| **Shell UART** | Use `mcumgr` CLI over the serial console |

### mcumgr CLI (UART example)

```shell
# Install mcumgr
go install github.com/apache/mynewt-mcumgr-cli/mcumgr@latest

# Upload new image over serial
mcumgr --conntype serial --connstring "dev=/dev/ttyUSB0,baud=115200" \
  image upload builds/rasprover/rasprover/zephyr/zephyr.signed.bin

# Confirm upload and trigger swap on next boot
mcumgr --conntype serial --connstring "dev=/dev/ttyUSB0,baud=115200" \
  image list
mcumgr --conntype serial --connstring "dev=/dev/ttyUSB0,baud=115200" \
  image test <hash-from-list>
mcumgr --conntype serial --connstring "dev=/dev/ttyUSB0,baud=115200" \
  reset
```

### mcumgr CLI (Bluetooth example)

```shell
mcumgr --conntype ble --connstring "peer_name=rasprover" \
  image upload builds/rasprover/rasprover/zephyr/zephyr.signed.bin
```

> **Image signing**: `sysbuild/mcuboot.conf` currently uses `BOOT_SIGNATURE_TYPE_NONE` (unsigned)
> for development. Before production use, generate an ECDSA-P256 key pair and switch to signed images.

## Debugging

JTAG debugging via OpenOCD 0.12.0+. See [Espressif JTAG docs](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/jtag-debugging/index.html) for wiring.

```shell
west debug --build-dir builds/rasprover
```

Use `debug.conf` for debug-optimised builds:

```shell
uv run west build -b ros_driver/esp32/procpu -p always --sysbuild \
  --build-dir builds/rasprover applications/rasprover \
  -- -DEXTRA_CONF_FILE=debug.conf
```

## zenoh-pico sensor publishing

The firmware publishes INA219 readings to a zenoh router on the host over **WiFi/TCP**. Messages are CDR-encoded `sensor_msgs/msg/BatteryState`, making them directly consumable by the [zenoh-ros2dds bridge](https://github.com/eclipse-zenoh/zenoh-plugin-ros2dds) with no conversion step.

The zenoh key follows the `rt/<topic>` convention the bridge uses for ROS2 topics. With the default prefix the key is `rt/rasprover/battery_state`, which appears in ROS2 as `/rasprover/battery_state`.

> Why TCP rather than serial? The upstream `zenoh-pico` west-module Zephyr integration unconditionally accesses `sock->_fd`, a struct field that only exists when at least one socket-based transport is enabled. A serial-only build doesn't compile. See `poe patch-zenoh` for the workspace patches that paper over this.

### WiFi credentials

SSID and PSK are stored at runtime via the Zephyr settings subsystem. Set them once over the shell:

```
settings set wifi/ssid \"my-network\"
settings set wifi/psk \"my-password\"
settings save
reboot
```

### Host setup

```shell
# 1. Run a zenoh router listening on TCP. The address must match
#    CONFIG_APP_ZENOH_LOCATOR (default tcp/192.168.1.10:7447).
zenohd -l tcp/0.0.0.0:7447

# 2. Launch the zenoh-ros2dds bridge on the host ROS2 machine
ros2 launch zenoh_bridge_ros2dds zenoh_bridge_ros2dds.launch.py

# 3. The topic is now visible in ROS2
ros2 topic echo /rasprover/battery_state sensor_msgs/msg/BatteryState
```

Fields populated in each message:

| Field | Source |
|-------|--------|
| `voltage` | INA219 bus voltage (V) |
| `current` | INA219 current (A) |
| `power_supply_status` | `DISCHARGING` (2) |
| `power_supply_health` | `GOOD` (2) |
| `present` | `true` |
| all others | `NaN` / 0 / empty |

### Configuration

| Kconfig | Default | Description |
|---------|---------|-------------|
| `APP_ZENOH` | y | Enable the zenoh publisher |
| `APP_ZENOH_LOCATOR` | `tcp/192.168.1.10:7447` | Locator passed to `z_open()`. Format `<protocol>/<host>:<port>`. |
| `APP_ZENOH_KEY_PREFIX` | `rt/rasprover` | Key prefix — `rt/` routes topics through the zenoh-ros2dds bridge |

#### Advanced tunables

These feed the auto-generated `zenoh-pico/config.h` (see `poe patch-zenoh`). Defaults match upstream zenoh-pico; only change them if you have a specific reason.

| Kconfig | Default | Maps to |
|---------|---------|---------|
| `APP_ZENOH_FRAG_MAX_SIZE` | `4096` | `Z_FRAG_MAX_SIZE` — max fragmented message size (bytes) |
| `APP_ZENOH_BATCH_UNICAST_SIZE` | `2048` | `Z_BATCH_UNICAST_SIZE` — max unicast batch size (bytes) |
| `APP_ZENOH_BATCH_MULTICAST_SIZE` | `2048` | `Z_BATCH_MULTICAST_SIZE` — max multicast batch size (bytes) |
| `APP_ZENOH_SOCKET_TIMEOUT_MS` | `100` | `Z_CONFIG_SOCKET_TIMEOUT` — default socket timeout (ms) |
| `APP_ZENOH_TRANSPORT_LEASE_MS` | `10000` | `Z_TRANSPORT_LEASE` — link lease announced to peers (ms) |
| `APP_ZENOH_TRANSPORT_LEASE_EXPIRE_FACTOR` | `3` | `Z_TRANSPORT_LEASE_EXPIRE_FACTOR` |
| `APP_ZENOH_RUNTIME_MAX_TASKS` | `64` | `Z_RUNTIME_MAX_TASKS` — max tasks in the zenoh-pico runtime |
| `APP_ZENOH_TRANSPORT_ACCEPT_TIMEOUT_MS` | `1000` | `Z_TRANSPORT_ACCEPT_TIMEOUT` — P2P link accept timeout (ms) |
| `APP_ZENOH_TRANSPORT_CONNECT_TIMEOUT_MS` | `10000` | `Z_TRANSPORT_CONNECT_TIMEOUT` — P2P link connect timeout (ms) |

## What it does

On startup the firmware initialises the INA219 current sensor, brings up WiFi (using credentials from settings), opens a zenoh client session to the configured router, and reads voltage, current, and power every `LOOP_DELAY_S` seconds (default 60). Each reading is logged over the console and published to the zenoh router over TCP.

## Configuration

| Kconfig | Default | Description |
|---------|---------|-------------|
| `APP_DISPLAY` | y | Enable LVGL display subsystem |
| `APP_DISPLAY_WORK_QUEUE_DEDICATED` | n | Use a dedicated work queue for UI updates |

Settings are persisted via the Zephyr settings subsystem (ZMS backend). Loop delay and WiFi credentials can be written at runtime using the shell `settings` commands.

## Planned

- Re-enable WiFi and display once integration is stable
- Golioth cloud connectivity (telemetry, remote settings) — deferred until the `pouch`
  module's `board.yml` is fixed upstream; Golioth OTA would then replace MCUmgr
