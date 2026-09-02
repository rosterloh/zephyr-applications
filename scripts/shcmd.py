#!/usr/bin/env python3
"""Reset a board, capture its boot log, then run Zephyr shell commands.

Opening the CH343/CDC port asserts DTR/RTS, which resets the board, so the boot
log comes for free. --boot is how long to keep reading it before the first
command; data_collection needs ~40 s because CONFIG_NET_CONFIG_INIT_TIMEOUT
delays main().

    mise x -- python scripts/shcmd.py /dev/cu.usbmodemXXXX --boot 40 \
        'device list' 'video format csi_capture_port out'
"""

import argparse
import sys
import time

import serial


def drain(port, seconds, out):
    """Read whatever arrives for `seconds`, echoing it to `out`."""
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        chunk = port.read(4096)
        if chunk:
            out.write(chunk.decode("utf-8", "replace"))
            out.flush()
        else:
            time.sleep(0.02)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("commands", nargs="*")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--boot", type=float, default=10.0, help="seconds of boot log to capture")
    ap.add_argument("--wait", type=float, default=3.0, help="seconds to wait after each command")
    args = ap.parse_args()

    with serial.Serial(args.port, args.baud, timeout=0.1) as port:
        # Toggle DTR/RTS low->high to force a reset even if the port was
        # already open elsewhere; then discard the reset glitch bytes.
        port.dtr = False
        port.rts = False
        time.sleep(0.1)
        port.dtr = True
        port.rts = True
        time.sleep(0.1)
        port.reset_input_buffer()

        drain(port, args.boot, sys.stdout)

        for cmd in args.commands:
            print(f"\n===== {cmd} =====", flush=True)
            port.write((cmd + "\r\n").encode())
            drain(port, args.wait, sys.stdout)


if __name__ == "__main__":
    main()
