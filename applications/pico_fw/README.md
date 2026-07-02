# Raspberry Pi Pico W Application

### Building

Built from the shared workspace like every other app, using Zephyr's in-tree
AIROC driver for the onboard CYW43439 (firmware blobs from `hal_infineon`):

```shell
uv run poe app pico_fw        # rpi_pico/rp2040/w
uv run poe flash pico_fw
```

### WiFi credentials

Connection management is handled by Zephyr's conn_mgr with the
`wifi_connectivity` implementation from the `rosterloh-drivers` module, which
auto-connects at boot and reconnects on loss. Credentials are baked in at
build time — set `CONFIG_WIFI_CREDENTIALS_STATIC_SSID` and
`CONFIG_WIFI_CREDENTIALS_STATIC_PASSWORD` in `prj.conf` (or a local overlay
conf) before flashing.

### Debugging

[Debug Probe](https://www.raspberrypi.com/documentation/microcontrollers/debug-probe.html)
