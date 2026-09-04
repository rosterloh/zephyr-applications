# Troubleshooting playbooks

## UART silent, but the board is alive

Symptom: `cat /dev/ttyACM0` produces nothing. `lsusb` still shows the
STLINK. The serial port file exists. *But* `ping6 <device-ipv6>` replies
in under a millisecond.

**Read this first:** A live ping with no UART means a thread crashed
hard — the network ISR/RX path is still serviced, the shell + log threads
are not. This is *not* a USB/power problem, so re-plugging won't help.

What to do:

1. Don't trust DTR. Don't `setDTR(False)` — that doesn't reset this
   board.
2. Use `references/reset.md` method 2 (openocd DAP transport) or
   method 1 (reflash). If openocd gives `PARTNO 0x0`, switch to
   `stlink-dap.cfg` — that is specifically what unblocks dual-core STM32s
   like the H745.
3. After reset, capture from boot. The fault will reappear if it's
   deterministic — look for `MPU FAULT`, `Stack overflow`, `ZEPHYR FATAL
   ERROR`. Note which thread (`Current thread: 0x... (<name>)`) — that's
   the one whose stack you need to widen.

## UART silent and ping also fails

Real connectivity problem. Try in order:

1. `lsusb` — is the STLINK enumerated at all?
2. `ls /dev/ttyACM*` — is the CDC-ACM bridge enumerated?
3. Power-cycle the board (unplug USB). Sometimes the STLINK VCP firmware
   gets stuck and only USB re-enumeration recovers.
4. If USB is fine but the target is dead, your last firmware probably
   bricked something — reflash a known-good image.

## I2C scan returns 0 devices

Run the transaction-timing measurement from `references/probing.md`
first. The number tells you which branch:

### < 1 ms per read — the bus works, the device doesn't answer

- Wrong 7-bit address. Datasheets often quote the 8-bit *write* address;
  divide by 2 (e.g. `0x70 → 0x38`).
- Device powered down or in hibernate. Some touch controllers (FT5336,
  GT911) won't ACK until INT is pulsed low. A physical unplug clears
  this. A soft reset of the host generally *won't*.
- Address shifted (some sensors have a strap pin that selects between
  two 7-bit addresses).

### 10–500 ms per read — the peripheral is frozen

- Peripheral clock not enabled. Inspect the controller's `clocks`
  property in `builds/<app>/zephyr/zephyr.dts` and confirm it's on the
  bus you expect. STM32H7: offset `0xf4` = APB4 (where I2C4 lives), so
  the gate must be APB4ENR, not APB1.
- Missing or wrong `timings`/`clock-frequency` DTS property. Without a
  valid timing register the controller won't drive SCL.
- Enable `CONFIG_I2C_LOG_LEVEL_DBG=y` and watch boot for NACK / timing
  errors.

## Driver shows READY but no events

- Kconfig: is the interrupt source enabled?
  `CONFIG_INPUT_FT5336_INTERRUPT=y`, `CONFIG_INPUT_GPIO_KEYS=y`, etc. Polling
  variants need their own symbol.
- DTS: is the INT GPIO pulled up correctly? Open-drain INTs need
  `GPIO_PULL_UP` and the edge type must match (active-low INT →
  falling-edge or both-edges trigger).
- `CONFIG_INPUT_LOG_LEVEL_DBG=y` (or analogous per-subsystem) and watch
  for read errors in the ISR/poll callbacks.

## Boot stalls or drivers fail to init

Capture from boot (`references/reset.md`) and grep for `<err>` and
`<wrn>`. The usual suspects:

- **SoC Kconfig mismatch** — e.g. `CONFIG_SOC_STM32H745XX` vs the
  core-specific `CONFIG_SOC_STM32H745XX_M7`. Grep
  `find . -name '*.c' -path '*/drivers/*'` for the guard that's
  expected and confirm it's set.
- **Init-priority ordering** — if device B depends on device A but B's
  `DEVICE_DT_DEFINE(..., POST_KERNEL, ..., priority=...)` is *lower*
  than A's, B runs first and fails. Bump B's priority above A's.
- **Stack overflow during init** — Garbled output, unexpected resets
  shortly after a driver's init log. Widen `CONFIG_MAIN_STACK_SIZE` or
  the offending thread's stack. The system-thread stack
  (`CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE`) is a common culprit when work
  items grow.

## Catching a stack/heap problem *before* it faults

The sections above react to an overflow after it's already corrupted
something. Find it proactively instead:

- **Per-thread peak stack use.** Build with `CONFIG_THREAD_ANALYZER=y`
  (+ `CONFIG_THREAD_ANALYZER_USE_PRINTK=y`), then `kernel stacks` from the
  shell lists each thread's stack size and high-water mark. Any thread near
  100% is one bad path from an overflow — widen it now. `kernel threads`
  adds state + (with `CONFIG_THREAD_RUNTIME_STATS=y`) CPU usage per thread,
  which also catches a thread spinning when it should be idle.
- **Heap exhaustion.** A `k_malloc`/`sys_heap` returning NULL shows up as a
  driver that "sometimes" fails to init or a dropped allocation, not a
  fault. `CONFIG_SYS_HEAP_RUNTIME_STATS=y` + `kernel heap` (where wired up)
  reports used/free/max. For the network pool specifically, `net mem` /
  `net allocs` (see `network.md`).
- **Turn silent corruption into a clean fault.** `CONFIG_HW_STACK_PROTECTION=y`
  (MPU-backed) traps a stack overflow at the moment it happens with the
  faulting thread named, instead of letting it scribble on a neighbour and
  crash somewhere unrelated later. Cheap; leave it on in debug builds.

## "Boot logs stop after a couple seconds"

Usually not a crash — just log dedup or a thread that runs once at boot
and goes idle. Verify by:

1. `kernel uptime` from the shell — if it climbs, the kernel is fine.
2. `device list` — anything FAILED here would have logged at boot.
3. `log info` — check that the modules you expect output from aren't at
   level 0 (OFF).

If those are clean, the board is healthy and just quiet.

## Garbled output / `Invalid type:` after a `settings write`

The shell RX ring filled while echoing your paste, dropping a chunk of
input. The split bytes get re-parsed as a new command and trip
`Invalid type:` (because the orphaned suffix doesn't match expected
argv). Fix: byte-pace the paste — `references/pyserial-howto.md`.
