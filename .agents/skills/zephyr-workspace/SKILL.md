---
name: zephyr-workspace
description: >
  Facts specific to the zephyr-applications workspace that the generic zephyr
  skill leaves out. Use when building, flashing, debugging or adding a board/app
  here; when the zephyr skill shows a bare `west`/`python3`/`esptool` command or
  says "check the workspace skill"; or when you need to know which board root,
  module or example app in this repo to look at. Triggers on `mise run app`,
  `mise run flash`, `agent-build`, rosterloh-drivers, applications/<app>,
  "which boards live where", "which apps use sysbuild", check-skills, patching
  Zephyr or zenoh-pico, `west patch`, zephyr/patches*.yml, patch-zephyr.
---

# zephyr-applications workspace

The generic Zephyr knowledge lives in the user-level `zephyr` skill
(`~/.agents/skills/zephyr`, from dotfiles). This skill maps them onto this repo. Build,
task and layout rules are in `AGENTS.md`. Task authoring is in the `mise` skill.

## Generic command → here

| zephyr skill shows | Run here |
|---|---|
| `west build -b <board> --build-dir builds/<app> <app-dir>` | `mise run app <app> [--board <qualifier>] [--sysbuild]`, or `mise run agent-build <app>` (tails the log, full log in `logs/<app>-build.log`) |
| `west flash -d builds/<app>` | `mise run flash <app>` |
| bare `west`, `python3`, `esptool`, `zcbor`, `protoc` | prefix with `mise x --` |
| `<app-dir>` | `applications/<app>` |
| `west update` | `mise run west-update`, then `mise run check-skills` |
| `west sdk install` | `mise run sdk-install` |
| full ESP32 chip erase | `mise x -- esptool --port <port> erase-flash`, then `mise run flash <app>` |

A board must be in the `app` task's `case` in `mise.toml` before `mise run app`
accepts it (see the `mise` skill), even if `west build -b` works.

## Board roots

| Root | Boards |
|---|---|
| `boards/<vendor>/<board>/` (this repo is `manifest.self`, `board_root: .`) | `robotis_openrb_150` |
| `deps/modules/lib/rosterloh-drivers/boards/` (`board_root` + `dts_root`) | `adafruit_qt_py_esp32c3`, `m5stack_unit_poe_p4`, `ros_driver`, `general_driver` (`waveshare/driver_boards`), `esp32p4_nano`, `esp32p4_wifi6_poe_eth`, `waveshare_esp32_s3_eth`; shields `adafruit_neokey_1x4`, `adafruit_neoslider`, `adafruit_rotary_encoder`, `arducam_tof_camera`, `raspberry_pi_camera_module_1` |
| `deps/zephyr/boards/` | everything else used here: `adafruit_qt_py_esp32s3`, `rpi_pico`, `xiao_esp32c5`, `arduino_nicla_vision`, `native_sim` |

rosterloh-drivers is a separate git repo (`rosterloh/zephyr-drivers`, remote
name `rosterloh`). Branch, commit and PR there, then `mise run west-update`.
Never vendor its files into this repo.

## Apps and what they exercise

