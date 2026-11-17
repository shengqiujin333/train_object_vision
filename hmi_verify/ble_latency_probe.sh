#!/usr/bin/env bash
# 板端执行：并行抓 ①BlueZ D-Bus PropertiesChanged ②root btmon 原始 HCI 广播，
# 用来判定「门磁反应慢」到底是设备广播周期，还是 BlueZ/内核去重漏收。
# 用法：cd /home/admin/railhmi-ui-fix && bash hmi_verify/ble_latency_probe.sh [seconds]
set -u
ROOT=/home/admin/railhmi-ui-fix
SEC=${1:-70}
D=/tmp/door_latency_dbus.txt
B=/tmp/door_latency_btmon.txt
rm -f "$D" "$B"

python3 "$ROOT/hmi_verify/ble_latency_dbus.py" "$SEC" > "$D" 2>&1 &
DPID=$!
echo admin | sudo -S timeout "$SEC" btmon -t > "$B" 2>&1 &
BPID=$!
wait "$DPID" 2>/dev/null || true
wait "$BPID" 2>/dev/null || true

echo "===== A. BlueZ D-Bus 每次门磁属性推送（看 counter 是否跳号） ====="
cat "$D"
echo "===== B. btmon 原始 HCI：LE Set Scan Parameters（连续扫描占空比） ====="
grep -A6 'LE Set Scan Parameters' "$B" | head -18
echo "===== C. btmon 原始 HCI：T1 每个广播包的真实时间戳 ====="
awk '/> HCI Event: LE Meta Event/{ts=$NF} /Address: C0:6D:9A:AC:C3:87/{print ts}' "$B" | sort -n | uniq
echo "===== D. 计数 ====="
echo -n "T1 ADV reports: "; grep -c 'Address: C0:6D:9A:AC:C3:87' "$B" || true
echo -n "all LE Meta events: "; grep -c '> HCI Event: LE Meta Event' "$B" || true
echo -n "D-Bus door signals: "; grep -c '^..:..:..\.' "$D" || true
echo "===== E. T1 广播上下文（第一段，看 ADV 数据/计数器） ====="
grep -n -m1 -B8 -A22 'Address: C0:6D:9A:AC:C3:87' "$B" | head -36
echo "===== END ====="
