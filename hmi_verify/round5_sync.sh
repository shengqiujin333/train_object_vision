#!/usr/bin/env bash
# 本地（Windows Git Bash）执行：同步「门磁 5 秒内识别」一轮改动到 RK3588。
# 用法：bash hmi_verify/round5_sync.sh
set -u
LOCAL=/e/work/rail_issue/rk3588_handover
REMOTE=/home/admin/railhmi-ui-fix
PSCP="/c/Program Files/PuTTY/pscp"
HK="SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU"
USERHOST="admin@192.168.0.103"

FILES=(
  app/config/app.yaml
  app/src/core/include/railhmi/core/config.h
  app/src/ui/CMakeLists.txt
  app/src/ui/src/hci_scan_filter.h
  app/src/ui/src/hci_scan_filter.cpp
  app/src/ui/src/door_sensor_worker.h
  app/src/ui/src/door_sensor_worker.cpp
  app/src/ui/src/door_sensor_runtime.h
  app/src/ui/src/door_sensor_runtime.cpp
  app/tests/core/test_door_sensor.cpp
  app/tools/check_runtime_resources_wiring.py
  packaging/railhmi.service
  docs/interfaces/INTERFACE_CONTRACT.md
  hmi_verify/ble_latency_dbus.py
  hmi_verify/ble_latency_probe.sh
  hmi_verify/ble_dedup_probe.sh
  hmi_verify/ble_poll_probe.py
  hmi_verify/ble_gatt_probe.py
  hmi_verify/round5_sync.sh
  hmi_verify/round5_verify.sh
  hmi_verify/round5_magnet_latency.sh
)

for f in "${FILES[@]}"; do
  [ -f "$LOCAL/$f" ] || { echo "LOCAL MISSING: $f"; exit 1; }
  "$PSCP" -q -hostkey "$HK" -pw admin "$LOCAL/$f" "$USERHOST:$REMOTE/$f" || {
    echo "SYNC FAILED: $f"; exit 1
  }
  echo "SYNC $f"
done
echo "SYNC OK (${#FILES[@]} files)"
