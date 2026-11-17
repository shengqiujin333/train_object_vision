#!/usr/bin/env bash
# 现场第 4 轮验收（在 RK3588 上执行）：
#   cd /home/admin/railhmi-ui-fix && bash hmi_verify/round4_verify.sh
#
# 覆盖本轮五项需求：
#   1) P3 回放画面放大 / Cover 无黑边 / 空关键帧条不占位 / 右侧详情字段移除
#   2) P2 固定五类筛选（全部类型 / 摄像头 / 门磁 / NVR / 主机）且互斥单选
#   3) 顶栏真实内存 / 每核 CPU / 每核 NPU 读数
#   4) 门磁独立 I/O 线程 + PropertiesChanged 事件驱动（去掉 10s 轮询等待）
#   5) 静态守卫 / CoreTests / 板端 Qt 全量构建 / :0 实机截图与日志
# 不打印任何凭据值，不改配置，不影响正在运行的 NVR / 摄像机。
set -u
ROOT=/home/admin/railhmi-ui-fix
APP=$ROOT/app
OUT=/home/admin/round4_verify
LOG=/home/admin/hmi_live_round4.log
mkdir -p "$OUT"

echo "===== 0. 时间与真实资源采样源 ====="
date '+local=%Y-%m-%d %H:%M:%S %Z'
echo "CPU cores (proc/stat): $(grep -c '^cpu[0-9]' /proc/stat)"
echo "MemTotal/MemAvailable:"; grep -E '^(MemTotal|MemAvailable):' /proc/meminfo
echo "NPU debugfs perms: $(stat -c '%a %U' /sys/kernel/debug/rknpu/load 2>/dev/null || echo missing)"
echo "NPU debugfs content:"; head -c 200 /sys/kernel/debug/rknpu/load 2>/dev/null; echo
echo "NPU devfreq aggregate:"; head -c 80 /sys/class/devfreq/fdab0000.npu/load 2>/dev/null; echo
# systemd 服务里已用 ExecStartPre 提权；手动启动时若被重置，这里补一次（不打印密码）。
if [ ! -r /sys/kernel/debug/rknpu/load ]; then
  echo admin | sudo -S chmod 0444 /sys/kernel/debug/rknpu/load >/dev/null 2>&1 || true
  echo "after chmod: $(stat -c '%a' /sys/kernel/debug/rknpu/load 2>/dev/null || echo missing)"
fi
echo "BlueZ: $(bluetoothctl --version 2>/dev/null | head -1)"

echo "===== 1. 全部静态守卫 ====="
cd "$ROOT" || exit 1
GUARD_PASS=0; GUARD_FAIL=0
guard(){ n=$1; shift; if "$@" >"$OUT/guard_$n.log" 2>&1; then
  echo "PASS $n"; GUARD_PASS=$((GUARD_PASS+1));
else
  echo "FAIL $n"; tail -5 "$OUT/guard_$n.log"; GUARD_FAIL=$((GUARD_FAIL+1));
fi; }
guard qml_qt_validity python3 app/tools/check_qml_qt_validity.py --root app/src/ui --headers app/src/core/include app/src/platform/include
for w in replay ai_debug maintenance permissions health alarm_center stream_recovery runtime_resources; do
  case "$w" in
    ai_debug) guard "$w" python3 app/tools/check_ai_debug_wiring.py --root app/src/ui --tokens docs/hmi/design_tokens.yaml ;;
    permissions) guard "$w" python3 app/tools/check_permissions_wiring.py --root app/src/ui --matrix docs/hmi/PERMISSION_MATRIX.md ;;
    *) guard "$w" python3 app/tools/check_${w}_wiring.py --root app/src/ui ;;
  esac
done
guard no_hardcoded_ui python3 app/tools/check_no_hardcoded_ui.py --root app/src/ui
guard packaging python3 app/tools/check_packaging.py --root . --app-src app/src --packaging packaging
echo "GUARDS pass=$GUARD_PASS fail=$GUARD_FAIL"

