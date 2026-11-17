#!/usr/bin/env bash
# 板端执行（需 root）：统计真实 T1 门磁的 HCI 广播间隔，判定「反应慢」的物理下限。
# 用法：echo admin | sudo -S bash ble_interval_probe.sh [seconds]
set -u
SEC=${1:-35}
OUT=/tmp/btmon_door.txt
echo "=== btmon capture ${SEC}s ==="
timeout "$SEC" btmon > "$OUT" 2>&1 || true
echo "--- total lines ---"; wc -l < "$OUT"
echo "--- HCI event types ---"
grep -o 'HCI Event: [^ ]*' "$OUT" 2>/dev/null | sort | uniq -c | head -20
echo "--- ADV_REPORT / LE Meta count ---"
grep -c -i 'LE Meta\|Advertising Report\|ADV_IND\|Adv Data' "$OUT" || true
echo "--- T1 MAC mentions ---"
grep -c -i 'C0:6D:9A:AC:C3:87\|C0-6D-9A-AC-C3-87' "$OUT" || true
echo "--- Name T1 mentions ---"
grep -c -i 'Name (short): T1\|Name (complete): T1\|T1' "$OUT" || true
echo "--- first 40 LE meta lines with timestamps ---"
grep -E '^[<>]|LE Meta|Advertising Report|Address|RSSI' "$OUT" | head -40
echo "=== END ==="
