# Live interop against upstream's machine

This is the real proof of API compatibility: our remote against upstream's
own machine implementation, unmodified, over host loopback.

`native_sim` uses `CONFIG_NET_NATIVE_OFFLOADED_SOCKETS`, so `zsock_*` calls
go straight to host syscalls. No TAP interface, no `net-tools`, no root.

## Build the machine

    cmake -S deps/modules/lib/protective-stop/pstop_c -B /tmp/pstop_machine_build
    cmake --build /tmp/pstop_machine_build --target machine_app

## Run the pair

Terminal 1:

    /tmp/pstop_machine_build/machine_app 8890

Terminal 2:

    mise run app protective_stop
    ./builds/protective_stop/zephyr/zephyr.exe

## What to expect

The machine logs each frame by type: `173` = BOND (0xAD), `85` = OK (0x55),
`146` = STOP (0x92). A successful bond is followed by heartbeats at 500 ms,
because `machine_app` advertises a 1000 ms window and the remote transmits at
half of it.

With no simulated pole closed, both loops read open and the remote correctly
reports STOP — fail-safe is the right behaviour for an unwired switch, not a
bug.

## Reading the counters

`machine_app` prints `Invalid request: N` for rejected frames. The ones that
matter here:

| N | Meaning | Likely cause |
|---|---|---|
| 10 | `PSTOP_MSG_LOST` | counter gap beyond `max_lost_messages + 1` — the counter advanced on a non-transmitting tick |
| 11 | `PSTOP_MSG_REPETITION` | duplicate counter — two BONDs in flight |
| 14 | `PSTOP_MSG_INVALID_CHECKSUM` | frame corrupted, or a field-layout divergence |
| 15 | `PSTOP_ERROR_INVALID_ID` | `receiver_id` does not match the machine's `machine_device_id` |
