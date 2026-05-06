# Zephyr Applications

<a href="https://github.com/rosterloh/zephyr-applications/actions/workflows/build.yml?query=branch%3Amain">
  <img src="https://github.com/rosterloh/zephyr-applications/actions/workflows/build.yml/badge.svg?event=push">
</a>

## Getting Started

You need [uv](https://docs.astral.sh/uv/getting-started/installation/) installed.
Everything else — Python, west, and the Zephyr SDK — is managed by the tooling.

### First-time setup

```shell
git clone https://github.com/rosterloh/zephyr-applications
cd zephyr-applications
uv sync                  # create .venv and install all Python tools
uv run poe setup         # west update (deps/) + Zephyr SDK toolchains
```

### Building

Build any app against its default (or an explicitly allowed) board:

```shell
uv run poe app motor_controller                        # default: robotis_openrb_150
uv run poe app joystick_controller                     # default: adafruit_qt_py_esp32s3
uv run poe app rasprover --sysbuild                    # rasprover hw build + MCUboot
uv run poe app rasprover --board native_sim/native/64  # rasprover native_sim
```

Each app has an allowed-board list (see the `app` task in `poe.toml`); boards outside it are rejected.

### Flashing

```shell
uv run poe flash motor_controller
```

### Available tasks

```shell
uv run poe --help
```

| Task | Description |
|------|-------------|
| `setup` | First-time workspace setup (west update + SDK install) |
| `west-update` | Fetch/update all west dependencies into `deps/` |
| `sdk-install` | Install Zephyr SDK toolchains for this project's targets |
| `app <name> [--board <b>] [--sysbuild]` | Build an app for its default (or an explicitly allowed) board |
| `flash <name>` | Flash a previously built app |
| `agent-build <name> [--board <b>] [--sysbuild]` | `app` with tail-truncated logs written to `logs/` |
| `run-rasprover-sim` | Run an already-built native_sim rasprover image |
| `fmt` | Format Python scripts with ruff |

## Repository layout

```
.
├── applications/       # Zephyr applications
├── boards/             # Out-of-tree board definitions
├── deps/               # West-managed dependencies (git-ignored)
├── scripts/            # Utility scripts
├── poe.toml            # Task runner configuration
├── pyproject.toml      # Python dependencies (uv)
└── west.yml            # West manifest
```

## Notes

- `golioth` and `pouch` are excluded until [pouch](https://github.com/golioth/pouch)
  adds `full_name` to its `board.yml` (required by the current Zephyr board schema).
  Re-enable in `west.yml` to restore Golioth support to `bluetooth_proxy_device`.
  `rasprover` has had Golioth removed and builds cleanly without it.
- `pico_fw` requires the `cyw43` module (beechwoods-software), which causes Kconfig
  conflicts on non-Pico targets and cannot be integrated into the shared workspace.
  Build it using its own manifest: `cd applications/pico_fw && west init -l . && west update`.
- `deps/` is git-ignored. Run `uv run poe west-update` to refresh after pulling
  changes to `west.yml`.
