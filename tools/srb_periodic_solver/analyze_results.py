#!/usr/bin/env python3
"""Score periodic-solver validation output with the competition V0.3 formula."""

from __future__ import annotations

import argparse
import csv
import heapq
import math
from collections import defaultdict
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("result", type=Path)
    args = parser.parse_args()

    count = under = over = exact = 0
    score_sum = relative_sum = 0.0
    worst: list[tuple[float, str, str, int, int]] = []
    groups: dict[str, list[float | int]] = defaultdict(lambda: [0, 0.0, 0.0])

    with args.result.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        required = {"From", "To", "delay", "ans", "fused_logic", "gap_block_added"}
        if reader.fieldnames is None or not required.issubset(reader.fieldnames):
            raise SystemExit(f"unexpected result columns: {reader.fieldnames}")
        for row in reader:
            golden = int(row["delay"])
            estimate = int(row["ans"])
            if golden == 0:
                relative = 0.0 if estimate == 0 else 1.0
                point = 1.0 if estimate == 0 else 0.0
            else:
                relative = abs(estimate - golden) / golden
                point = 1.0 - math.tanh(4.0 * relative)
            count += 1
            score_sum += point
            relative_sum += relative
            under += estimate < golden
            over += estimate > golden
            exact += estimate == golden
            item = (relative, row["From"], row["To"], golden, estimate)
            if len(worst) < 10:
                heapq.heappush(worst, item)
            elif item > worst[0]:
                heapq.heapreplace(worst, item)
            for label in (
                f"fused={row['fused_logic']}",
                f"gap_block={row['gap_block_added']}",
            ):
                group = groups[label]
                group[0] += 1
                group[1] += point
                group[2] += relative

    if not count:
        raise SystemExit("no result rows")
    print(f"rows={count}")
    print(f"accuracy_score={100.0 * score_sum / count:.6f}")
    print(f"mape_percent={100.0 * relative_sum / count:.6f}")
    print(f"under={under} over={over} exact={exact}")
    for label, (size, group_score, group_relative) in sorted(groups.items()):
        print(
            f"{label} rows={int(size)} accuracy={100.0 * group_score / size:.6f} "
            f"mape={100.0 * group_relative / size:.6f}"
        )
    print("worst_relative_errors:")
    for relative, source, target, golden, estimate in sorted(worst, reverse=True):
        print(
            f"  rel={100.0 * relative:.3f}% golden={golden} estimate={estimate} "
            f"{source} -> {target}"
        )


if __name__ == "__main__":
    main()
