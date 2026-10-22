# protective_stop

A Zephyr **remote** for the Polymath pstop protocol — the unit carrying the
physical stop switch. Interoperates with an upstream machine (`machn`, the
ROS 2 `protective_stop_machine` node, or the plain-C `host` app).

Design: `docs/superpowers/specs/2026-09-13-protective-stop-design.md`

## Build

    mise run app protective_stop                                    # native_sim
    mise run app protective_stop --board waveshare_esp32_s3_eth/esp32s3/procpu

## Protocol library

The pstop protocol is **not reimplemented here**. `pstop_c` is vendored via
west at `deps/modules/lib/protective-stop`, pinned to a SHA, and consumed as a
Zephyr module (`CONFIG_PSTOP=y` in `prj.conf`). The Zephyr port of
`time_get_now()` lives in the module itself, not in this app.

## Tests

    mise x -- west twister -T applications/protective_stop -p native_sim/native/64 --inline-logs

## Control plane

An HTTP server on port 80 (8080 on native_sim) speaks upstream's
`docs/guides/http_api.md`, so upstream's tooling drives this remote unmodified:

| Route | Auth | |
|---|---|---|
| `GET /state.json` | none | telemetry, per-slot `pstop_machines[]` (state 3 = REJECTED), lockstep attribution in `pstop_mm_*` |
| `POST /api/pstop_peers?slot=N&ip=&port=[&id=0x..]` | none | set a machine slot; `&clear=1` idles it, `&rebond=1` is the way out of REJECTED |
| `POST /api/pstop_peer?ip=&port=` | none | legacy single-machine form, slot 0 |
| `GET/POST /api/role[?role=operator\|stop_only]` | admin | live, no reboot |
| `POST /api/ring_offset?n=0..15` | none | which physical pixel is LED 1; persisted |
| `POST /api/ring_led1?on=0\|1` | none | locate: only LED 1, white; expires after 5 min |
| `GET /api/health` | none | lifetime wear counters and warnings (upstream's format) |
| `POST /api/health/reset?what=button\|all&confirm=1` | admin | after a switch swap, or a refurbished unit |
| `GET /api/last_log` | none | tail of the previous boot's log (survives a reset, not a power cycle) |
| `GET /api/coredump` | admin | raw dump from the coredump partition, 404 when none |
| `GET /admin/` | admin | admin page: status, machines, role, ring, WiFi, device id, health |
| `GET /admin/api/status`, `POST /admin/api/restart` | admin | |
| `GET/POST /admin/api/settings` | admin | JSON `{"ring_offset":N,"device_id":N}`; device id 0 = from the MAC |
| `GET/POST /admin/api/wifi` | admin | JSON `{"networks":[{"ssid","pass"}]}`, up to 4; `"********"` keeps a stored password |
| `GET /admin/api/verbose`, `POST /admin/api/verbose/toggle` | admin | debug logging on/off at runtime |

Admin is HTTP Basic, user `admin`, password `CONFIG_PROTECTIVE_STOP_ADMIN_PASSWORD`.
There is no default: until one is set the admin routes answer 403 and the
remote warns at boot. Keep it out of the repo with a local fragment:

    echo 'CONFIG_PROTECTIVE_STOP_ADMIN_PASSWORD="..."' > ~/pstop-admin.conf
    mise x -- west build -b native_sim/native/64 --build-dir builds/protective_stop \
        applications/protective_stop -- -DEXTRA_CONF_FILE=$HOME/pstop-admin.conf

Peer changes are picked up by the comparator on its next tick.

Not ported: upstream's microlink/Tailscale, OTA and USB routes (this remote has
none of those), and `/api/admission`, which is machine-side.

Upstream's one-remote-many-machines test runs against the simulator as is:

    ./builds/protective_stop/zephyr/zephyr.exe &
    make -C <protective-stop>/host
    python3 <protective-stop>/tools/pstop_multi_machine_test.py \
        --chip 127.0.0.1:8080 --host-ip 127.0.0.1 -n 3

The simulator's sessions send from 18891+ rather than upstream's 8891+
(`CONFIG_PROTECTIVE_STOP_LOCAL_PORT`) because that test's machines listen on
8890+ of the same host.

## Hardware (Waveshare ESP32-S3-ETH)

| | |
|---|---|
| Stop switch | DPST loopback, GPIO39->40 and GPIO41->42 (app overlay) |
| LED ring | 16 x WS2812 on GPIO17, RMT channel 1 (app overlay, alias `pstop-ring`) |
| Status pixel | onboard WS2812 on GPIO21, RMT channel 0 (board, alias `led-strip`) |
| Ethernet | onboard W5500 on SPI2, DHCP; preferred while it holds a lease |
| WiFi | station, DHCP; joins the stored networks in order when Ethernet is down |

The device id is `0x01` followed by the low 24 bits of the factory MAC, as
upstream derives it, unless pinned through `/admin/api/settings`. Remotes that
ran slices 1-3 have `0xC0A80105` pinned; post `{"device_id":0}` to move them
to the MAC-derived id (and update the machine's allowlist to match).

The ring and status pixel colours are upstream's: see `src/ring.h`.

## Interop

See `tests/interop/README.md` for running this remote against upstream's own
`machine_app`.
