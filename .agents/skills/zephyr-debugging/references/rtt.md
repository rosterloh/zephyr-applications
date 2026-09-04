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

### ESP32 over the built-in USB-JTAG — use probe-rs (verified on macOS)

ESP32-C3/S3 boards expose a USB-JTAG probe (VID:PID `303a:1001`) with no
external hardware needed. `probe-rs list` should show `ESP JTAG`.

Getting RTT out of them needs two non-obvious steps:

1. **Force `HAS_SEGGER_RTT`.** ESP32 SoCs don't advertise it, so
   `CONFIG_USE_SEGGER_RTT=y` alone won't configure. Add a `select HAS_SEGGER_RTT`
   in the application's own `Kconfig`, then the usual
   `CONFIG_USE_SEGGER_RTT=y` + `CONFIG_LOG_BACKEND_RTT=y`.
2. **Attach without resetting**, which is the whole point when you're chasing a
   bug that only appears after a connection or pairing completes:

```bash
probe-rs attach --chip esp32s3 --non-interactive builds/<app>/zephyr/zephyr.elf
```

`--non-interactive` is required when backgrounding it, otherwise it dies with
"Failed to create readline".

Halt and backtrace:

```bash
probe-rs gdb --chip esp32s3 --gdb-connection-string 127.0.0.1:1337
# then, in xtensa-esp32s3-elf-gdb:  target remote :1337
```

Generic OpenOCD (SDK or homebrew 0.12.0) does connect the JTAG
(`esp_usb_jtag` + `caps_descriptor 0x2000`) but **lacks the `rtt setup` / `rtt
start` commands**, so it cannot do RTT — use probe-rs unless you install
Espressif's `openocd-esp32` fork. probe-rs handles both the C3 (RISC-V) and S3
(Xtensa).

## Traps

- **RTT is polled, not interrupt-driven on the host.** Very bursty logs can
  overflow the up-buffer before the host reads it — bump
  `CONFIG_SEGGER_RTT_BUFFER_SIZE_UP` if you see gaps.
- **The control block lives in RAM**, so a full chip-erase reflash wipes it
  until the new image re-inits RTT. Capture after boot, not across reset.
- **Only one host tool at a time** owns the probe. Close openocd before
  running `west flash`, and vice versa.
- **Opening the ESP32 USB-JTAG console can reset the chip.** On the C3 it resets
  when pyserial asserts DTR/RTS, so set `dtr=False; rts=False` *before*
  `.open()`. On the **S3 it resets on open regardless** of those flags — use a
  single open and never reconnect. A monitor with a reconnect loop will
  re-reset the chip and drop the BLE connection you were trying to observe.
- **Bursty protocol traces need bigger buffers.** An SMP/pairing burst drops
  messages unless both `CONFIG_LOG_BUFFER_SIZE` and
  `CONFIG_SEGGER_RTT_BUFFER_SIZE_UP` are raised. Prefer deferred logging;
  `CONFIG_LOG_MODE_IMMEDIATE=y` with verbose BT logging can overflow
  `CONFIG_BT_LONG_WQ_STACK_SIZE` and crash at boot instead.
