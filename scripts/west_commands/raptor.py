# Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
#
# SPDX-License-Identifier: Apache-2.0

"""MCUboot signing keys and hawkBit publishing against a raptor server.

  west raptor keygen <app>    generate applications/<app>/keys/mcuboot.pem
                              (ECDSA-P256). `mise run app <app> --sysbuild`
                              signs with it when present.
  west raptor publish <app>   verify builds/<app>/<app>/zephyr/zephyr.signed.bin
                              against that key, then `raptorctl publish` it.
  west raptor <args...>       anything else is passed to raptorctl as is.

raptorctl must be on PATH and logged in (`west raptor login --url ...`).
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

from west.commands import WestCommand

OWN = ("keygen", "publish")


def _key(topdir: Path, app: str) -> Path:
    return topdir / "applications" / app / "keys" / "mcuboot.pem"


def _version(app_dir: Path) -> str:
    fields = dict(re.findall(r"^(\w+)\s*=\s*(\d*)", (app_dir / "VERSION").read_text(), re.M))
    return f"{fields['VERSION_MAJOR']}.{fields['VERSION_MINOR']}.{fields['PATCHLEVEL']}"


class Raptor(WestCommand):
    def __init__(self) -> None:
        super().__init__("raptor", "MCUboot keys and hawkBit publishing via raptorctl", __doc__ or "")

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name,
            help=self.help,
            description=self.description,
            formatter_class=argparse.RawDescriptionHelpFormatter,
        )
        parser.add_argument("cmd", help="keygen | publish | any raptorctl command")
        parser.add_argument("rest", nargs=argparse.REMAINDER, help="arguments for the command")
        return parser

    def do_run(self, args, unknown_args):
        if args.cmd not in OWN:
            return self._raptorctl([args.cmd, *args.rest])
        sub = argparse.ArgumentParser(prog=f"west raptor {args.cmd}")
        sub.add_argument("app")
        if args.cmd == "keygen":
            sub.add_argument("--force", action="store_true", help="replace an existing key")
            opts = sub.parse_args(args.rest)
        else:
            sub.add_argument("--version", help="default: VERSION + '+' + build ID prefix")
            # The rest (e.g. --ds-type, --vendor) goes to raptorctl publish.
            opts, extra = sub.parse_known_args(args.rest)
        topdir = Path(self.topdir)
        if not (topdir / "applications" / opts.app / "VERSION").is_file():
            self.die(f"unknown app {opts.app!r}: no applications/{opts.app}/VERSION")
        key = _key(topdir, opts.app)
        if args.cmd == "keygen":
            return self._keygen(key, opts.app, opts.force)
        return self._publish(topdir, opts.app, key, opts.version, extra)

    def _imgtool(self, *argv: str, check: bool = True) -> subprocess.CompletedProcess:
        mcuboot = Path(self.manifest.get_projects(["mcuboot"])[0].abspath)
        return subprocess.run([sys.executable, str(mcuboot / "scripts" / "imgtool.py"), *argv], check=check)

    def _raptorctl(self, argv: list[str]) -> int:
        if not shutil.which("raptorctl"):
            self.die("raptorctl not on PATH (see https://github.com/rosterloh/raptor/issues/199)")
        return subprocess.run(["raptorctl", *argv]).returncode

    def _keygen(self, key: Path, app: str, force: bool) -> int:
        if key.exists() and not force:
            # Losing this key strands every device whose MCUboot trusts it.
            self.die(f"{key} exists; --force replaces it and devices with the old key reject new images")
        key.parent.mkdir(parents=True, exist_ok=True)
        tmp = key.with_suffix(".tmp")
        self._imgtool("keygen", "-k", str(tmp), "-t", "ecdsa-p256")
        tmp.chmod(0o600)
        tmp.replace(key)
        self.inf(f"wrote {key} (private, gitignored). Back it up; rebuild with `mise run app {app} --sysbuild`,")
        self.inf("then reflash MCUboot over USB: devices on the old bootloader key reject images signed with it.")
        return 0

    def _publish(self, topdir: Path, app: str, key: Path, version: str | None, extra: list[str]) -> int:
        image = topdir / "builds" / app / app / "zephyr" / "zephyr.signed.bin"
        if not key.is_file():
            self.die(f"no key at {key}; run `west raptor keygen {app}` first")
        if not image.is_file():
            self.die(f"no {image}; build with `mise run app {app} --sysbuild`")
        # Rejects unsigned or other-key images, e.g. built before keygen.
        if self._imgtool("verify", "-k", str(key), str(image), check=False).returncode:
            self.die(f"{image} is not signed by {key}; rebuild with `mise run app {app} --sysbuild`")
        if not version:
            build_id = subprocess.run(
                [sys.executable, str(topdir / "scripts" / "build_id.py"), str(image)], capture_output=True, text=True
            ).stdout.strip()
            version = _version(topdir / "applications" / app) + (f"+{build_id[:12]}" if build_id else "")
        # application → DDI part bApp, the only one Zephyr's hawkBit client accepts (raptor#200).
        return self._raptorctl(
            ["publish", str(image), "--version", version, "--name", app, "--module-type", "application", *extra]
        )
