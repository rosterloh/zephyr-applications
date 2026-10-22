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
| `GET /admin/api/status`, `POST /admin/api/restart` | admin | |

Admin is HTTP Basic, user `admin`, password `CONFIG_PROTECTIVE_STOP_ADMIN_PASSWORD`
(upstream's public default `microlink` until you change it; the remote warns at
boot). Peer changes are picked up by the comparator on its next tick.

Not yet ported: the admin HTML pages, `/api/ring`, `/api/health`,
`/api/last_log`, settings/WiFi/coredump admin routes. `/api/admission` is a
machine-side route and has no meaning here.

Upstream's one-remote-many-machines test runs against the simulator as is:

    ./builds/protective_stop/zephyr/zephyr.exe &
    make -C <protective-stop>/host
    python3 <protective-stop>/tools/pstop_multi_machine_test.py \
        --chip 127.0.0.1:8080 --host-ip 127.0.0.1 -n 3

The simulator's sessions send from 18891+ rather than upstream's 8891+
(`CONFIG_PROTECTIVE_STOP_LOCAL_PORT`) because that test's machines listen on
8890+ of the same host.

## Interop

See `tests/interop/README.md` for running this remote against upstream's own
`machine_app`.
