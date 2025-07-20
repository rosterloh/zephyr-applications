# ESP32 RaspRover Application

[WaveShare Wiki](https://www.waveshare.com/wiki/RaspRover)

### Initialisation

The first step is to initialise the workspace folder (``esp_ws``) where
the ``rasprover`` and all Zephyr modules will be cloned. Run the following
command:

```shell
west init -m https://github.com/rosterloh/rasprover --mr main esp_ws
cd esp_ws
west update
west blobs fetch hal_espressif
```

### Building

All commands are implemented as VSCode tasks. press ctrl+shift+b for the build tasks menu

### Debugging

ESP32 support on OpenOCD is available upstream as of version 0.12.0. Download and install OpenOCD from [OpenOCD](https://github.com/openocd-org/openocd)

Further documentation can be obtained from the SoC vendor in [JTAG debugging for ESP32](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/jtag-debugging/index.html)
