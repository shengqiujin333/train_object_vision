#!/usr/bin/env bash
# 板端以 root 执行：临时关闭控制器 LE 去重，测门磁「真实」广播频率。
# 用法：echo admin | sudo -S bash hmi_verify/ble_dedup_probe.sh [seconds]
# 仅诊断：不改 HMI 代码/配置，结束后 bluetoothd 会自行重设扫描参数。
set -u
ROOT=/home/admin/railhmi-ui-fix
SEC=${1:-40}
B=/tmp/door_dedup_btmon.txt
D=/tmp/door_dedup_dbus.txt
rm -f "$B" "$D"

echo "=== 关闭控制器去重 (LE Set Scan Enable filter_dup=0) ==="
hcitool cmd 0x08 0x000c 01 00 2>&1 | head -4
# bluetoothd 每约 10s 会重启扫描并重新开启去重，这里每秒补一次，保证窗口内关掉。
( for i in $(seq 1 $((SEC + 2))); do hcitool cmd 0x08 0x000c 01 00 >/dev/null 2>&1; sleep 1; done ) &
KPID=$!
timeout "$SEC" btmon -t > "$B" 2>&1 &
BPID=$!
python3 "$ROOT/hmi_verify/ble_latency_dbus.py" "$SEC" > "$D" 2>&1 &
DPID=$!
wait "$DPID" 2>/dev/null || true
wait "$BPID" 2>/dev/null || true
kill "$KPID" 2>/dev/null || true

echo "===== A. BlueZ D-Bus 推送（去重关闭后） ====="
cat "$D"
echo "===== B. HCI 层 T1 ADV 真实时间戳与间隔 ====="
awk '/> HCI Event: LE Meta Event/{ts=$NF} /Address: C0:6D:9A:AC:C3:87/{print ts}' "$B" \
  | sort -n | uniq | awk '{if(NR>1) printf "  %s  (+%.3fs)\n", $1, $1-prev; else printf "  %s\n", $1; prev=$1}'
echo "===== C. 计数对比 ====="
echo -n "T1 ADV reports (HCI): "; grep -c 'Address: C0:6D:9A:AC:C3:87' "$B" || true
echo -n "D-Bus door signals   : "; grep -c '^..:..:..\.' "$D" || true
echo "===== D. 窗口内 Filter duplicates 设置 ====="
grep -A2 'LE Set Scan En' "$B" | grep -i 'Filter duplicates' | sort | uniq -c
echo "===== E. 各 T1 帧的厂商数据/计数器 ====="
awk '/> HCI Event: LE Meta Event/{ts=$NF} /Address: C0:6D:9A:AC:C3:87/{want=1} want && /Data: ffffff/{print ts \" \" \$0; want=0}' "$B" | head -40
echo "===== END ====="
