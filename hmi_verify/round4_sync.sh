#!/usr/bin/env bash
# 本地（Windows Git Bash）执行：把第 4 轮「P3 画面放大 / P2 固定类型 / 顶栏真实资源读数 /
# 门磁事件驱动加速」以及此前各轮的全部源码改动，完整同步到 RK3588。
# 用法：bash hmi_verify/round4_sync.sh
set -u
LOCAL=/e/work/rail_issue/rk3588_handover
REMOTE=/home/admin/railhmi-ui-fix
PSCP="/c/Program Files/PuTTY/pscp"
HK="SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU"
USERHOST="admin@192.168.0.103"

FILES=(
  app/build.sh
  app/config/app.yaml
  app/src/core/CMakeLists.txt
  app/src/core/include/railhmi/core/alarm_center.h
  app/src/core/include/railhmi/core/config.h
  app/src/core/include/railhmi/core/door_sensor.h
  app/src/core/include/railhmi/core/status.h
  app/src/core/src/alarm_center.cpp
  app/src/core/src/config.cpp
  app/src/core/src/door_sensor.cpp
  app/src/core/src/status.cpp
  app/src/core/tools/status_probe.cpp
  app/src/ui/CMakeLists.txt
  app/src/ui/qml.qrc
  app/src/ui/qml/main.qml
  app/src/ui/qml/components/CameraTile.qml
  app/src/ui/qml/components/CameraZoomOverlay.qml
  app/src/ui/qml/components/ResourceMeters.qml
  app/src/ui/qml/components/TopStatusBar.qml
  app/src/ui/qml/components/VirtualKeyboard.qml
  app/src/ui/qml/components/VideoSurface.qml
  app/src/ui/qml/pages/P1Realtime.qml
  app/src/ui/qml/pages/P2AlarmCenter.qml
  app/src/ui/qml/pages/P3Replay.qml
  app/src/ui/qml/pages/P4AllCameras.qml
  app/src/ui/qml/pages/P6Maintenance.qml
  app/src/ui/src/alarm_model.cpp
  app/src/ui/src/alarm_model.h
  app/src/ui/src/credential_env.cpp
  app/src/ui/src/credential_env.h
  app/src/ui/src/door_sensor_runtime.cpp
  app/src/ui/src/door_sensor_runtime.h
  app/src/ui/src/door_sensor_worker.cpp
  app/src/ui/src/door_sensor_worker.h
  app/src/ui/src/main.cpp
  app/src/ui/src/nvr_status.cpp
  app/src/ui/src/nvr_status.h
  app/src/ui/src/replay_model.cpp
  app/src/ui/src/system_metrics.cpp
  app/src/ui/src/system_metrics.h
  app/src/ui/src/vehicle_state.cpp
  app/src/ui/src/vehicle_state.h
  app/src/ui/src/video_runtime.cpp
  app/src/ui/src/video_runtime.h
  app/tests/core/CMakeLists.txt
  app/tests/core/main.cpp
  app/tests/core/test_alarm_center.cpp
  app/tests/core/test_config.cpp
  app/tests/core/test_door_sensor.cpp
  app/tests/core/test_status.cpp
  app/tools/check_alarm_center_wiring.py
  app/tools/check_permissions_wiring.py
  app/tools/check_replay_wiring.py
  app/tools/check_runtime_resources_wiring.py
  app/tools/check_stream_recovery_wiring.py
  packaging/railhmi.service
  docs/interfaces/INTERFACE_CONTRACT.md
  docs/hmi/NVR_HISTORY_INTEGRATION.md
  hmi_verify/ble_signal_probe.py
  hmi_verify/ble_signal_census.py
  hmi_verify/ble_door_keys_probe.py
  hmi_verify/ble_interval_probe.sh
  hmi_verify/round4_sync.sh
  hmi_verify/round4_verify.sh
  hmi_verify/round4_extra_verify.sh
)

OK=0
for f in "${FILES[@]}"; do
  [ -f "$LOCAL/$f" ] || { echo "LOCAL MISSING: $f"; exit 1; }
  "$PSCP" -q -hostkey "$HK" -pw admin "$LOCAL/$f" "$USERHOST:$REMOTE/$f" || {
    echo "SYNC FAILED: $f"; exit 1
  }
  OK=$((OK+1))
  printf 'SYNC %2d/%d %s\n' "$OK" "${#FILES[@]}" "$f"
done
echo "SYNC OK ($OK files)"
