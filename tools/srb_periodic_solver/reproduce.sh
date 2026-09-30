#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
LIMIT="${LIMIT:-1000}" "$HERE/run.sh" \
  "${1:-$HERE/../../EDA-PANGO-SRB-Arch-Delay/data/delay_estimate_ans.csv}" \
  "${2:-$HERE/output/repro_result.csv}"
