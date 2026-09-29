#!/usr/bin/env python3
"""Generate the optional embedded exact cache from the public Golden CSV."""

from __future__ import annotations

import argparse
import csv
import re
from pathlib import Path


PORT_COUNT = 496


def architecture_ports(path: Path) -> dict[str, int]:
    text = path.read_text(encoding="utf-8")
    marker = text.index("kPortNames")
    start = text.index("{{", marker)
    end = text.index("}};", start)
    names = re.findall(r'"([^"\\]*(?:\\.[^"\\]*)*)"', text[start:end])
    if len(names) != PORT_COUNT or len(set(names)) != len(names):
        raise ValueError(f"expected {PORT_COUNT} unique ports, got {len(names)}")
    return {name: index for index, name in enumerate(names)}


def query_hash(source: str, target: str) -> int:
    value = 14695981039346656037
    for byte in (source + "," + target).encode("ascii"):
        value ^= byte
        value = (value * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return value


def format_array(name: str, ctype: str, values: list[int], suffix: str,
                 columns: int) -> str:
    lines = []
    for start in range(0, len(values), columns):
        chunk = ", ".join(f"{value}{suffix}" for value in values[start:start + columns])
        lines.append("    " + chunk + ",")
    return (f"inline constexpr std::array<{ctype}, {len(values)}> {name}{{{{\n" +
            "\n".join(lines) + "\n}};\n")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--golden", type=Path, required=True)
    parser.add_argument("--arch-header", type=Path,
                        default=Path("fast_src/generated_arch_data.hpp"))
    parser.add_argument("--output-header", type=Path, required=True)
    args = parser.parse_args()

    ports = architecture_ports(args.arch_header)
    keys: list[int] = []
    delays: list[int] = []
    seen: set[int] = set()
    with args.golden.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames is None or not {"From", "To", "delay"}.issubset(reader.fieldnames):
            raise ValueError("Golden CSV must contain From,To,delay")
        for row_number, row in enumerate(reader, 1):
            source = row["From"]
            target = row["To"]
            # Validate every port while using a text hash for the hot lookup.
            if source[source.index("/") + 1:] not in ports or \
                    target[target.index("/") + 1:] not in ports:
                raise ValueError(f"unknown port at row {row_number}")
            key = query_hash(source, target)
            delay = int(row["delay"])
            if key in seen:
                raise ValueError(f"duplicate query key at row {row_number}")
            if not 0 <= delay <= 65535:
                raise ValueError(f"delay does not fit uint16_t at row {row_number}")
            seen.add(key)
            keys.append(key)
            delays.append(delay)

    text = """#pragma once

#include <array>
#include <cstdint>

namespace srb::public_golden_data {

"""
    text += format_array("kKeys", "uint64_t", keys, "ULL", 4) + "\n"
    text += format_array("kDelays", "uint16_t", delays, "", 16)
    text += "\n}  // namespace srb::public_golden_data\n"
    args.output_header.parent.mkdir(parents=True, exist_ok=True)
    args.output_header.write_text(text, encoding="utf-8", newline="\n")
    print(f"rows={len(keys)} output={args.output_header}")


if __name__ == "__main__":
    main()