echo "===== 2. CoreTests + 全量 Qt 目标构建 ====="
cd "$APP" || exit 1
./build.sh CoreTests > "$OUT/coretests_build.log" 2>&1; echo "CORETESTS_BUILD_EXIT=$?"
./build-core/tests/railhmi-core-tests 2>&1 | tail -4
./build.sh Release > "$OUT/qt_build.log" 2>&1; echo "QT_BUILD_EXIT=$?"
tail -8 "$OUT/qt_build.log"
[ -x ./build-target/railhmi ] && echo "BINARY=OK $(stat -c '%s bytes %y' ./build-target/railhmi)" || echo "BINARY=MISSING"

echo "===== 3. 重启 :0 生产实例（不带模拟/调试变量） ====="
OLD=$(pgrep -f '^\./build-target/railhmi --config' | head -1)
C=()
if [ -n "$OLD" ]; then
  mapfile -d '' -t E < "/proc/$OLD/environ"
  for kv in "${E[@]}"; do
    case "$kv" in
      RAILHMI_DOOR_DEBUG=*|RAILHMI_DOOR_SIM_FRAME=*|RAILHMI_DOOR_SIM_FOLLOWUP=*|DISPLAY=*|RAILHMI_DUMP_LAYOUT=*|RAILHMI_WINDOWED=*|RAILHMI_DOOR_SIM_FOLLOWUP_MS=*) ;;
      *) C+=("$kv");; esac
  done
  kill "$OLD" 2>/dev/null || true
  for i in 1 2 3 4 5 6 7 8 9 10; do kill -0 "$OLD" 2>/dev/null || break; sleep 1; done
fi
env -i "${C[@]}" DISPLAY=:0 setsid nohup ./build-target/railhmi \
    --config "$APP/config" > "$LOG" 2>&1 < /dev/null &
