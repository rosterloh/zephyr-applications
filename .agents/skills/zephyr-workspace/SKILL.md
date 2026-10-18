---
name: zephyr-workspace
description: >
  Facts specific to the zephyr-applications workspace that the generic zephyr-*
  skills leave out. Use when building, flashing, debugging or adding a board/app
  here; when a zephyr-* skill shows a bare `west`/`python3`/`esptool` command or
  says "check the workspace skill"; or when you need to know which board root,
  module or example app in this repo to look at. Triggers on `mise run app`,
  `mise run flash`, `agent-build`, rosterloh-drivers, applications/<app>,
  "which boards live where", "which apps use sysbuild", check-skills.
---

# zephyr-applications workspace

The generic Zephyr knowledge lives in the user-level `zephyr-*` skills
(`~/.agents/skills`, from dotfiles). This skill maps them onto this repo. Build,
task and layout rules are in `AGENTS.md`. Task authoring is in the `mise` skill.

## Generic command → here

| zephyr-* skills show | Run here |
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
| `boards/<vendor>/<board>/` (this repo is `manifest.self`, `board_root: .`) | `robotis_openrb_150`, `adafruit_qt_py_esp32c3` |
| `deps/modules/lib/rosterloh-drivers/boards/` (`board_root` + `dts_root`) | `ros_driver`, `general_driver` (`waveshare/driver_boards`), `esp32p4_nano`, `esp32p4_wifi6_poe_eth`, `waveshare_esp32_s3_eth`; shields `adafruit_neokey_1x4`, `adafruit_neoslider`, `adafruit_rotary_encoder`, `arducam_tof_camera`, `raspberry_pi_camera_module_1` |
| `deps/zephyr/boards/` | everything else used here: `adafruit_qt_py_esp32s3`, `rpi_pico`, `xiao_esp32c5`, `arduino_nicla_vision`, `native_sim` |

rosterloh-drivers is a separate git repo (`rosterloh/zephyr-drivers`, remote
name `rosterloh`). Branch, commit and PR there, then `mise run west-update`.
Never vendor its files into this repo.

## Apps and what they exercise

| Topic in a zephyr-* skill | Look at |
|---|---|
| sysbuild + MCUboot | apps with a `sysbuild.conf`: `bluetooth_proxy_device`, `bt_keys`, `data_collection`, `force_sensor`, `joystick_controller`, `pt_mcp`, `rasprover`. A board-dependent `Kconfig.sysbuild` lives in `bluetooth_proxy_device`. |
| video / UVC (`video.md`) | `embedded_vision` (Nicla Vision; ESP32-P4-Nano CSI variant) |
| USB stack mismatch trap (`usb-device.md`) | `motor_controller/src/usb.c` |
| Pico-ROS / zenoh, Twist pub/sub | `rasprover` (subscriber), `joystick_controller` (publisher, analog-axis input) |
| stale ZMS after reflash (`storage.md`) | `pt_mcp` on xiao_esp32c5 |
| SSD1306 + LVGL hang (`lvgl.md`) | `rasprover` ships with the OLED disabled |
| shared app code | `applications/common/<module>/` (see AGENTS.md for why it isn't a `zephyr_library()`) |

protobuf: nanopb is cloned only because esp-hosted-mcu (ESP32-P4) needs it. No
app here uses protobuf. `protoc` comes from `mise.toml`.

## CI and native_sim

CI (`.github/workflows`) builds `rasprover --sysbuild`, `data_collection
--sysbuild`, `embedded_vision`, `force_sensor`, `joystick_controller`,
`motor_controller` and `pico_fw` on their default boards. It builds no
native_sim target, and macOS can't build native_sim either. To run
emulator-backed ztests on a Mac, use twister on `qemu_cortex_m3 --force-platform`
with an extra overlay that adds a `zephyr,i2c-emul-controller` labelled `i2c0`.

## check-skills

`mise run check-skills` validates the zephyr-* skills in `~/.agents/skills`
plus this repo's `.agents/skills` against `deps/`. It checks cited `CONFIG_*`
symbols, reference routing and validation stamps. The shared skills are also
used by other workspaces on other Zephyr commits, so a stamp mismatch is a
warning to re-check, not a failure.

## Validation Checklist

- [ ] Commands taken from a zephyr-* skill were run through `mise run`/`mise x --`,
      with build output under `builds/<app>`.
- [ ] A new board builds through `mise run app <app> --board <qualifier>`, not
      just bare `west build`.
- [ ] After `mise run west-update`, `mise run check-skills` reports no unknown
      symbols.
