# native_sim (debug on the host, no hardware)

Build the firmware as an ordinary Linux executable and run it as a process.
This is the highest-leverage debugging move for **logic** bugs — anything
that isn't tied to real hardware timing or a specific peripheral — because
the whole host toolchain comes for free: plain gdb, valgrind, the
sanitizers, coverage, and rr record/replay. Most off-by-ones, races on
shared state, buffer overruns, and use-after-frees that take hours to corner
on-target reproduce here in seconds with a sanitizer pointing right at the
line.

When it **won't** help: bugs that depend on real peripheral behaviour, ISR
timing, clock rates, or a sensor's actual responses. native_sim stubs or
simulates those. Use it for the firmware's logic, not its hardware coupling.

## Build & run

```
uv run west build -b native_sim -p always -d build/<app> applications/<app>
./build/<app>/zephyr/zephyr.exe            # runs as a normal process
./build/<app>/zephyr/zephyr.exe --help     # native_sim runtime options
```

Networking works via a host tap interface (`zeth`, set up by Zephyr's
net-tools) — see `network.md` for capturing it. A UART maps to a PTY or
stdio, so the shell-driving pattern from `serial.md` works against the
process too.

## gdb — just attach, no probe

```
gdb ./build/<app>/zephyr/zephyr.exe
(gdb) break my_buggy_function
(gdb) run
(gdb) bt / print / step
```
No openocd, no flashing, no `west attach`. Breakpoints and watchpoints are
software/host — fast and unlimited, unlike the handful of hardware
watchpoints on a Cortex-M.

## Sanitizers — the reason to bother

```
uv run west build -b native_sim -d build/<app> applications/<app> -- \
    -DCONFIG_ASAN=y -DCONFIG_UBSAN=y
./build/<app>/zephyr/zephyr.exe
```
- **ASAN** catches buffer overflows / use-after-free with the exact
  offending stack at the moment of the bad access — the thing that on a
  Cortex-M only shows up as a much-later HardFault with a useless backtrace.
- **UBSAN** flags integer overflow, misaligned access, bad shifts.
- `valgrind ./zephyr.exe` is an alternative when you can't rebuild with
  ASAN, but it's slower and less precise on a freestanding-style binary.

## Coverage & reproducibility

```
# coverage: which lines a test/run actually exercised
uv run west build -b native_sim ... -- -DCONFIG_COVERAGE=y
./zephyr.exe && gcovr -r .            # or lcov/genhtml
# twister runs this for you:
uv run west twister -T tests --coverage -p native_sim

# rr — record once, replay deterministically backwards/forwards
rr record ./build/<app>/zephyr/zephyr.exe
rr replay        # reverse-continue to the instruction that corrupted state
```
`rr` is the closer for "I can't catch the moment it goes wrong" — record the
failing run once, then step *backwards* from the symptom to the cause.

## Traps

- **It's not your target.** A bug that only repros on native_sim (or only
  on hardware) is itself a clue — usually an `#ifdef`, a timing assumption,
  or uninitialised memory the host happens to zero. Don't "fix" a
  native_sim-only symptom without checking it exists on real hardware.
- **64-bit host vs 32-bit target.** Pointer-size and `long`-size
  assumptions can hide or appear here; `native_sim/native/64` vs the default
  32-bit variant matters for those.
- **No real time.** `k_sleep` is fast-forwarded; anything timing-dependent
  behaves differently than on-target.
