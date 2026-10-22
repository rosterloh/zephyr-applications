# protective_stop — Zephyr remote for the Polymath pstop protocol

**Status:** approved design, not yet implemented
**Date:** 2026-09-13
**Upstream:** [polymathrobotics/protective-stop](https://github.com/polymathrobotics/protective-stop) @ `4ce8094a060544ca8a2be1084c711a41445af221` (2026-09-10)

## Goal

A Zephyr application, `applications/protective_stop/`, that acts as a pstop
**remote** — the handheld/panel unit carrying the physical stop switch. It must
interoperate, unmodified, with an existing upstream **machine**: the ESP32
`machn` unit, the ROS 2 `protective_stop_machine` node, or the plain-C `host`
application.

Two compatibility surfaces are in scope:

1. The 48-byte pstop wire protocol (bond, heartbeat, stop, arming).
2. The HTTP control plane documented in upstream `docs/API.md`, including the
   `/admin` routes.

Out of scope, each its own project: Tailscale/WireGuard (`components/microlink`,
~28k lines), OTA, and the machine role.

## Non-goals

- Reimplementing the pstop protocol. See "Protocol library" below.
- SIL 3 / PL e certification. Upstream targets these; they are engineering
  targets there and not claimed here. The architecture preserves upstream's
  safety *mechanisms* (dual-channel loopback, lockstep comparison, fail-safe
  defaults) so a future safety case is not foreclosed, but no certification
  work is part of this.
- Byte-compatibility with ESP-IDF's coredump format. See slice 4.

## Protocol library: vendored, not rewritten

`pstop_c` is Apache-2.0, pure C99 and OS-agnostic. Its only OS dependency is
`time_get_now()`, returning monotonic milliseconds. Upstream's entire ESP-IDF
port is a CMakeLists plus a 20-line `esp_timer` shim
(`components/pstop/port/pstop_time_esp.c`).

We do the same. `west.yml` gains one top-level project:

```yaml
- name: protective-stop
  url: https://github.com/polymathrobotics/protective-stop.git
  revision: 4ce8094a060544ca8a2be1084c711a41445af221
  path: deps/modules/lib/protective-stop
```

Pinned to a SHA, not `main`: this is safety-relevant code and a silent upstream
change to the state machine must be a deliberate, reviewed bump.

No `zephyr/module.yml` is required — the app's `CMakeLists.txt` references the
path directly, exactly as upstream's ESP-IDF wrapper does. Contributing a
`module.yml` upstream is intended follow-up work, after which this can become a
conventional Zephyr module.

A remote needs **6 of the 11** core sources; the rest are machine-side:

| Compiled | Not compiled |
|---|---|
| `checksum.c` `device_id.c` `endian.c` `os.c` `protocol_data.c` `pstop_msg.c` | `machine.c` `protocol.c` `pstop_application.c` `pstop_remote_data.c` |
| plus our `src/pstop_time.c` | upstream `time.c` (Linux-only; returns `0` elsewhere) |

Our port in full:

```c
uint64_t time_get_now(void) { return (uint64_t)k_uptime_get(); }
```

`k_uptime_get()` is monotonic milliseconds since boot — no RTC or NTP
discontinuities, which is what a heartbeat/timeout clock requires.

### Wire format (for reference; implemented by the above, not by us)

48 bytes, little-endian, CRC-16 over the first 46:

```
version:u8  message:u8  stamp:u64  received_stamp:u64
id:u32  receiver_id:u32  heartbeat_timeout:u32
counter:u32  received_counter:u32  padding1:u32  padding2:u32  checksum:u16
```

`message` ∈ { OK `0x55`, STOP `0x92`, BOND `0xAD`, UNBOND `0x6A`,
UNKNOWN `0x0F` }. `device_id_t` is an IPv4 address as `uint32_t` at
`PSTOP_VERSION 0x02`.

**Device ID derivation.** Because the ID *is* an IPv4 address, the remote takes
its own `device_id` from the active uplink's address once the interface is up,
and re-derives it on address change. A settings key `pstop/device_id` overrides
this when non-zero — required on `native_sim`, where offloaded sockets mean
Zephyr has no interface address of its own, and useful on hardware for pinning a
stable ID across DHCP leases. The machine's operator allowlist is keyed on this
value, so an unpinned ID that moves with DHCP will silently demote a remote to
stop-only.

Note: `firmware/main/main.c:10` says "40-byte buffer". That comment is stale;
the encoder writes 48.

## Safety architecture

Preserved from upstream, because these mechanisms are the point of the device.

### Dual-channel loopback

Two independent loopback channels run through the two poles of an external DPST
normally-closed stop switch:

```
channel A:  GPIO39 (drive) --> pole 1 --> GPIO40 (sense)
channel B:  GPIO41 (drive) --> pole 2 --> GPIO42 (sense)
```

Each tick, the owning sampler drives its OUT pin high, reads the echo, drives it
low, reads again. The channel counts as CLOSED only when `rb_hi == 1 && rb_lo ==
0` — proving continuity *and* ruling out a stuck-high short. IN pins are pulled
down, so an open loop or a cut wire reads STOP. Until both phases have been
sampled the channel reads OPEN, so the device is fail-safe at boot.

The decision logic (debounce, boot priming, verdict) lives in
`src/estop_verdict.c` with no HAL dependency, mirroring upstream's
`firmware/main/estop_verdict.{c,h}` split. Upstream split it so the SIL-critical
core is host-testable to MC/DC — on their board the JTAG pins *are* the loop
pins, so on-target coverage is impossible. We keep the split for the same
testability reason; `gpio_emul` additionally lets us test the HAL glue.

### Lockstep comparison

Two sampler threads and one comparator thread on a 10 Hz tick. Each sampler
independently reads **its own** channel, builds a `pstop_msg_t` for every
configured machine slot, and encodes each to 48 bytes. The comparator `memcmp`s
the two encodings and transmits only on an exact match.

Comparing encoded bytes rather than verdicts is deliberate: one `memcmp` covers
the switch reading *and* the counter, timestamp, receiver ID, and both message
buffers. The CRC is inside the compared region, so a fault in CRC computation is
caught too.

Failure behaviour: mismatch ⇒ transmit nothing ⇒ every bonded machine
heartbeat-times-out and stops. A single-channel fault (one loop open, the other
intact) therefore cannot mask a real stop.

Threads rather than pinned cores: the safety property is *two independent
samplers whose encodings must match*, which threads satisfy. This works on
`native_sim` today and maps onto `CONFIG_SMP` pinned cores on the S3 with no
change to the comparator. Each sampler must publish within 80 ms of the tick;
a missed deadline is treated as a mismatch.

## Sessions and transport

Four machine slots, one UDP socket each, local ports 8891–8894. Per slot: bond
state machine (one BOND in flight, 5 s retry), counters, and a reply-loss
rebond watchdog.

**Rate is machine-governed.** The machine advertises its per-operator
`heartbeat_ms` in the `heartbeat_timeout` field of every reply, including the
bond ack. The remote adopts it per session and transmits every
`heartbeat_timeout / 2`, clamped to [100, 1000] ms.

Sends are **decimated ticks**: the per-session counter advances only on
transmitting ticks. This is a protocol requirement, not an optimisation —
upstream `protocol.c` rejects counter gaps larger than `max_lost_messages + 1`,
so incrementing on every 10 Hz tick while transmitting at 400 ms would produce
gaps of 4 and drop the bond. Stop-switch sampling stays at 10 Hz regardless;
the sampling cadence is a safety property and never slows down.

The rebond watchdog must fire *later* than the machine's own bond-drop timeout,
so a reply blip never causes a nuisance rebond. Derived per session from the
adopted heartbeat (`heartbeat_ms × max_missed + jitter margin`), with a 2500 ms
floor covering the pre-first-reply state. Upstream hard-codes `max_missed = 5`
against `machn`; we inherit that coupling and document it, since the value is
not on the wire.

A STOP, and the press-and-release arming gesture, is broadcast to every slot.
Each machine still enforces its own `min_stop_ms` veto and its own operator
allowlist.

## Persistence

Zephyr **Settings** API on a **ZMS** backend (`CONFIG_ZMS`,
`CONFIG_SETTINGS_ZMS`).

Settings-over-ZMS rather than raw ZMS: raw ZMS is a flat `uint32_t id → bytes`
store, so using it directly means inventing an ID map and keeping it stable
across firmware versions forever. Settings gives string keys and handles
allocation. Upstream's NVS blobs are string-keyed too, so the persistence model
stays recognisable.

Keys: `pstop/peers/<0..3>` (ip, port, machine id), `pstop/role`
(`stop_only` | `operator`), `pstop/ring_off`, `pstop/health` (wear counters),
`pstop/admin_pw`.

**API note:** in the current Zephyr tree `zephyr/fs/zms.h` is deprecated and
emits a `__WARN`. The live header is `zephyr/kvss/zms.h`; the subsystem moved to
`subsys/kvss/zms`. Use the `kvss` path.

On `native_sim`, ZMS runs on `flash_simulator` with its native backend
(`flash_simulator_native.c`), which persists to a host file — so settings
survive restarts in simulation and the persistence path is genuinely exercised
rather than stubbed.

## LED indication

Two separate WS2812 chains, matching upstream:

| Chain | Pin | Role | Upstream source |
|---|---|---|---|
| 16-LED ring | GPIO17 | pstop link state, divided into one segment per configured slot | `dcs_pstop_ring.c` |
| 1 status pixel | GPIO21 | blinks the IPv4 last octet; red strobe on fault | `dcs_rgb.c` |

Driven via `worldsemi,ws2812-pulse-io` on `espressif,esp32-rmt` — the
vendor-neutral `pulse_io` API backed by the same RMT peripheral upstream uses.
`WS2812_STRIP_GPIO` is not an option: it hard-depends on
`SOC_SERIES_NRF51/52/53/91` because the bit-banging is Cortex-M inline assembly.
The ESP32-S3 has four RMT TX channels (0–3, per the `espressif,esp32-rmt`
binding); the two chains take channels 0 and 1. No contention with the W5500,
which is on SPI2.

`src/app_ring.c` computes the 16-pixel RGB buffer as a **pure function** —
segment division across slots, rotation offset, locate mode, per-state colours —
and a thin tail either calls `led_strip_update_rgb()` or logs the buffer. The
pixel computation is unit-tested on `native_sim`; only the final push needs
hardware. The rotation offset exists because the ring can be installed in any of
16 orientations, so "LED 1" is a per-device provisioning setting.

## HTTP control plane

`CONFIG_HTTP_SERVER` with dynamic resource handlers, `json_obj_encode` for
responses, port 80.

**Unauthenticated:** `GET /state.json`, `GET /api/health`,
`POST /api/pstop_peer`, `POST /api/pstop_peers`, `POST /api/ring_offset`,
`POST /api/ring_led1`, `GET /api/last_log`.

**Admin (HTTP Basic):** `GET /admin/`, `/admin/api/status`,
`GET|POST /admin/api/settings`, `POST /admin/api/restart`,
`GET|POST /admin/api/verbose{,/toggle}`, `GET|POST /admin/api/wifi`,
`GET|POST /api/role`, `POST /api/health/reset`, `GET /api/coredump`.

Basic auth uses `HTTP_SERVER_REGISTER_HEADER_CAPTURE` to capture the
`Authorization` header and `zephyr/sys/base64.h` to decode it. Both are
in-tree; no new dependency.

**Dropped** (WireGuard/microlink-coupled, and Tailscale is out of scope):
`/admin/api/peers`, `/admin/api/peers/allowed`, `/api/derp*`, `/api/ts_boot`.
**Dropped** (no Zephyr equivalent): `/api/enter_download` — ESP-IDF's USB
download mode is an ESP-ROM feature with no portable counterpart; the
equivalent is the board's boot button plus `esptool`.
**Dropped** (transport selection is out of scope): `/api/iface/*`,
`/api/usb_enable`, `/api/pstop_num`, `/api/wifi_tx_power`.
**Deferred with OTA:** `/admin/api/ota*`, `/admin/api/fleet-ota/*`.

### Two deliberate incompatibilities

1. **`/state.json` omits fields we do not have** (`derp_region`, `wg_paused`,
   `ml_state`, and other Tailscale keys) rather than emitting zeros. A
   monitoring system reading `derp_region: 0` from a device with no DERP is
   worse off than one seeing a missing key.
2. **`/api/coredump` is route-compatible, not payload-compatible.** Zephyr's
   coredump format is not ESP-IDF's, so `espcoredump.py` will not decode it. The
   Zephyr-side equivalent is the `coredump` shell plus
   `zephyr/scripts/coredump/`. This is documented in the app README so nobody
   points the wrong decoder at it.

## Board

**The board already exists** in the `rosterloh-drivers` module as
`boards/waveshare/esp32_s3_eth/`, board name `waveshare_esp32_s3_eth`,
qualifier `waveshare_esp32_s3_eth/esp32s3/procpu`. No new board definition is
needed. The variant is ESP32-S3-WROOM-1U-**N16R8** — 16 MB flash, 8 MB PSRAM,
i.e. twice the flash of upstream's 8 MB build.

Already provided, and matching upstream's pinout exactly:

| Item | State |
|---|---|
| W5500 on SPI2 — MOSI 11, MISO 12, SCLK 13, CS 14, INT 10, RST 9 | present, `spi-max-frequency = <40000000>` |
| WiFi, BT HCI, TRNG, WDT | enabled |
| Console on native USB-Serial/JTAG (UART0 is not broken out) | `zephyr,console = &usb_serial` |
| `storage_partition` — 192 K @ `0xfb0000` | present, for ZMS |
| `coredump_partition` — 4 K @ `0xfff000` | present |
| MCUboot `boot_partition` + `slot0`/`slot1` (5952 K each) + `scratch` | present, from `partitions_0x0_amp_16M.dtsi` |
| `rmt@60016000`, `compatible = "espressif,esp32-rmt"`, `#pulse-io-cells = <1>` | present in `esp32s3_common.dtsi`, `status = "disabled"` |

Two gaps, belonging in different places:

- **Board PR to `rosterloh-drivers`:** the onboard WS2812 status pixel on
  GPIO21. It is soldered to the board, so it belongs in the board definition,
  not an app overlay. Needs `&rmt` enabled with pinctrl routing TX0→GPIO21, and
  a 1-pixel `worldsemi,ws2812-pulse-io` node.
- **App overlay:** the external 16-LED ring on GPIO17 (RMT TX1) and the four
  DPST loopback GPIOs (39/40, 41/42). These are part of the pstop enclosure
  wiring, not the board, so they are app-specific.

Per the workspace's module policy, the board change is committed and PR'd in
`rosterloh-drivers` and picked up here by `mise run west-update` — not vendored
into this repo.

Two consequences worth recording:

- **OTA needs no partition rework.** The layout is already MCUboot-shaped with
  two app slots and a scratch area, so slice 1's original "leave headroom for
  OTA" requirement is already satisfied. Whichever of mcumgr or hawkbit is
  chosen is purely additive.
- **`coredump_partition` is 4 K.** That is enough for Zephyr's default coredump
  (registers plus stack), not for a full RAM image. If `/api/coredump` needs to
  carry more, the partition has to grow — which is a board change, so it should
  be decided before fielding units rather than after.

## Layout

```
applications/protective_stop/
  CMakeLists.txt  Kconfig  prj.conf  VERSION  README.md  tests.yaml
  boards/native_sim_native_64.conf
  boards/waveshare_esp32_s3_eth_esp32s3_procpu.conf
  boards/waveshare_esp32_s3_eth_esp32s3_procpu.overlay   # ring + DPST pins
  src/main.c            init, thread spawn
  src/pstop_time.c      the port
  src/estop_verdict.c   pure verdict/debounce/priming logic, no HAL
  src/estop_gpio.c      drive and sample the two loopback channels
  src/lockstep.c        two samplers + comparator
  src/session.c         per-slot socket, bond FSM, counters, rebond watchdog
  src/app_ring.c        pure pixel computation + led_strip push
  src/app_settings.c    ZMS-backed Settings
  src/http_api.c        unauthenticated routes
  src/http_admin.c      Basic auth + /admin routes
  tests/verdict/        ztest: estop_decide truth table
  tests/wire/           ztest: encoding vs committed golden frames
  tests/ring/           ztest: pixel computation
boards/waveshare/esp32s3_eth/
```

## Testing

Four layers, increasing in value:

1. **`tests/verdict`** — `estop_decide` truth table: both closed, both open,
   stuck high, cut wire, boot priming, debounce. `native_sim`, no hardware.
2. **`tests/ring`** — pixel computation: segment division for 1–4 configured
   slots, rotation offset wraparound, locate mode overriding state colours.
   Pure function, no hardware.
3. **`tests/wire`** — encode/decode against golden 48-byte frames generated once
   from upstream's host build and committed as a fixture. Since we compile the
   same sources, encoding is identical by construction today; the fixture
   catches future divergence (a swapped encoder, a packing or endianness bug on
   a new target).
4. **Live interop** — build upstream `pstop_c/examples/machine/machine_app` on
   the host, run the `native_sim` remote against it over loopback, drive the
   switch through `gpio_emul`. This is the actual proof of API compatibility.

`native_sim` networking uses `CONFIG_NET_NATIVE_OFFLOADED_SOCKETS=y`, which
hands `zsock_*` calls to host syscalls — so the simulated remote and upstream's
real `machine_app` talk over ordinary host loopback, with no TAP interface, no
`net-tools` setup and no root. Note this app is the first in the workspace to
enable networking on `native_sim`; `rasprover` explicitly disables it.

Once the control plane exists, upstream's `tools/pstop_multi_machine_test.py`
and `tools/hil/` should drive a Zephyr device unmodified. That is the acceptance
test for slice 3.

## Delivery slices

Each ends somewhere verifiable. Slice 1 is independent of 2 and 3; slice 4
depends on all three.

| # | Slice | Done when |
|---|---|---|
| 1 | Board gap — PR the onboard GPIO21 status pixel + `&rmt` enable to `rosterloh-drivers` | `blinky`-equivalent lights the onboard pixel on real hardware |
| 2 | Safety core on `native_sim` — `pstop_c` project, time port, lockstep, `gpio_emul` channels, sessions, ZMS Settings | ztests green **and** the sim remote bonds, heartbeats and arms against upstream's `machine_app` |
| 3 | Control plane on `native_sim` — HTTP server, `/state.json`, `/api/*`, admin auth | upstream's Python tooling drives the sim unmodified |
| 4 | Hardware bring-up — app overlay for the ring and DPST pins, real GPIO loopback, W5500, WiFi routes, coredump routes | a physical press stops a real machine; the ring shows per-slot state |

Slice 1 was originally scoped as writing a board definition from scratch. It
is now a small additive PR, because the board already exists with the W5500,
partitions and MCUboot layout in place.

## Deferred decisions

- **OTA mechanism.** mcumgr (`subsys/mgmt/mcumgr/grp/img_mgmt`) versus hawkbit
  (`subsys/mgmt/hawkbit`); both in-tree. This is a push-versus-pull choice:
  mcumgr is operator-initiated over a transport we already have and pairs with
  upstream's direct `POST /admin/api/ota` upload; hawkbit is device-initiated
  polling, which matches upstream's `fleet-ota/check` + interval model. Both
  need MCUboot and sysbuild, which `rasprover` already demonstrates in this
  workspace, and the board's partition table is already MCUboot-shaped — so
  nothing in the earlier slices has to anticipate the choice, and no
  abstraction seam will be built speculatively.
- **Contributing `zephyr/module.yml` upstream**, after which the west project
  becomes a conventional Zephyr module.
