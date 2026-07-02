# RTT (SEGGER Real-Time Transfer)

RTT carries logs and an interactive shell over the debug probe (SWD/JTAG)
instead of a UART. Reach for it when:

- the board has no spare UART, or the UART pins are used by the application;
- you need logging that survives `printk`-blocking contexts or runs faster
  than 115200 baud;
- the UART shell is dead but the CPU is alive and a probe is attached
  (often faster to inspect than spinning up gdb).

It needs a debug probe. J-Link is native; ST-LINK and CMSIS-DAP work via
openocd or pyOCD, which implement RTT on top of memory reads.

## Firmware side (Kconfig)

```
CONFIG_USE_SEGGER_RTT=y          # the RTT control block + up/down buffers
CONFIG_RTT_CONSOLE=y             # console (printk) over RTT, or:
CONFIG_LOG_BACKEND_RTT=y         # logging subsystem backend over RTT
CONFIG_SHELL_BACKEND_RTT=y       # interactive shell over RTT (channel 0)
# if you want RTT shell *and* keep the UART shell, both backends coexist
```

Drop these in a `boards/<board>_debug.conf` overlay (mirror the existing
`debug.conf` pattern) so the production config stays clean.

## Host side

### J-Link
```
JLinkRTTLogger -Device <chip> -If SWD -Speed 4000 -RTTChannel 0 rtt.log
# or the GUI: JLinkRTTViewer
```

### openocd (ST-LINK / CMSIS-DAP) — what these boards use
RTT works by polling the target's RTT control block in RAM. Start openocd,
then in its telnet console (`telnet localhost 4444`) or via a config:
```
rtt setup 0x20000000 0x40000 "SEGGER RTT"   # search RAM region for the block
rtt start
rtt server start 9090 0                       # expose RTT channel 0 on TCP 9090
```
Then read the stream:
```
nc localhost 9090            # logs / shell I/O
```
The shell is bidirectional over that socket — you can drive it the same way
`serial.md` drives the UART (drain, write `cmd\r`, read, strip ANSI), just
point pyserial-style logic at the socket instead of `/dev/ttyACM0`.

`rtt setup` needs a RAM range that contains the control block. If `rtt
start` can't find the `SEGGER RTT` marker, widen the range or confirm
`CONFIG_USE_SEGGER_RTT=y` actually linked it in (`nm zephyr.elf | grep -i
_SEGGER_RTT`).

### pyOCD
```
pyocd rtt -t <target>
```

## Traps

- **RTT is polled, not interrupt-driven on the host.** Very bursty logs can
  overflow the up-buffer before the host reads it — bump
  `CONFIG_SEGGER_RTT_BUFFER_SIZE_UP` if you see gaps.
- **The control block lives in RAM**, so a full chip-erase reflash wipes it
  until the new image re-inits RTT. Capture after boot, not across reset.
- **Only one host tool at a time** owns the probe. Close openocd before
  running `west flash`, and vice versa.
