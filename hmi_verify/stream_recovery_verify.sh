#!/usr/bin/env bash
# 断流复现 + 自动重连验收（RK3588，需 sudo iptables）。
# 前置：HMI 已用本轮新二进制在 :0 运行，日志为 hmi_live_round3.log。
# 只屏蔽单路相机的入向包，验证看门狗真实重建、单路隔离、链路恢复后自动出帧。
set -u
LOG=/home/admin/hmi_live_round3.log
[ -f "$LOG" ] || LOG=$(ls -t /home/admin/hmi_live*.log 2>/dev/null | head -1)
OUT=/home/admin/stream_recovery_verify
IP=192.168.1.167
CAM=CAM-06
mkdir -p "$OUT"
echo "LOG=$LOG"

cleanup(){ sudo iptables -D INPUT -s "$IP" -j DROP 2>/dev/null || true; }
trap cleanup EXIT INT TERM
snap0(){ DISPLAY=:0 ffmpeg -y -loglevel error -f x11grab -video_size 1280x800 -i :0 -frames:v 1 "$1" >/dev/null 2>&1; }

echo "===== 0. 前置状态 ====="
date '+%F %T %Z'
pgrep -af '^\./build-target/railhmi --config' | head -3
grep -a 'credentials:' "$LOG" | tail -2
echo "--- 六路初始状态 ---"
tail -n 600 "$LOG" | grep -a 'video channel' | tail -12 || true

echo "===== 1. 屏蔽 $CAM ($IP) 入向包，观察看门狗 ====="
cleanup
sudo iptables -I INPUT -s "$IP" -j DROP
echo "BLOCKED at $(date '+%T')"
sleep 32
echo "--- 看门狗重建日志 ---"
tail -n 30000 "$LOG" | grep -a 'video watchdog: rebuild' | tail -12 || echo "(none)"
echo "--- $CAM 状态变化 ---"
tail -n 30000 "$LOG" | grep -a "video channel \"$CAM\"" | tail -12 || true
echo "--- 其它五路是否被误重建（应为空） ---"
tail -n 30000 "$LOG" | grep -a 'video watchdog: rebuild' | grep -av "\"$CAM\"" | tail -8 || echo "(none)"
# 截图前切回实时监控页：验收图必须能直接看到六路画面，而不是停留在上一轮页面。
DISPLAY=:0 xdotool mousemove 44 92 click 1
sleep 1
snap0 "$OUT/RECOVERY_BLOCKED.png"

echo "===== 2. 恢复链路，等待自动重连出帧 ====="
cleanup
echo "UNBLOCKED at $(date '+%T')"
for i in 1 2 3 4 5 6 7 8; do
  sleep 10
  echo "--- t+$((i*10))s ---"
  tail -n 6000 "$LOG" | grep -a "video channel \"$CAM\"" | tail -3 || true
  tail -n 6000 "$LOG" | grep -a 'video watchdog: rebuild' | tail -1 || true
done
DISPLAY=:0 xdotool mousemove 44 92 click 1
sleep 1
snap0 "$OUT/RECOVERY_RESTORED.png"

echo "===== 3. 结论线索 ====="
echo "A. 屏蔽期间应看到 video watchdog: rebuild \"$CAM\" attempt>=1，backoff_ms 递增"
echo "B. 其它五路不应出现 rebuild（单路隔离）"
echo "C. 恢复后应看到 $CAM 回到 live presentable=true"
echo "D. QML 运行期错误（应 0 行）:"
tail -n 8000 "$LOG" | grep -a -iE 'qml.*error|TypeError|ReferenceError|Cannot assign|is not defined' | head -8 || true
echo "===== DONE ====="
