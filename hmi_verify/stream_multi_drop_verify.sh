#!/usr/bin/env bash
# 多路同时断流复现 + 自动重连验收（RK3588，需 sudo iptables）。
# 同时屏蔽 CAM-05(192.168.1.165) 与 CAM-06(192.168.1.167) 的入向包，
# 验证两路看门狗各自独立重建、其余四路不受影响、解除后两路各自恢复出帧。
set -u
LOG=/home/admin/hmi_live_round3.log
[ -f "$LOG" ] || LOG=$(ls -t /home/admin/hmi_live*.log 2>/dev/null | head -1)
OUT=/home/admin/stream_recovery_verify
mkdir -p "$OUT"
echo "LOG=$LOG"

cleanup(){
  for ip in 192.168.1.165 192.168.1.167; do
    sudo iptables -D INPUT -s "$ip" -j DROP 2>/dev/null || true
  done
}
trap cleanup EXIT INT TERM
snap0(){ DISPLAY=:0 ffmpeg -y -loglevel error -f x11grab -video_size 1280x800 -i :0 -frames:v 1 "$1" >/dev/null 2>&1; }

BEFORE=$(grep -ac 'video watchdog: rebuild' "$LOG")
echo "===== 0. 前置（已有 rebuild 行数=$BEFORE） ====="
date '+%F %T %Z'
DISPLAY=:0 xdotool mousemove 44 92 click 1
sleep 1

echo "===== 1. 同时屏蔽 CAM-05 + CAM-06 ====="
cleanup
for ip in 192.168.1.165 192.168.1.167; do sudo iptables -I INPUT -s "$ip" -j DROP; done
echo "BLOCKED at $(date '+%T')"
sleep 30
echo "--- 本轮新增 rebuild 日志 ---"
grep -a 'video watchdog: rebuild' "$LOG" | tail -8
echo "--- 两路状态变化 ---"
for c in 05 06; do
  echo "[CAM-$c]"
  tail -n 30000 "$LOG" | grep -a "video channel \"CAM-$c\"" | tail -4
done
echo "--- 其余四路是否被误重建（应为空） ---"
grep -a 'video watchdog: rebuild' "$LOG" | grep -avE '"CAM-0[56]"' | tail -8 || echo "(none)"
AFTER_BLOCK=$(grep -ac 'video watchdog: rebuild' "$LOG")
echo "REBUILD_DELTA_BLOCK=$((AFTER_BLOCK-BEFORE))"
snap0 "$OUT/MULTI_BLOCKED.png"

echo "===== 2. 解除屏蔽，等待两路各自恢复 ====="
cleanup
echo "UNBLOCKED at $(date '+%T')"
for i in 1 2 3 4; do
  sleep 15
  echo "--- t+$((i*15))s ---"
  for c in 05 06; do
    tail -n 6000 "$LOG" | grep -a "video channel \"CAM-$c\"" | tail -1
  done
done
echo "--- 最终 rebuild 总数 / 新增 ---"
grep -a 'video watchdog: rebuild' "$LOG" | tail -8
AFTER_ALL=$(grep -ac 'video watchdog: rebuild' "$LOG")
echo "REBUILD_DELTA_TOTAL=$((AFTER_ALL-BEFORE))"
echo "--- QML 运行期错误（应为 0 行） ---"
tail -n 8000 "$LOG" | grep -a -iE 'qml.*error|TypeError|ReferenceError|Cannot assign|is not defined' | head -8 || true
DISPLAY=:0 xdotool mousemove 44 92 click 1
sleep 2
snap0 "$OUT/MULTI_RESTORED.png"
echo "===== DONE ====="
