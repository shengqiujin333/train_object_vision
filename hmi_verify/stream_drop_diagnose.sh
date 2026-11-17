#!/usr/bin/env bash
# 视频流断流现场取证（RK3588，只读，不改动运行中的进程）。
# 用法：bash stream_drop_diagnose.sh [日志文件]
# 目的：区分「静默停帧（无 GStreamer 错误）」与「bus ERROR/EOS」，并排除 UDP 套接字溢出。
set -u
LOG=${1:-}
if [ -z "$LOG" ] || [ ! -f "$LOG" ]; then
  LOG=$(ls -t /home/admin/hmi_live*.log 2>/dev/null | head -1)
fi
OUT=/home/admin/stream_drop_evidence
mkdir -p "$OUT"
echo "LOG=$LOG"
[ -f "$LOG" ] || { echo "NO LOG FOUND"; exit 1; }

echo "===== 0. 时间 / 进程 / 资源 ====="
date '+local=%Y-%m-%d %H:%M:%S %Z'
date -u '+utc  =%Y-%m-%d %H:%M:%S UTC'
pgrep -af 'railhmi' | head -5
for p in $(pgrep -f '^\./build-target/railhmi --config'); do
  echo "--- pid $p ---"
  grep -E '^(VmRSS|Threads|State)' "/proc/$p/status" 2>/dev/null
done
free -m | head -3

echo "===== 1. 通道状态变化时间线（video channel / reconnect / frozen / offline） ====="
grep -aE 'video channel|reconnect|ReconnectAttempt|frozen|offline|eos' "$LOG" | tail -100

echo "===== 2. GStreamer / RTSP 错误与超时 ====="
grep -a -iE 'error|timeout|eos|rtsp|disconnect|failed|unable|could not|cut' "$LOG" | tail -80

echo "===== 3. 启动与首帧（确认六路是否曾同时在线） ====="
grep -aE 'video workers started|presentable|configuration OK' "$LOG" | tail -30

echo "===== 4. 内核 UDP 缓冲配置（应为 rmem_max=16MB / default=4MB） ====="
sysctl net.core.rmem_max net.core.rmem_default net.core.wmem_max 2>/dev/null
cat /etc/sysctl.d/99-udp-buffers.conf 2>/dev/null || echo "NO 99-udp-buffers.conf"

echo "===== 5. UDP InErrors / RcvbufErrors 30s 差分 ====="
grep '^Udp:' /proc/net/snmp > "$OUT/udp_t0.txt"
sleep 30
grep '^Udp:' /proc/net/snmp > "$OUT/udp_t1.txt"
echo "--- t0 ---"; cat "$OUT/udp_t0.txt"
echo "--- t1 ---"; cat "$OUT/udp_t1.txt"
echo "解读：InErrors / RcvbufErrors 增长 = 板端套接字缓冲溢出（IDR 突发丢包）；不增长 = 链路/相机侧停发。"

echo "===== 6. 六路 RTSP 端口可达性 ====="
for ip in 192.168.1.65 192.168.1.160 192.168.1.162 192.168.1.163 192.168.1.165 192.168.1.167; do
  if timeout 2 bash -c "echo > /dev/tcp/$ip/554" 2>/dev/null; then
    echo "$ip:554 OPEN"
  else
    echo "$ip:554 CLOSED/TIMEOUT"
  fi
done

echo "===== 7. 判定要点 ====="
echo "A. 日志只有 'video channel ... offline/frozen' 而无 GStreamer ERROR -> 静默停帧（UDP 常见）。"
echo "B. 有 ERROR/EOS 但之后再无 'video workers started' / generation 前进 -> 状态机标记了重连，但没有重建 pipeline。"
echo "C. 无论 A/B，生产链路当前都没有自动重建入口（见代码结论），只能重启进程恢复。"
echo "===== DONE ====="
