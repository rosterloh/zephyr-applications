# data_collection

Networked data-collection application for the Waveshare **ESP32-P4-Nano**
(`esp32p4_nano/esp32p4/hpcore`). Brings up wired Ethernet with DHCPv4 and
exposes an MCUmgr/SMP management surface over UDP, plus a UART shell.

## Features

- **Ethernet** — on-chip RMII EMAC (`CONFIG_ETH_ESP32`) with DHCPv4 addressing.
  The assigned IPv4 address is logged once the lease is acquired.
- **SMP over UDP** — MCUmgr OS group (remote reboot, echo, taskstat) reachable
  with `mcumgr --conntype udp`.
- **Camera** — IMX219 (Raspberry Pi Camera v2) over MIPI CSI-2, via Zephyr's
  in-tree `raspberry_pi_camera_module_2` shield rather than a bespoke overlay.
  `CMakeLists.txt` sets `SHIELD` so no `--shield` argument is needed. Frames are
  RAW10 and drawn from PSRAM through the shared multi-heap.
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

Console and shell come out of the board's USB-C port at 115200. That port is the
on-board USB-UART bridge on `uart0` (GPIO37/38), and the same port also carries
the ROM log and the MCUboot log, so one serial connection shows the whole boot
chain. The board's other USB connector is a USB-A **host** port (J2) and has no
console role.

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

### Upgrading over the network

Verified end to end on hardware, including revert-on-failure.

```bash
uv run poe app data_collection --sysbuild
mcumgr --conntype udp --connstring <board-ip>:1337 image list
mcumgr --conntype udp --connstring <board-ip>:1337 image upload \
    builds/data_collection/data_collection/zephyr/zephyr.signed.bin
mcumgr --conntype udp --connstring <board-ip>:1337 image test <new-hash>
mcumgr --conntype udp --connstring <board-ip>:1337 reset
# board now runs the new image with flags "active" (on trial, not confirmed)
mcumgr --conntype udp --connstring <board-ip>:1337 image confirm <new-hash>
```

**Always pass the hash to `image confirm`.** The bare `mcumgr image confirm`
form, which is supposed to confirm the running image, fails against this
firmware with `Error: 3` and leaves the flags untouched — the request is
rejected before it reaches the confirm logic. Passing the running image's hash
explicitly works. Confirming a hash that is not the active slot is refused by
design (`IMG_MGMT_ERR_IMAGE_CONFIRMATION_DENIED`).

If a test image is never confirmed, the next reset reverts to the previous
image, which MCUboot kept in slot1 — verified: an unconfirmed upgrade was rolled
back and the old image came up `active confirmed` again. This safety net exists
only because `sysbuild.conf` selects swap-using-move; see the comment in that
file for why the ESP32 family default would silently remove it.

Images are unsigned for development (`BOOT_SIGNATURE_TYPE_NONE`, the board's
`Kconfig.sysbuild` default). Generate a key and switch to
`CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256` before shipping.

Note that the app only logs its DHCP address on the `NET_EVENT_IPV4_ADDR_ADD`
event, and `main()` registers that callback several seconds after the lease
normally arrives, so the address is usually never printed. Find the board with
`arp -an | grep <board-mac>` after an ARP sweep of the subnet until that is
fixed.

One flashing caveat inherited from the swap-using-move layout: the image
trailer lives at the **end** of slot0, and `west flash` only erases the region
it writes. A board still carrying an older, larger image can leave stale
trailer bytes behind that confuse MCUboot. `uv run esptool --port <port>
erase-flash` clears it.
