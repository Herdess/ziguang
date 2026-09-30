#!/usr/bin/env python3
"""Check exact From/To leakage between a leading holdout and later training rows."""

from __future__ import annotations

import argparse
from pathlib import Path


def request_key(line: bytes) -> bytes:
    first = line.find(b",")
    second = line.find(b",", first + 1)
    if first < 0 or second < 0:
        raise ValueError("malformed CSV row")
    return line[:second]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("csv", type=Path)
    parser.add_argument("--holdout-rows", type=int, default=1_000_000)
    args = parser.parse_args()

    holdout: set[bytes] = set()
    overlap_rows = 0
    overlap_keys: set[bytes] = set()
    train_rows = 0
    with args.csv.open("rb") as stream:
        stream.readline()
        for index, line in enumerate(stream):
            key = request_key(line)
            if index < args.holdout_rows:
                holdout.add(key)
            else:
                train_rows += 1
                if key in holdout:
                    overlap_rows += 1
                    overlap_keys.add(key)

    print(
        f"holdout_rows={args.holdout_rows} holdout_unique={len(holdout)} "
        f"train_rows={train_rows} overlap_rows={overlap_rows} "
        f"overlap_unique={len(overlap_keys)}"
    )


if __name__ == "__main__":
    main()
