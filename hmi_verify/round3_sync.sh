#!/usr/bin/env bash
# 本地（Windows Git Bash）执行：把 P3/P2 现场定稿改动同步到 RK3588
# 用法：bash hmi_verify/round3_sync.sh
set -u
LOCAL=/e/work/rail_issue/rk3588_handover
REMOTE=/home/admin/railhmi-ui-fix
PSCP="/c/Program Files/PuTTY/pscp"
HK="SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU"
USERHOST="admin@192.168.0.103"

FILES=(
  app/src/ui/qml.qrc
  app/src/ui/CMakeLists.txt
  app/src/ui/src/credential_env.h
  app/src/ui/src/credential_env.cpp
  app/src/ui/src/main.cpp
  app/src/ui/src/replay_model.cpp
  app/src/ui/src/video_runtime.cpp
  app/src/ui/qml/components/VirtualKeyboard.qml
  app/src/ui/qml/pages/P2AlarmCenter.qml
  app/src/ui/qml/pages/P3Replay.qml
  app/tools/check_replay_wiring.py
  app/tools/check_permissions_wiring.py
  app/tools/check_alarm_center_wiring.py
  app/tools/check_stream_recovery_wiring.py
  docs/interfaces/INTERFACE_CONTRACT.md
  docs/hmi/NVR_HISTORY_INTEGRATION.md
  packaging/railhmi.service
  hmi_verify/round3_verify.sh
  hmi_verify/round3_interactive.sh
  hmi_verify/stream_drop_diagnose.sh
  hmi_verify/stream_recovery_verify.sh
  hmi_verify/stream_multi_drop_verify.sh
)

for f in "${FILES[@]}"; do
  echo "SYNC $f"
  "$PSCP" -hostkey "$HK" -pw admin "$LOCAL/$f" "$USERHOST:$REMOTE/$f" || {
    echo "SYNC FAILED: $f"; exit 1
  }
done
echo "SYNC OK (${#FILES[@]} files)"
