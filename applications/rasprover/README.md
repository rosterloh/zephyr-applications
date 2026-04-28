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

## What it does

On startup the firmware initialises the INA219 current sensor and reads voltage, current, and power every `LOOP_DELAY_S` seconds (default 60), logging the values over serial.

The SSD1306 display and WiFi stack are present in the build but not yet active — `app_display_init()` and `app_net_connect()` are commented out in `main.c` pending integration work.

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
