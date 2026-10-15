#!/bin/sh
# railhmi 依赖 / 构建环境核验入口 (ITEM-001)
#
# 在目标板 (RK3588, Ubuntu 22.04 aarch64) 或主机上运行只读依赖探针，
# 输出 Markdown 报告 + 原始日志 + 机器可读 JSON，便于归档与对比。
#
# 用法:
#   ./run_env_check.sh [输出目录]      # 默认 docs/hmi/test_evidence/ENV-B-dependency
#   ./run_env_check.sh out --probe-cameras   # 显式开启相机端口探测 (需授权)
#
# 说明:
#   - 只读探测，不安装/不修改系统。
#   - 主机结果与目标板结果必须分开记录；主机可运行不代表目标板可运行。
#   - 相机/NVR 网络探测默认关闭。

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PY=${PYTHON:-python3}
command -v "$PY" >/dev/null 2>&1 || PY=python

OUT_DIR=${1:-"$SCRIPT_DIR/../../docs/hmi/test_evidence/ENV-B-dependency"}
shift 2>/dev/null || true

mkdir -p "$OUT_DIR"
STAMP=$(date +%Y%m%d-%H%M%S 2>/dev/null || echo manual)
HOST=$(hostname 2>/dev/null || echo unknown)
BASE="$OUT_DIR/env-probe-$HOST-$STAMP"

echo "[run_env_check] python=$PY"
echo "[run_env_check] out=$BASE.md"

"$PY" "$SCRIPT_DIR/env_probe.py" \
  --out "$BASE.md" \
  --raw-out "$BASE.raw.txt" \
  --json-out "$BASE.json" \
  "$@" >/dev/null

echo "[run_env_check] wrote:"
echo "  $BASE.md"
echo "  $BASE.raw.txt"
echo "  $BASE.json"
