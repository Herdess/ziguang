#!/usr/bin/env python3
"""Generate compact, translation-invariant residual rules for the C++ solver."""

from __future__ import annotations

import argparse
import csv
import re
from pathlib import Path


def pin(pin_text: str) -> tuple[int, int, str]:
    site, port = pin_text.rsplit("/", 1)
    _, x, y = site.split("_")
    return int(x), int(y), port


def read_port_ids(header: Path) -> dict[str, int]:
    text = header.read_text(encoding="utf-8")
    match = re.search(r"kPortNames\s*=\s*\{\{(.*?)\}\};", text, re.S)
    if not match:
        raise RuntimeError("cannot locate kPortNames")
    names = re.findall(r'"([^"]+)"', match.group(1))
    return {name: index for index, name in enumerate(names)}


def packed_key(source: int, target: int, dx: int, dy: int) -> int:
    if not (0 <= source < 512 and 0 <= target < 512 and -128 <= dx < 128 and -1024 <= dy < 1024):
        raise ValueError("residual key is outside packed range")
    return source | (target << 9) | ((dx + 128) << 18) | ((dy + 1024) << 26)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--train", type=Path, required=True)
    parser.add_argument("--arch-header", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--skip-train-rows", type=int, default=1_000_000)
    parser.add_argument("--threshold", type=int, default=10)
    parser.add_argument("--minimum-count", type=int, default=5)
    parser.add_argument("--maximum-spread", type=int, default=10)
    args = parser.parse_args()

    ports = read_port_ids(args.arch_header)
    stats: dict[tuple[str, str, int, int], list[int]] = {}
    train_rows = 0
    with args.train.open(newline="", encoding="utf-8") as stream:
        for index, row in enumerate(csv.DictReader(stream)):
            if index < args.skip_train_rows:
                continue
            train_rows += 1
            residual = int(row["delay"]) - int(row["ans"])
            if abs(residual) < args.threshold:
                continue
            source_x, source_y, source_port = pin(row["From"])
            target_x, target_y, target_port = pin(row["To"])
            key = (source_port, target_port, target_x - source_x, target_y - source_y)
            value = stats.get(key)
            if value is None:
                stats[key] = [1, residual, residual, residual]
            else:
                value[0] += 1
                value[1] += residual
                value[2] = min(value[2], residual)
                value[3] = max(value[3], residual)

    rules = []
    for (source, target, dx, dy), (count, total, minimum, maximum) in stats.items():
        if count < args.minimum_count or maximum - minimum > args.maximum_spread:
            continue
        adjustment = round(total / count)
        rules.append((packed_key(ports[source], ports[target], dx, dy), adjustment))
    rules.sort()

    lines = [
        "#pragma once",
        "",
        "#include <array>",
        "#include <cstdint>",
        "",
        "namespace solver::generated {",
        "",
        "struct ResidualRule { uint64_t key; int16_t adjustment; };",
        f"inline constexpr std::array<ResidualRule, {len(rules)}> kResidualRules = {{{{",
    ]
    lines.extend(f"    {{{key}ULL, {adjustment}}}," for key, adjustment in rules)
    lines.extend([
        "}};",
        "",
        "}  // namespace solver::generated",
        "",
    ])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines), encoding="utf-8")
    print(f"train_rows={train_rows} candidate_keys={len(stats)} emitted_rules={len(rules)}")


if __name__ == "__main__":
    main()
