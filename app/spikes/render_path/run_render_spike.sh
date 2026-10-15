#!/usr/bin/env bash
# railhmi 渲染路径选型 spike 驱动 (ITEM-002)
#
# 在目标板 (RK3588) 上用一路 RTSP 依次实测 path=gl 与 path=cpu，
# 收集 fps/CPU/GPU/色彩 JSON，调用 analyze.py 给出选择，并写 Markdown 摘要。
#
# 用法:
#   ./run_render_spike.sh --url 'rtsp://...' [--duration 20] [--outdir DIR]
#                          [--min-fps 20] [--snapshot] [--skip-build] [--dry-run]
#
# 说明:
#   - 默认强制 UDP 由 spike 管线内置；相机凭据来自项目配置/口令，不写死。
#   - --dry-run 只打印计划，不构建不运行 (可在无板环境校验入口)。
#   - 无目标板/无相机时不要执行真实测量；本脚本不假定任何设备可用。

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# 解释器解析: 默认 python3; 若不存在或“存在但不可用”(如 Windows Store shim 报错) 则回退 python
PY=${PYTHON:-python3}
if ! command -v "$PY" >/dev/null 2>&1 || ! "$PY" -c 'pass' >/dev/null 2>&1; then
  PY=python
fi
URL=""
SOURCE="rtsp"
FILE=""
DURATION=20
OUTDIR="$SCRIPT_DIR/../../../docs/hmi/test_evidence/ITEM-002-render-path"
MIN_FPS=20
SNAPSHOT=0
SKIP_BUILD=0
DRY_RUN=0
BUILD_DIR="$SCRIPT_DIR/build"

while [ $# -gt 0 ]; do
  case "$1" in
    --url) URL="$2"; shift 2;;
    --source) SOURCE="$2"; shift 2;;
    --file) FILE="$2"; shift 2;;
    --duration) DURATION="$2"; shift 2;;
    --outdir) OUTDIR="$2"; shift 2;;
    --min-fps) MIN_FPS="$2"; shift 2;;
    --snapshot) SNAPSHOT=1; shift;;
    --skip-build) SKIP_BUILD=1; shift;;
    --dry-run) DRY_RUN=1; shift;;
    -h|--help) sed -n '2,20p' "$0"; exit 0;;
    *) echo "unknown arg: $1" >&2; exit 2;;
  esac
done

BIN="$BUILD_DIR/render_spike"
STAMP=$(date +%Y%m%d-%H%M%S 2>/dev/null || echo manual)

echo "[render-spike] source=$SOURCE url=${URL:-<none>} file=${FILE:-<none>} duration=${DURATION}s outdir=$OUTDIR"

if [ "$DRY_RUN" -eq 1 ]; then
  echo "[render-spike] DRY-RUN plan:"
  echo "  1) build: cmake -S '$SCRIPT_DIR' -B '$BUILD_DIR' -DCMAKE_BUILD_TYPE=Release && cmake --build '$BUILD_DIR' -j"
  echo "  2) run:   '$BIN' --path gl  --source $SOURCE --url '<url>' --file '<file>' --duration $DURATION --out '$OUTDIR/gl-$STAMP.json'"
  echo "  3) run:   '$BIN' --path cpu --source $SOURCE --url '<url>' --file '<file>' --duration $DURATION --out '$OUTDIR/cpu-$STAMP.json'"
  echo "  4) decide: '$PY' '$SCRIPT_DIR/analyze.py' --gl ... --cpu ... --min-fps $MIN_FPS --out '$OUTDIR/selection-$STAMP.json' --md-out '$OUTDIR/selection-$STAMP.md'"
  echo "[render-spike] dry-run OK (no build, no device access)"
  exit 0
fi

if [ "$SOURCE" = "rtsp" ] && [ -z "$URL" ]; then
  echo "ERROR: --url is required for --source rtsp" >&2; exit 2
fi
if [ "$SOURCE" = "file" ] && [ -z "$FILE" ]; then
  echo "ERROR: --file is required for --source file" >&2; exit 2
fi

mkdir -p "$OUTDIR"

if [ "$SKIP_BUILD" -eq 0 ] && [ ! -x "$BIN" ]; then
  echo "[render-spike] building..."
  cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
  cmake --build "$BUILD_DIR" -j"$(nproc 2>/dev/null || echo 2)"
fi

if [ ! -x "$BIN" ]; then echo "ERROR: $BIN missing (build failed or --skip-build)" >&2; exit 3; fi

GL_JSON="$OUTDIR/gl-$STAMP.json"
CPU_JSON="$OUTDIR/cpu-$STAMP.json"
GL_SNAP=""; CPU_SNAP=""
if [ "$SNAPSHOT" -eq 1 ]; then
  GL_SNAP="$OUTDIR/gl-$STAMP.png"; CPU_SNAP="$OUTDIR/cpu-$STAMP.png"
fi

echo "[render-spike] path=gl"
"$BIN" --path gl --source "$SOURCE" --url "$URL" --file "$FILE" --duration "$DURATION" \
  --out "$GL_JSON" ${GL_SNAP:+--snapshot "$GL_SNAP"} || echo "[render-spike] gl exited non-zero (recorded in JSON)"

echo "[render-spike] path=cpu"
"$BIN" --path cpu --source "$SOURCE" --url "$URL" --file "$FILE" --duration "$DURATION" \
  --out "$CPU_JSON" ${CPU_SNAP:+--snapshot "$CPU_SNAP"} || echo "[render-spike] cpu exited non-zero (recorded in JSON)"

SEL_JSON="$OUTDIR/selection-$STAMP.json"
SEL_MD="$OUTDIR/selection-$STAMP.md"
echo "[render-spike] deciding..."
set +e
"$PY" "$SCRIPT_DIR/analyze.py" --gl "$GL_JSON" --cpu "$CPU_JSON" --min-fps "$MIN_FPS" \
  --out "$SEL_JSON" --md-out "$SEL_MD"
RC=$?
set -e
echo "[render-spike] analyze exit=$RC"
echo "[render-spike] artifacts:"
echo "  $GL_JSON"
echo "  $CPU_JSON"
echo "  $SEL_JSON"
echo "  $SEL_MD (markdown fragment for build_env_check.md §14)"
exit 0
