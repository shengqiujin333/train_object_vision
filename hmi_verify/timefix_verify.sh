#!/usr/bin/env bash
# 验收：UI 时间按本机时区显示 + 实机重启到新二进制，真实门磁仍为关门态。
set -u
APP=/home/admin/railhmi-ui-fix/app
OUT=/home/admin/door_verify
mkdir -p "$OUT"

echo "===== 0. 本机时间 ====="
date '+local=%Y-%m-%d %H:%M:%S %Z'; date -u '+utc  =%Y-%m-%d %H:%M:%S UTC'
echo "===== 0b. 核心测试计数 ====="
"$APP/build-core/tests/railhmi-core-tests" 2>&1 | tail -3

echo "===== 1. 实机 :0 重启到新二进制（不注入任何仿真帧） ====="
OLD=$(pgrep -f '^\./build-target/railhmi --config' | head -1)
if [ -n "$OLD" ]; then
  mapfile -d '' -t E < "/proc/$OLD/environ"
  C=()
  for kv in "${E[@]}"; do
    case "$kv" in
      RAILHMI_DOOR_DEBUG=*|RAILHMI_DOOR_SIM_FRAME=*|RAILHMI_DOOR_SIM_FOLLOWUP=*|DISPLAY=*) ;;
      *) C+=("$kv");; esac
  done
  kill "$OLD" 2>/dev/null || true
  for i in 1 2 3 4 5 6 7 8 9 10; do kill -0 "$OLD" 2>/dev/null || break; sleep 1; done
  cd "$APP" || exit 1
  env -i "${C[@]}" DISPLAY=:0 setsid nohup ./build-target/railhmi --config "$APP/config" \
      > /home/admin/hmi_live.log 2>&1 < /dev/null &
  echo "RESTARTED :0 old_pid=$OLD"
fi
sleep 10

echo "===== 2. :99 仿真开门帧 -> 报警中心时间 ====="
if ! xdpyinfo -display :99 >/dev/null 2>&1; then
  setsid Xvfb :99 -screen 0 1280x800x24 -nolisten tcp > "$OUT/xvfb.log" 2>&1 < /dev/null &
  sleep 2
fi
snap99(){ DISPLAY=:99 ffmpeg -y -loglevel error -f x11grab -video_size 1280x800 -i :99 -frames:v 1 "$1" >/dev/null 2>&1; }
cd "$APP" || exit 1
DISPLAY=:99 QT_QUICK_BACKEND=software RAILHMI_DOOR_DEBUG=1 \
  RAILHMI_DOOR_SIM_FRAME=07785634120801635500 \
  timeout 20 ./build-target/railhmi --config "$APP/config" > /home/admin/timefix_verify.log 2>&1 &
pid=$!
sleep 8
DISPLAY=:99 xdotool mousemove 44 180 click 1; sleep 2; snap99 "$OUT/TIMEFIX_p2.png"
DISPLAY=:99 xdotool mousemove 44 92 click 1;  sleep 2; snap99 "$OUT/TIMEFIX_p1.png"
wait "$pid" || true
echo "--- 门磁日志 ---"
grep -a 'door-sensor' /home/admin/timefix_verify.log | grep -aE 'SYNTHETIC|snapshot level' | head -8
echo "--- QML 错误 ---"
grep -a -iE 'qml.*error|TypeError|ReferenceError' /home/admin/timefix_verify.log | head -6
echo "--- 本机时间（截 P2 前后） ---"
date '+local=%Y-%m-%d %H:%M:%S %Z'

echo "===== 3. 实机 :0 真实关门态截图 ====="
snap0(){ DISPLAY=:0 ffmpeg -y -loglevel error -f x11grab -video_size 1280x800 -i :0 -frames:v 1 "$1" >/dev/null 2>&1; }
DISPLAY=:0 xdotool mousemove 44 92 click 1;  sleep 2; snap0 "$OUT/TIMEFIX_LIVE_p1.png"
DISPLAY=:0 xdotool mousemove 44 532 click 1; sleep 2; snap0 "$OUT/TIMEFIX_LIVE_p6.png"
DISPLAY=:0 xdotool mousemove 44 92 click 1;  sleep 1
echo "===== 4. 六路视频与门磁状态 ====="
grep -aE 'video workers started|presentable' /home/admin/hmi_live.log | tail -4
DISPLAY=:0 xrandr 2>/dev/null | grep -m1 '\*' || true
ls -l "$OUT"/TIMEFIX_*.png
