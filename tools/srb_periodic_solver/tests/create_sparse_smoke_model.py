#!/usr/bin/env python3
"""Create a sparse full-size grid file for loader/lookup smoke tests only."""

from __future__ import annotations

import argparse
import base64
from pathlib import Path


MODEL_SIZE = 1_840_254_976


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    prefix_file = Path(__file__).with_name("grid_header_prefix.b64")
    prefix = base64.b64decode(prefix_file.read_text(encoding="ascii"), validate=True)
    if len(prefix) != 1248:
        raise RuntimeError(f"unexpected grid header prefix size: {len(prefix)}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("wb") as stream:
        stream.write(prefix)
        stream.truncate(MODEL_SIZE)
    print(f"created sparse smoke model: {args.output} ({MODEL_SIZE} bytes)")


if __name__ == "__main__":
    main()
