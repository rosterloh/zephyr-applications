# sysbuild and MCUboot

`sysbuild` builds **multiple images in one invocation** — typically MCUboot
plus your application, sometimes a network-core image too. MCUboot is the usual
reason to reach for it.

In this workspace, six apps opt in via a `sysbuild.conf`:
`bluetooth_proxy_device`, `bt_keys`, `force_sensor`, `joystick_controller`,
`pt_mcp`, `rasprover`. Build them with:

```bash
mise run app rasprover --sysbuild
```

## Contents

1. [How sysbuild changes the build](#model)
2. [Namespacing config across images](#namespacing)
3. [Swap modes](#swap-modes)
4. [Signing](#signing)
5. [Flash partitions](#partitions)
6. [Confirming an image / DFU](#dfu)
7. [Traps](#traps)

## <a name="model"></a>How sysbuild changes the build

Without sysbuild, `west build` produces one image. With it, you get a *domain*
per image, and the build directory gains a subdirectory per domain:

```
builds/rasprover/
├── mcuboot/zephyr/zephyr.elf
├── rasprover/zephyr/zephyr.elf          # your app — note the extra level
└── zephyr/                              # sysbuild's own artifacts
    └── merged.hex                       # bootloader + signed app, for flashing
```

Two consequences that catch people out:

- **Artifact paths gain a domain segment.** `builds/<app>/zephyr/zephyr.elf`
  becomes `builds/<app>/<app>/zephyr/zephyr.elf`. Anything pointing a debugger
  or `addr2line` at the old path silently reads the wrong (or a missing) ELF.
  See `../../zephyr-debugging/references/gdb.md`.
- **`merged.hex` is what you flash**, not the app's own `zephyr.bin`. Flashing
  only the app leaves the bootloader absent, or the app unsigned and therefore
  unbootable.

Three config files, all optional, next to `prj.conf`:

| File | Scope |
|------|-------|
| `sysbuild.conf` | sysbuild's own `SB_CONFIG_*` symbols |
| `Kconfig.sysbuild` | declare extra sysbuild symbols (must `source "share/sysbuild/Kconfig"`) |
| `sysbuild/<image>.conf` | extra Kconfig fragments for another image, e.g. `sysbuild/mcuboot.conf` |

A minimal `sysbuild.conf`:

```kconfig
SB_CONFIG_BOOTLOADER_MCUBOOT=y
SB_CONFIG_MCUBOOT_MODE_SWAP_USING_MOVE=y
```

A `Kconfig.sysbuild` adding a board-dependent symbol (from
`applications/bluetooth_proxy_device`):

```kconfig
source "share/sysbuild/Kconfig"

config NET_CORE_BOARD
	string
	default "nrf5340dk/nrf5340/cpunet" if $(BOARD) = "nrf5340dk"
```

## <a name="namespacing"></a>Namespacing config across images

Three different prefixes, and mixing them up is the single most common sysbuild
mistake:

| Prefix | Lives in | Applies to |
|--------|----------|-----------|
| `CONFIG_*` | `prj.conf` | the application image only |
| `SB_CONFIG_*` | `sysbuild.conf` | sysbuild itself (which images to build, signing, swap mode) |
| `<image>_CONFIG_*` | command line / `sysbuild/<image>.conf` | that named image |

To change MCUboot's own configuration you cannot put `CONFIG_*` in your app's
`prj.conf` — that configures your app. Either add
`applications/<app>/sysbuild/mcuboot.conf`:

```kconfig
# This is MCUboot's own prj.conf fragment
CONFIG_BOOT_MAX_IMG_SECTORS=256
CONFIG_LOG=y
```

or pass it inline:

```bash
mise x -- west build --sysbuild -b <board> --build-dir builds/<app> applications/<app> \
    -- -Dmcuboot_CONFIG_BOOT_MAX_IMG_SECTORS=256
```

Your app's own options still use the plain form (`-DCONFIG_FOO=y`) or, more
explicitly, `-D<app>_CONFIG_FOO=y`.

## <a name="swap-modes"></a>Swap modes

Set exactly one `SB_CONFIG_MCUBOOT_MODE_*`. This decides both the partition
layout MCUboot expects and what a failed update does.

| Mode | Partitions needed | Notes |
|------|-------------------|-------|
| `SINGLE_APP` | `slot0_partition` | No app-initiated DFU; recovery only via MCUboot serial recovery |
| `SWAP_USING_MOVE` | `slot0`, `slot1` | Revert on failure, no scratch area needed |
| `SWAP_USING_OFFSET` | `slot0`, `slot1` | Newer alternative to MOVE |
| `SWAP_SCRATCH` | `slot0`, `slot1`, `scratch` | Needs a dedicated scratch partition |
| `OVERWRITE_ONLY` | `slot0`, `slot1` | Smallest and fastest, but **no revert** — a bad image bricks until reflash |
| `DIRECT_XIP[_WITH_REVERT]` | `slot0`, `slot1` | Runs from whichever slot; app must be linked for both |
| `RAM_LOAD[_WITH_REVERT]` | `slot0`, `slot1` | Copies to RAM and runs there |
| `FIRMWARE_UPDATER` | `slot0`, `slot1` | Dedicated updater app in the second slot |

If MCUboot is built with `MCUBOOT_DOWNGRADE_PREVENTION`, the application must
also select `CONFIG_MCUBOOT_BOOTLOADER_NO_DOWNGRADE` — otherwise the app can
offer an update the bootloader will refuse, which presents as an update that
"succeeds" and then silently doesn't take.

## <a name="signing"></a>Signing

MCUboot validates a signature on every boot, so an unsigned or wrongly-signed
image will not run. Sysbuild signs the app automatically as part of the build.

```kconfig
SB_CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256=y
SB_CONFIG_BOOT_SIGNATURE_KEY_FILE="${APPLICATION_CONFIG_DIR}/keys/mcuboot_priv.pem"

# Optional image encryption
SB_CONFIG_BOOT_ENCRYPTION_KEY_FILE="${APPLICATION_CONFIG_DIR}/keys/enc_priv.pem"
```

Signature types: `SB_CONFIG_BOOT_SIGNATURE_TYPE_NONE`, `..._RSA`,
`..._ECDSA_P256`, `..._ED25519`.

`${APPLICATION_CONFIG_DIR}` expands to the app directory, which keeps the path
working regardless of where the build runs from.

**With no key file configured, MCUboot builds with its committed development
key.** That is fine for bench work and unacceptable in production — anyone can
sign an image for your device. Generate a real key with
`imgtool keygen -k mcuboot_priv.pem -t ecdsa-p256` and keep it out of git.

Changing signature type or key invalidates every already-deployed image: the new
bootloader will not accept the old app and vice versa. Plan that as a
full reflash, not an OTA.

## <a name="partitions"></a>Flash partitions

MCUboot needs the slots to exist in devicetree with the labels it expects:

```dts
&flash0 {
    partitions {
        compatible = "fixed-partitions";
        #address-cells = <1>;
        #size-cells = <1>;

        boot_partition: partition@0 {
            label = "mcuboot";
            reg = <0x00000000 0xc000>;
        };

        slot0_partition: partition@c000 {
            label = "image-0";
            reg = <0x0000c000 0x69000>;
        };

        slot1_partition: partition@75000 {
            label = "image-1";
            reg = <0x00075000 0x69000>;
        };

        storage_partition: partition@de000 {
            label = "storage";
            reg = <0x000de000 0x6000>;
        };
    };
};
```

Rules that are easy to violate:

- **`slot0` and `slot1` must be the same size.** A swap between differently
  sized slots cannot work.
- **Slots must be erase-block aligned.** A slot starting mid-sector fails at
  runtime, not at build time.
- Most SoC families already define these in their board DTS — check before
  writing your own, and prefer overriding sizes to redefining the whole node.

For how partitions relate to NVS/ZMS and filesystem mounting, see
`./storage.md` and `./filesystems.md`.

## <a name="dfu"></a>Confirming an image / DFU

In any swap mode with revert, a freshly-written image boots **once** as a test.
If nothing confirms it, the next reset rolls back — which looks like "my update
keeps disappearing".

```c
#include <zephyr/dfu/mcuboot.h>

if (!boot_is_img_confirmed()) {
    /* Only confirm after your own health checks pass */
    int err = boot_write_img_confirmed();

    if (err) {
        LOG_ERR("failed to confirm image: %d", err);
    }
}
```

`boot_request_upgrade(BOOT_UPGRADE_TEST)` marks the secondary slot for a test
swap on next boot; `BOOT_UPGRADE_PERMANENT` skips the test cycle.

Enable the MCUboot-side Kconfig for whichever transport does the upload —
`CONFIG_MCUMGR_TRANSPORT_BT` / `..._UART`, or the USB DFU class
(`CONFIG_USBD_DFU`, see
`../../zephyr-peripherals/references/usb-device.md`).

## <a name="traps"></a>Traps

- **`west flash` after a sysbuild build must flash `merged.hex`.** Flashing the
  app alone gives an unsigned image in slot 0 and a device that hangs in the
  bootloader. `mise run flash <app>` uses the build directory, so make sure it
  was a `--sysbuild` build if that's what you intend.
- **Artifact paths gain a domain level.** `builds/<app>/<app>/zephyr/zephyr.elf`,
  not `builds/<app>/zephyr/zephyr.elf`. Stale debugger paths read the wrong ELF
  and produce nonsense symbols.
- **ESP32-P4-Nano rev 1.3 will not boot an MCUboot/sysbuild image.** That board
  is a pre-v3 engineering sample and needs `CONFIG_SOC_ESP32P4_REV_1_3`;
  MCUboot does not come up on that silicon. Build app-only there and rely on
  ESP32 simple boot — do not add `--sysbuild` for that target expecting it to
  work. (See also the PSRAM limitation in
  `../../zephyr-peripherals/references/video.md`.)
- **`OVERWRITE_ONLY` has no revert.** It is tempting because it's smaller and
  faster, but a bad OTA is unrecoverable without physical access. Only pick it
  when you have a serial-recovery path.
- **An unconfirmed image reverts on the next boot.** If an update "won't stick",
  check `boot_write_img_confirmed()` is actually being called before the reset —
  this is far more common than a genuine swap failure.
- **Changing swap mode changes the required partitions.** Switching to
  `SWAP_SCRATCH` without adding `scratch_partition` fails at MCUboot runtime,
  after a successful build.
- **`CONFIG_*` in `prj.conf` never configures MCUboot.** Use
  `sysbuild/mcuboot.conf` or `-Dmcuboot_CONFIG_*`. Symbols placed in the wrong
  file are silently ignored — see `./kconfig.md`.
- **Erasing flash wipes your bootloader too.** A full `esptool erase-flash` or
  `nrfjprog --eraseall` removes MCUboot; reflash `merged.hex`, not just the app.
  Note that on ESP32 targets a plain `mise run flash` only erases the app region, so
  stale ZMS/settings data can survive and wedge the firmware — see
  `./storage.md`.
