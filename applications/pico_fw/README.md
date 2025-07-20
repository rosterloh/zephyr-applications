# Raspberry Pi Pico W Application

### Initialisation

The first step is to initialise the workspace folder (``pico-workspace``) where
the ``pico_fw`` and all Zephyr modules will be cloned. Run the following
command:

```shell
west init -m https://github.com/rosterloh/pico_fw --mr main pico-workspace
cd pico-workspace
west update
```

### Building

All commands are implemented as VSCode tasks. press ctrl+shift+b for the build tasks menu

### Debugging

[Debug Probe](https://www.raspberrypi.com/documentation/microcontrollers/debug-probe.html)
