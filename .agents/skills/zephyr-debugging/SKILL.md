---
name: zephyr-debugging
description: >
  Debug running Zephyr firmware across every transport — the UART shell,
  SEGGER RTT over a debug probe, source-level gdb, and the network stack.
  Use this skill whenever the user wants to interrogate a live board: send
  shell commands, capture or interpret a boot log, diagnose driver init
  failures, probe I2C/SPI/GPIO, measure timing, decode a fault/HardFault,
  attach a debugger, step through code, or figure out why networking isn't
  working (ping6 fails, no packets, capture a pcap with tcpdump/tshark/
  Wireshark, inspect the net shell). Triggers on "check the serial output",
  "scan the I2C bus", "look at the boot log", "why isn't the driver
  initialising", "the board hangs", "decode this fault", "attach gdb",
  "use RTT", "post-mortem a crash", "analyse a coredump", "debug it on
  native_sim", "run it under ASAN/valgrind", "find the stack overflow",
  "capture the traffic", "why can't it ping", or anything that involves
  talking to or inspecting a flashed Zephyr device — live, post-mortem, or
  on the host simulator. Board- and app-agnostic. For the Synapse protocol
  specifically, use synapse-trace-debug.
---

# Zephyr Debugging

Validated against: Zephyr 4.4.99 (62acbd571c72, 2026-09-04). Re-check with `mise run check-skills`.

Debugging a flashed Zephyr board spans several transports and tools. They're
not interchangeable — each answers a different question. Pick by **what you
need to learn**, escalating only as far as you must. The UART shell answers
most questions; RTT, gdb, and packet capture are escalations for when it
can't.

## First decision: which approach?

| What you're trying to do                                          | Approach            | Reference (`references/`) |
|-------------------------------------------------------------------|---------------------|----------------------|
| Send shell commands, read boot logs, probe I2C/SPI/GPIO, timing   | **UART shell**      | `serial.md`          |
| Board has no free UART / shell is dead but CPU alive + probe wired | **RTT over SWD**    | `rtt.md`             |
| Thread faulted, hang before shell, step code, decode a HardFault  | **gdb (live)**      | `gdb.md`             |
| Crash already happened + board rebooted; intermittent/field fault | **coredump**        | `coredump.md`        |
| Logic bug not tied to real hardware (races, overruns, off-by-one) | **native_sim**      | `native_sim.md`      |
| ping6 fails, no/dropped packets, inspect or capture wire traffic  | **network**         | `network.md`         |
| Reset the board (reflash, openocd, software SYSRESETREQ)          | any                 | `reset.md`           |
| Synapse protocol handshake / blob validation                      | (separate skill)    | use `synapse-trace-debug` |

Start at the cheapest row that can answer your question. "Driver won't
init" → serial first (`device list`, boot log); only drop to gdb if the
shell is silent or the log stops at a fault. Two cheap escalations worth
remembering early: a memory/logic bug is usually fastest to corner on
**native_sim** with sanitizers (no hardware), and an intermittent crash is a
job for **coredump**, not for sitting attached with gdb.

**When it's not software at all.** If `i2c scan` is empty, GPIO reads look
wrong, or a bus misbehaves and the driver/DTS all check out, the bug is
electrical — stop debugging firmware and put a **logic analyzer or scope**
(Saleae, sigrok/PulseView) on the wire. An agent can't drive a probe, but
recognising the handoff saves hours of chasing a software ghost.

## How they relate

- **Serial** is the default and the hub — most playbooks live there, and the
  other transports borrow its read/drain/strip-ANSI pattern.
- **RTT** is the same logs and shell, just carried over the debug probe
  instead of a UART. Use it when the UART isn't available.
- **gdb** is the escalation when no shell output exists to read: faults,
  early hangs, deadlocks, single-stepping.
- **coredump** is gdb shifted in time — same backtrace/registers, but from a
  frozen crash captured earlier instead of a live halt.
- **native_sim** moves the whole thing onto the host: gdb + valgrind/ASAN +
  coverage + rr, for any bug that isn't hardware-timing-specific.
- **Network** combines on-device introspection (the `net` shell, driven like
  any serial command) with host-side packet capture.

## Detailed references

Load only the one the table points you at:

- **`serial.md`** — UART shell core pattern, the "is the board talking?"
  triage, and serial-specific traps. Routes onward to the serial deep-dives:
  - `reset.md` — every way to reset and find the boot boundary
  - `probing.md` — `device`/`kernel`/`gpio`/`input`/`i2c`/`flash`/`settings`
  - `troubleshooting.md` — silent-UART-but-alive, empty I2C scan, stalls,
    stack overflows
  - `pyserial-howto.md` — DTR-less open, paced pastes, monitoring, ANSI
- **`rtt.md`** — SEGGER RTT logs/shell over J-Link / openocd / pyOCD.
- **`gdb.md`** — `west debug`/`attach`, Zephyr thread awareness, Cortex-M
  fault decode (CFSR/BFAR), breakpoints, watchpoints, SVD register lookup.
- **`coredump.md`** — capture a crash and replay it into gdb post-mortem.
- **`native_sim.md`** — run firmware on the host: gdb, ASAN/UBSAN, valgrind,
  coverage, rr record/replay.
- **`network.md`** — `net` shell introspection + tcpdump/tshark/Wireshark
  capture, reading a pcap, link-local gotchas.

## Universal traps (every approach)

- **One owner of the debug probe.** openocd/gdb and RTT can't share the
  adapter; stop one before `west flash` or starting the other.
- **DTR is not a reset button** on STM32H745I-DISCO and bare USB-UART
  adapters — a capture starting mid-stream means DTR did nothing; reflash or
  use openocd (`reset.md`).
- **Validate a fresh boot** by the first timestamp: `[00:00:00.0xx]` is a
  real reset; anything later is mid-stream.
- **Capture loosely, filter post-hoc.** Aggressive live filters (serial
  monitor lines, tshark display filters) routinely eat the one error you
  needed. Log to a file, `grep` afterwards.
- **Run python in the project venv** (`mise x -- python3 ...`) or pyserial and
  friends won't import.

## Validation Checklist

- [ ] The symptom reproduces on demand *before* you change anything. A bug
      you can't trigger twice can't be shown fixed.
- [ ] The transport is proven alive before you conclude the board is dead:
      a prompt or `kernel version` response on serial/RTT. A silent UART is
      more often the console config than a hung CPU.
- [ ] The log you're reading is a fresh boot — first timestamp near
      `[00:00:00.0xx]`, not a mid-stream capture.
- [ ] A fault is decoded to a **source line** (gdb / `addr2line` against the
      matching `builds/<app>/zephyr/zephyr.elf`), not left as a register dump.
- [ ] Root cause named, and it explains every observed symptom — not just the
      first one. If part of the behaviour is still unexplained, keep going.
- [ ] After the fix: the original reproduction runs clean repeatedly, and any
      temporary debug config (log levels, `CONFIG_ASSERT`, sanitizers) is
      either removed or deliberately kept.
