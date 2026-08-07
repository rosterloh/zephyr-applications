# Serial / UART shell debugging

The default way to talk to a running Zephyr board: its UART shell, prompt
`uart:~$`. Use this when the board boots far enough to bring the shell up.
If it doesn't, escalate to `gdb.md`; if the UART pins are unavailable but a
debug probe is attached, `rtt.md` gives you the same logs over SWD.

## Core idea

You have Python + `pyserial`. Open the device (default `/dev/ttyACM0`),
talk to the shell whose prompt is `uart:~$`. Pattern: drain stale data,
write a command + `\r`, read back the response, strip ANSI before matching.
**Run python inside the project venv** — `mise x -- python3 ...` from the
project root or pyserial won't import.

```python
import serial, time, re

def strip_ansi(b: bytes) -> str:
    return re.sub(r'\x1b\[[0-9;=?]*[mABCDEFGHJKLMST]', '',
                  b.decode('utf-8', errors='replace'))

with serial.Serial('/dev/ttyACM0', 115200, timeout=2) as s:
    time.sleep(0.5)
    s.read(s.in_waiting or 1)          # drain stale data
    def cmd(text, delay=0.5):
        s.write((text + '\r').encode()); time.sleep(delay)
        out = b''
        while s.in_waiting:
            out += s.read(s.in_waiting); time.sleep(0.05)
        return strip_ansi(out)
    print(cmd('device list'))
```

Ports vary: `/dev/ttyACM0`, `/dev/ttyUSB0`. Ask if unsure.

## First decision: is the board even talking?

Run this triage before reaching for tools. The answers route you to
different playbooks:

| Symptom                                          | Likely cause                                         | Where to look                     |
|--------------------------------------------------|------------------------------------------------------|-----------------------------------|
| `cat /dev/ttyACM0` is silent, USB shows STLINK   | UART driver crashed or a thread halted the system    | `troubleshooting.md`              |
| Silent on cat **but** `ping6 <device>` replies   | A thread faulted; CPU + net stack still alive        | `troubleshooting.md` then `gdb.md`|
| Boot lines appear then stop after a few seconds  | Normal — log dedup, or a periodic thread crashed     | `troubleshooting.md`              |
| Lines garbled or `Invalid type:` after a paste   | Shell TX echo backpressure dropping input bytes      | `pyserial-howto.md`               |
| Need a fresh-from-power-on boot log              | Reflash to reset; do not trust DTR                   | `reset.md`                        |
| Need to reset without reflashing                 | openocd DAP transport, or `devmem` SYSRESETREQ       | `reset.md`                        |
| No UART pins, but a debug probe is wired         | Use SEGGER RTT for logs/shell over SWD               | `rtt.md`                          |

**Always validate a fresh boot** by the first timestamp: `[00:00:00.0xx]`
is a real reset; anything later means you captured mid-stream.

## Serial traps

- **DTR is not a reset button.** On STM32H745I-DISCO and many bare USB-UART
  adapters, opening pyserial pulses DTR but it doesn't reach NRST. If your
  capture starts mid-stream, DTR did nothing — use reflash or openocd
  (`reset.md`).
- **Drain before every command.** Stale bytes in the read buffer corrupt
  command output. `s.read(s.in_waiting or 1)` is safe to call any time.
- **Strip ANSI** before regex/substring matching.
- **Don't over-filter inside a monitor loop.** Capture loosely to a file,
  `grep` post-hoc. Aggressive line filters routinely eat the one `<err>`
  line you needed.

## Detailed serial references

- **`reset.md`** — every way to reset the board (reflash, DTR, openocd
  HLA→DAP fallback, software SYSRESETREQ via `devmem`), and how to spot the
  boot boundary in the capture.
- **`probing.md`** — shell-command reference for `device`, `kernel`,
  `gpio`, `input`, `i2c` (scan/read/write/timing), `flash`, `settings`,
  and how to time an I2C transaction.
- **`troubleshooting.md`** — diagnostic playbooks: silent UART with CPU
  alive, I2C scan empty, driver READY but no events, boot stalls, thread
  stack overflows.
- **`pyserial-howto.md`** — DTR-less open, byte-paced long pastes, drain
  patterns, monitoring without over-filtering, ANSI escapes, debug.conf
  overlay.
