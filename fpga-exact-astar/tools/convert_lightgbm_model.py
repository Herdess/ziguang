#!/usr/bin/env python3
"""Convert a LightGBM text model to the compact SRB inference format."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

import lightgbm as lgb
import numpy as np


HEADER = struct.Struct("<8s6I")
NODE_V1 = struct.Struct("<iidIHBB")
NODE_V3 = struct.Struct("<iiiHBB")


def build_compact(model: lgb.Booster, storage: str):
    dumped = model.dump_model()
    nodes: list[tuple[int, int, int, int, int, int]] = []
    leaves: list[float] = []
    category_data: list[int] = []
    roots: list[int] = []

    def add_leaf(value: float) -> int:
        index = len(leaves)
        leaves.append(float(value))
        return -index - 1

    def add_node(item: dict[str, object]) -> int:
        if "leaf_index" in item:
            return add_leaf(float(item["leaf_value"]))
        index = len(nodes)
        nodes.append((0, 0, 0, 0, 0, 0))
        left = add_node(item["left_child"])
        right = add_node(item["right_child"])
        categorical = item["decision_type"] == "=="
        category_offset = 0
        category_count = 0
        decision = 0
        if categorical:
            values = sorted(int(value) for value in str(item["threshold"]).split("||"))
            if values and (values[0] < 0 or values[-1] > 65535):
                raise ValueError("categorical value does not fit uint16")
            decision = len(category_data)
            if storage == "compact":
                category_count = len(values)
                category_data.extend(values)
            else:
                category_count = values[-1] // 64 + 1 if values else 0
                masks = [0] * category_count
                for value in values:
                    masks[value // 64] |= 1 << (value % 64)
                category_data.extend(masks)
            if category_count > 65535:
                raise ValueError("category data does not fit uint16 count")
        else:
            # Every runtime feature is integral.  For integer x,
            # x <= floating_threshold is exactly x <= floor(threshold).
            decision = int(np.floor(float(item["threshold"])))
        feature = int(item["split_feature"])
        if feature > 255:
            raise ValueError("feature index does not fit uint8")
        nodes[index] = (
            left, right, decision, category_count, feature, int(categorical))
        return index

    for tree in dumped["tree_info"]:
        roots.append(add_node(tree["tree_structure"]))
    return dumped["feature_names"], roots, nodes, leaves, category_data


def compact_predict(rows: np.ndarray, roots, nodes, leaves, category_data,
                    storage: str) -> np.ndarray:
    result = np.zeros(rows.shape[0], dtype=np.float64)
    for row_index, row in enumerate(rows):
        total = 0.0
        for root in roots:
            node_index = root
            while node_index >= 0:
                left, right, decision, count, feature, categorical = nodes[node_index]
                value = int(row[feature])
                if categorical:
                    category = int(value)
                    if storage == "compact":
                        values = category_data[decision:decision + count]
                        go_left = category in values
                    else:
                        word = category // 64
                        go_left = (0 <= category and word < count and
                                   (category_data[decision + word] >> (category % 64)) & 1)
                else:
                    go_left = value <= decision
                node_index = left if go_left else right
            total += leaves[-node_index - 1]
        result[row_index] = total
    return result


def write_binary(path: Path, feature_names, roots, nodes, leaves, category_data,
                 storage: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as handle:
        version = 1 if storage == "compact" else 3
        magic = b"SRBGBM1\0" if storage == "compact" else b"SRBGBM3\0"
        handle.write(HEADER.pack(
            magic, version, len(feature_names), len(roots), len(nodes),
            len(leaves), len(category_data)))
        handle.write(struct.pack(f"<{len(roots)}I", *roots))
        for node in nodes:
            if storage == "compact":
                left, right, decision, count, feature, categorical = node
                handle.write(NODE_V1.pack(
                    left, right, float(decision) if not categorical else 0.0,
                    decision if categorical else 0, count, feature, categorical))
            else:
                handle.write(NODE_V3.pack(*node))
        handle.write(struct.pack(f"<{len(leaves)}d", *leaves))
        value_format = "H" if storage == "compact" else "Q"
        handle.write(struct.pack(f"<{len(category_data)}{value_format}", *category_data))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input-model", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--verify-rows", type=int, default=128)
    parser.add_argument("--storage", choices=("compact", "bitmap"), default="compact")
    args = parser.parse_args()

    model = lgb.Booster(model_file=str(args.input_model))
    feature_names, roots, nodes, leaves, category_data = build_compact(
        model, args.storage)
    write_binary(
        args.output, feature_names, roots, nodes, leaves, category_data, args.storage)

    if args.verify_rows:
        rng = np.random.default_rng(20260929)
        rows = rng.integers(0, 1024, size=(args.verify_rows, len(feature_names))).astype(
            np.float32)
        expected = model.predict(rows, raw_score=True)
        actual = compact_predict(
            rows, roots, nodes, leaves, category_data, args.storage)
        difference = np.max(np.abs(expected - actual))
        if difference > 1e-12:
            raise RuntimeError(f"compact predictor mismatch: {difference}")
        print(f"verification_rows={args.verify_rows} max_abs_difference={difference:.3g}")

    print(f"features={len(feature_names)} trees={len(roots)} nodes={len(nodes)} "
          f"leaves={len(leaves)} category_data={len(category_data)} "
          f"storage={args.storage}")
    print(f"binary_bytes={args.output.stat().st_size} output={args.output}")


if __name__ == "__main__":
    main()
