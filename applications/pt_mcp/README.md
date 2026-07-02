# pt_mcp

MCP server exposing a pan/tilt motor control tool, running on a Seeed XIAO
ESP32C5. Based on Zephyr's `mcp_server_hello_world` sample. Built with
sysbuild for MCUboot, with OTA firmware updates over MCUmgr SMP/UDP.

## Hardware

- Seeed XIAO ESP32C5 (`xiao_esp32c5/esp32c5/hpcore`)
- Waveshare ST3215-style bus servos (pan ID 2, tilt ID 1) on the XIAO UART
  pins (TX GPIO11 / RX GPIO12) at 1 Mbaud

## Build and flash

```bash
uv run poe app pt_mcp --sysbuild
uv run poe flash pt_mcp
```

## WiFi provisioning

Credentials are stored in settings (ZMS) and auto-connect at boot via the
`wifi_connectivity` module. Provision once over the USB shell:

```
wifi cred add -s <ssid> -k 1 -p <psk>
wifi cred auto_connect
```

## MCP

The server listens on `http://pt-mcp.local:8080/mcp` (mDNS) once WiFi is up.
It provides one tool:

- `motor_control` — `{"action":"get"}` returns the pan/tilt positions in
  radians; `{"action":"set","pan":<rad>,"tilt":<rad>}` commands setpoints
  (either axis optional).

## OTA over SMP

MCUmgr SMP listens on UDP port 1337. With a sysbuild image:

```bash
mcumgr --conntype udp --connstring [<ip>]:1337 image upload builds/pt_mcp/pt_mcp/zephyr/zephyr.signed.bin
mcumgr --conntype udp --connstring [<ip>]:1337 image list
mcumgr --conntype udp --connstring [<ip>]:1337 image test <hash>
mcumgr --conntype udp --connstring [<ip>]:1337 reset
# after verifying the new image boots:
mcumgr --conntype udp --connstring [<ip>]:1337 image confirm
```
