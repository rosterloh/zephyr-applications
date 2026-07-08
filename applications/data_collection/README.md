# data_collection

Networked data-collection application for the Waveshare **ESP32-P4-Nano**
(`esp32p4_nano/esp32p4/hpcore`). Brings up wired Ethernet with DHCPv4 and
exposes an MCUmgr/SMP management surface over UDP, plus a UART shell.

## Features

- **Ethernet** — on-chip RMII EMAC (`CONFIG_ETH_ESP32`) with DHCPv4 addressing.
  The assigned IPv4 address is logged once the lease is acquired.
- **SMP over UDP** — MCUmgr OS group (remote reboot, echo, taskstat) reachable
  with `mcumgr --conntype udp`.
- **Shell** — interactive console on `uart0`.

## Build & flash

```bash
uv run poe app data_collection            # board defaults to esp32p4_nano/esp32p4/hpcore
uv run poe flash data_collection
```

The console is on `uart0` (GPIO pins). The board's USB-C is the
USB-Serial-JTAG interface, which the ROM uses for flashing but which does not
currently carry the Zephyr console — attach a USB-UART adapter to `uart0` to
see shell/log output.

## OTA / MCUboot — not enabled (hardware limitation)

This app is built **without MCUboot** (ESP "simple boot"), so it has no
image-upgrade path. That is a deliberate, hardware-driven choice, not an
oversight:

- The physical board is an **ESP32-P4 rev v1.3 engineering sample**
  (ROM `esp32p4-eco2-20240710`). Pre-v3.0 ESP32-P4 silicon is "preliminary".
- The MCUboot second-stage bootloader image built by sysbuild is loaded
  **corrupt** by the ROM on this silicon (SHA-256 mismatch) and panics with an
  illegal instruction before reaching the application. This reproduces on
  Espressif's own in-tree `esp32p4_function_ev_board`, so it is a
  silicon/toolchain-tree issue, not a fault in this application. See the
  upstream reports: esphome/esphome#15336 and esp-rs/esp-idf-sys#376.

The SMP-over-UDP transport and OS group are kept precisely so that an OTA build
only needs the MCUboot/image pieces added back. **To enable OTA-over-SMP** once
you have production **rev v3.x** ESP32-P4 hardware:

1. Restore `sysbuild.conf` / `sysbuild/mcuboot.conf`.
2. Add back to `prj.conf`: `CONFIG_BOOTLOADER_MCUBOOT`, `CONFIG_FLASH`,
   `CONFIG_FLASH_MAP`, `CONFIG_STREAM_FLASH`, `CONFIG_IMG_MANAGER`,
   `CONFIG_IMG_ERASE_PROGRESSIVELY`, `CONFIG_MCUBOOT_IMG_MANAGER`,
   `CONFIG_MCUMGR_GRP_IMG`.
3. Build with `uv run poe app data_collection --sysbuild` and upload images
   with `mcumgr --conntype udp --connstring <board-ip>:1337 image upload ...`.
