#!/usr/bin/env python3
"""Train a compact architecture-aware residual model on public training rows.

The input estimate must be produced with --no-public-cache.  A deterministic
query hash selects the validation fold, so no Golden answer from that fold is
used for fitting or early model construction.
"""

from __future__ import annotations

import argparse
import json
import re
import time
from pathlib import Path

import lightgbm as lgb
import numpy as np

from analyze_fast_estimator import architecture_ports, competition_score, load_rows
from analyze_generalization import block_mask, crossed_delay, deterministic_fold


def smoothed_leave_one_out_encoding(
        key: np.ndarray, target: np.ndarray, training: np.ndarray,
        smoothing: float) -> np.ndarray:
    """Encode a category using training labels without exposing its own label."""
    key = key.astype(np.int64, copy=False)
    size = int(key.max()) + 1
    training_key = key[training]
    training_target = target[training].astype(np.float64, copy=False)
    count = np.bincount(training_key, minlength=size).astype(np.float64)
    total = np.bincount(
        training_key, weights=training_target, minlength=size).astype(np.float64)
    global_mean = float(training_target.mean())

    encoded = (total[key] + smoothing * global_mean) / (count[key] + smoothing)
    encoded[training] = (
        total[training_key] - training_target + smoothing * global_mean
    ) / (count[training_key] - 1.0 + smoothing)
    return encoded.astype(np.float32)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--golden", type=Path, required=True)
    parser.add_argument("--estimate", type=Path, required=True)
    parser.add_argument("--features", type=Path,
                        help="numeric feature CSV emitted by --feature-output")
    parser.add_argument("--arch-header", type=Path,
                        default=Path("fast_src/generated_arch_data.hpp"))
    parser.add_argument("--gap", type=Path, default=Path("arch/SRB_Gap.json"))
    parser.add_argument("--output-model", type=Path)
    parser.add_argument("--input-model", type=Path,
                        help="load an existing model instead of training")
    parser.add_argument("--checkpoints", default="",
                        help="comma-separated tree counts to evaluate")
    parser.add_argument("--validation-fold", type=int, default=0)
    parser.add_argument("--train-all", action="store_true",
                        help="fit the final model on every positive Golden row")
    parser.add_argument("--rounds", type=int, default=1200)
    parser.add_argument("--learning-rate", type=float, default=0.035)
    parser.add_argument("--leaves", type=int, default=48)
    parser.add_argument("--depth", type=int, default=8)
    parser.add_argument("--min-data", type=int, default=250)
    parser.add_argument("--seed", type=int, default=20260929)
    parser.add_argument("--threads", type=int, default=2)
    parser.add_argument("--output-validation", type=Path,
                        help="optional compressed holdout predictions for ensembling")
    parser.add_argument("--output-feature-sample", type=Path,
                        help="optional NPZ containing the first feature rows for C++ parity tests")
    parser.add_argument("--feature-sample-rows", type=int, default=128)
    parser.add_argument("--target-encoding", action="store_true",
                        help="add leakage-safe category residual statistics")
    parser.add_argument("--objective", choices=("l1", "huber", "fair"),
                        default="l1")
    parser.add_argument("--base", choices=("calibrated", "raw"),
                        default="calibrated")
    args = parser.parse_args()
    if not args.input_model and not args.output_model:
        parser.error("training requires --output-model")

    ports = architecture_ports(args.arch_header)
    golden, estimate, sx, sy, tx, ty, source, target = load_rows(
        args.golden, args.estimate, ports)
    fold = deterministic_fold(args.golden, 5)
    validation = fold == args.validation_fold
    training = ~validation
    if args.train_all:
        training = np.ones_like(validation, dtype=bool)

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
    vertical_mask = np.zeros(golden.size, dtype=np.int32)
    for index, site in enumerate(vertical_sites):
        vertical_mask |= (((np.minimum(sx, tx) <= site) &
                           (site < np.maximum(sx, tx))).astype(np.int32) << index)
    horizontal_mask = np.zeros(golden.size, dtype=np.int32)
    for index, site in enumerate(horizontal_sites):
        horizontal_mask |= (((np.minimum(sy, ty) <= site) &
                             (site < np.maximum(sy, ty))).astype(np.int32) << index)

    source_x_zone = np.searchsorted(vertical_sites, sx).astype(np.int32)
    target_x_zone = np.searchsorted(vertical_sites, tx).astype(np.int32)
    source_y_zone = np.searchsorted(horizontal_sites, sy).astype(np.int32)
    target_y_zone = np.searchsorted(horizontal_sites, ty).astype(np.int32)
    y_zone_count = len(horizontal_sites) + 1
    source_zone = source_x_zone * y_zone_count + source_y_zone
    target_zone = target_x_zone * y_zone_count + target_y_zone

    ordered_port_names = [name for name, _ in sorted(
        ports.items(), key=lambda item: item[1])]
    family_names = [re.sub(r"\[\d+\]$", "", name) for name in ordered_port_names]
    family_ids = {name: index for index, name in enumerate(dict.fromkeys(family_names))}
    family_by_port = np.asarray([family_ids[name] for name in family_names], dtype=np.int32)
    lane_by_port = np.asarray([
        int(match.group(1)) if (match := re.search(r"\[(\d+)\]$", name)) else 0
        for name in ordered_port_names
    ], dtype=np.int32)
    source_family = family_by_port[source]
    target_family = family_by_port[target]
    source_lane = lane_by_port[source]
    target_lane = lane_by_port[target]
    domain_ids = {name: index for index, name in enumerate("ZIAS")}
    wire_ids = {name: index for index, name in enumerate("SDQL-")}
    compass_ids = {name: index for index, name in enumerate("ENSW-")}
    variant_ids = {name: index for index, name in enumerate("AB-")}
    bank_ids = {name: index for index, name in enumerate("ABCDEFGH-")}
    suffix_names = [
        name[3:] if name[0] in "AS" else "-" for name in ordered_port_names]
    suffix_ids = {name: index for index, name in enumerate(dict.fromkeys(suffix_names))}
    domain_by_port = np.asarray([domain_ids[name[0]] for name in ordered_port_names])
    wire_by_port = np.asarray([
        wire_ids[name[1]] if name[0] in "ZI" else wire_ids["-"]
        for name in ordered_port_names])
    compass_by_port = np.asarray([
        compass_ids[name[2]] if name[0] in "ZI" else compass_ids["-"]
        for name in ordered_port_names])
    variant_by_port = np.asarray([
        variant_ids[name[3]] if name[0] in "ZI" and len(family_names[index]) == 4
        else variant_ids["-"] for index, name in enumerate(ordered_port_names)])
    bank_by_port = np.asarray([
        bank_ids[name[2]] if name[0] in "AS" else bank_ids["-"]
        for name in ordered_port_names])
    suffix_by_port = np.asarray([suffix_ids[name] for name in suffix_names])

    source_y_mod100 = sy % 100
    target_y_mod100 = ty % 100
    midpoint_y_mod100 = midpoint_y % 100
    source_y_band = sy // 50
    target_y_band = ty // 50
    source_x_side = np.where(sx < 76, 0, np.where(sx <= 89, 1, 2))
    target_x_side = np.where(tx < 76, 0, np.where(tx <= 89, 1, 2))
    crosses_central_column = (((sx < 76) & (tx > 89)) |
                              ((tx < 76) & (sx > 89))).astype(np.int32)
    source_open_distance = np.where(
        source_y_mod100 >= 50, 0,
        np.minimum(50 - source_y_mod100, source_y_mod100 + 1))
    target_open_distance = np.where(
        target_y_mod100 >= 50, 0,
        np.minimum(50 - target_y_mod100, target_y_mod100 + 1))
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
        "vertical_gap_mask", "horizontal_gap_mask",
        "source_family", "target_family", "source_lane", "target_lane",
        "source_domain", "target_domain", "source_wire", "target_wire",
        "source_compass", "target_compass", "source_variant", "target_variant",
        "source_bank", "target_bank", "source_suffix", "target_suffix",
        "source_y_mod100", "target_y_mod100", "midpoint_y_mod100",
        "source_y_band", "target_y_band",
        "source_x_side", "target_x_side", "crosses_central_column",
        "source_open_distance", "target_open_distance",
    ]
    feature_columns = [
        estimate, sx, sy, tx, ty, dx, dy, abs_dx, abs_dy,
        abs_dx + abs_dy, np.maximum(abs_dx, abs_dy),
        source, target, sign, midpoint_x, midpoint_y,
        vertical_delay, horizontal_delay, vertical_count, horizontal_count,
        blocks, source_x_zone, source_y_zone, target_x_zone, target_y_zone,
        source_zone, target_zone,
        nearest_vertical_source, nearest_vertical_target,
        nearest_horizontal_source, nearest_horizontal_target,
        abs_dx % 10, abs_dy % 12,
        vertical_mask, horizontal_mask,
        source_family, target_family, source_lane, target_lane,
        domain_by_port[source], domain_by_port[target],
        wire_by_port[source], wire_by_port[target],
        compass_by_port[source], compass_by_port[target],
        variant_by_port[source], variant_by_port[target],
        bank_by_port[source], bank_by_port[target],
        suffix_by_port[source], suffix_by_port[target],
        source_y_mod100, target_y_mod100, midpoint_y_mod100,
        source_y_band, target_y_band,
        source_x_side, target_x_side, crosses_central_column,
        source_open_distance, target_open_distance,
    ]
    base_estimate = estimate
    if args.features:
        with args.features.open(encoding="utf-8") as handle:
            search_names = ["search_" + name for name in handle.readline().strip().split(",")]
        search_features = np.loadtxt(
            args.features, delimiter=",", skiprows=1, dtype=np.float32)
        if search_features.shape[0] != golden.size:
            raise ValueError("search feature row count does not match Golden rows")
        names.extend(search_names)
        feature_columns.extend(search_features[:, index]
                               for index in range(search_features.shape[1]))
        if args.base == "raw":
            base_estimate = search_features[:, search_names.index("search_raw")].astype(
                np.int32)
            # The calibrated estimate was fitted on all public rows.  Exclude
            # it from strict holdout runs so validation answers cannot leak
            # through the pre-existing calibration tables.
            names.pop(0)
            feature_columns.pop(0)
    elif args.base == "raw":
        raise ValueError("--base raw requires --features")
    positive = (golden > 0) & (base_estimate > 0)
    fit_training = training & positive
    fit_validation = validation & positive
    target_residual = np.zeros(golden.size, dtype=np.float32)
    target_residual[positive] = np.log(
        golden[positive] / base_estimate[positive]).astype(np.float32)
    if args.target_encoding:
        port_count = len(ordered_port_names)
        family_count = len(family_ids)
        delay_bucket = np.minimum(base_estimate.astype(np.int64) // 64, 127)
        encoding_specs = (
            ("te_source_port", source, 20.0),
            ("te_target_port", target, 20.0),
            ("te_family_pair",
             source_family * family_count + target_family, 12.0),
            ("te_source_port_target_family",
             source * family_count + target_family, 24.0),
            ("te_source_family_target_port",
             source_family * port_count + target, 24.0),
            ("te_source_port_direction", source * 9 + sign, 18.0),
            ("te_target_port_direction", target * 9 + sign, 18.0),
            ("te_family_pair_direction",
             (source_family * family_count + target_family) * 9 + sign, 24.0),
            ("te_source_port_raw", source * 128 + delay_bucket, 32.0),
            ("te_target_port_raw", target * 128 + delay_bucket, 32.0),
        )
        for name, key, smoothing in encoding_specs:
            names.append(name)
            feature_columns.append(smoothed_leave_one_out_encoding(
                key, target_residual, fit_training, smoothing))
    features = np.column_stack(feature_columns).astype(np.float32)
    if args.output_feature_sample:
        args.output_feature_sample.parent.mkdir(parents=True, exist_ok=True)
        sample_rows = min(args.feature_sample_rows, features.shape[0])
        np.savez_compressed(
            args.output_feature_sample,
            names=np.asarray(names),
            features=features[:sample_rows],
            base_estimate=base_estimate[:sample_rows],
        )
        print(f"feature_sample={args.output_feature_sample} rows={sample_rows}")
    categorical_names = {
        "source_port", "target_port", "direction", "block_mask",
        "source_x_zone", "source_y_zone", "target_x_zone", "target_y_zone",
        "source_zone", "target_zone", "dx_mod_10", "dy_mod_12",
        "vertical_gap_mask", "horizontal_gap_mask",
        "source_family", "target_family", "source_lane", "target_lane",
        "source_domain", "target_domain", "source_wire", "target_wire",
        "source_compass", "target_compass", "source_variant", "target_variant",
        "source_bank", "target_bank", "source_suffix", "target_suffix",
        "source_y_band", "target_y_band", "source_x_side", "target_x_side",
        "crosses_central_column",
    }
    categorical = [index for index, name in enumerate(names)
                   if name in categorical_names]

    if args.input_model:
        model = lgb.Booster(model_file=str(args.input_model))
        selected_iteration = model.current_iteration()
    else:
        train_set = lgb.Dataset(
            features[fit_training], label=target_residual[fit_training],
            feature_name=names, categorical_feature=categorical, free_raw_data=False)
        objectives = {
            "l1": "regression_l1",
            "huber": "huber",
            "fair": "fair",
        }
        params = {
            "objective": objectives[args.objective],
            "metric": "l1",
            "learning_rate": args.learning_rate,
            "num_leaves": args.leaves,
            "max_depth": args.depth,
            "min_data_in_leaf": args.min_data,
            "feature_fraction": 0.85,
            "bagging_fraction": 0.85,
            "bagging_freq": 1,
            "lambda_l1": 0.02,
            "lambda_l2": 2.0,
            "cat_l2": 10.0,
            "cat_smooth": 20.0,
            "max_cat_threshold": 64,
            "max_bin": 127,
            "num_threads": args.threads,
            "seed": args.seed,
            "feature_fraction_seed": args.seed,
            "bagging_seed": args.seed,
            "verbosity": -1,
            "force_col_wise": True,
        }
        if args.objective == "huber":
            params["alpha"] = 0.85
        elif args.objective == "fair":
            # Residuals are logarithmic relative errors and are usually only a
            # few hundredths, so fair_c=1 would behave almost like L2.
            params["fair_c"] = 0.02
        if args.train_all:
            model = lgb.train(
                params, train_set, num_boost_round=args.rounds,
                callbacks=[lgb.log_evaluation(50)])
            selected_iteration = model.current_iteration()
        else:
            validation_set = lgb.Dataset(
                features[fit_validation], label=target_residual[fit_validation],
                feature_name=names, categorical_feature=categorical,
                reference=train_set, free_raw_data=False)
            model = lgb.train(
                params, train_set, num_boost_round=args.rounds,
                valid_sets=[validation_set], valid_names=["validation"],
                callbacks=[lgb.early_stopping(80), lgb.log_evaluation(50)])
            selected_iteration = model.best_iteration

    importance = sorted(
        zip(names, model.feature_importance(importance_type="gain")),
        key=lambda item: item[1], reverse=True)
    print("feature_importance=" + ",".join(
        f"{name}:{gain:.1f}" for name, gain in importance[:20]))

    def predict_validation(iteration: int) -> tuple[np.ndarray, float]:
        started = time.perf_counter()
        prediction_at_iteration = base_estimate[validation].astype(np.int64).copy()
        residual = model.predict(
            features[fit_validation], num_iteration=iteration)
        prediction_at_iteration[positive[validation]] = np.maximum(
            0, np.rint(base_estimate[fit_validation] * np.exp(residual))).astype(np.int64)
        return prediction_at_iteration, time.perf_counter() - started

    print(f"base={args.base}")
    print(f"seed={args.seed}")
    print(f"target_encoding={args.target_encoding}")
    print(f"objective={args.objective}")
    print(f"learning_rate={args.learning_rate}")
    print(f"train_all={args.train_all}")
    print(f"best_iteration={selected_iteration}")
    if args.train_all:
        if args.checkpoints or args.output_validation:
            raise ValueError("checkpoint scoring requires a held-out validation fold")
        print(f"training_rows={fit_training.sum()}")
    else:
        for checkpoint in (int(value) for value in args.checkpoints.split(",") if value):
            if checkpoint <= 0 or checkpoint > model.current_iteration():
                raise ValueError(f"invalid checkpoint {checkpoint}")
            checkpoint_prediction, checkpoint_seconds = predict_validation(checkpoint)
            print(f"checkpoint={checkpoint} "
                  f"score={competition_score(golden[validation], checkpoint_prediction):.6f} "
                  f"predict_seconds={checkpoint_seconds:.6f}")

        prediction, prediction_seconds = predict_validation(selected_iteration)
        score = competition_score(golden[validation], prediction)
        nonzero = golden[validation] > 0
        relative = (np.abs(prediction[nonzero] - golden[validation][nonzero]) /
                    golden[validation][nonzero])
        print(f"validation_fold={args.validation_fold}")
        print(f"validation_predict_seconds={prediction_seconds:.6f}")
        print(f"base_score={competition_score(golden[validation], base_estimate[validation]):.6f}")
        print(f"calibrated_score={competition_score(golden[validation], estimate[validation]):.6f}")
        print(f"model_score={score:.6f}")
        print(f"mape_percent={relative.mean() * 100.0:.6f}")
        print("ape_percentiles=" + ",".join(
            f"{value * 100.0:.4f}" for value in np.quantile(
                relative, [0.5, 0.9, 0.95, 0.99])))
    if args.output_model:
        args.output_model.parent.mkdir(parents=True, exist_ok=True)
        model.save_model(str(args.output_model), num_iteration=selected_iteration)
        print(f"model={args.output_model}")
    if args.output_validation and not args.train_all:
        args.output_validation.parent.mkdir(parents=True, exist_ok=True)
        np.savez_compressed(
            args.output_validation,
            row=np.flatnonzero(validation),
            golden=golden[validation],
            prediction=prediction,
        )
        print(f"validation_predictions={args.output_validation}")


if __name__ == "__main__":
    main()