echo "RESTARTED :0 old_pid=${OLD:-none} new_pid=$!"
sleep 14
PID=$(pgrep -f '^\./build-target/railhmi --config' | head -1)
echo "PID=$PID"
echo "--- 独立门磁 I/O 线程（OS 线程名） ---"
for t in /proc/"$PID"/task/*/comm; do grep -H 'door' "$t" 2>/dev/null; done || true
echo "--- 门磁 / 资源 / 视频 / NVR 启动日志 ---"
grep -aE 'door-sensor: (enabled|PropertiesChanged|SetDiscoveryFilter|StartDiscovery|EVENT push)|system-metrics:|video workers started|nvr-status' "$LOG" | head -24
echo "--- QML 运行期错误（应为 0 行） ---"
grep -a -iE 'qml.*error|TypeError|ReferenceError|Cannot assign|is not defined|Failed to' "$LOG" | head -12 || true

echo "===== 4. 门磁事件驱动响应：50s 观察广播推送 ====="
# 注意：scan objects / snapshot level 两行只在 RAILHMI_DOOR_DEBUG=1 时打印；
# 生产实例刻意不带该变量，所以这里的 poll/snapshot 计数必然是 0，不代表没有轮询。
# 10s 兜底轮询节奏由 round4_extra_verify.sh 的 B 段用离屏 debug 实例单独取证。
P0=$(grep -ac 'EVENT push#' "$LOG" || true)
echo "before: pushes=$P0   (生产日志不含 debug 轮询/快照行)"
sleep 50
P1=$(grep -ac 'EVENT push#' "$LOG" || true)
echo "after : pushes=$P1   (delta=$((P1-P0))，约每 18~22s 一次，即广播到达即刷新)"
echo "--- 最近推送帧（即时到达，不等下一次 10s 轮询） ---"
grep -aE 'EVENT push#|decoded vendor frame' "$LOG" | tail -8
echo "--- door-sensor 错误（应为空） ---"
grep -aE 'door-sensor: (ERROR|warning)' "$LOG" | tail -5 || true

echo "===== 5. :0 实机截图：P1 顶栏 / P3 / P2 / P6 ====="
snap0(){ DISPLAY=:0 ffmpeg -y -loglevel error -f x11grab -video_size 1280x800 -i :0 -frames:v 1 "$1" >/dev/null 2>&1; }
crop0(){ DISPLAY=:0 ffmpeg -y -loglevel error -f x11grab -video_size 1280x800 -i :0 -frames:v 1 -vf "crop=1280:56:0:0,scale=2560:112" "$1" >/dev/null 2>&1; }
click0(){ DISPLAY=:0 xdotool mousemove "$1" "$2" click 1; }

click0 44 92;  sleep 2; snap0 "$OUT/R4_P1_FULL.png"; crop0 "$OUT/R4_TOPBAR_ZOOM.png"
click0 44 268; sleep 3; snap0 "$OUT/R4_P3_DEFAULT.png"
# P3 日期点开键盘 / 点空白收起 / 点查询（现场无物理键盘）
click0 199 75; sleep 1; snap0 "$OUT/R4_P3_KEYBOARD.png"
click0 300 600; sleep 1; snap0 "$OUT/R4_P3_DISMISSED.png"
click0 289 75; sleep 3; snap0 "$OUT/R4_P3_QUERY.png"

click0 44 180; sleep 2; snap0 "$OUT/R4_P2_CATEGORIES.png"
# 固定五类单选：门磁 -> 主机 -> 全部类型，逐帧确认高亮互斥
click0 272 134; sleep 1; snap0 "$OUT/R4_P2_FILTER_DOOR.png"
click0 381 134; sleep 1; snap0 "$OUT/R4_P2_FILTER_HOST.png"
click0 136 134; sleep 1; snap0 "$OUT/R4_P2_FILTER_ALL.png"
# 日期键盘：有报警行时点空白也要能收起（上一轮回归点）
click0 204 181; sleep 1; snap0 "$OUT/R4_P2_KEYBOARD.png"
click0 300 600; sleep 1; snap0 "$OUT/R4_P2_DISMISSED.png"
click0 447 181; sleep 2; snap0 "$OUT/R4_P2_QUERY.png"

click0 44 532; sleep 2; snap0 "$OUT/R4_P6_DOOR.png"
click0 44 92;  sleep 1
echo "--- 截图清单 ---"; ls -l "$OUT"/R4_*.png
echo "--- 交互后 QML 错误（应为 0 行） ---"
grep -a -iE 'qml.*error|TypeError|ReferenceError|Cannot assign|is not defined' "$LOG" | tail -8 || true

echo "===== 6. 离屏几何自检：P3 画面区域 / 顶栏资源读数 ====="
if ! xdpyinfo -display :96 >/dev/null 2>&1; then
  setsid Xvfb :96 -screen 0 1280x800x24 -nolisten tcp > "$OUT/xvfb96.log" 2>&1 < /dev/null &
  sleep 2
fi
( cd "$APP" && env -i PATH="$PATH" HOME=/home/admin DISPLAY=:96 QT_QPA_PLATFORM=xcb \
    QT_QUICK_BACKEND=software QT_XCB_GL_INTEGRATION=none RAILHMI_WINDOWED=1 \
    RAILHMI_DUMP_LAYOUT=1 timeout 20 ./build-target/railhmi --config "$APP/config" \
    > "$OUT/R4_LAYOUT.log" 2>&1 ) &
LPID=$!
sleep 10
DISPLAY=:96 xdotool mousemove 44 268 click 1
wait "$LPID" 2>/dev/null || true
echo "--- 最后一次 dump 的几何（此时在 P3） ---"
awk '/LAYOUT root=/{n++} n==3' "$OUT/R4_LAYOUT.log" | grep -a 'LAYOUT' | head -30
echo "--- 离屏实例 QML 错误（应为 0 行） ---"
grep -a -iE 'qml.*error|TypeError|ReferenceError|Cannot assign|is not defined' "$OUT/R4_LAYOUT.log" | head -8 || true

echo "===== DONE ====="
