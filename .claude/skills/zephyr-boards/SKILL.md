---
name: zephyr-boards
description: >
  Out-of-tree hardware definitions in this workspace: Hardware Model v2
  board directories (board.yml, Kconfig.<board>, Kconfig.defconfig,
  <board>_defconfig, board.cmake, pre_dt_board.cmake, twister yaml),
  SoC/cpucluster/revision qualifiers, shields, and the `zephyr/module.yml`
  roots that make any of it visible to the build. Use when adding or
  porting a board, adding a cpucluster/revision/variant, wiring a flash
  runner, adding a shield, creating an out-of-tree Zephyr module, or
  debugging "board not found" / "board qualifiers not found" / a board
  whose defconfig or DTS seems to be ignored. Triggers on board.yml,
  module.yml, HWMv2, board_root, dts_root, BOARD_ROOT, `west boards`,
  Kconfig.defconfig, configdefault, pre_dt_board.cmake, EXTRA_DTC_FLAGS,
  "add a board", "new board", "board revision", "appcpu/procpu",
  "hpcore/lpcore", "shield".
---

# Zephyr Boards & Out-of-Tree Modules

Validated against: Zephyr 4.4.99 (cee159bb557d, 2026-08-07). Re-check with `mise run check-skills`.

## Scope

Defining hardware to Zephyr and getting it *discovered*: the HWMv2 board
directory, board identity/qualifiers, and the module roots that register
boards, DTS bindings and shields. Does NOT cover writing the board's
devicetree content or drivers (see `zephyr-peripherals`), Kconfig symbol
semantics (see `zephyr-system`), or bringing up a dead board (see
`zephyr-debugging`).

## Boards live in two roots here

| Root | Registered by | Boards |
|------|---------------|--------|
| `boards/<vendor>/<board>/` | this repo's `zephyr/module.yml` (`board_root: .`) | `robotis_openrb_150`, `adafruit_qt_py_esp32c3` |
| `deps/modules/lib/rosterloh-drivers/boards/<vendor>/<board>/` | that module's `zephyr/module.yml` (`board_root: .`, `dts_root: .`) | `ros_driver`, `esp32p4_nano`, `waveshare_esp32_s3_eth` + `boards/shields/*` |

Everything else (`adafruit_qt_py_esp32s3`, `rpi_pico`, `xiao_esp32c5`,
`arduino_nicla_vision`, `native_sim`) is in-tree under `deps/zephyr/boards/`.

`west boards -f "{name} {qualifiers} {dir}"` is the ground truth for what is
discovered and from where. If a board isn't listed, no amount of fixing its
files will help — the *root* isn't registered.

**Module boards are a separate git checkout.** Edit
`deps/modules/lib/rosterloh-drivers/boards/...` in place, commit and PR in that
repo, then `mise run west-update`. Never copy a module board into this repo's
`boards/` (see CLAUDE.md).

## The file set

Paths below are real examples in this workspace — read them before writing a
new board.

| File | Job |
|------|-----|
| `board.yml` | Identity: name, full_name, vendor, socs, cpuclusters, revisions, variants. |
| `Kconfig.<board>` | `config BOARD_<NAME>` + `select SOC_*` — hardware facts that cannot be overridden. |
| `Kconfig.defconfig` | `configdefault` overrides of *subsystem* defaults for this board. |
| `Kconfig` | Board-scoped tunables, e.g. `HEAP_MEM_POOL_ADD_SIZE_BOARD`. |
| `<board>_<cluster>_defconfig` | Default `CONFIG_*` for what is physically soldered (crystal, console UART, bootloader). |
| `<board>[_<cluster>].dts` | The board's devicetree; `*-pinctrl.dtsi` and connector `*.dtsi` alongside. |
| `board.cmake` | Flash/debug runners, normally by including `${ZEPHYR_BASE}/boards/common/*.board.cmake`. |
| `pre_dt_board.cmake` | `EXTRA_DTC_FLAGS` — the sanctioned way to silence a DTC warning. |
| `<board>_<cluster>.yaml` | Twister metadata: `identifier:` (the full qualifier string), `arch`, `supported:` list. |
| `Kconfig.sysbuild` | Sysbuild-scope symbols (MCUboot etc.) for this board. |
| `doc/index.rst` | Board documentation; keep the pinout image next to it. |

## Identity and qualifiers

```yaml
# deps/modules/lib/rosterloh-drivers/boards/waveshare/esp32p4_nano/board.yml
board:
  name: esp32p4_nano
  full_name: ESP32-P4-Nano
  vendor: waveshare
  socs:
  - name: esp32p4
```

The build target is `<board>[@revision]/<soc>/<cpucluster>`. Zephyr derives the
cpuclusters from the SoC, so `esp32p4_nano` yields
`esp32p4_nano/esp32p4/hpcore` and `esp32p4_nano/esp32p4/lpcore` with no extra
YAML. Revisions are declared explicitly — the in-tree `adafruit_qt_py_esp32s3`
uses `revision: {format: custom, revisions: ["", "psram"]}`, which is what makes
`adafruit_qt_py_esp32s3@psram/esp32s3/procpu` a valid target.

