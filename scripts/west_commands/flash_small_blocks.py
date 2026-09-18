# Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
#
# SPDX-License-Identifier: Apache-2.0

"""Flash an Espressif image using a reduced write-block size.

The CH343 USB-UART bridge on these boards corrupts host->device bulk writes at
esptool's default 1024-byte flash block: short command packets and every read
are clean, so chip detection, MAC reads and erase all succeed, while the stub
upload fails its RAM checksum and a plain `west flash` dies with

    Failed to write to target flash after seq 0 (result was 0105: ...)

Dropping the block size below the corruption threshold gets the image through.
It is slow (~70 kbit/s for a 256-byte block) but reliable, and it is the only
path that works on a CH343 board, so the address and image are read from the
build directory rather than retyped: an ESP32 ROM loads its image header from
0x1000, not 0x0, and getting that wrong bricks the boot in a way that looks
like a corrupt flash.
"""

from pathlib import Path

import yaml
from west.commands import WestCommand

DEFAULT_BLOCK = 256


class FlashSmallBlocks(WestCommand):
    def __init__(self):
        super().__init__(
            "flash-small-blocks",
            "flash an Espressif image in small blocks (CH343 bridge workaround)",
            self.__doc__,
        )

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(self.name, help=self.help, description=self.description)
        parser.add_argument("-d", "--build-dir", default="build", help="build directory (default: build)")
        parser.add_argument(
            "-b",
            "--block-size",
            type=int,
            default=DEFAULT_BLOCK,
            help=f"flash/RAM write block in bytes (default: {DEFAULT_BLOCK}); halve it if the write still fails",
        )
        parser.add_argument("-p", "--port", help="serial port (default: let esptool autodetect)")
        parser.add_argument("--baud", default="115200", help="baud rate (default: 115200)")
        parser.add_argument(
            "--no-reset",
            action="store_true",
            help="stay in the bootloader instead of booting the new image; use it when the "
            "application energises actuators at boot, so the operator picks the moment",
        )
        return parser

    def do_run(self, args, _unknown):
        build_dir = Path(args.build_dir)
        runners = yaml.safe_load((build_dir / "zephyr" / "runners.yaml").read_text())

        bin_file = build_dir / "zephyr" / runners["config"]["bin_file"]
        address = self._app_address(runners)

        # Applied to the class before esptool builds a loader, because both the
        # stub upload (RAM) and the flash write inherit these at connect time.
        import esptool
        from esptool.loader import ESPLoader

        ESPLoader.FLASH_WRITE_SIZE = args.block_size
        ESPLoader.ESP_RAM_BLOCK = args.block_size

        self.inf(f"flashing {bin_file} at {address} in {args.block_size}-byte blocks")

        argv = ["--baud", args.baud, "--no-stub", "--before", "default-reset"]
        if args.port:
            argv += ["--port", args.port]
        argv += ["--after", "no-reset" if args.no_reset else "hard-reset"]
        argv += ["write-flash", "-u", "--flash-size", "detect", address, str(bin_file)]

        esptool.main(argv)

    @staticmethod
    def _app_address(runners):
        """Read the image offset the esp32 runner would have used."""
        for arg in runners.get("args", {}).get("esp32", []):
            if arg.startswith("--esp-app-address="):
                return arg.split("=", 1)[1]
        raise RuntimeError("no --esp-app-address in runners.yaml; is this an Espressif build?")
