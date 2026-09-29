#!/usr/bin/env python3
"""Evaluate several strict-holdout predictions without training on holdout labels."""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np

from analyze_fast_estimator import competition_score


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("predictions", type=Path, nargs="+")
    args = parser.parse_args()

    loaded = [np.load(path) for path in args.predictions]
    row = loaded[0]["row"]
    golden = loaded[0]["golden"]
    for path, item in zip(args.predictions[1:], loaded[1:]):
        if not np.array_equal(item["row"], row):
            raise ValueError(f"validation rows differ: {path}")
        if not np.array_equal(item["golden"], golden):
            raise ValueError(f"Golden values differ: {path}")

    predictions = np.stack([item["prediction"] for item in loaded])
    for path, prediction in zip(args.predictions, predictions):
        print(f"model={path} score={competition_score(golden, prediction):.6f}")

    arithmetic = np.rint(np.mean(predictions, axis=0)).astype(np.int64)
    geometric = np.rint(np.exp(np.mean(np.log(np.maximum(predictions, 1)), axis=0))).astype(
        np.int64)
    median = np.rint(np.median(predictions, axis=0)).astype(np.int64)
    for name, prediction in (
            ("arithmetic_mean", arithmetic),
            ("geometric_mean", geometric),
            ("median", median)):
        score = competition_score(golden, prediction)
        relative = np.abs(prediction - golden) / np.maximum(golden, 1)
        print(f"ensemble={name} score={score:.6f} "
              f"mape_percent={relative.mean() * 100.0:.6f}")


if __name__ == "__main__":
    main()
