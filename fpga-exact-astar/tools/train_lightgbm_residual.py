#!/usr/bin/env python3
"""Train a compact architecture-aware residual model on public training rows.

The input estimate must be produced with --no-public-cache.  A deterministic
query hash selects the validation fold, so no Golden answer from that fold is
used for fitting or early model construction.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import lightgbm as lgb
import numpy as np

from analyze_fast_estimator import architecture_ports, competition_score, load_rows
from analyze_generalization import block_mask, crossed_delay, deterministic_fold


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--golden", type=Path, required=True)
    parser.add_argument("--estimate", type=Path, required=True)
    parser.add_argument("--arch-header", type=Path,
                        default=Path("fast_src/generated_arch_data.hpp"))
    parser.add_argument("--gap", type=Path, default=Path("arch/SRB_Gap.json"))
    parser.add_argument("--output-model", type=Path, required=True)
    parser.add_argument("--validation-fold", type=int, default=0)
    parser.add_argument("--rounds", type=int, default=1200)
    args = parser.parse_args()

    ports = architecture_ports(args.arch_header)
    golden, estimate, sx, sy, tx, ty, source, target = load_rows(
        args.golden, args.estimate, ports)
    fold = deterministic_fold(args.golden, 5)
    validation = fold == args.validation_fold
    training = ~validation

    dx = tx - sx
    dy = ty - sy
    abs_dx = np.abs(dx)
    abs_dy = np.abs(dy)
    sign = (np.sign(dx) + 1) * 3 + np.sign(dy) + 1
    midpoint_x = (sx + tx) // 2
    midpoint_y = (sy + ty) // 2

    gap = json.loads(args.gap.read_text(encoding="utf-8"))["Gap"]
    vertical = [line for line in gap["Line"] if line["direction"] == "vertical"]
    horizontal = [line for line in gap["Line"] if line["direction"] == "horizontal"]
    vertical_sites = np.asarray([line["site"] for line in vertical], dtype=np.int32)
    horizontal_sites = np.asarray([line["site"] for line in horizontal], dtype=np.int32)
    vertical_delay = crossed_delay(
        sx, tx, vertical_sites,
        np.asarray([line["delay"] for line in vertical], dtype=np.int32))
    horizontal_delay = crossed_delay(
        sy, ty, horizontal_sites,
        np.asarray([line["delay"] for line in horizontal], dtype=np.int32))
    vertical_count = crossed_delay(
        sx, tx, vertical_sites, np.ones(len(vertical), dtype=np.int32))
    horizontal_count = crossed_delay(
        sy, ty, horizontal_sites, np.ones(len(horizontal), dtype=np.int32))
    blocks = block_mask(sx, sy, tx, ty, gap["Block"])

    source_x_zone = np.searchsorted(vertical_sites, sx).astype(np.int32)
    target_x_zone = np.searchsorted(vertical_sites, tx).astype(np.int32)
    source_y_zone = np.searchsorted(horizontal_sites, sy).astype(np.int32)
    target_y_zone = np.searchsorted(horizontal_sites, ty).astype(np.int32)
    y_zone_count = len(horizontal_sites) + 1
    source_zone = source_x_zone * y_zone_count + source_y_zone
    target_zone = target_x_zone * y_zone_count + target_y_zone

    nearest_vertical_source = np.min(np.abs(sx[:, None] - vertical_sites[None, :]), axis=1)
    nearest_vertical_target = np.min(np.abs(tx[:, None] - vertical_sites[None, :]), axis=1)
    nearest_horizontal_source = np.min(np.abs(sy[:, None] - horizontal_sites[None, :]), axis=1)
    nearest_horizontal_target = np.min(np.abs(ty[:, None] - horizontal_sites[None, :]), axis=1)

    names = [
        "estimate", "source_x", "source_y", "target_x", "target_y",
        "dx", "dy", "abs_dx", "abs_dy", "manhattan", "chebyshev",
        "source_port", "target_port", "direction",
        "midpoint_x", "midpoint_y",
        "vertical_gap_delay", "horizontal_gap_delay",
        "vertical_gap_count", "horizontal_gap_count", "block_mask",
        "source_x_zone", "source_y_zone", "target_x_zone", "target_y_zone",
        "source_zone", "target_zone",
        "source_nearest_vgap", "target_nearest_vgap",
        "source_nearest_hgap", "target_nearest_hgap",
        "dx_mod_10", "dy_mod_12",
    ]
    features = np.column_stack([
        estimate, sx, sy, tx, ty, dx, dy, abs_dx, abs_dy,
        abs_dx + abs_dy, np.maximum(abs_dx, abs_dy),
        source, target, sign, midpoint_x, midpoint_y,
        vertical_delay, horizontal_delay, vertical_count, horizontal_count,
        blocks, source_x_zone, source_y_zone, target_x_zone, target_y_zone,
        source_zone, target_zone,
        nearest_vertical_source, nearest_vertical_target,
        nearest_horizontal_source, nearest_horizontal_target,
        abs_dx % 10, abs_dy % 12,
    ]).astype(np.float32)

    positive = (golden > 0) & (estimate > 0)
    if not np.all(positive):
        raise ValueError("residual training currently requires positive delays")
    target_residual = np.log(golden / estimate).astype(np.float32)
    categorical_names = {
        "source_port", "target_port", "direction", "block_mask",
        "source_x_zone", "source_y_zone", "target_x_zone", "target_y_zone",
        "source_zone", "target_zone", "dx_mod_10", "dy_mod_12",
    }
    categorical = [index for index, name in enumerate(names)
                   if name in categorical_names]

    train_set = lgb.Dataset(
        features[training], label=target_residual[training],
        feature_name=names, categorical_feature=categorical, free_raw_data=False)
    validation_set = lgb.Dataset(
        features[validation], label=target_residual[validation],
        feature_name=names, categorical_feature=categorical,
        reference=train_set, free_raw_data=False)
    params = {
        "objective": "regression_l1",
        "metric": "l1",
        "learning_rate": 0.035,
        "num_leaves": 48,
        "max_depth": 8,
        "min_data_in_leaf": 250,
        "feature_fraction": 0.85,
        "bagging_fraction": 0.85,
        "bagging_freq": 1,
        "lambda_l1": 0.02,
        "lambda_l2": 2.0,
        "max_bin": 127,
        "num_threads": 2,
        "seed": 20260929,
        "feature_fraction_seed": 20260929,
        "bagging_seed": 20260929,
        "verbosity": -1,
        "force_col_wise": True,
    }
    model = lgb.train(
        params, train_set, num_boost_round=args.rounds,
        valid_sets=[validation_set], valid_names=["validation"],
        callbacks=[lgb.early_stopping(80), lgb.log_evaluation(50)])

    residual = model.predict(features[validation], num_iteration=model.best_iteration)
    prediction = np.maximum(
        0, np.rint(estimate[validation] * np.exp(residual))).astype(np.int64)
    score = competition_score(golden[validation], prediction)
    relative = np.abs(prediction - golden[validation]) / golden[validation]
    print(f"validation_fold={args.validation_fold}")
    print(f"best_iteration={model.best_iteration}")
    print(f"baseline_score={competition_score(golden[validation], estimate[validation]):.6f}")
    print(f"model_score={score:.6f}")
    print(f"mape_percent={relative.mean() * 100.0:.6f}")
    print("ape_percentiles=" + ",".join(
        f"{value * 100.0:.4f}" for value in np.quantile(relative, [0.5, 0.9, 0.95, 0.99])))
    args.output_model.parent.mkdir(parents=True, exist_ok=True)
    model.save_model(str(args.output_model), num_iteration=model.best_iteration)
    print(f"model={args.output_model}")


if __name__ == "__main__":
    main()
