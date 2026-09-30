#!/usr/bin/env python3
"""Evaluate sparse, translation-invariant corrections for large repeatable errors."""

from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path


def pin(pin_text: str) -> tuple[int, int, str]:
    site, port = pin_text.rsplit("/", 1)
    _, x, y = site.split("_")
    return int(x), int(y), port


def score(golden: int, estimate: int) -> float:
    if golden == 0:
        return 1.0 if estimate == 0 else 0.0
    return 1.0 - math.tanh(4.0 * abs(estimate - golden) / golden)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--train", type=Path, required=True)
    parser.add_argument("--holdout", type=Path, required=True)
    parser.add_argument("--skip-train-rows", type=int, default=1_000_000)
    parser.add_argument("--threshold", type=int, default=50)
    args = parser.parse_args()

    # count, sum, minimum, maximum for large-error occurrences of each structural key.
    stats: dict[tuple[str, str, int, int], list[int]] = {}
    large_rows = train_rows = 0
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
            large_rows += 1

    configurations = (
        (3, 5),
        (5, 0),
        (5, 5),
        (5, 10),
        (7, 5),
        (10, 5),
        (10, 10),
        (15, 10),
        (20, 10),
    )
    rule_sets: list[dict[tuple[str, str, int, int], int]] = []
    for minimum_count, maximum_spread in configurations:
        rules = {
            key: round(value[1] / value[0])
            for key, value in stats.items()
            if value[0] >= minimum_count and value[3] - value[2] <= maximum_spread
        }
        rule_sets.append(rules)

    totals = [0.0] * (len(rule_sets) + 1)
    changed = [0] * len(rule_sets)
    holdout_rows = 0
    with args.holdout.open(newline="", encoding="utf-8") as stream:
        for row in csv.DictReader(stream):
            source_x, source_y, source_port = pin(row["From"])
            target_x, target_y, target_port = pin(row["To"])
            key = (source_port, target_port, target_x - source_x, target_y - source_y)
            golden = int(row["delay"])
            base = int(row["ans"])
            totals[0] += score(golden, base)
            for index, rules in enumerate(rule_sets):
                adjustment = rules.get(key, 0)
                changed[index] += adjustment != 0
                totals[index + 1] += score(golden, base + adjustment)
            holdout_rows += 1

    print(
        f"train_rows={train_rows} large_error_rows={large_rows} "
        f"candidate_keys={len(stats)} holdout_rows={holdout_rows}"
    )
    print(f"base accuracy={100.0 * totals[0] / holdout_rows:.6f}")
    for config, rules, changed_rows, total in zip(
        configurations, rule_sets, changed, totals[1:]
    ):
        print(
            f"min_count={config[0]} max_spread={config[1]} rules={len(rules)} "
            f"changed={changed_rows} accuracy={100.0 * total / holdout_rows:.6f}"
        )


if __name__ == "__main__":
    main()
