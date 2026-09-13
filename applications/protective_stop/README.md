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
west at `deps/modules/lib/protective-stop`, pinned to a SHA, and its
remote-side sources are compiled directly (see `pstop_c.cmake`). Our entire
port is `src/pstop_time.c`.

## Tests

    mise x -- west twister -T applications/protective_stop -p native_sim --inline-logs
