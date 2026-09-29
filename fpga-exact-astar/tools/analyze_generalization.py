#!/usr/bin/env python3
"""Measure architecture-aware residual features on a deterministic holdout.

This tool deliberately starts from a no-public-cache result CSV.  It never
uses the public answer lookup at inference time; Golden delays are used only
to fit residual corrections on the training partition and to score the held
out partition.
"""

from __future__ import annotations

import argparse
import csv
import json
import zlib
from pathlib import Path

import numpy as np

from analyze_fast_estimator import (
    architecture_ports,
    competition_score,
    fit_effect,
    load_rows,
)


def deterministic_fold(path: Path, folds: int) -> np.ndarray:
    values: list[int] = []
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            key = (row["From"] + "," + row["To"]).encode("utf-8")
            values.append((zlib.crc32(key) & 0xFFFFFFFF) % folds)
    return np.asarray(values, dtype=np.uint8)


def crossed_delay(a: np.ndarray, b: np.ndarray,
                  sites: np.ndarray, delays: np.ndarray) -> np.ndarray:
    low = np.minimum(a, b)
    high = np.maximum(a, b)
    result = np.zeros(a.size, dtype=np.int32)
    for site, delay in zip(sites, delays):
        result += ((low <= site) & (site < high)) * int(delay)
    return result


def block_mask(sx: np.ndarray, sy: np.ndarray,
               tx: np.ndarray, ty: np.ndarray,
               blocks: list[dict[str, object]]) -> np.ndarray:
    min_x = np.minimum(sx, tx)
    max_x = np.maximum(sx, tx)
    min_y = np.minimum(sy, ty)
    max_y = np.maximum(sy, ty)
    result = np.zeros(sx.size, dtype=np.int32)
    for index, block in enumerate(blocks):
        intersects = ((min_x <= int(block["right"])) &
                      (max_x >= int(block["left"])) &
                      (min_y <= int(block["upper"])) &
                      (max_y >= int(block["lower"])))
        result |= intersects.astype(np.int32) << index
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--golden", type=Path, required=True)
    parser.add_argument("--estimate", type=Path, required=True)
    parser.add_argument("--arch-header", type=Path,
                        default=Path("fast_src/generated_arch_data.hpp"))
    parser.add_argument("--gap", type=Path, default=Path("arch/SRB_Gap.json"))
    parser.add_argument("--folds", type=int, default=5)
    parser.add_argument("--validation-fold", type=int, default=0)
    args = parser.parse_args()

    ports = architecture_ports(args.arch_header)
    golden, estimate, sx, sy, tx, ty, source, target = load_rows(
        args.golden, args.estimate, ports)
    fold = deterministic_fold(args.golden, args.folds)
    validation = fold == args.validation_fold
    training = ~validation
    prediction = estimate.astype(np.float64)

    print(f"rows={golden.size} train={training.sum()} validation={validation.sum()}")
    print(f"baseline_train={competition_score(golden[training], prediction[training]):.6f}")
    print(f"baseline_validation={competition_score(golden[validation], prediction[validation]):.6f}")

    def apply(name: str, group: np.ndarray, count: int, shrink: float) -> None:
        nonlocal prediction
        effect = fit_effect(golden, prediction, group, count, training, shrink)
        prediction *= np.exp(effect[group])
        score = competition_score(golden[validation], prediction[validation])
        print(f"{name}_validation={score:.6f} groups={count} shrink={shrink:g}")

    # Absolute placement is absent from the current relative-displacement model.
    apply("source_site_4x4", (sx // 4) * 138 + sy // 4, 30 * 138, 80.0)
    apply("target_site_4x4", (tx // 4) * 138 + ty // 4, 30 * 138, 80.0)
    apply("source_site_2x2", (sx // 2) * 275 + sy // 2, 60 * 275, 120.0)
    apply("target_site_2x2", (tx // 2) * 275 + ty // 2, 60 * 275, 120.0)

    dx = tx - sx
    dy = ty - sy
    signed_dx = dx + 119
    signed_dy2 = np.floor_divide(dy, 2) + 275
    apply("signed_displacement", signed_dx * 550 + signed_dy2,
          239 * 550, 40.0)

    midpoint_x = (sx + tx) // 2
    midpoint_y = (sy + ty) // 2
    apply("midpoint_4x4", (midpoint_x // 4) * 138 + midpoint_y // 4,
          30 * 138, 100.0)

    gap = json.loads(args.gap.read_text(encoding="utf-8"))["Gap"]
    vertical = [line for line in gap["Line"] if line["direction"] == "vertical"]
    horizontal = [line for line in gap["Line"] if line["direction"] == "horizontal"]
    vertical_delay = crossed_delay(
        sx, tx,
        np.asarray([line["site"] for line in vertical], dtype=np.int32),
        np.asarray([line["delay"] for line in vertical], dtype=np.int32))
    horizontal_delay = crossed_delay(
        sy, ty,
        np.asarray([line["site"] for line in horizontal], dtype=np.int32),
        np.asarray([line["delay"] for line in horizontal], dtype=np.int32))
    gap_group = np.minimum(vertical_delay, 1023) * 64 + np.minimum(horizontal_delay, 63)
    apply("gap_crossing", gap_group, 1024 * 64, 30.0)

    blocks = gap["Block"]
    blocks_crossed = block_mask(sx, sy, tx, ty, blocks)
    apply("block_bbox", blocks_crossed, 1 << len(blocks), 100.0)

    x_lines = np.asarray(sorted(line["site"] for line in vertical), dtype=np.int32)
    y_lines = np.asarray(sorted(line["site"] for line in horizontal), dtype=np.int32)
    source_zone = np.searchsorted(x_lines, sx) * (len(y_lines) + 1) + np.searchsorted(y_lines, sy)
    target_zone = np.searchsorted(x_lines, tx) * (len(y_lines) + 1) + np.searchsorted(y_lines, ty)
    zone_count = (len(x_lines) + 1) * (len(y_lines) + 1)
    apply("source_target_zone", source_zone * zone_count + target_zone,
          zone_count * zone_count, 60.0)
    apply("source_port_zone", source * zone_count + source_zone,
          len(ports) * zone_count, 100.0)
    apply("target_port_zone", target * zone_count + target_zone,
          len(ports) * zone_count, 100.0)

    rounded = np.maximum(0, np.rint(prediction)).astype(np.int64)
    print(f"final_train={competition_score(golden[training], rounded[training]):.6f}")
    print(f"final_validation={competition_score(golden[validation], rounded[validation]):.6f}")
    nonzero = golden[validation] != 0
    relative = np.abs(rounded[validation][nonzero] - golden[validation][nonzero]) / golden[validation][nonzero]
    print("validation_ape_percentiles=" + ",".join(
        f"{value * 100.0:.4f}" for value in np.quantile(relative, [0.5, 0.9, 0.95, 0.99])))


if __name__ == "__main__":
    main()
