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
uv run poe app data_collection --sysbuild   # MCUboot + app; board defaults to esp32p4_nano/esp32p4/hpcore
uv run poe flash data_collection
```

Build **with `--sysbuild`**: that is what produces the MCUboot bootloader and a
signed, upgradeable application image. A plain `uv run poe app data_collection`
still compiles and boots via ESP simple boot, but has no upgrade path.

`west flash` writes both sysbuild domains in `domains.yaml` `flash_order` —
MCUboot to `0x2000`, then the app to `0x20000`. No `--domain` argument is
needed; if you pipe the output through `tail` you will only see the second
write and wrongly conclude MCUboot was skipped.

Console and shell come out of the board's USB-C port (the USB-Serial-JTAG
interface used for flashing) at 115200 — the same port carries the ROM log,
the MCUboot log and the Zephyr shell.

## OTA / MCUboot

MCUboot **works on this board**, including the rev v1.3 engineering sample
(ROM `esp32p4-eco2-20240710`). It did not until mid-2026: the second-stage
bootloader image was loaded corrupt by the ROM and panicked with an illegal
instruction before reaching the application. That was an upstream software bug,
not a silicon limitation, and Espressif fixed it in Zephyr
`adc3d53fd33` ("soc: esp32p4: Fix MCUboot RAM layout on rev 1.3") — pre-v3 P4
puts the bootloader in low SRAM rather than at the top of the app region.
Espressif's own in-tree `waveshare_esp32p4_eth` board is also rev v1.3 and now
defaults to MCUboot.

Two prerequisites, both already in place:

- The board must clock its CPUs at a frequency the silicon has. Rev v1.3 does
  90/180/360 MHz, not the SoC dtsi default of 400 MHz; the `esp32p4_nano` board
  definition sets 360 MHz. `soc/espressif/esp32p4/soc.c` `BUILD_ASSERT`s this.
- MCUboot's sector bookkeeping is sized from the slot size, so a large slot can
  overflow its `dram_seg`. The 16M partition table this board inherits gives
  1984 sectors/slot ≈ 31 kB of `.bss`, which fits. A board on the 32M table
  needs a smaller-slot partition override.

Verified on hardware: ROM → MCUboot (`Loading image 0 - slot 0`) → Zephyr, with
PSRAM initialised and the Ethernet PHY detected.

**Upgrading over the network** (untested end to end — needs a DHCP-served
Ethernet link):

```bash
uv run poe app data_collection --sysbuild
mcumgr --conntype udp --connstring <board-ip>:1337 image list
mcumgr --conntype udp --connstring <board-ip>:1337 image upload \
    builds/data_collection/data_collection/zephyr/zephyr.signed.bin
mcumgr --conntype udp --connstring <board-ip>:1337 image test <hash>
mcumgr --conntype udp --connstring <board-ip>:1337 reset
```

Images are unsigned for development (`BOOT_SIGNATURE_TYPE_NONE`, the board's
`Kconfig.sysbuild` default). Generate a key and switch to
`CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256` before shipping.

One flashing caveat inherited from the swap-using-move layout: the image
trailer lives at the **end** of slot0, and `west flash` only erases the region
it writes. A board still carrying an older, larger image can leave stale
trailer bytes behind that confuse MCUboot. `uv run esptool --port <port>
erase-flash` clears it.
