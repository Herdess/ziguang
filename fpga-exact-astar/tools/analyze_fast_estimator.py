#!/usr/bin/env python3
"""Validate and generate the deterministic O(1) fast-estimator calibration."""

from __future__ import annotations

import argparse
import csv
import re
from pathlib import Path

import numpy as np


SCALE_NUMERATOR = 1306
SCALE_DENOMINATOR = 1000
INTERCEPT = 170
Q_SHIFT = 14
Q_SCALE = 1 << Q_SHIFT


def competition_score(golden: np.ndarray, estimate: np.ndarray) -> float:
    zero = golden == 0
    relative = np.zeros_like(golden, dtype=np.float64)
    relative[~zero] = np.abs(estimate[~zero] - golden[~zero]) / golden[~zero]
    points = 1.0 - np.tanh(4.0 * relative)
    points[zero] = estimate[zero] == 0
    return float(points.mean() * 100.0)


def architecture_ports(path: Path) -> dict[str, int]:
    text = path.read_text(encoding="utf-8")
    marker = text.index("kPortNames")
    start = text.index("{{", marker)
    end = text.index("}};", start)
    names = re.findall(r'"([^"\\]*(?:\\.[^"\\]*)*)"', text[start:end])
    if len(names) != 496 or len(set(names)) != len(names):
        raise ValueError(f"expected 496 unique architecture ports, got {len(names)}")
    return {name: index for index, name in enumerate(names)}


def pin_parts(text: str, port_ids: dict[str, int]) -> tuple[int, int, int]:
    slash = text.index("/")
    x_text, y_text = text[4:slash].split("_", 1)
    return int(x_text), int(y_text), port_ids[text[slash + 1 :]]


def load_rows(golden_path: Path, estimate_path: Path, port_ids: dict[str, int]):
    columns: list[list[int]] = [[] for _ in range(8)]
    with golden_path.open(newline="", encoding="utf-8") as golden_file, \
            estimate_path.open(newline="", encoding="utf-8") as estimate_file:
        golden_reader = csv.DictReader(golden_file)
        estimate_reader = csv.DictReader(estimate_file)
        while True:
            golden_row = next(golden_reader, None)
            estimate_row = next(estimate_reader, None)
            if golden_row is None or estimate_row is None:
                if golden_row is not None or estimate_row is not None:
                    raise ValueError("golden and estimate row counts differ")
                break
            if (golden_row["From"], golden_row["To"]) != \
                    (estimate_row["From"], estimate_row["To"]):
                raise ValueError("golden and estimate rows are not aligned")
            sx, sy, source = pin_parts(golden_row["From"], port_ids)
            tx, ty, target = pin_parts(golden_row["To"], port_ids)
            values = (int(golden_row["delay"]), int(estimate_row["delay"]),
                      sx, sy, tx, ty, source, target)
            for column, value in zip(columns, values):
                column.append(value)
    return tuple(np.asarray(column, dtype=np.int32) for column in columns)


def fit_effect(golden: np.ndarray, prediction: np.ndarray, group: np.ndarray,
               count: int, fit: np.ndarray, shrink: float) -> np.ndarray:
    valid = fit & (golden > 0) & (prediction > 0)
    residual = np.log(golden[valid] / prediction[valid])
    totals = np.bincount(group[valid], weights=residual, minlength=count)
    counts = np.bincount(group[valid], minlength=count)
    return totals / (counts + shrink)


def format_array(name: str, values: np.ndarray, columns: int = 16) -> str:
    q14 = np.rint(np.exp(values) * Q_SCALE)
    clipped = np.count_nonzero((q14 < 0) | (q14 > 65535))
    if clipped:
        raise ValueError(f"{name}: {clipped} Q14 coefficients do not fit uint16_t")
    numbers = [str(int(value)) for value in q14]
    lines = []
    for start in range(0, len(numbers), columns):
        lines.append("    " + ", ".join(numbers[start:start + columns]) + ",")
    return (f"inline constexpr std::array<uint16_t, {len(numbers)}> {name}{{{{\n" +
            "\n".join(lines) + "\n}};\n")


