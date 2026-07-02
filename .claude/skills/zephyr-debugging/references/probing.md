# Shell command reference (probing)

These commands all run from `uart:~$`. They require the matching Kconfig
(`CONFIG_*_SHELL=y`) — see the bottom of the file for the common ones.

## Device + kernel

```
device list          # all devices with READY / FAILED status
kernel version       # Zephyr version
kernel uptime        # milliseconds since boot
kernel threads       # thread list + state (+ CPU% with THREAD_RUNTIME_STATS)
kernel stacks        # per-thread stack size + high-water (THREAD_ANALYZER)
log info             # registered log modules and their levels
log enable dbg <mod> # raise a module's level live — no reflash
```

`log enable`/`log disable` change verbosity at runtime, so you can crank a
suspect module to debug without rebuilding. `kernel stacks` is the proactive
stack-overflow check — see `troubleshooting.md`.

## I2C

```
i2c scan i2c@<addr>                       # probe 7-bit addresses 0x03–0x77
i2c read i2c@<addr> 0x38 0x00 1           # read 1 byte from reg 0x00 at 0x38
i2c write i2c@<addr> 0x38 0xA5 0x01       # write 0x01 to reg 0xA5
i2c speed i2c@<addr> 1                    # switch to FAST (400 kHz), re-probe
i2c recover i2c@<addr>                    # 9 SCL pulses to unstick SDA
```

Find the controller name from `device list` (look for `i2c@<address>`) or
from the generated DTS at `build/<app>/zephyr/zephyr.dts`.

### Measuring an I2C transaction

Time a single read from Python to distinguish "device absent" from
"peripheral frozen":

```python
t0 = time.monotonic()
s.write(b'i2c read i2c@58001c00 0x38 0x00 1\r')
out = b''
while True:
    ch = s.read(1)
    if ch: out += ch
    if b'uart:~$' in out and len(out) > 20: break
    if time.monotonic() - t0 > 5: break
print(f"{(time.monotonic()-t0)*1000:.1f}ms")
```

- **< 1 ms** → a real NACK. The device is absent or at a different
  address.
- **25–500 ms** → software timeout. The I2C peripheral is not generating
  clock — wrong timing register, peripheral clock not enabled, or bus
  hung.

This host-side number includes serial round-trip and is only good enough to
pick a branch. For **real on-device timing** (sub-µs, jitter, ISR latency)
measure with the DWT cycle counter in firmware — `uint32_t c =
k_cycle_get_32(); ...; uint32_t dt = k_cycle_get_32() - c;` then convert with
`sys_clock_hw_cycles_per_sec()`. For *scheduling*-level questions (who
preempted whom, ISR latency over time) that's a job for tracing — SEGGER
SystemView / Percepio Tracealyzer / Zephyr CTF via `CONFIG_TRACING`, viewed
in their GUI; not something to drive over the shell.

## GPIO

```
gpio get gpiog 2                          # read PG2 level
gpio conf gpiog 2 in                      # reconfigure as input
gpio conf gpiog 2 outh                    # output, drive high
gpio conf gpiog 2 outl                    # output, drive low
```

## Input subsystem

```
input report show                         # list event types (informational)
input report <type> <code> <value>        # inject a synthetic event
```

## Flash + settings

```
flash erase mt25ql512ab1 0 0x10000        # erase 64 KB from start
flash page_info mt25ql512ab1 0            # erase block size at offset 0
flash read mt25ql512ab1 0 64              # hex-dump 64 B from offset 0

settings list                             # list all settings keys
settings list ssh                         # list under 'ssh/' subtree
settings read hex <name>                  # hex-dump a value
settings write <name> <hex>               # write hex-encoded bytes
settings delete <name>                    # remove a key
```

For long-value writes (hex blobs over a few hundred bytes), see
`references/pyserial-howto.md` — the shell's RX ring needs paced input or
it drops bytes.

## Networking

```
net iface                                 # link state, IPv4/IPv6 addresses, DHCP
net stats                                 # per-protocol packet counters
net tcp                                   # active TCP connections
net dns                                   # DNS resolver state
```

`net iface` is the quickest way to confirm "do I have a global IPv6 yet?"
which most network services wait on.

## Kconfig requirements

Add these to `prj.conf` or a `debug.conf` overlay (see
`references/pyserial-howto.md`) for the commands above to exist:

```kconfig
CONFIG_SHELL=y
CONFIG_SHELL_BACKEND_SERIAL=y
CONFIG_DEVICE_SHELL=y        # `device` command
CONFIG_KERNEL_SHELL=y        # `kernel` command
CONFIG_I2C_SHELL=y
CONFIG_GPIO_SHELL=y
CONFIG_INPUT_SHELL=y
CONFIG_FLASH_SHELL=y
CONFIG_SETTINGS_SHELL=y
CONFIG_NET_SHELL=y
```