| Topic in the zephyr skill | Look at |
|---|---|
| sysbuild + MCUboot | apps with a `sysbuild.conf`: `bt_keys`, `data_collection`, `force_sensor`, `joystick_controller`, `pt_mcp`, `rasprover`. |
| video / UVC (`zephyr/references/peripherals/video.md`) | `embedded_vision` (Nicla Vision; ESP32-P4-Nano CSI variant) |
| USB stack mismatch trap (`zephyr/references/peripherals/usb-device.md`) | `motor_controller/src/usb.c` |
| Pico-ROS / zenoh, Twist pub/sub | `rasprover` (subscriber), `joystick_controller` (publisher, analog-axis input) |
| stale ZMS after reflash (`zephyr/references/system/storage.md`) | `pt_mcp` on xiao_esp32c5 |
| SSD1306 + LVGL hang (`zephyr/references/peripherals/lvgl.md`) | `rasprover` ships with the OLED disabled |
| shared app code | `applications/common/<module>/` (see AGENTS.md for why it isn't a `zephyr_library()`) |

protobuf: nanopb is cloned only because esp-hosted-mcu (ESP32-P4) needs it. No
app here uses protobuf. `protoc` comes from `mise.toml`.

## CI and native_sim

CI (`.github/workflows`) builds `rasprover --sysbuild`, `data_collection
--sysbuild`, `embedded_vision`, `force_sensor`, `joystick_controller`,
`motor_controller` and `pico_fw` on their default boards. It builds no
native_sim target, and macOS can't build native_sim either. Suites also list
the Espressif QEMU boards `waveshare_esp32_s3_eth/esp32s3/procpu/qemu` (Xtensa)
and `adafruit_qt_py_esp32c3/esp32c3/qemu` (RISC-V) in `integration_platforms`.
They run on a Mac after `mise run qemu-install`, and CI runs them too. Only
suites that don't need emulated buses use them: an emulated I2C/SPI/GPIO bus
bypasses the ESP's own peripheral drivers, so it adds nothing over
`qemu_cortex_m3`. Float code traps
with EXCCAUSE 32 on the Xtensa board (Zephyr never sets CPENABLE on ESP), so
float-heavy suites list only the C3.

## Patching west modules

Fixes not yet upstream are carried as `git format-patch` files and applied with
`west patch`, never committed into `deps/`.

| Module | Manifest | Patches | Task |
|---|---|---|---|
| zenoh-pico (`deps/modules/lib/zenoh`) | `zephyr/patches.yml` | `zephyr/patches/zenoh/` | `mise run patch-zenoh` |
| Zephyr (`deps/zephyr`) | `zephyr/patches-zephyr.yml` | `zephyr/patches/zephyr/` | `mise run patch-zephyr` |

`mise run west-update` runs `west patch clean` on both, updates, then both
patch tasks. Each task cleans before applying (`git apply` isn't idempotent) and
fails if the module tree ends up unchanged, since `west patch apply` reports
"0 patches applied successfully" instead of failing when `module:` resolves to
nothing. Use the workspace path (`deps/modules/lib/zenoh`) as `module:`.

Rules the manifests depend on:

- One manifest per module. `checkout-command` runs in every module a manifest
  names, and `git checkout --` reverts nothing if one pathspec is unknown there.
- `checkout-command` lists exactly the patched files, and `clean-command` is
  `""`. The default (`git checkout .` + `git clean -dfx`) would wipe PR work in
  the checkout and zenoh's generated `config.h`.
- Editing a `.patch` file means updating its `sha256sum`.

Add a patch:

1. `west patch -l zephyr/patches-zephyr.yml clean` (applied patches are
   uncommitted edits), then commit the fix on a branch in `deps/zephyr`. The
   branch doubles as the upstream PR branch.
2. `git -C deps/zephyr format-patch -1 -o ../../zephyr/patches/zephyr/`, numbered after the last one.
3. Add an entry: `path`, `sha256sum` (`shasum -a 256 <file>`), `module`,
   `author`/`email`/`date`, `upstreamable`, `issue` (tracking issue here or the
   upstream PR), `merge-pr` once an upstream PR exists, and `comments` saying
   what breaks without it.
4. Add every file it touches to `checkout-command`.
5. `git -C deps/zephyr checkout -` back to west's revision, then `mise run patch-zephyr`.

Retire a patch once its `merge-pr` merged and the pinned revision contains it
(`git -C deps/zephyr merge-base --is-ancestor <merge-sha> HEAD`):
delete the entry and file, but keep its paths in `checkout-command` with a
comment, so stale checkouts and CI caches still revert. Close its tracking issue.
If that empties a manifest, remove the manifest and its patch task too, or the
"changed nothing" check fails. Pico-ROS carries no patches now; `west-update`
only reverts the old ones.

## check-skills

`mise run check-skills` runs the zephyr skill's bundled
`scripts/check_symbols.py` over that skill plus this repo's `.agents/skills`,
against `deps/`. It checks cited `CONFIG_*`
symbols, reference routing and validation stamps. The shared skill is also
used by other workspaces on other Zephyr commits, so a stamp mismatch is a
warning to re-check, not a failure.

## Validation Checklist

- [ ] Commands taken from the zephyr skill were run through `mise run`/`mise x --`,
      with build output under `builds/<app>`.
- [ ] A new board builds through `mise run app <app> --board <qualifier>`, not
      just bare `west build`.
- [ ] After `mise run west-update`, `mise run check-skills` reports no unknown
      symbols.
- [ ] A new or edited patch has a matching `sha256sum`, its files are in
      `checkout-command`, and `mise run patch-<module>` succeeds.
