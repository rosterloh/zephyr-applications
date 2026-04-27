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
cp .env.example .env     # set MY_BOARD for your target
```

### Building

Use the named shortcuts for common targets:

```shell
uv run poe build-motor      # robotis_openrb_150
uv run poe build-joystick   # adafruit_qt_py_esp32s3/esp32s3/procpu
uv run poe build-pico       # rpi_pico/rp2040
```

Or build any app against any board via `MY_BOARD` in `.env`:

```shell
uv run poe build applications/motor_controller
```

### Flashing

```shell
uv run poe flash applications/motor_controller
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
| `build <app>` | Build an app — requires `MY_BOARD` in `.env` or environment |
| `flash <app>` | Flash a previously built app |
| `build-motor` | Build `motor_controller` for robotis\_openrb\_150 |
| `build-joystick` | Build `joystick_controller` for adafruit\_qt\_py\_esp32s3 |
| `build-pico` | Build `pico_fw` for rpi\_pico/rp2040 |
| `fmt` | Format Python scripts with ruff |

## Repository layout

```
.
├── applications/       # Zephyr applications
├── boards/             # Out-of-tree board definitions
├── deps/               # West-managed dependencies (git-ignored)
├── drivers/            # Out-of-tree drivers
├── dts/                # Device tree bindings
├── scripts/            # Utility scripts
├── tests/              # Twister test suites
├── poe.toml            # Task runner configuration
├── pyproject.toml      # Python dependencies (uv)
└── west.yml            # West manifest
```

## Testing

```shell
west twister -T tests -v --inline-logs --integration

# Single test
west twister -p native_sim -s drivers/seesaw/drivers.sensor.seesaw
```

## Notes

- `golioth` and `pouch` modules are excluded from the west manifest until
  [pouch#board.yml](https://github.com/golioth/pouch) adds `full_name` (required
  by the current Zephyr board schema). Re-enable them in `west.yml` to build
  `rasprover` and `bluetooth_proxy_device`.
- `deps/` is git-ignored. Run `uv run poe west-update` to refresh after pulling
  changes to `west.yml`.
