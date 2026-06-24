#!/usr/bin/env python3
"""Byte-swap RGB565 LVGL image data generated as a C source file."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


MAP_PATTERN = re.compile(
    r"(?s)(const\s+LV_ATTRIBUTE_MEM_ALIGN\s+LV_ATTRIBUTE_LARGE_CONST\s+"
    r"LV_ATTRIBUTE_IMAGE_DISP\s+uint8_t\s+disp_map\[\]\s*=\s*\{)(.*?)(\n\};)"
)
BYTE_PATTERN = re.compile(r"0x[0-9a-fA-F]{2}")


def swap_rgb565_bytes(source: str) -> str:
    match = MAP_PATTERN.search(source)
    if match is None:
        raise ValueError("disp_map array was not found")

    byte_values = [token.lower() for token in BYTE_PATTERN.findall(match.group(2))]
    if len(byte_values) % 2 != 0:
        raise ValueError(f"disp_map contains an odd byte count: {len(byte_values)}")

    for index in range(0, len(byte_values), 2):
        byte_values[index], byte_values[index + 1] = byte_values[index + 1], byte_values[index]

    lines = []
    for index in range(0, len(byte_values), 16):
        chunk = ", ".join(byte_values[index : index + 16])
        lines.append(f"  {chunk},")

    swapped_map = "\n" + "\n".join(lines)
    return source[: match.start(2)] + swapped_map + source[match.end(2) :]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate a byte-swapped RGB565 LVGL C image source."
    )
    parser.add_argument("input", type=Path, help="Original LVGL C image source, e.g. src/disp.c")
    parser.add_argument(
        "output",
        type=Path,
        nargs="?",
        help="Output path. Omit with --in-place to overwrite the input.",
    )
    parser.add_argument(
        "--in-place",
        action="store_true",
        help="Overwrite the input file instead of writing a separate output file.",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if args.in_place and args.output is not None:
        raise SystemExit("Do not pass an output path together with --in-place")
    if not args.in_place and args.output is None:
        raise SystemExit("Pass an output path, or use --in-place")

    source = args.input.read_text(encoding="utf-8")
    swapped = swap_rgb565_bytes(source)
    output = args.input if args.in_place else args.output
    output.write_text(swapped, encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
