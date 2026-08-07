# RP2350-POE-ETH test application

Evaluation firmware for the [Waveshare RP2350-POE-ETH](https://www.waveshare.com/rp2350-poe-eth.htm),
built to answer one question: *can this board carry a fielded, PoE-powered,
OTA-updatable Zephyr device?*

It exercises the pieces that decide that:

- the onboard **W6300** Ethernet controller (upstream `wiznet,w6300` driver) on
  hardware SPI0,
- **DHCPv4** address acquisition,
- **MCUboot** via `sysbuild`, with two image slots and swap-using-move so a bad
  update reverts,
- **hawkBit** DDI polling, with the running image confirmed on init,
- the **WS2812** status LED and the PoE/USB power sense lines,
- a shell (`hawkbit`, `net`, `kernel`, `flash`) for poking at it on the bench.

## Building

```bash
mise run app rp2350_poe_eth --sysbuild
# or, with truncated output and a full log in logs/
mise run agent-build rp2350_poe_eth --sysbuild
```

The board only has an MCUboot target in the allowed list, because hawkBit
requires `CONFIG_BOOTLOADER_MCUBOOT=y`. To build something without a bootloader,
target the plain variant directly:

```bash
mise x -- west build -b rp2350_poe_eth/rp2350a/m33 --build-dir builds/hello \
    deps/zephyr/samples/hello_world
```

## Flashing

Use a SWD probe — sysbuild flashes both images in the right order:

```bash
mise run flash rp2350_poe_eth
```

Note that the OpenOCD shipped in the Zephyr SDK does not support the RP2350 —
use the Raspberry Pi fork, or probe-rs / J-Link.

Drag-and-drop UF2 is only half usable here. The bootloader is fine:

```bash
cp builds/rp2350_poe_eth/mcuboot/zephyr/zephyr.uf2 /media/$USER/RP2350/
```

but `rp2350_poe_eth/zephyr/zephyr.uf2` is generated from the *unsigned* binary,
so MCUboot will reject it. For the application image, flash
`rp2350_poe_eth/zephyr/zephyr.signed.confirmed.hex` over SWD, or convert it
yourself to a UF2 at `0x10010000` (`picotool uf2 convert`).

## Configuring hawkBit

The defaults in `prj.conf` are placeholders:

```
CONFIG_HAWKBIT_SERVER="192.168.1.110"
CONFIG_HAWKBIT_PORT=8080
CONFIG_HAWKBIT_DDI_SECURITY_TOKEN="abcd1234"
```

Point them at your instance, or set them at runtime from the shell (needs
`CONFIG_HAWKBIT_SET_SETTINGS_RUNTIME=y`):

```
uart:~$ hawkbit set addr 10.0.0.5
uart:~$ hawkbit set port 8080
uart:~$ hawkbit run
```

The device ID comes from `hwinfo` (the RP2350 flash unique ID) unless you
override it with `hawkbit_set_device_identity_cb()`.

### Signing

Development builds are unsigned (`CONFIG_BOOT_SIGNATURE_TYPE_NONE=y` in
`sysbuild/mcuboot.conf`). Before shipping anything, switch to
`CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256` and generate a key:

```bash
mise x -- west sign --tool imgtool --key <key.pem>
```

## Status LED

| Colour | Meaning                            |
|--------|------------------------------------|
| Amber  | booting                            |
| Blue   | link up, no DHCP lease yet         |
| Green  | online, hawkBit polling            |
| Purple | downloading an update              |
| Red    | error — check the console          |

## Flash layout

`rp2350_poe_eth/rp2350a/m33/mcuboot`, sized for the 16 MB part:

| Offset     | Size    | Partition            |
|------------|---------|----------------------|
| `0x000000` | 64 KB   | MCUboot              |
| `0x010000` | 1.5 MB  | `image-0` (running)  |
| `0x190000` | 1.5 MB  | `image-1` (staged)   |
| `0x310000` | 13.9 MB | storage (settings)   |

The application currently occupies ~252 KB, so there is a lot of headroom —
enough that the same configuration still fits comfortably if your board turns
out to carry a smaller flash part (see the note in the board's `.dtsi`).
