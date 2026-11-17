#!/usr/bin/env bash
# 验收：P1 删除与顶栏重复的关键状态行后，布局无大片留白、五格画面与报警框完好。
set -u
APP=/home/admin/railhmi-ui-fix/app
OUT=/home/admin/p1_strip_verify
LOG=/home/admin/hmi_live.log
mkdir -p "$OUT"

echo "===== 1. 实机 :0 重启到新二进制（干净环境） ====="
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
env -i "${C[@]}" DISPLAY=:0 setsid nohup ./build-target/railhmi --config "$APP/config" \
  > "$LOG" 2>&1 < /dev/null &
echo "RESTARTED old=${OLD:-none} new=$!"
sleep 14

echo "===== 2. 布局几何自检（Xvfb :98） ====="
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
grep -a 'LAYOUT' "$OUT/layout98.log" | tail -14

echo "===== 3. 真值 / 错误日志 ====="
pgrep -af '^\./build-target/railhmi --config' | head -2
grep -aE 'nvr-status:|door-sensor: enabled|video workers started' "$LOG" | head -5
echo "--- QML 错误数 ---"
grep -a -icE 'qml.*error|TypeError|ReferenceError|is not defined' "$LOG" || true
echo "--- 残留关键状态对象（QML 源码层，应为 0） ---"
grep -c 'statusStrip\|KeyStatusStrip\|statusNavigate' "$APP/src/ui/qml/pages/P1Realtime.qml" || true

echo "===== 4. 实机截图 P1 / P4 ====="
snap(){ DISPLAY=:0 ffmpeg -y -loglevel error -f x11grab -video_size 1280x800 -i :0 -frames:v 1 "$OUT/$1" >/dev/null 2>&1; }
DISPLAY=:0 xdotool mousemove 44 92 click 1;  sleep 2; snap P1_NOSTRIP.png
DISPLAY=:0 xdotool mousemove 44 356 click 1; sleep 2; snap P4_NOSTRIP.png
DISPLAY=:0 xdotool mousemove 44 92 click 1;  sleep 1
ls -l "$OUT"/P*_NOSTRIP.png
