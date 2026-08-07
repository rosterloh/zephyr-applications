# Resetting the board

Use the cheapest method that actually works on the hardware in front of
you. The list is ordered roughly by speed and ordered by reliability
inversely (so default to the *last* one if you're not sure).

## Reflash to reset (most reliable, ~9 s)

`west flash` (and any wrapper mise task that calls it) drives NRST as part
of programming. Start the serial reader in the background, kick off the
flash, then collect the captured log. Cheap, deterministic, and gives you
"booted with the image I think it has."

```bash
mise x -- python3 -u <<'PY' > /tmp/serial.log 2>&1 &
import serial, time, re, sys
strip = lambda b: re.sub(r'\x1b\[[0-9;=?]*[mABCDEFGHJKLMST]', '',
                          b.decode('utf-8', errors='replace'))
s = serial.Serial('/dev/ttyACM0', 115200, timeout=2)
time.sleep(0.2); s.read(s.in_waiting or 1)
deadline = time.time() + 25
while time.time() < deadline:
    data = s.read(1024)
    if data:
        sys.stdout.write(strip(data)); sys.stdout.flush()
PY
sleep 1
mise run flash <app>            # or: mise x -- west flash -r openocd
wait
cat /tmp/serial.log
```

The default `west flash` runner is whatever was resolved at configure
time; on STM32 workspaces that often means `stm32cubeprogrammer` (rarely
installed). Pass `-r openocd` explicitly or use the project's mise task,
which usually pins openocd.

## openocd reset (no reflash, ~3 s)

The Zephyr SDK ships openocd; it is **not** on `PATH`:

```bash
SDK_OOCD=/home/rio/zephyr-sdk-1.0.1/hosttools/sysroots/x86_64-pokysdk-linux/usr/bin/openocd
$SDK_OOCD -s $(dirname $SDK_OOCD)/../share/openocd/scripts \
  -f interface/stlink.cfg -f target/stm32h7x.cfg \
  -c "init; reset halt; reset run; exit"
```

### When openocd reports `Cortex-M PARTNO 0x0 is unrecognized`

Switch the transport from HLA to DAP — change the interface config:

```bash
-f interface/stlink-dap.cfg
```

The HLA transport that plain `stlink.cfg` selects is unreliable on
multi-core STM32 SoCs (notably H745/H747); the DAP transport reads the
DPIDR correctly and supports `reset halt`. This fixed it for us when
nothing else would.

### openocd refuses to connect

`examination failed` or `Error: failed to halt` usually means another
openocd or a `west attach` session already owns the probe. Kill it:

```bash
pkill -f openocd
```

## Software SYSRESETREQ (no probe needed)

When openocd is busy, missing, or just slow, the running Zephyr shell can
trigger an architectural reset by writing to ARM SCB->AIRCR. From a python
session that already has a shell open:

```python
s.write(b'devmem 0xE000ED0C 32 0x05FA0004\r')
```

The key `0x05FA` plus bit 2 (SYSRESETREQ) trips the system reset on any
Cortex-M. `devmem` is a Zephyr shell built-in (`CONFIG_DEVMEM_SHELL`,
usually on by default). Don't expect a clean ACK — the board will start
re-booting before the response prints.

## DTR reset (cheap when wired right, but flaky)

```python
s.setDTR(False); time.sleep(0.1); s.setDTR(True)
out = b''
deadline = time.time() + 5
while time.time() < deadline:
    data = s.read(512)
    if data:
        out += data
        deadline = time.time() + 0.5
```

DTR-to-NRST is **not** guaranteed even on STLINK-V3. Confirmed silently
ineffective on STM32H745I-DISCO. Bare CH340/CP2102/FT232 adapters
usually don't bridge it either. Use this only when you've measured DTR
actually resetting *this specific* hardware. Otherwise reach for one of
the methods above.

## Spotting the boot boundary in a capture

A reader started before the reset trigger will catch some tail of the
previous boot. Identify the new boot by:

1. Timestamps wrapping back to `[00:00:00.0xx]` — anything `[00:NN:NN]`
   with a non-zero minute is mid-stream.
2. The `*** Booting Zephyr OS build vX.Y.Z ***` banner.

Everything before that boundary is stale and can be ignored.

## When pyserial opening itself resets the board

`serial.Serial(port, ...)` asserts DTR on open by default, which on some
bridges does pull the target into reset. If you don't want that, see
`references/pyserial-howto.md` for the no-touch open pattern.
