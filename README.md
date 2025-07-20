# ESP32S3 Joystick Controller Application

<a href="https://github.com/rosterloh/joystick_controller/actions/workflows/build.yml?query=branch%3Amain">
  <img src="https://github.com/rosterloh/joystick_controller/actions/workflows/build.yml/badge.svg?event=push">
</a>

[Adafruit QT Py ESP32-S3](https://learn.adafruit.com/adafruit-qt-py-esp32-s3/overview)

## Getting Started

Before getting started, make sure you have a proper Zephyr development
environment. Follow the official
[Zephyr Getting Started Guide](https://docs.zephyrproject.org/latest/getting_started/index.html).

Download the SDK with west
```shell
west sdk install --version 0.17.1 --install-dir ~/zephyr-sdk -t xtensa-espressif_esp32s3_zephyr-elf
west packages pip --install
```

### Initialisation

The first step is to initialise the workspace folder (``zephyr_ws``) where
the ``joystick_controller`` and all Zephyr modules will be cloned. Run the following
command:

```shell
mkdir -p ~/zephyr_ws/applications
git clone https://github.com/rosterloh/joystick_controller ~/zephyr_ws/applications
west init -l ~/zephyr_ws/applications/joystick_controller
cd ~/zephyr_ws
west update
west blobs fetch hal_espressif
```

### Building

All commands are implemented as VSCode tasks. press ctrl+shift+b for the build tasks menu

### Debugging

ESP32-S3 support on OpenOCD is available upstream as of version 0.12.0. Download and install OpenOCD from [OpenOCD](https://github.com/openocd-org/openocd)

ESP32-S3 has a built-in JTAG circuitry and can be debugged without any additional chip. Only an USB cable connected to the D+/D- pins is necessary.

Further documentation can be obtained from the SoC vendor in [JTAG debugging for ESP32-S3](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/jtag-debugging/)

## Testing

### Testing the application

```shell
west twister -T app -v --inline-logs --integration
```

### Testing the drivers

Run all tests with

```shell
west twister -T tests -v --inline-logs --integration
```

or a specific test with

```shell
west twister -p native_sim -s drivers/seesaw/drivers.sensor.seesaw
```
