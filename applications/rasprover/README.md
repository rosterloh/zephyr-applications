# rasprover

Firmware for the [Waveshare RaspRover](https://www.waveshare.com/wiki/RaspRover) robot platform, running on the Waveshare ROS Driver board (ESP32).

## Hardware

| Component | Part | Interface |
|-----------|------|-----------|
| MCU | ESP32 (Xtensa LX6, 240 MHz) | — |
| Current/power monitor | INA219 | I2C @ 0x42 |
| OLED display | SSD1306 128×32 | I2C @ 0x3C |

Board: `ros_driver/esp32/procpu` (defined in [rosterloh-drivers](https://github.com/rosterloh/zephyr-drivers))

## Building

From the workspace root:

```shell
uv run poe build-rasprover
```

Or manually:

```shell
uv run west build -b ros_driver/esp32/procpu -p always \
  --build-dir builds/rasprover applications/rasprover
```

## Flashing

```shell
uv run poe flash applications/rasprover
```

## Debugging

JTAG debugging via OpenOCD 0.12.0+. See [Espressif JTAG docs](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/jtag-debugging/index.html) for wiring.

```shell
west debug --build-dir builds/rasprover
```

Use `debug.conf` to enable debug optimisations:

```shell
uv run west build -b ros_driver/esp32/procpu -p always \
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

Settings are persisted via Zephyr's settings subsystem (ZMS backend). Loop delay and WiFi credentials can be written at runtime using the shell settings commands.

## Planned

- Re-enable WiFi and display once integration is stable
- Golioth cloud connectivity (telemetry, OTA, remote settings) — deferred until the `pouch` module's `board.yml` is fixed upstream
