#!/usr/bin/env python3
"""Report a build's linker memory usage against the last one recorded in --db.

Prints a Markdown table and rewrites --db with this build's numbers.
"""

import argparse
import json
import re
import sys
from pathlib import Path

UNITS = {"B": 1, "KB": 1024, "MB": 1024**2, "GB": 1024**3}
ROW = re.compile(r"^\s*(\S+):\s+(\d+) (B|KB|MB|GB)\s+(\d+) (B|KB|MB|GB)\s+[\d.]+%\s*$")
STEP = re.compile(r"Performing build step for '([^']+)'")
NEARLY_FULL = 0.95


def parse(text: str) -> dict[str, dict[str, list[int]]]:
    """Return {image: {region: [used, size]}}; non-sysbuild builds have one image, "app"."""
    images: dict[str, dict[str, list[int]]] = {}
    image = "app"
    in_table = False
    for line in text.splitlines():
        if step := STEP.search(line):
            image = step[1]
        if line.startswith("Memory region"):
            in_table = True
        elif in_table and (row := ROW.match(line)):
            # Empty regions like IDT_LIST are noise.
            if used := int(row[2]) * UNITS[row[3]]:
                images.setdefault(image, {})[row[1]] = [used, int(row[4]) * UNITS[row[5]]]
        else:
            in_table = False
    return images


def delta(used: int, old: list[int] | None) -> str:
    if old is None:
        return "new"
    return f"{used - old[0]:+,}" if used != old[0] else "0"


def render(app: str, new: dict, old: dict) -> str:
    lines = [f"### {app} footprint", "", "| Image | Region | Used (B) | Change | Full |", "|---|---|--:|--:|--:|"]
    for image, regions in new.items():
        for region, (used, size) in regions.items():
            # Tiny fixed-size regions like mcuboot_hdr are always full.
            mark = " ⚠️" if size >= 1024 and used / size >= NEARLY_FULL else ""
            change = delta(used, old.get(image, {}).get(region))
            lines.append(f"| {image} | {region} | {used:,} | {change} | {used / size:.1%}{mark} |")
    if not old:
        lines += ["", "No baseline from main yet."]
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--app", required=True)
    parser.add_argument("--db", type=Path, required=True)
    args = parser.parse_args()

    new = parse(args.log.read_text(errors="replace"))
    if not new:
        sys.exit(f"no linker memory table in {args.log}")
    old = json.loads(args.db.read_text()) if args.db.exists() else {}
    print(render(args.app, new, old))
    args.db.write_text(json.dumps(new))


if __name__ == "__main__":
    main()
