# data_collection

Networked data-collection application for Waveshare ESP32-P4 boards. Brings up
wired Ethernet with DHCPv4 and exposes an MCUmgr/SMP management surface over
UDP, plus a UART shell.

Two boards are supported, and the app is identical on both — each carries the
on-chip RMII EMAC, PSRAM, and a Raspberry Pi 15-pin CSI connector wired to the
`csi_*` labels the camera shields bind to:

| Board | Notes |
|---|---|
| `esp32p4_nano/esp32p4/hpcore` | Default. 16 MB PSRAM. |
| `esp32p4_wifi6_poe_eth/esp32p4/hpcore` | **ESP32-P4-WIFI6-POE-ETH.** 32 MB PSRAM, PoE-powered RJ45, on-board ESP32-C6 radio over SDIO. |

## Features

- **Ethernet** — on-chip RMII EMAC (`CONFIG_ETH_ESP32`) with DHCPv4 addressing.
  The assigned IPv4 address is logged once the lease is acquired.
- **SMP over UDP** — MCUmgr OS group (remote reboot, echo, taskstat) reachable
  with `mcumgr --conntype udp`, plus a custom camera group (below).
- **Camera** — a MIPI CSI-2 module selected at build time by shield. Defaults to
  the IMX219 (Raspberry Pi Camera v2) via Zephyr's in-tree
  `raspberry_pi_camera_module_2`; pass `--shield arducam_tof_camera` for the
  Arducam ToF depth camera. The capture path takes the highest bit depth the camera
  advertises that fits the app's PSRAM ceiling, so nothing in the app is pinned
  to one sensor. Frames are
  drawn from PSRAM through the shared multi-heap.
- **Shell** — interactive console on `uart0`.

## Build & flash

```bash
uv run poe app data_collection --sysbuild   # MCUboot + app; board defaults to esp32p4_nano/esp32p4/hpcore
uv run poe flash data_collection

# ESP32-P4-WIFI6-POE-ETH, with the Arducam ToF camera instead of the default IMX219
mise run app data_collection -b esp32p4_wifi6_poe_eth/esp32p4/hpcore -s arducam_tof_camera --sysbuild
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

## SMP camera group

`src/cam_mgmt.c` registers a custom MCUmgr group so a host can drive the camera
over the SMP transport already used for OTA, rather than only the boot-time
capture and the interactive `video` shell. Enabled by `CONFIG_APP_CAM_MGMT`
(default `y` when `MCUMGR` && `VIDEO`).

**Group id `0x1000`.** Custom groups start at `MGMT_GROUP_ID_PERUSER` (64) and
the Zephyr-specific groups count *down* from there, so `0x1000` keeps clear of
both. Requests and responses are CBOR maps, as in every MCUmgr group.

| cmd | name | op | request | response |
|---|---|---|---|---|
| 0 | `INFO` | read | — | `group` u32, `cam` tstr, `fmt` u32, `w` u16, `h` u16, `ready` bool |
| 1 | `CAPTURE` | write | — | `seq` u32, `size` u32, `w` u16, `h` u16, `fmt` u32 |
| 2 | `READ` | read | `seq` u32, `off` u32, `len` u16 | `seq` u32, `off` u32, `data` bstr, `eof` bool |

- `group` is the command-set version (currently `1`), so clients need not be
  pinned to a firmware build.
- `fmt` is the Zephyr fourcc (`VIDEO_PIX_FMT_SBGGR10P`). `INFO`'s `fmt`/`w`/`h`
  are the format the camera negotiated on the most recent capture, and are zero
  before the first one. Still size buffers from `CAPTURE`'s `size`, which is
  the authoritative frame length.
- `CAPTURE` is a *write* because it drives the sensor and discards the
  previously retained frame. `seq` starts at 1 and increments per capture.
- `READ` pages the retained frame: repeat with `off += len(data)` until `eof`.
  `len` is clamped to 1024 B, which fits both the response encode buffer
  (`CONFIG_MCUMGR_TRANSPORT_NETBUF_SIZE`, 2048 B under UDP) and the datagram
  (`CONFIG_MCUMGR_TRANSPORT_UDP_MTU`, 1500 B).
- `READ` answers `MGMT_ERR_ENOENT` (3) if `seq` does not match the buffered
  frame — nothing captured yet, or a newer `CAPTURE` replaced it mid-pull.
  Restart from the `seq` that `CAPTURE` returned.

Only one frame is buffered: it stays checked out of the video buffer pool (PSRAM,
~2.5 MB per RAW10 frame) so `READ` serves it without a copy, and is released at
the top of the next `CAPTURE`.

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
