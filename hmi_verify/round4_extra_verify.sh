#!/usr/bin/env bash
# 第 4 轮补充验收（RK3588 上执行）：
#   A. P3 真正点「播放」后，确认 16:9 + Cover 的回放画面无巨幅黑边
#   B. 门磁「最多 10s 一次」的全量轮询节奏，与事件推送同时生效
# 说明：生产实例不设 RAILHMI_DOOR_DEBUG，故 scan objects 行不打在生产日志里；
#       B 段用独立离屏实例带 debug 开关取证，不影响 :0 的现场画面（最后回到 P1）。
set -u
ROOT=/home/admin/railhmi-ui-fix
APP=$ROOT/app
OUT=/home/admin/round4_verify
LOG=/home/admin/hmi_live_round4.log
mkdir -p "$OUT"

snap0(){ DISPLAY=:0 ffmpeg -y -loglevel error -f x11grab -video_size 1280x800 -i :0 -frames:v 1 "$1" >/dev/null 2>&1; }
click0(){ DISPLAY=:0 xdotool mousemove "$1" "$2" click 1; }

echo "===== A. :0 P3 真实播放 ====="
click0 44 268; sleep 2
click0 289 75; sleep 3; snap0 "$OUT/R4_P3_QUERY2.png"
click0 103 766; sleep 10; snap0 "$OUT/R4_P3_PLAYING.png"
echo "--- 回放 / 解码 / NVR 日志 ---"
grep -aE 'replay|playback|decoder|history|NVR|nvr' "$LOG" | tail -18
echo "--- QML 运行期错误（应为 0 行） ---"
grep -a -iE 'qml.*error|TypeError|ReferenceError|Cannot assign|is not defined' "$LOG" | tail -6 || true
click0 44 92; sleep 1

echo "===== B. 门磁 10s 兜底轮询节奏（离屏 :95 + DOOR_DEBUG） ====="
if ! xdpyinfo -display :95 >/dev/null 2>&1; then
  setsid Xvfb :95 -screen 0 1280x800x24 -nolisten tcp > "$OUT/xvfb95.log" 2>&1 < /dev/null &
  sleep 2
fi
( cd "$APP" && env -i PATH="$PATH" HOME=/home/admin DISPLAY=:95 QT_QPA_PLATFORM=xcb \
    QT_QUICK_BACKEND=software QT_XCB_GL_INTEGRATION=none RAILHMI_WINDOWED=1 \
    RAILHMI_DOOR_DEBUG=1 timeout 36 ./build-target/railhmi --config "$APP/config" \
    > "$OUT/R4_POLL.log" 2>&1 ) &
LPID=$!
wait "$LPID" 2>/dev/null || true
echo "--- scan objects 时间戳（相邻间隔应为 10s，即最多每 10s 一次） ---"
grep -a 'scan objects' "$OUT/R4_POLL.log" | tail -8
echo "--- 同一窗口内的事件推送（不受轮询节奏限制，广播到达即刷新） ---"
grep -a 'EVENT push#' "$OUT/R4_POLL.log" | tail -6
echo "--- 汇总快照（pushes / polls 计数） ---"
grep -a 'snapshot level' "$OUT/R4_POLL.log" | tail -3
echo "--- 离屏实例 QML 错误（应为 0 行） ---"
grep -a -iE 'qml.*error|TypeError|ReferenceError|Cannot assign|is not defined' "$OUT/R4_POLL.log" | head -6 || true
echo "===== DONE ====="
