#!/usr/bin/env bash
# railhmi 构建入口（ITEM-003）
#
# 用法:
#   ./build.sh Release     # target: aarch64 板端本机构建（Qt + GStreamer + librga + SQLite）
#   ./build.sh Preview     # preview: 主机 x86_64 预览（Qt + 模拟输入；需宿主 Qt 工具链）
#   ./build.sh CoreTests   # core: 仅 railhmi-core/platform 单元测试（最小工具链: C++ 编译器 + CMake）
#   ./build.sh MediaCheck  # media: 仅 railhmi_media target 层（GStreamer + librga，无 Qt）编译检查
#   ./build.sh check       # 只做依赖自检，不构建
#
# 退出码:
#   0  成功
#   1  缺少构建依赖（会逐项打印缺什么，不静默失败）
#   2  参数错误
#   3  CMake 配置或编译失败
#
# 说明:
#   - 不在产品源码里写死开发机路径；构建目录为 app/build-<mode>/。
#   - 板端本机构建（无交叉工具链）；主机预览使用宿主 Qt。
#   - 主机可编译不等于目标板可运行。

set -u

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

usage() {
  sed -n '2,18p' "$0"
}

MODE="${1:-}"
case "$MODE" in
  Release|Target|target)   NAME="target";  BUILD_DIR="$SCRIPT_DIR/build-target";  CMAKE_ARGS="-DRAILHMI_BUILD_TARGET=ON" ;;
  Preview|preview)         NAME="preview"; BUILD_DIR="$SCRIPT_DIR/build-preview"; CMAKE_ARGS="-DRAILHMI_BUILD_PREVIEW=ON -DRAILHMI_SIMULATION=ON" ;;
  CoreTests|coretests|Core|core) NAME="core-tests"; BUILD_DIR="$SCRIPT_DIR/build-core"; CMAKE_ARGS="-DRAILHMI_BUILD_CORE_TESTS=ON" ;;
  MediaCheck|media)        NAME="media-check"; BUILD_DIR="$SCRIPT_DIR/build-media"; CMAKE_ARGS="-DRAILHMI_BUILD_MEDIA=ON" ;;
  check)                   NAME="check";   BUILD_DIR=""; CMAKE_ARGS="" ;;
  -h|--help|"")            usage; exit 2 ;;
  *) echo "build.sh: unknown mode '$MODE' (expected Release|Preview|CoreTests|check)" >&2; usage; exit 2 ;;
esac

have() { command -v "$1" >/dev/null 2>&1; }

MISSING=()

require_cmd() { # name
  if ! have "$1"; then MISSING+=("$1"); fi
}

# 基础构建工具
require_cmd cmake
CXX=""
for c in c++ g++ clang++; do if have "$c"; then CXX="$c"; break; fi; done
[ -n "$CXX" ] || MISSING+=("C++ compiler (c++/g++/clang++)")

# 平台/目标依赖自检
# Qt Quick/Qml 需要专门的开发包（qtdeclarative5-dev），仅有 qtbase5-dev(含 qmake) 不够。
HAVE_QML="no"
for d in /usr/lib/aarch64-linux-gnu/cmake/Qt5Quick /usr/lib/x86_64-linux-gnu/cmake/Qt5Quick /usr/lib/cmake/Qt5Quick; do
  [ -f "$d/Qt5QuickConfig.cmake" ] && HAVE_QML="yes"
done
if [ "$HAVE_QML" = "no" ] && have qmake; then
  QLIB=$(qmake -query QT_INSTALL_LIBS 2>/dev/null || true)
  [ -n "$QLIB" ] && [ -f "$QLIB/cmake/Qt5Quick/Qt5QuickConfig.cmake" ] && HAVE_QML="yes"
fi
HAVE_GST="no"; if have pkg-config && pkg-config --exists gstreamer-1.0 2>/dev/null; then HAVE_GST="yes"; fi
HAVE_RGA="no"; if { have pkg-config && pkg-config --exists librga 2>/dev/null; } || [ -f /usr/include/rga/im2d.h ] || [ -f /usr/local/include/rga/im2d.h ]; then HAVE_RGA="yes"; fi
HAVE_SQLITE="no"; if { have pkg-config && pkg-config --exists sqlite3 2>/dev/null; } || [ -f /usr/include/sqlite3.h ]; then HAVE_SQLITE="yes"; fi

echo "[build.sh] mode=$NAME host=$(uname -m 2>/dev/null || echo unknown) compiler=${CXX:-none} cmake=$(cmake --version 2>/dev/null | head -n1)"

if [ "$NAME" = "target" ]; then
  [ "$HAVE_QML" = "yes" ]   || MISSING+=("Qt5 Quick/Qml dev (qtbase5-dev qtdeclarative5-dev qml-module-qtquick2)")
  [ "$HAVE_GST" = "yes" ]   || MISSING+=("GStreamer 1.0 dev (gstreamer-1.0/app/video + gst-inspect)")
  [ "$HAVE_RGA" = "yes" ]   || MISSING+=("librga dev (im2d.h + librga; pkg-config librga)")
  [ "$HAVE_SQLITE" = "yes" ] || echo "[build.sh] note: SQLite3 dev not found -> SqliteStore disabled (install libsqlite3-dev to enable)"
fi
if [ "$NAME" = "preview" ]; then
  [ "$HAVE_QML" = "yes" ] || MISSING+=("Qt5 Quick/Qml dev for host preview (qtbase5-dev qtdeclarative5-dev qml-module-qtquick2)")
fi
if [ "$NAME" = "media-check" ]; then
  [ "$HAVE_GST" = "yes" ] || MISSING+=("GStreamer 1.0 dev (gstreamer-1.0/app/video)")
  [ "$HAVE_RGA" = "yes" ] || MISSING+=("librga dev (im2d.h + librga)")
fi

if [ "${#MISSING[@]}" -gt 0 ]; then
  echo "[build.sh] MISSING BUILD DEPENDENCIES (mode=$NAME):" >&2
  for m in "${MISSING[@]}"; do echo "  - $m" >&2; done
  echo "[build.sh] 依赖缺失，未执行构建（非静默失败）。请按上表安装或改用其它档位。" >&2
  exit 1
fi
echo "[build.sh] dependency check: Qt=${HAVE_QML} gstreamer=${HAVE_GST} librga=${HAVE_RGA} sqlite3=${HAVE_SQLITE}"

if [ "$NAME" = "check" ]; then
  echo "[build.sh] check complete (no build requested)"
  exit 0
fi

echo "[build.sh] configuring: cmake -S '$SCRIPT_DIR' -B '$BUILD_DIR' -DCMAKE_BUILD_TYPE=Release $CMAKE_ARGS"
if ! cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release $CMAKE_ARGS; then
  echo "[build.sh] CMake configure failed (mode=$NAME)" >&2
  exit 3
fi
JOBS=$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)
echo "[build.sh] building: cmake --build '$BUILD_DIR' -j$JOBS"
if ! cmake --build "$BUILD_DIR" -j"$JOBS"; then
  echo "[build.sh] build failed (mode=$NAME)" >&2
  exit 3
fi

echo "[build.sh] build OK (mode=$NAME); artifacts under $BUILD_DIR"
if [ "$NAME" = "core-tests" ]; then
  echo "[build.sh] run: $BUILD_DIR/tests/railhmi-core-tests"
fi
if [ "$NAME" = "media-check" ]; then
  echo "[build.sh] media layer: $BUILD_DIR/src/media/librailhmi_media.a"
fi
exit 0
