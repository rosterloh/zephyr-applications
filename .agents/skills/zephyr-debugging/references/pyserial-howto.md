# pyserial: opening, pacing, capturing

Practical patterns for the things that bite repeatedly.

## Opening without touching DTR/RTS

Default `serial.Serial(port, baud)` asserts DTR on open, which on some
USB-CDC bridges (most notably STM32H745I-DISCO) pulses NRST and resets
the target — turning every "let me check the shell" into a fresh boot
sequence. Worse, sometimes it leaves the board in a confused state where
UART output stops entirely while the CPU keeps running.

`dsrdtr=False, rtscts=False` is **not** enough — those flags only disable
flow control, they don't deassert DTR. The proper no-touch open:

```python
s = serial.Serial()
s.port = "/dev/ttyACM0"
s.baudrate = 115200
s.timeout = 0.5
s.dtr = False
s.rts = False
s.open()
```

If even this doesn't help and the board still reboots when you connect,
the bridge firmware probably ties RTS or some other modem-status line to
NRST; there's no software workaround for that — use openocd to manage
resets instead (`references/reset.md`).

## Byte-paced long pastes

For shell commands over ~256 chars (typically `settings write <hex
blob>`), the shell's TX echo backpressures the RX ring and drops input.
The symptom is `Invalid type:` or weirdly-parsed argv because the dropped
chars split a single arg into multiple. Fix: write in 16-byte chunks with
a small delay between:

```python
def send_paced(s, line, chunk=16, delay=0.04):
    data = (line + "\r").encode()
    for i in range(0, len(data), chunk):
        s.write(data[i : i + chunk])
        s.flush()
        time.sleep(delay)
    # collect 4 s of response
    end = time.time() + 4.0
    out = b""
    while time.time() < end:
        chunk_read = s.read(4096)
        if chunk_read:
            out += chunk_read
    return out
```

Increase `delay` to 0.08–0.1 if the device echoes verbosely (debug log
levels turned up).

If you have control over the firmware, bumping
`CONFIG_SHELL_BACKEND_SERIAL_RX_RING_BUFFER_SIZE` to 2048+ helps a lot;
the default of 64 is way too small for any kind of paste.

## Drain reliably before sending

Stale bytes in the kernel's TTY buffer corrupt your command output. Drain
both pyserial's read queue and the in_waiting count:

```python
s.read(s.in_waiting or 1)  # safe even when nothing is queued
```

Call this at start, and again before any command where output ordering
matters.

## Strip ANSI before matching

The Zephyr shell emits color codes and cursor control sequences that
break naive `in` / regex matching. Strip with:

```python
import re

strip_ansi = lambda b: re.sub(r"\x1b\[[0-9;=?]*[mABCDEFGHJKLMST]", "", b.decode("utf-8", errors="replace"))
```

Run on every read before saving or comparing.

## Capturing logs without over-filtering

Aggressive in-loop filters routinely eat the one `<err>` line you needed.
Rule of thumb: filter only known-noisy periodic logs (BMS-style state
spam, flash enumeration), and write everything else to a file. Then
`grep` post-hoc:

```python
# in the capture loop, only drop *known* noise
if "bms_service" in line and "soc=" in line:
    continue
if "flash_stm32_qspi" in line:
    continue
sys.stdout.write(line)
```

```bash
# afterwards, search broadly
grep -iE 'err|wrn|fail|fault|stack|halt|panic' /tmp/serial.log
```

If you're using the `Monitor` tool, this matters extra: the filter runs
inside a long-running loop, so the loop becomes the only record of those
log lines. Save raw, then narrow.

## Project venv

`import serial` only works inside the project venv. If
`mise x -- python3 -c "import serial"` fails with `ModuleNotFoundError`,
you're invoking python from outside the project root:

```bash
cd /path/to/project        # IMPORTANT
mise x -- python3 ...
```

`mise x -- west <command>` has the same constraint — west extensions, the
build, and any pyserial use all rely on the same `.venv/`.

## Debug build overlays

To enable extra shell commands and log levels without touching the
production `prj.conf`, drop a `debug.conf` next to it:

```kconfig
CONFIG_SHELL=y
CONFIG_SHELL_BACKEND_SERIAL=y
CONFIG_SHELL_BACKEND_SERIAL_RX_RING_BUFFER_SIZE=2048
CONFIG_I2C_SHELL=y
CONFIG_INPUT_SHELL=y
CONFIG_FLASH_SHELL=y
CONFIG_SETTINGS_SHELL=y
CONFIG_NET_SHELL=y
CONFIG_I2C_LOG_LEVEL_DBG=y
```

Build with:

```bash
mise x -- west build ... --extra-conf debug.conf
```
