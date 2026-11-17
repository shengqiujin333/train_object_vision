#!/usr/bin/env bash
# 可选：真实开关门端到端延迟实测（需要人在现场分开/贴合磁铁一次）。
# 用法：cd /home/admin/railhmi-ui-fix && echo admin | sudo -S bash hmi_verify/round5_magnet_latency.sh
#
# 会以 1s 粒度记录 ①BlueZ 缓存里门状态字节变化 ②HMI 事件推送/快照变化，
# 并打印二者的时间差。运行前请确认 :0 生产实例已按 round5_verify.sh 部署
# （关去重 + 1s 轮询）；脚本不改任何配置、不重启进程。
set -u
ROOT=/home/admin/railhmi-ui-fix
OUT=/home/admin/round5_verify
LOG=/home/admin/hmi_live_round5.log.clean
[ -f "$LOG" ] || LOG=/home/admin/hmi_live_round5.log
mkdir -p "$OUT"
P=$OUT/magnet_bluez.txt
H=$OUT/magnet_hmi.txt
rm -f "$P" "$H"

echo "请在提示时操作磁铁：t=10s 分开，t=40s 贴合。"
echo "开始计时 $(date '+%H:%M:%S')"
python3 "$ROOT/hmi_verify/ble_poll_probe.py" 65 1 > "$P" 2>&1 &
PPID=$!
timeout 65 tail -F -n 0 "$LOG" 2>/dev/null \
  | grep -aE 'EVENT push#|decoded vendor frame|snapshot level|door_ajar event' > "$H" &
HPID=$!
sleep 10; echo ">>> 现在把磁铁分开（开门）"
sleep 30; echo ">>> 现在把磁铁贴合（关门）"
wait "$PPID" 2>/dev/null || true
kill "$HPID" 2>/dev/null || true

echo "===== BlueZ 缓存门状态变化（1s 采样） ====="
cat "$P"
echo "===== HMI 同窗口事件 ====="
cat "$H" | head -40
echo "===== 人工对照 ====="
echo "把 BlueZ 首次出现 door=1 / door=0 的时刻，与 HMI 首条 EVENT push / snapshot level=alarm 对齐，"
echo "差值即端到端识别延迟；目标 <= 5s。若 HMI 日志没有 decoded vendor frame，"
echo "说明生产实例未带 RAILHMI_DOOR_DEBUG，只看 EVENT push 与顶栏截图时间即可。"
echo "===== END ====="
