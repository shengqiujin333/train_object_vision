#!/usr/bin/env bash
# 本轮现场需求板端验收：
#   顶栏 6 项真值 / NVR 真实 TCP 探测 / 车辆状态人工置位 / 门磁最多 10s 一读
#   P1 底部受电弓+轮轨小画面与报警框 / P1+P4 点击单路放大浮层
set -u
ROOT=/home/admin/railhmi-ui-fix
APP=$ROOT/app
OUT=/home/admin/ui_round_verify
LOG=/home/admin/hmi_live_round2.log
mkdir -p "$OUT"

echo "===== 0. 本机时间 ====="
date '+local=%Y-%m-%d %H:%M:%S %Z'; date -u '+utc  =%Y-%m-%d %H:%M:%S UTC'

echo "===== 1. 静态守卫 + 核心测试 + 顶栏探针 ====="
cd "$ROOT" || exit 1
python3 app/tools/check_qml_qt_validity.py --root app/src/ui 2>&1 | tail -2
python3 app/tools/check_no_hardcoded_ui.py --root app/src/ui 2>&1 | tail -2
python3 app/tools/check_packaging.py --root . 2>&1 | tail -2
"$APP/build-core/tests/railhmi-core-tests" 2>&1 | tail -3
echo "--- status-probe mixed ---"
"$APP/build-core/tools/railhmi-status-probe" --scenario mixed --role maintainer 2>&1 | tail -14

echo "===== 2. 实机 :0 重启到本轮新二进制 ====="
OLD=$(pgrep -f '^\./build-target/railhmi --config' | head -1)
C=()
if [ -n "$OLD" ]; then
  mapfile -d '' -t E < "/proc/$OLD/environ"
  for kv in "${E[@]}"; do
    case "$kv" in
      RAILHMI_DOOR_DEBUG=*|RAILHMI_DOOR_SIM_FRAME=*|RAILHMI_DOOR_SIM_FOLLOWUP=*|DISPLAY=*) ;;
      *) C+=("$kv");; esac
  done
  kill "$OLD" 2>/dev/null || true
  for i in 1 2 3 4 5 6 7 8 9 10; do kill -0 "$OLD" 2>/dev/null || break; sleep 1; done
fi
cd "$APP" || exit 1
env -i "${C[@]}" DISPLAY=:0 RAILHMI_DOOR_DEBUG=1 setsid nohup ./build-target/railhmi \
    --config "$APP/config" > "$LOG" 2>&1 < /dev/null &
echo "RESTARTED :0 old_pid=${OLD:-none} new_pid=$!"

echo "===== 3. 布局几何自检（Xvfb :98, RAILHMI_DUMP_LAYOUT=1） ====="
if ! xdpyinfo -display :98 >/dev/null 2>&1; then
  setsid Xvfb :98 -screen 0 1280x800x24 -nolisten tcp > "$OUT/xvfb98.log" 2>&1 < /dev/null &
  sleep 2
fi
( DISPLAY=:98 QT_QUICK_BACKEND=software RAILHMI_DUMP_LAYOUT=1 \
  timeout 12 ./build-target/railhmi --config "$APP/config" > "$OUT/layout98.log" 2>&1 ) &
p98=$!
sleep 3
DISPLAY=:98 xdotool mousemove 44 92 click 1
wait "$p98" || true
grep -a 'LAYOUT' "$OUT/layout98.log" | tail -20

echo "===== 4. 等待实机启动（NVR 首探 1.5s / 门磁 10s 轮询） ====="
sleep 14
echo "--- 新进程 ---"
pgrep -af '^\./build-target/railhmi --config' | head -3
echo "--- NVR / 门磁 / 车辆状态 / 视频日志 ---"
grep -aE 'nvr-status:|vehicle-state:' "$LOG" | head -8
grep -a 'door-sensor:' "$LOG" | grep -aE 'timer|poll|scan objects|snapshot level|SetDiscoveryFilter|StartDiscovery' | head -12
grep -aE 'video workers started|presentable' "$LOG" | tail -3
echo "--- QML 运行期错误 ---"
grep -a -iE 'qml.*error|TypeError|ReferenceError|Cannot assign|is not defined' "$LOG" | head -10 || true

echo "===== 5. 实机截图：P1 / P4 / 放大浮层 ====="
snap0(){ DISPLAY=:0 ffmpeg -y -loglevel error -f x11grab -video_size 1280x800 -i :0 -frames:v 1 "$1" >/dev/null 2>&1; }
click0(){ DISPLAY=:0 xdotool mousemove "$1" "$2" click 1; }

click0 44 92;  sleep 2; snap0 "$OUT/ROUND2_LIVE_p1.png"
click0 44 356; sleep 2; snap0 "$OUT/ROUND2_LIVE_p4.png"
# P1 前方主画面点击 -> 放大浮层
click0 44 92;  sleep 2; click0 490 293; sleep 2; snap0 "$OUT/ROUND2_ZOOM_front.png"
# 点遮罩空白区关闭
click0 30 760; sleep 1; snap0 "$OUT/ROUND2_CLOSE_front.png"
# P1 受电弓小画面点击 -> 放大浮层
click0 251 701; sleep 2; snap0 "$OUT/ROUND2_ZOOM_pantograph.png"
click0 30 760; sleep 1
# P4 第一格点击 -> 放大浮层
click0 44 356; sleep 2; click0 293 241; sleep 2; snap0 "$OUT/ROUND2_ZOOM_p4_cam01.png"
click0 30 760; sleep 1; snap0 "$OUT/ROUND2_CLOSE_p4.png"
# 回到 P1 待命
click0 44 92;  sleep 1

echo "===== 6. 门磁轮询节奏（debug 日志时间戳间隔） ====="
grep -a 'scan objects' "$LOG" | tail -8
echo "===== 7. 截图清单 ====="
ls -l "$OUT"/ROUND2_*.png
