#!/usr/bin/env bash
# railhmi-core 单元测试单命令入口（ITEM-008）
#
# 构建（板端/最小工具链本机）并运行 railhmi-core-tests，归档完整日志与 JUnit XML，
# 打印逐项 [PASS]/[FAIL] 与最终退出码。测试不依赖 Qt/GStreamer/相机；
# SQLite 存在时启用持久化测试（RAILHMI_HAVE_SQLITE），缺失时自动跳过。
#
# 用法:
#   app/tests/run_core_tests.sh [--outdir DIR] [--stamp TAG] [--no-build]
#
# 产物（默认 outdir=app/tests/evidence）:
#   core-tests-<stamp>.log   完整 stdout（逐项 + 汇总 + RESULT 行）
#   core-tests-<stamp>.xml   JUnit XML（可被 CI 消费）
#
# 退出码: 0 全部通过；1 有失败；2 参数错误；3 构建或二进制缺失。

set -u

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
APP_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)

OUTDIR="$SCRIPT_DIR/evidence"
STAMP=$(date +%Y%m%d-%H%M%S 2>/dev/null || echo manual)
NO_BUILD=0

while [ $# -gt 0 ]; do
  case "$1" in
    --outdir) OUTDIR="$2"; shift 2;;
    --stamp) STAMP="$2"; shift 2;;
    --no-build) NO_BUILD=1; shift;;
    -h|--help) sed -n '2,16p' "$0"; exit 0;;
    *) echo "run_core_tests: unknown arg '$1'" >&2; exit 2;;
  esac
done

mkdir -p "$OUTDIR"
LOG="$OUTDIR/core-tests-$STAMP.log"
XML="$OUTDIR/core-tests-$STAMP.xml"
BIN="$APP_DIR/build-core/tests/railhmi-core-tests"

echo "[run_core_tests] app=$APP_DIR outdir=$OUTDIR stamp=$STAMP"

if [ "$NO_BUILD" -eq 0 ]; then
  echo "[run_core_tests] building (build.sh CoreTests) ..."
  if ! bash "$APP_DIR/build.sh" CoreTests; then
    echo "[run_core_tests] build FAILED" >&2
    exit 3
  fi
fi

if [ ! -x "$BIN" ]; then
  echo "[run_core_tests] missing test binary: $BIN (build first or drop --no-build)" >&2
  exit 3
fi

echo "[run_core_tests] running $BIN --junit $XML"
"$BIN" --junit "$XML" 2>&1 | tee "$LOG"
RC=${PIPESTATUS[0]}

echo "[run_core_tests] log=$LOG"
echo "[run_core_tests] junit=$XML"
echo "[run_core_tests] exit=$RC"
exit "$RC"
