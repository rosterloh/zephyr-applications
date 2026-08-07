# gdb (source-level debugging & fault decode)

Escalate to gdb when the shell can't answer the question: a thread faulted,
the system hangs before the shell comes up, you need to step through code,
or you have a fault dump with raw register values and no symbolised
backtrace. gdb attaches through the same debug probe as RTT.

> If the crash already happened and the board rebooted — i.e. you *can't*
> halt it live — you want post-mortem instead: see `coredump.md`, which
> replays a captured crash into this same gdb.
> If the bug isn't hardware-specific, debugging on the host with native_sim
> (`native_sim.md`) is far faster — plain gdb, unlimited breakpoints, plus
> sanitizers that pinpoint memory bugs a Cortex-M only shows as a late fault.

## Starting a session

west wraps the probe + gdb for you:
```
mise x -- west debug   -d builds/<app>     # flash, reset, attach, halt at main
mise x -- west attach  -d builds/<app>     # attach to a *running* target, no reset
mise x -- west debugserver -d builds/<app> # just the gdbserver; connect gdb yourself
```
`west attach` is the one you usually want when chasing a live hang — it
doesn't reset, so you catch the system in the bad state.

Manual path (when west's runner doesn't fit, e.g. a custom openocd cfg):
```
openocd -f <board.cfg>                          # terminal 1, gdbserver on :3333
arm-none-eabi-gdb builds/<app>/zephyr/zephyr.elf # terminal 2
(gdb) target extended-remote localhost:3333
```
The ELF is `builds/<app>/zephyr/zephyr.elf` — load it so gdb has symbols.

> **ESP32-S3** uses `openocd-esp32` + `xtensa-esp32s3-elf-gdb`; `west debug`
> selects the right pair automatically. The Zephyr-awareness and fault
> sections below are Cortex-M specific.

## Zephyr thread awareness

openocd has a Zephyr RTOS layer that lists threads as gdb "processes". Build
with:
```
CONFIG_DEBUG_THREAD_INFO=y     # exposes the kernel thread list to the debugger
```
Then:
```
(gdb) info threads             # all Zephyr threads + their backtraces
(gdb) thread <n>               # switch context
(gdb) bt                       # backtrace of the selected thread
```
Without `CONFIG_DEBUG_THREAD_INFO` you only ever see the CPU's current
stack, which is useless for "which thread deadlocked".

## Decoding a fault (Cortex-M)

When you get a `>>> ZEPHYR FATAL ERROR` dump but no symbols, build with:
```
CONFIG_EXTRA_EXCEPTION_INFO=y  # dumps callee regs + fault status
CONFIG_DEBUG=y                 # -g, keeps symbols
```
The dump prints faulting PC/LR and the CFSR/HFSR fault-status bits. To turn
an address into a line without a live target:
```
arm-none-eabi-addr2line -e builds/<app>/zephyr/zephyr.elf 0x<PC>
```
Live, halted at the fault, read the System Control Block fault registers:
```
(gdb) x/1xw 0xE000ED28          # CFSR  — Configurable Fault Status
(gdb) x/1xw 0xE000ED2C          # HFSR  — HardFault Status
(gdb) x/1xw 0xE000ED34          # MMFAR — faulting data address (if MMARVALID)
(gdb) x/1xw 0xE000ED38          # BFAR  — bus fault address  (if BFARVALID)
```
A set `PRECISERR` (CFSR bit 1) + valid BFAR points straight at the bad
dereference.

## Useful breakpoints

```
(gdb) break k_panic
(gdb) break z_fatal_error        # catch every fatal before it reboots
(gdb) break sys_reboot
(gdb) monitor reset halt         # openocd: reset and stop at the vector table
```

## Watchpoints — "who corrupted this?"

When a variable changes to garbage and you don't know who wrote it, a
hardware data watchpoint catches the write in the act:
```
(gdb) watch my_global            # break when the value changes
(gdb) watch *(uint32_t*)0x20001234   # or a raw address
(gdb) rwatch my_global           # break on read
```
Cortex-M has only a handful of hardware watchpoints (DWT comparators, often
4) — gdb errors if you ask for more. Scope tightly. On native_sim
(`native_sim.md`) watchpoints are software and effectively unlimited.

## Peripheral registers by name (SVD)

The raw addresses above (`0xE000ED28` etc.) are fine for the core SCB, but
for SoC peripherals load the chip's SVD so gdb shows registers by name and
decodes bitfields. With PyCortexMDebug (`pip install pycortexmdebug`):
```
(gdb) source /path/to/PyCortexMDebug/scripts/gdb.py
(gdb) svd_load STM32H563.svd
(gdb) svd I2C1                   # all I2C1 registers, decoded
(gdb) svd I2C1 ISR              # one register, bitfields broken out
```
Vendor SVDs ship with the STM32Cube packs / cmsis-svd. This turns "is the
I2C peripheral clock-gated?" from address arithmetic into a named lookup —
pairs well with the I2C-frozen branch in `troubleshooting.md`.

## Traps

- **One owner of the probe.** Stop openocd/gdb before `west flash`; the RTT
  tools and gdb can't share the adapter simultaneously.
- **Optimised builds lie.** `-Os` inlines and reorders; `<optimized out>`
  locals are normal. Build the suspect file with `CONFIG_NO_OPTIMIZATIONS=y`
  (or `-O0` per-file) when you need to step cleanly.
- **`west attach` may halt a thread mid-critical-section** — resuming is
  usually fine, but don't `monitor reset` if you wanted the live state.
