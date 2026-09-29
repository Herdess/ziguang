#!/usr/bin/env python3
"""Score an estimate CSV with the competition V0.3 accuracy formula."""

from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--golden", type=Path, required=True)
    parser.add_argument("--estimate", type=Path, required=True)
    args = parser.parse_args()

    count = 0
    score_sum = 0.0
    relative_sum = 0.0
    under = 0
    over = 0
    exact = 0

    with args.golden.open(newline="", encoding="utf-8") as golden_file, \
            args.estimate.open(newline="", encoding="utf-8") as estimate_file:
        golden_reader = csv.DictReader(golden_file)
        estimate_reader = csv.DictReader(estimate_file)
        if golden_reader.fieldnames is None or not {"From", "To", "delay"}.issubset(golden_reader.fieldnames):
            raise SystemExit("Golden CSV must contain From,To,delay")
        if estimate_reader.fieldnames != ["From", "To", "delay"]:
            raise SystemExit("estimate CSV header must be exactly From,To,delay")

        while True:
            golden_row = next(golden_reader, None)
            estimate_row = next(estimate_reader, None)
            if golden_row is None or estimate_row is None:
                if golden_row is not None or estimate_row is not None:
                    raise SystemExit("row count mismatch")
                break
            count += 1
            if (golden_row["From"], golden_row["To"]) != \
                    (estimate_row["From"], estimate_row["To"]):
                raise SystemExit(f"row {count}: From/To mismatch")

            golden = int(golden_row["delay"])
            estimate = int(estimate_row["delay"])
            if golden == 0:
                point = 1.0 if estimate == 0 else 0.0
                relative = 0.0 if estimate == 0 else 1.0
            else:
                relative = abs(estimate - golden) / golden
                point = 1.0 - math.tanh(4.0 * relative)
            score_sum += point
            relative_sum += relative
            under += estimate < golden
            over += estimate > golden
            exact += estimate == golden

    if count == 0:
        raise SystemExit("no data rows")
    print(f"rows={count}")
    print(f"accuracy_score={100.0 * score_sum / count:.6f}")
    print(f"mape_percent={100.0 * relative_sum / count:.6f}")
    print(f"under={under} over={over} exact={exact}")


if __name__ == "__main__":
    main()
