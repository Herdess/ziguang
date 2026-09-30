#!/usr/bin/env python3
"""Fit lightweight residual corrections on extra Golden rows and test on a holdout."""

from __future__ import annotations

import argparse
import csv
import math
from collections import defaultdict
from pathlib import Path


Stats = list[int]


def port_name(pin: str) -> str:
    return pin.rsplit("/", 1)[1]


def add(stats: dict[object, Stats], key: object, residual: int) -> None:
    value = stats[key]
    value[0] += 1
    value[1] += residual


def correction(stats: dict[object, Stats], key: object, prior: float, strength: int) -> int:
    value = stats.get(key)
    if value is None:
        return round(prior)
    count, total = value
    return round((total + strength * prior) / (count + strength))


def point(golden: int, estimate: int) -> float:
    if golden == 0:
        return 1.0 if estimate == 0 else 0.0
    return 1.0 - math.tanh(4.0 * abs(estimate - golden) / golden)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--train", type=Path, required=True)
    parser.add_argument("--holdout", type=Path, required=True)
    parser.add_argument("--skip-train-rows", type=int, default=1_000_000)
    parser.add_argument("--pair-strength", type=int, default=32)
    args = parser.parse_args()

    pair_stats: dict[object, Stats] = defaultdict(lambda: [0, 0])
    source_stats: dict[object, Stats] = defaultdict(lambda: [0, 0])
    target_stats: dict[object, Stats] = defaultdict(lambda: [0, 0])
    residual_sum = train_rows = 0
    with args.train.open(newline="", encoding="utf-8") as stream:
        for index, row in enumerate(csv.DictReader(stream)):
            if index < args.skip_train_rows:
                continue
            source = port_name(row["From"])
            target = port_name(row["To"])
            residual = int(row["delay"]) - int(row["ans"])
            add(pair_stats, (source, target), residual)
            add(source_stats, source, residual)
            add(target_stats, target, residual)
            residual_sum += residual
            train_rows += 1

    global_mean = residual_sum / train_rows
    scores = [0.0, 0.0, 0.0]
    errors = [0.0, 0.0, 0.0]
    holdout_rows = 0
    used_pairs = 0
    with args.holdout.open(newline="", encoding="utf-8") as stream:
        for row in csv.DictReader(stream):
            source = port_name(row["From"])
            target = port_name(row["To"])
            golden = int(row["delay"])
            base = int(row["ans"])
            source_prior = correction(source_stats, source, global_mean, 256)
            target_prior = correction(target_stats, target, source_prior, 256)
            pair_key = (source, target)
            pair_adjustment = correction(
                pair_stats, pair_key, target_prior, args.pair_strength
            )
            used_pairs += pair_key in pair_stats
            estimates = (base, base + round(global_mean), base + pair_adjustment)
            for index, estimate in enumerate(estimates):
                scores[index] += point(golden, estimate)
                errors[index] += abs(estimate - golden) / golden if golden else float(estimate != 0)
            holdout_rows += 1

    labels = ("base", "global", "hierarchical_pair")
    print(
        f"train_rows={train_rows} holdout_rows={holdout_rows} "
        f"pair_groups={len(pair_stats)} global_mean={global_mean:.6f} "
        f"pair_coverage={100.0 * used_pairs / holdout_rows:.4f}%"
    )
    for label, score, error in zip(labels, scores, errors):
        print(
            f"{label} accuracy={100.0 * score / holdout_rows:.6f} "
            f"mape={100.0 * error / holdout_rows:.6f}"
        )


if __name__ == "__main__":
    main()
