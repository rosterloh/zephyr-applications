# rasprover

Firmware for the [Waveshare RaspRover](https://www.waveshare.com/wiki/RaspRover) robot platform, running on the Waveshare ROS Driver board (ESP32).

## Hardware

| Component | Part | Interface |
|-----------|------|-----------|
| MCU | ESP32 (Xtensa LX6, 240 MHz) | — |
| Current/power monitor | INA219 | I2C @ 0x42 |
| OLED display | SSD1306 128×32 | I2C @ 0x3C |
| Pan/tilt gimbal | Waveshare bus servos | UART1 @ GPIO19 TX / GPIO18 RX |

Board: `ros_driver/esp32/procpu` (defined in [rosterloh-drivers](https://github.com/rosterloh/zephyr-drivers))

## Flash layout

MCUboot is the bootloader. The board DTS includes Espressif's standard AMP
table, `partitions_0x0_amp_4M.dtsi`:

| Partition | Offset | Size | Purpose |
|-----------|--------|------|---------|
| `mcuboot` | 0x0 | 64 KB | Bootloader |
| `sys` | 0x10000 | 64 KB | Reserved |
| `slot0` | 0x20000 | 1344 KB | Primary application image |
| `slot1` | 0x170000 | 1344 KB | OTA update image |
| `slot0_appcpu` | 0x2C0000 | 448 KB | APPCPU image |
| `slot1_appcpu` | 0x330000 | 448 KB | APPCPU OTA image |
| `storage` | 0x3B0000 | 192 KB | Zephyr settings (ZMS) |
| `coredump` | 0x3FF000 | 4 KB | Coredump area |

MCUboot is configured in **swap-using-move** mode (the ESP32 default): on reboot after an OTA upload, MCUboot moves the new image from slot1 into slot0 before booting it.

> **This layout changed.** The board previously carried a hand-rolled table with
> `mcuboot` at 0x1000 and `slot0` at 0x10000. Units flashed with that layout
> cannot take an OTA update to this one — they need a wired reflash. The change
> was made so the board has an APPCPU slot at all; without one the
> `ros_driver/esp32/appcpu` target could not be configured.

## Building

Sysbuild is required — it builds MCUboot and the application together:

```shell
mise run app rasprover --sysbuild
```

Or manually:

```shell
mise x -- west build -b ros_driver/esp32/procpu -p always --sysbuild \
  --build-dir builds/rasprover applications/rasprover
```

This produces two binaries:
- `builds/rasprover/mcuboot/zephyr/zephyr.bin` — bootloader (flash once)
- `builds/rasprover/rasprover/zephyr/zephyr.signed.bin` — application (OTA target)

## Flashing

Initial flash (bootloader + application):

```shell
mise run flash rasprover
```

## OTA updates

OTA is handled via [MCUmgr](https://docs.zephyrproject.org/latest/services/device_mgmt/mcumgr.html) using the SMP protocol.
Two transports are enabled:

| Transport | How to use |
|-----------|------------|
| **Bluetooth LE** | Connect with nRF Connect for Mobile or `mcumgr` CLI |
| **Shell UART** | Use `mcumgr` CLI over the serial console |

### mcumgr CLI (UART example)

```shell
# Install mcumgr
go install github.com/apache/mynewt-mcumgr-cli/mcumgr@latest

# Upload new image over serial
mcumgr --conntype serial --connstring "dev=/dev/ttyUSB0,baud=115200" \
  image upload builds/rasprover/rasprover/zephyr/zephyr.signed.bin

# Confirm upload and trigger swap on next boot
mcumgr --conntype serial --connstring "dev=/dev/ttyUSB0,baud=115200" \
  image list
mcumgr --conntype serial --connstring "dev=/dev/ttyUSB0,baud=115200" \
  image test <hash-from-list>
mcumgr --conntype serial --connstring "dev=/dev/ttyUSB0,baud=115200" \
  reset
```

### mcumgr CLI (Bluetooth example)

```shell
mcumgr --conntype ble --connstring "peer_name=rasprover" \
  image upload builds/rasprover/rasprover/zephyr/zephyr.signed.bin
```

> **Image signing**: `sysbuild/mcuboot.conf` currently uses `BOOT_SIGNATURE_TYPE_NONE` (unsigned)
> for development. Before production use, generate an ECDSA-P256 key pair and switch to signed images.

## Debugging

JTAG debugging via OpenOCD 0.12.0+. See [Espressif JTAG docs](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/jtag-debugging/index.html) for wiring.

```shell
mise x -- west debug --build-dir builds/rasprover
```

Use `debug.conf` for debug-optimised builds:

```shell
mise x -- west build -b ros_driver/esp32/procpu -p always --sysbuild \
  --build-dir builds/rasprover applications/rasprover \
  -- -DEXTRA_CONF_FILE=debug.conf
```

## Pico-ROS node

The firmware joins the host ROS 2 graph as an ordinary node named `rasprover`,
using [Pico-ROS](https://github.com/Pico-ROS/Pico-ROS-software) over
**WiFi/TCP**. Pico-ROS is a ROS 2 client layer built *on top of* zenoh-pico --
zenoh-pico is still the transport -- that speaks the same wire conventions as
[`rmw_zenoh`](https://github.com/ros2/rmw_zenoh): key expressions of the form
`<domain>/<topic>/<type>_/RIHS01_<hash>`, `@ros2_lv` liveliness tokens for
discovery, and the rmw attachment struct on every sample.

> **This replaced the zenoh-ros2dds topology.** Previously the firmware
> published bare CDR under `rt/<topic>` and a host-side
> `zenoh-bridge-ros2dds` mapped it into ROS 2. The ROS-visible topic names are
> unchanged, but the host now runs `rmw_zenohd` and ROS 2 with
> `RMW_IMPLEMENTATION=rmw_zenoh_cpp`; a host still running the old bridge will
> not see this firmware at all.

| ROS 2 topic | Direction | Message |
|-------------|-----------|---------|
| `/rasprover/battery_state` | published | `sensor_msgs/msg/BatteryState` |
| `/joint_states` | published at 20 Hz | `sensor_msgs/msg/JointState` |
| `/rasprover/cmd_vel` | subscribed | `geometry_msgs/msg/Twist` |
| `/rasprover/gimbal_cmd` | subscribed | `sensor_msgs/msg/JointState` |

Message headers publish immediately with a zero timestamp until SNTP sets
`SYS_CLOCK_REALTIME`; after sync, header stamps use realtime. The *rmw
attachment* timestamp is separate and comes from zenoh-pico's
`CLOCK_MONOTONIC`, so `rmw_message_info.source_timestamp` on the host reflects
board uptime rather than wall clock. Use the header stamp.

> Why TCP rather than serial? The upstream `zenoh-pico` west-module Zephyr
> integration unconditionally accesses `sock->_fd`, a struct field that only
> exists when at least one socket-based transport is enabled. A serial-only
> build doesn't compile. See `mise run patch-zenoh` for the workspace patches
> that paper over this.

### Message types

`src/rasprover_types.h` is the X-macro type list `picoserdes` generates
serialize/deserialize code from -- one entry per ROS message this firmware
touches, plus the types they embed. It is a hand-trimmed copy of upstream's
`examples/example_types.h`, because picoserdes emits code for **every** entry
in the list; regenerate entries with Pico-ROS's `tools/type-gen` if a message
is added.

`tests/picoserdes` pins the resulting bytes against golden arrays derived by
hand from the message IDL. Run it with:

```shell
mise x -- west build -b native_sim/native/64 -p always \
  --build-dir builds/picoserdes applications/rasprover/tests/picoserdes
./builds/picoserdes/zephyr/zephyr.exe
```

### WiFi credentials

Credentials are stored via the Zephyr `wifi_credentials` subsystem (backed by
settings/ZMS) and applied by the connection manager, which auto-connects at
boot and reconnects on loss. Store them once over the shell:

```
wifi cred add -s "my-network" -k 1 -p "my-password"
wifi cred list
```

(`-k 1` is WPA2-PSK; see `wifi cred add -h` for other security types.) No
reboot needed -- the connection manager retries every few seconds until
credentials are available.

### Host setup

```shell
# 1. Install rmw_zenoh and start its router. The address must match
#    CONFIG_APP_PICOROS_LOCATOR (default tcp/192.168.1.10:7447); by default
#    rmw_zenohd listens on tcp/0.0.0.0:7447.
ros2 run rmw_zenoh_cpp rmw_zenohd

# 2. Point ROS 2 at rmw_zenoh. ROS_DOMAIN_ID must match
#    CONFIG_APP_PICOROS_DOMAIN_ID (default 0).
export RMW_IMPLEMENTATION=rmw_zenoh_cpp
export ROS_DOMAIN_ID=0

# 3. The node and its topics are now visible to ROS 2 directly -- no bridge.
ros2 node list
ros2 topic echo /rasprover/battery_state
ros2 topic echo /joint_states

ros2 topic pub /rasprover/cmd_vel geometry_msgs/msg/Twist \
  "{linear: {x: 0.1}, angular: {z: 0.0}}"

ros2 topic pub /rasprover/gimbal_cmd sensor_msgs/msg/JointState \
  "{name: ['pan_joint', 'tilt_joint'], position: [0.0, 0.0]}"
```

A gimbal command must name **both** `pan_joint` and `tilt_joint`; partial
commands are rejected rather than holding the missing axis.

Fields populated in each BatteryState message:

| Field | Source |
|-------|--------|
| `voltage` | INA219 bus voltage (V) |
| `current` | INA219 current (A) |
| `power_supply_status` | `DISCHARGING` (2) |
| `power_supply_health` | `GOOD` (1) |
| `present` | `true` |
| all others | `NaN` / 0 / empty |

Fields populated in each JointState message:

| Field | Source |
|-------|--------|
| `name` | `left_wheel_joint`, `right_wheel_joint`, `pan_joint`, `tilt_joint` |
| `position` | actuator feedback position (rad) |
| `velocity` | actuator feedback velocity (rad/s), or 0 before velocity feedback is valid |
| `effort` | empty |

### Configuration

Application-level symbols (`applications/rasprover/Kconfig`):

| Kconfig | Default | Description |
|---------|---------|-------------|
| `APP_PICOROS` | y | Enable the Pico-ROS node |
| `APP_PICOROS_LOCATOR` | `tcp/192.168.1.10:7447` | Router endpoint. Format `<protocol>/<host>:<port>`. |
| `APP_PICOROS_NODE_NAME` | `rasprover` | Node name announced to the ROS graph |
| `APP_PICOROS_DOMAIN_ID` | `0` | Must match the host's `ROS_DOMAIN_ID` |
| `APP_PICOROS_BATTERY_STATE_TOPIC` | `rasprover/battery_state` | BatteryState topic, without the leading slash |
| `APP_PICOROS_CMD_VEL_TOPIC` | `rasprover/cmd_vel` | Twist command topic |
| `APP_PICOROS_JOINT_STATE_TOPIC` | `joint_states` | JointState feedback topic |
| `APP_PICOROS_JOINT_STATE_PUBLISH_HZ` | `20` | JointState publish rate |
| `APP_PICOROS_GIMBAL_CMD_TOPIC` | `rasprover/gimbal_cmd` | JointState pan/tilt setpoint topic |
| `APP_GIMBAL` | y | Enable pan/tilt gimbal actuator control |
| `APP_TIME_SYNC` | y | Enable SNTP synchronization for ROS header timestamps |
| `APP_TIME_SNTP_SERVER` | `pool.ntp.org` | SNTP server used for realtime clock sync |
| `APP_TIME_SNTP_RETRY_SEC` | `15` | Retry interval after a failed SNTP sync |
| `APP_TIME_SNTP_RESYNC_SEC` | `3600` | Resync interval after a successful SNTP sync |

#### Advanced tunables

These live in `applications/common/picoros/Kconfig` because the generated
`zenoh-pico/config.h` (see `mise run patch-zenoh`) names them directly -- a
missing symbol is a zenoh-pico compile error, not a silent default. Values
match upstream zenoh-pico except the lease pair, which `rmw_zenohd` requires.

| Kconfig | Default | Maps to |
|---------|---------|---------|
| `PICOROS_FRAG_MAX_SIZE` | `4096` | `Z_FRAG_MAX_SIZE` |
| `PICOROS_BATCH_UNICAST_SIZE` | `2048` | `Z_BATCH_UNICAST_SIZE` |
| `PICOROS_BATCH_MULTICAST_SIZE` | `2048` | `Z_BATCH_MULTICAST_SIZE` |
| `PICOROS_SOCKET_TIMEOUT_MS` | `100` | `Z_CONFIG_SOCKET_TIMEOUT` |
| `PICOROS_TRANSPORT_LEASE_MS` | `60000` | `Z_TRANSPORT_LEASE` -- 60 s to match `rmw_zenohd` |
| `PICOROS_TRANSPORT_LEASE_EXPIRE_FACTOR` | `2` | `Z_TRANSPORT_LEASE_EXPIRE_FACTOR` |
| `PICOROS_RUNTIME_MAX_TASKS` | `64` | `Z_RUNTIME_MAX_TASKS` |
| `PICOROS_TRANSPORT_ACCEPT_TIMEOUT_MS` | `1000` | `Z_TRANSPORT_ACCEPT_TIMEOUT` |
| `PICOROS_TRANSPORT_CONNECT_TIMEOUT_MS` | `10000` | `Z_TRANSPORT_CONNECT_TIMEOUT` |

`CONFIG_ZENOH_PICO_THREADS_NUM` (zenoh-pico's own Kconfig, default 4) sizes the
preallocated pthread stack pool at `CONFIG_MAIN_STACK_SIZE` each. This
application only starts the read and lease tasks, so it is the first lever to
reach for if DRAM gets tight.

## What it does

On startup the firmware initialises the INA219 current sensor, brings up WiFi (using credentials from settings), opens a Pico-ROS session to the configured router, declares its node, publishers and subscribers, and reads voltage, current, and power every `LOOP_DELAY_S` seconds (default 60). Each reading is logged over the console and published as a `sensor_msgs/BatteryState`.

## Display configuration

| Kconfig | Default | Description |
|---------|---------|-------------|
| `APP_DISPLAY` | y | Enable LVGL display subsystem |
| `APP_DISPLAY_WORK_QUEUE_DEDICATED` | n | Use a dedicated work queue for UI updates |

Settings are persisted via the Zephyr settings subsystem (ZMS backend). Loop delay and WiFi credentials can be written at runtime using the shell `settings` commands.

## Planned

- Re-enable WiFi and display once integration is stable
- ROS 2 services and parameters (`CONFIG_PICOROS_SERVICES` /
  `CONFIG_PICOROS_PARAMS`) -- the zenoh-pico features they need are already
  compiled in, so this is application work only
- Golioth cloud connectivity (telemetry, remote settings) — deferred until the `pouch`
  module's `board.yml` is fixed upstream; Golioth OTA would then replace MCUmgr