def write_header(path: Path, effects: list[tuple[str, np.ndarray]]) -> None:
    text = f"""#pragma once

#include <array>
#include <cstdint>

namespace srb::fast_calibration {{

inline constexpr uint32_t kScaleNumerator = {SCALE_NUMERATOR};
inline constexpr uint32_t kScaleDenominator = {SCALE_DENOMINATOR};
inline constexpr uint32_t kIntercept = {INTERCEPT};
inline constexpr uint32_t kQShift = {Q_SHIFT};
inline constexpr uint32_t kQHalf = {Q_SCALE // 2};
inline constexpr uint32_t kPortCount = 496;

"""
    for name, values in effects:
        text += format_array(name, values) + "\n"
    text += "}  // namespace srb::fast_calibration\n"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def feature_groups(sx: np.ndarray, sy: np.ndarray, tx: np.ndarray,
                   ty: np.ndarray, source: np.ndarray, target: np.ndarray,
                   raw: np.ndarray) -> list[tuple[str, np.ndarray, int, float]]:
    dx = tx - sx
    dy = ty - sy
    abs_dx = np.abs(dx)
    abs_dy = np.abs(dy)
    sign = (np.sign(dx) + 1) * 3 + np.sign(dy) + 1
    delay64 = np.minimum(raw // 64, 127)
    signed_x = np.sign(dx) * np.minimum(abs_dx // 10, 11) + 11
    signed_y = np.sign(dy) * np.minimum(abs_dy // 12, 45) + 45
    return [
        ("kFineSpatialQ14", np.minimum(abs_dx // 5, 23) * 91 +
         np.minimum(abs_dy // 6, 90), 24 * 91, 1.0),
        ("kRawDelayQ14", np.minimum(raw // 32, 255), 256, 5.0),
        ("kSignQ14", sign, 9, 500.0),
        ("kRemainderQ14", (abs_dx % 10) * 12 + abs_dy % 12, 120, 2000.0),
        ("kSourcePortQ14", source, 496, 2000.0),
        ("kTargetPortQ14", target, 496, 2000.0),
        ("kPortPairQ14", source * 496 + target, 496 * 496, 5.0),
        ("kTargetDelayQ14", target * 128 + delay64, 496 * 128, 100.0),
        ("kSourceDelayQ14", source * 128 + delay64, 496 * 128, 100.0),
        ("kTargetSignQ14", target * 9 + sign, 496 * 9, 20.0),
        ("kSourceSignQ14", source * 9 + sign, 496 * 9, 20.0),
        ("kSignedSpatialQ14", signed_x * 91 + signed_y, 23 * 91, 5.0),
    ]


def fit_model(golden: np.ndarray, raw: np.ndarray,
              groups: list[tuple[str, np.ndarray, int, float]], fit: np.ndarray):
    prediction = (SCALE_NUMERATOR * raw / SCALE_DENOMINATOR) + INTERCEPT
    effects: list[tuple[str, np.ndarray]] = []
    for name, group, count, shrink in groups:
        effect = fit_effect(golden, prediction, group, count, fit, shrink)
        prediction *= np.exp(effect[group])
        effects.append((name, effect))
    return prediction, effects


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--golden", type=Path, required=True)
    parser.add_argument("--estimate", type=Path, required=True,
                        help="uncalibrated fast-estimator CSV")
    parser.add_argument("--arch-header", type=Path,
                        default=Path("fast_src/generated_arch_data.hpp"))
    parser.add_argument("--output-header", type=Path)
    args = parser.parse_args()

    ports = architecture_ports(args.arch_header)
    golden, raw, sx, sy, tx, ty, source, target = load_rows(
        args.golden, args.estimate, ports)
    groups = feature_groups(sx, sy, tx, ty, source, target, raw)
    rows = golden.size
    validation = np.arange(rows) % 5 == 0
    training = ~validation

    validation_prediction, _ = fit_model(golden, raw, groups, training)
    print(f"rows={rows} ports={len(ports)}")
    print(f"raw_score={competition_score(golden, raw):.6f}")
    print(f"validation_score={competition_score(golden[validation], validation_prediction[validation]):.6f}")

    final_prediction, effects = fit_model(
        golden, raw, groups, np.ones(rows, dtype=np.bool_))
    print(f"final_full_fit_score={competition_score(golden, final_prediction):.6f}")
    nonzero = golden != 0
    relative = np.abs(final_prediction[nonzero] - golden[nonzero]) / golden[nonzero]
    print("ape_percentiles=" + ",".join(
        f"{value * 100.0:.3f}" for value in np.quantile(
            relative, [0.5, 0.9, 0.95, 0.99])))
    if args.output_header:
        write_header(args.output_header, effects)
        print(f"wrote_header={args.output_header}")


if __name__ == "__main__":
    main()