That same string is what goes in the twister yaml's `identifier:`, in
`mise run app --board`, and in the `app` task's allowed-board list.

## The three Kconfig files, and which one you want

Getting these confused is the usual reason a board setting "does nothing".

```kconfig
# Kconfig.esp32p4_nano — hardware truth, per cpucluster
config BOARD_ESP32P4_NANO
	select SOC_ESP32P4_HPCORE if BOARD_ESP32P4_NANO_ESP32P4_HPCORE
	select SOC_ESP32P4_LPCORE if BOARD_ESP32P4_NANO_ESP32P4_LPCORE
	select SOC_ESP32P4_REV_1_3
```

`select` is for facts the application must not override — which SoC is on the
PCB, and which silicon revision. (This board is a rev v1.3 engineering sample;
that `select` is load-bearing.)

```kconfig
# Kconfig.defconfig — change a subsystem's default, still overridable
if BOARD_ESP32P4_NANO_ESP32P4_HPCORE
configdefault NET_L2_ETHERNET
	default y
endif
```

Use `configdefault` here, not `config` — it adjusts the default of a symbol
defined elsewhere without redefining it.

```kconfig
# Kconfig — a board-scoped tunable
config HEAP_MEM_POOL_ADD_SIZE_BOARD
	int
	default 4096 if BOARD_ESP32P4_NANO_ESP32P4_HPCORE
```

And `_defconfig` for what is soldered down:

```
# boards/robotis/openrb_150/robotis_openrb_150_defconfig
CONFIG_SOC_ATMEL_SAMD_XOSC32K=y
CONFIG_BOOTLOADER_BOSSA=y
CONFIG_UART_CONSOLE=y
```

## Traps

- **A new board is invisible until its root is registered.** Boards in this repo
  work because the repo is itself a Zephyr module (`zephyr/module.yml` with
  `board_root: .`) and `west.yml` lists it as `manifest.self`. A board dropped
  anywhere else is simply not found. Confirm with `west boards`, not by guessing
  at the error.
- **Multi-cpucluster boards reject a bare board name.** `west build -b
  ros_driver` fails with a list of valid qualifiers
  (`ros_driver/esp32/procpu`, `ros_driver/esp32/appcpu`) — that error is the
  answer, not a bug. Always pass the full qualifier.
- **Directory name ≠ board name.** `boards/waveshare/esp32_s3_eth/` defines
  `waveshare_esp32_s3_eth`. `west build -b` and the twister `identifier:` use
  the **name** from `board.yml`; only humans use the directory.
- **`mise.toml`'s `app` task has its own allowlist.** A working board still
  can't be built through `mise run app` until it's added to that `case` — see
  the `mise` skill.
- **DTC warnings need `pre_dt_board.cmake`, not edits to the warning.** e.g.
  `boards/robotis/openrb_150/pre_dt_board.cmake` appends
  `-Wno-spi_bus_bridge` and `-Wno-unique_unit_address_if_enabled` because SAMD
  sercom nodes legitimately break those checks.
- **`SOC_NAME`, `SOC_SERIES`, `SOC_FAMILY` and `SOC_V2_DIR` CMake variables are
  deprecated** (4.5 migration guide). Use `CONFIG_SOC`, `CONFIG_SOC_SERIES`,
  `CONFIG_SOC_FAMILY` and `SOC_FULL_DIR` in board/module CMake.
- **A module's `board_root` also needs `dts_root`** if the module ships
  bindings, or its boards' DTS will reference compatibles Zephyr can't resolve.
  `rosterloh-drivers` sets both.
- **Shields are boards' poor cousin and live under `boards/shields/`** in the
  same root — `mise x -- west build ... --shield <name>`, no `board.yml`.

## Validation Checklist

- [ ] `west boards -f "{name} {qualifiers} {dir}"` lists the board, with the
      expected qualifiers, resolved from the directory you edited.
- [ ] Every declared qualifier actually builds: one build per cpucluster /
      revision you claim to support, not just the default.
- [ ] `builds/<app>/zephyr/.config` shows the `_defconfig` and
      `Kconfig.defconfig` values taking effect — and the `select`ed `SOC_*`
      symbol is set.
- [ ] `builds/<app>/zephyr/zephyr.dts` resolves to the hardware you intended
      (correct pinctrl, console UART, connectors).
- [ ] A flash runner is wired and works: `mise run flash <app>` reaches the
      board (or the `board.cmake` include list explains why not).
- [ ] The twister yaml `identifier:` string is byte-identical to a real build
      target, and `supported:` lists only what the DTS actually enables.
- [ ] The board is in the `app` task's allowed list in `mise.toml`, and
      `mise run app <app> --board <qualifier>` succeeds.
- [ ] `mise run check-skills` passes — new `CONFIG_*` names cited anywhere in
      the skill/docs resolve against the current tree.
