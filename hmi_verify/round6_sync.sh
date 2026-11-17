#!/usr/bin/env bash
# 本地（Windows Git Bash）执行：同步「HCI 原始直读门磁」一轮改动到 RK3588。
# 用法：bash hmi_verify/round6_sync.sh
set -u
LOCAL=/e/work/rail_issue/rk3588_handover
REMOTE=/home/admin/railhmi-ui-fix
PSCP="/c/Program Files/PuTTY/pscp"
HK="SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU"
USERHOST="admin@192.168.0.103"

FILES=(
  app/src/ui/src/hci_scan_filter.h
  app/src/ui/src/hci_scan_filter.cpp
  app/src/ui/src/door_sensor_worker.h
  app/src/ui/src/door_sensor_worker.cpp
  app/src/ui/src/door_sensor_runtime.h
  app/src/ui/src/door_sensor_runtime.cpp
  app/tools/check_runtime_resources_wiring.py
  hmi_verify/round6_sync.sh
  hmi_verify/round6_verify.sh
)

for f in "${FILES[@]}"; do
  [ -f "$LOCAL/$f" ] || { echo "LOCAL MISSING: $f"; exit 1; }
  "$PSCP" -q -hostkey "$HK" -pw admin "$LOCAL/$f" "$USERHOST:$REMOTE/$f" || {
    echo "SYNC FAILED: $f"; exit 1
  }
  echo "SYNC $f"
done
echo "SYNC OK (${#FILES[@]} files)"
