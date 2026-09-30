#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
INPUT="${1:-$HERE/../../EDA-PANGO-SRB-Arch-Delay/data/delay_estimate_ans.csv}"
OUTPUT="${2:-$HERE/output/result.csv}"
MODEL="${MODEL:-$HERE/data/grid208_4x8_u16.bin}"
LIMIT_ARG=()
if [[ -n "${LIMIT:-}" ]]; then LIMIT_ARG=(--limit "$LIMIT"); fi

if [[ ! -f "$MODEL" ]]; then
  echo "模型不存在: $MODEL" >&2
  echo "请建立 data/grid208_4x8_u16.bin 到共享模型文件的符号链接。" >&2
  exit 2
fi
mkdir -p "$(dirname -- "$OUTPUT")"
make -C "$HERE" -j2
exec "$HERE/build/srb_periodic_solver" \
  --input "$INPUT" \
  --model "$MODEL" \
  --output "$OUTPUT" \
  --always-fused-logic \
  "${LIMIT_ARG[@]}"
