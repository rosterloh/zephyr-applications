#!/usr/bin/env python3
# Copyright (c) 2026 Richard Osterloh
# SPDX-License-Identifier: Apache-2.0
"""Print the GNU build ID found in a zephyr.elf, zephyr.bin or binary coredump.

Scans for the note header rather than parsing the file, so the same lookup
works on an ELF, a flash image and a dump pulled off a device (see
applications/common/build_id). Little-endian targets only.

  build_id.py FILE              print the ID as hex; exit 1 if none
  build_id.py FILE --note OUT   also write the raw note (header + ID) to OUT
"""

import argparse
import struct
import sys

# namesz=4, descsz=20 (SHA-1), type=NT_GNU_BUILD_ID, name="GNU\0"
HEADER = struct.pack("<III", 4, 20, 3) + b"GNU\0"


def find_note(data: bytes) -> bytes | None:
    at = data.find(HEADER)
    if at < 0 or at + len(HEADER) + 20 > len(data):
        return None
    return data[at : at + len(HEADER) + 20]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("file")
    parser.add_argument("--note", metavar="OUT", help="write the raw note here")
    args = parser.parse_args()

    with open(args.file, "rb") as f:
        note = find_note(f.read())
    if note is None:
        print(f"{args.file}: no GNU build ID", file=sys.stderr)
        return 1
    if args.note:
        with open(args.note, "wb") as f:
            f.write(note)
    print(note[len(HEADER) :].hex())
    return 0


if __name__ == "__main__":
    sys.exit(main())
