# Coredump (post-mortem debugging)

Debug a crash *after* the board has already rebooted. When a fault is
intermittent — happens once an hour, only under load, only in the field —
you can't sit attached with gdb waiting for it. Coredump captures
registers + RAM at the moment of `z_fatal_error` and lets you replay it
into gdb later. This is the escalation from `gdb.md` for crashes you can't
reproduce on demand.

## Firmware side

```
CONFIG_DEBUG_COREDUMP=y
CONFIG_DEBUG_COREDUMP_BACKEND_LOGGING=y    # dump to the log/console, or:
CONFIG_DEBUG_COREDUMP_BACKEND_FLASH_PARTITION=y   # survive reboot in flash
CONFIG_DEBUG_COREDUMP_MEMORY_DUMP_MIN=y    # just stacks+regs (small), or:
CONFIG_DEBUG_COREDUMP_MEMORY_DUMP_THREADS=y       # all thread stacks
# _MEMORY_DUMP_LINKER_RAM dumps all of RAM — biggest, most complete
```

- **Logging backend**: the dump is printed to the console as a base64/hex
  block bracketed by `#CD:BEGIN#` … `#CD:END#`. Capture the boot log the
  usual way (`serial.md`) and save the block.
- **Flash backend**: the dump persists across the reboot. Pull it later
  with the `coredump` shell command (`coredump find`, `coredump print`) or
  read the partition over SMP — handy for "it crashed overnight, what
  happened".

## Host side — replay into gdb

Zephyr ships the tooling under `scripts/coredump/`:

```
# 1. turn the captured log block into a coredump binary
uv run python $ZEPHYR_BASE/scripts/coredump/coredump_serial_log_parser.py \
    boot.log coredump.bin

# 2. start a gdbserver that speaks the coredump
uv run python $ZEPHYR_BASE/scripts/coredump/coredump_gdbserver.py \
    builds/<app>/zephyr/zephyr.elf coredump.bin

# 3. connect normal gdb to it
arm-none-eabi-gdb builds/<app>/zephyr/zephyr.elf
(gdb) target remote localhost:1234
(gdb) bt
(gdb) info registers
```

From here it's an ordinary gdb session — `bt`, `info threads` (if you dumped
all thread stacks), `frame`, `print` — except the target is the frozen
crash, not a live board. You can't continue/step (there's no CPU), but you
get the full backtrace and any RAM you dumped.

## Traps

- **Dump scope vs. usefulness.** `MEMORY_DUMP_MIN` gives you the faulting
  thread's backtrace and not much else; if `bt` bottoms out in garbage or
  you need another thread's state, you needed `_THREADS` or `_LINKER_RAM`.
  Bigger dumps cost flash/log time — start MIN, widen if it's not enough.
- **The ELF must match the firmware** that produced the dump exactly — same
  commit, same config. A stale ELF gives plausible-looking but wrong
  symbols. Keep the `zephyr.elf` for any build you might need to post-mortem.
- **Logging-backend dumps can be truncated** if the console drops bytes
  mid-dump (the same backpressure as any long output — see
  `pyserial-howto.md`). A parser error usually means a gap; recapture.
