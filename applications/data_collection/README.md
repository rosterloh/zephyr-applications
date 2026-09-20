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

## Watchdog and coredump

Two mechanisms, covering the two ways this app has been seen to die.

**Task watchdog** (`CONFIG_APP_WATCHDOG`, shared code in
`applications/common/watchdog/`). The app is event driven — `main()` returns
once the network and camera are up — so there is no superloop to feed a
long-lived channel from. Coverage is a channel around each bounded operation
instead:

| Channel | Registered | Timeout | Covers |
|---|---|---|---|
| `boot` | start of `main()`, deleted before it returns | 60 s | DHCPv4, camera bring-up, the boot capture |
| `capture` | entry to `cam_mgmt_capture()`, deleted on every exit path | 10 s | format negotiation, buffer enqueue, the 2 s dequeue |

A `capture` channel is registered *before* `frame_lock` is taken, so a caller
that queues behind an already-wedged capture is monitored rather than parked in
an unmonitored `K_FOREVER` wait.

Both boards alias the TIMG MWDT as `watchdog0`, so `CONFIG_TASK_WDT_HW_FALLBACK`
has real hardware behind it. That matters more than the software layer here: the
failure in
[zephyr-drivers#43](https://github.com/rosterloh/zephyr-drivers/issues/43) takes
the *whole system* quiet — no ISR, no system workqueue, no log processor — and
only the hardware watchdog resets a board in that state.

On a timeout the log names the channel before the board reboots:

```
<err> app_watchdog: Task watchdog timeout: 'capture' (channel 1) stalled - rebooting
```

**Coredump** (`CONFIG_DEBUG_COREDUMP`) fires on a fault and writes to the
`coredump` flash partition, so the dump **survives the reboot that follows it**.
Both boards inherit `partitions_0x2000_default_16M.dtsi`, which already carries
`coredump_partition` — 4 KB at `0xfff000` — so no devicetree change was needed.

Retrieve it on the next boot, over the shell:

```
uart:~$ coredump find
Stored coredump found
uart:~$ coredump print
#CD:BEGIN#
#CD:5a4501000...
...
#CD:END#
uart:~$ coredump erase          # free the partition for the next one
```

Save that console output to a file, then decode it on the host:

```bash
# Rebuild the binary from the #CD: lines
mise x -- python deps/zephyr/scripts/coredump/coredump_serial_log_parser.py \
    console.log coredump.bin

# Serve it to gdb, against the *exact* elf that was running
mise x -- python deps/zephyr/scripts/coredump/coredump_gdbserver.py \
    builds/data_collection/data_collection/zephyr/zephyr.elf coredump.bin

# In another terminal
~/zephyr-sdk-$(cat deps/zephyr/SDK_VERSION)/riscv64-zephyr-elf/bin/riscv64-zephyr-elf-gdb \
    builds/data_collection/data_collection/zephyr/zephyr.elf \
    -ex "target remote :1234" -ex "bt"
```

The `.elf` must match the running image exactly. A dump opened against a
different build resolves to plausible-looking nonsense rather than failing.

`coredump verify` checks the stored dump's integrity, and `coredump error get`
reports a write that failed or was truncated — a truncated dump is never
silent.

Three sizing and safety notes, all worth knowing before changing the config:

- **`MEMORY_DUMP_MIN` with a bounded stack top.** The partition is one 4 KB
  sector. `MIN` enables `THREAD_STACK_TOP` automatically, but its limit
  defaults to `-1` — unbounded, stack pointer to end of region — so
  `CONFIG_DEBUG_COREDUMP_THREAD_STACK_TOP_LIMIT_FOR_CURRENT=2048` caps it.
  2 KB of stack plus the thread struct, register block and header leaves
  headroom in 4 KB, and 2 KB of frames is a deep backtrace.
- **A stall does not produce a dump.** The watchdog timeout handler runs in ISR
  context, and the flash backend cannot safely be driven from there: Zephyr's
  ESP32 sync flash write and erase are not IRAM-resident, so writing from an
  ISR on an XIP part risks running with the cache disabled, and a sector erase
  does not fit inside the 20 ms `CONFIG_TASK_WDT_HW_FALLBACK_DELAY` before the
  hardware watchdog resets the SoC mid-write. What a stall leaves is the
  channel name in the log. Dumping one would need the write deferred to a
  high-priority thread plus a longer fallback delay.
- **The console backend is the alternative**, and it *can* dump a stall — but
  it loses everything if nobody is attached when the board reboots, which for a
  headless box is most of the time. `LOGGING_UDP` needs a peer address fixed at
  build time that cannot be committed; pass it with `--extra-conf` for a one-off
  headless capture of a fault.

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
