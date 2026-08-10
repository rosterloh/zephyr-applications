---
name: zephyr-system
description: >
  Application-level Zephyr subsystems: Kconfig build configuration,
  Settings persistence, NVS/ZMS direct flash storage, file systems
  (LittleFS/FAT/ext2), Shell CLI commands, the Ztest test
  framework + Twister, power management (system sleep states,
  device runtime PM, wake sources, PM policy locks), and multi-image
  builds with sysbuild + MCUboot. Use when editing
  prj.conf or boards/*.conf, debugging "unmet dependencies" or
  symbol-visibility errors, persisting configuration or runtime state,
  mounting a filesystem, registering shell commands with
  SHELL_CMD_REGISTER, writing Ztest test cases, configuring
  testcase.yaml, reducing current draw, wiring wake sources, adding a
  bootloader, signing an image, or setting up OTA/DFU.
  Triggers on prj.conf, CONFIG_*, settings_save/load, nvs_write,
  fs_mount, SHELL_CMD_*, ZTEST(), twister, "store this on flash",
  CONFIG_PM, pm_device_runtime_get/put, pm_policy_state_lock_*, "low
  power", "deep sleep", "wake source", sysbuild, SB_CONFIG_*, MCUboot,
  "slot0_partition", "swap mode", imgtool, "sign the image",
  boot_write_img_confirmed, or "the update reverts".
---

# Zephyr System

Validated against: Zephyr 4.4.99 (cee159bb557d, 2026-08-07). Re-check with `mise run check-skills`.

## Scope

Application-facing system services that sit above the kernel and
drivers — build configuration, persistence, filesystems, the
interactive shell, and tests. Does NOT cover defining device drivers
(see `zephyr-peripherals`), kernel primitives (see `zephyr-kernel`),
or payload encoding (see `zephyr-serialization`).

## Pick the right reference

| You're working on...                                                   | Load                                |
|------------------------------------------------------------------------|-------------------------------------|
| Kconfig syntax, prj.conf, "unmet dependencies", menuconfig             | `references/kconfig.md`             |
| Settings subsystem (save/load), backend selection (NVS/ZMS/FCB/File)   | `references/settings.md`            |
| Direct NVS or ZMS flash storage with numeric IDs                       | `references/storage.md`             |
| Mounting LittleFS / FAT / ext2 / FCB, file I/O, partition layout       | `references/filesystems.md`         |
| Registering shell commands (`SHELL_CMD_REGISTER`), backends, getopt    | `references/shell-commands.md`      |
| Ztest test cases, fixtures, FFF mocking, Twister config                | `references/testing.md`             |
| System/device PM, wake sources, policy locks, reducing current draw    | `references/power-management.md`    |
| sysbuild, MCUboot, signing, slot layout, DFU / confirming an image     | `references/sysbuild-mcuboot.md`    |

For JSON/CBOR/Protobuf payload encoding, see the `zephyr-serialization`
skill.

## Universal traps

- **`CONFIG_*` is case-sensitive.** "Unmet dependencies" almost always
  means a prerequisite Kconfig isn't enabled — read the failed
  expression literally, don't paraphrase.
- **`west flash` typically erases the storage partition** along with
  the firmware. Settings / NVS data does NOT survive a normal flash
  unless you explicitly preserve those sectors.
- **Settings handler `h_set` callbacks must consume the value** with
  `read_cb` — returning success without reading the buffer leaves the
  next read in an inconsistent state.
- **Twister's per-test logs live under `twister-out/<board>/<test>/`** —
  the top-level twister output only summarizes pass/fail.
- **System PM only fires when no thread is runnable.** A single polling
  thread (or a busy loop with short `k_sleep`s) keeps the scheduler busy
  and silently prevents *all* deep idle. Make threads block on a
  semaphore/event instead — see `references/power-management.md`.

## Validation Checklist

- [ ] Every symbol you assigned survives to `builds/<app>/zephyr/.config`.
      A `prj.conf` line dropped by an unmet dependency leaves no error — the
      resolved `.config` is the only proof it took effect.
- [ ] The Kconfig stage printed no `attempt to assign the value ... to the
      undefined symbol` warning (for handwritten fragments this aborts the
      build; if you saw one, the skill or your fragment is stale — run
      `mise run check-skills`).
- [ ] Persistence tested across a **power cycle**, not a soft reset:
      `settings_load()` returns 0 and the value is the one you stored.
- [ ] After a `west flash`, you know whether the storage partition was
      erased — if the value vanished, that's the cause, not your code.
- [ ] Tests actually *ran*: twister/ztest reports PASS per suite (check
      `twister-out/<board>/<test>/` for the log), not just a successful build.
- [ ] MCUboot/sysbuild: the new image is confirmed, or you have demonstrated
      the revert path by deliberately not confirming one boot.
