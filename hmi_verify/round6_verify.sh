#!/usr/bin/env bash
# 板端执行：cd /home/admin/railhmi-ui-fix && bash hmi_verify/round6_verify.sh
# 验收「HCI 原始直读门磁广播」：不依赖 BlueZ discovery，直接读 HCI 事件，
# 1~2s 一帧，<=5s 识别新状态。BlueZ 只作为无权限时的降级路径。
set -u
ROOT=/home/admin/railhmi-ui-fix
APP=$ROOT/app
OUT=/home/admin/round6_verify
LOG=/home/admin/hmi_live_round6.log
mkdir -p "$OUT"

echo "===== 0. 前置状态 ====="
date '+local=%Y-%m-%d %H:%M:%S %Z'
echo "binary: $(ls -l "$APP/build-target/railhmi" 2>/dev/null | awk '{print $5}') bytes"
echo "caps: $(getcap "$APP/build-target/railhmi" 2>/dev/null || echo none)"

echo "===== 1. 全部静态守卫 ====="
cd "$ROOT" || exit 1
PASS=0; FAIL=0
guard(){ n=$1; shift; if "$@" >"$OUT/guard_$n.log" 2>&1; then echo "PASS $n"; PASS=$((PASS+1));
  else echo "FAIL $n"; tail -5 "$OUT/guard_$n.log"; FAIL=$((FAIL+1)); fi; }
guard qml_qt_validity python3 app/tools/check_qml_qt_validity.py --root app/src/ui --headers app/src/core/include app/src/platform/include
for w in replay ai_debug maintenance permissions health alarm_center stream_recovery runtime_resources; do
  case "$w" in
    ai_debug) guard "$w" python3 app/tools/check_ai_debug_wiring.py --root app/src/ui --tokens docs/hmi/design_tokens.yaml ;;
    permissions) guard "$w" python3 app/tools/check_permissions_wiring.py --root app/src/ui --matrix docs/hmi/PERMISSION_MATRIX.md ;;
    *) guard "$w" python3 app/tools/check_${w}_wiring.py --root app/src/ui ;;
  esac
done
guard no_hardcoded_ui python3 app/tools/check_no_hardcoded_ui.py --root app/src/ui
guard packaging python3 app/tools/check_packaging.py --root . --app-src app/src --packaging packaging
echo "GUARDS pass=$PASS fail=$FAIL"

echo "===== 2. CoreTests + Qt 构建 ====="
cd "$APP" || exit 1
./build.sh CoreTests > "$OUT/coretests_build.log" 2>&1; echo "CORETESTS_BUILD_EXIT=$?"
./build-core/tests/railhmi-core-tests 2>&1 | tail -3
./build.sh Release > "$OUT/qt_build.log" 2>&1; echo "QT_BUILD_EXIT=$?"
tail -6 "$OUT/qt_build.log"

echo "===== 3. 授予 HCI 能力 ====="
echo admin | sudo -S /usr/sbin/setcap cap_net_admin,cap_net_raw+ep "$APP/build-target/railhmi" 2>/dev/null
echo "after setcap: $(getcap "$APP/build-target/railhmi" 2>/dev/null || echo FAILED)"

echo "===== 4. 先确保 BlueZ 在干净状态（不受上次裸 HCI 残留影响） ====="
echo admin | sudo -S systemctl restart bluetooth 2>/dev/null
sleep 3
echo "BlueZ Discovering=$(dbus-send --system --print-reply --dest=org.bluez /org/bluez/hci0 org.freedesktop.DBus.Properties.Get string:org.bluez.Adapter1 string:Discovering 2>/dev/null | grep -oP 'boolean \K\w+' || echo unknown)"

echo "===== 5. 重启 :0（带 DOOR_DEBUG 收集 HCI 帧节奏证据） ====="
OLD=$(pgrep -f '^\./build-target/railhmi --config' | head -1)
C=()
if [ -n "$OLD" ]; then
  mapfile -d '' -t E < "/proc/$OLD/environ"
  for kv in "${E[@]}"; do
    case "$kv" in
      RAILHMI_DOOR_SIM_FRAME=*|RAILHMI_DOOR_SIM_FOLLOWUP=*|DISPLAY=*|RAILHMI_DUMP_LAYOUT=*|RAILHMI_WINDOWED=*|RAILHMI_DOOR_DEBUG=*|RAILHMI_DOOR_SIM_FOLLOWUP_MS=*) ;;
      *) C+=("$kv");; esac
  done
  kill "$OLD" 2>/dev/null || true
  for i in 1 2 3 4 5 6 7 8 9 10; do kill -0 "$OLD" 2>/dev/null || break; sleep 1; done
fi
env -i "${C[@]}" DISPLAY=:0 RAILHMI_DOOR_DEBUG=1 setsid nohup ./build-target/railhmi \
    --config "$APP/config" > "$LOG" 2>&1 < /dev/null &
echo "RESTARTED old_pid=${OLD:-none} new_pid=$!"
sleep 12
PID=$(pgrep -f '^\./build-target/railhmi --config' | head -1)
echo "PID=$PID"

echo "--- 关键启动日志（HCI raw mode + fallback 判定） ---"
grep -aE 'door-sensor: (enabled|raw HCI|warning: raw HCI|HCI filter_dup|warning: LE Set|PropertiesChanged subscribed|SetDiscoveryFilter OK|StartDiscovery OK)|system-metrics:|nvr-status:|video workers started' "$LOG" | head -20
echo "--- I/O mode ---"
grep -a 'door-sensor: I/O mode=' "$LOG" | head -3
echo "--- QML 错误（应为 0 行） ---"
grep -a -iE 'qml.*error|TypeError|ReferenceError|Cannot assign|is not defined' "$LOG" | head -8 || true

echo "===== 6. 40s 取证：HCI 直读帧节奏 / BlueZ 状态完好性 ====="
# HCI 直读模式下，BlueZ Discovering 应该保持原有状态（HMI 不碰 BlueZ discovery）。
echo "--- 40s 后 BlueZ 状态（HCI 直读不应破坏它） ---"
sleep 28
echo "BlueZ Discovering=$(dbus-send --system --print-reply --dest=org.bluez /org/bluez/hci0 org.freedesktop.DBus.Properties.Get string:org.bluez.Adapter1 string:Discovering 2>/dev/null | grep -oP 'boolean \K\w+' || echo unknown)"
echo "BlueZ Device1 count=$(dbus-send --system --print-reply --dest=org.bluez / org.freedesktop.DBus.ObjectManager.GetManagedObjects 2>/dev/null | grep -c 'org.bluez.Device1')"

echo "--- HCI 直读帧节奏（raw HCI advertisement received） ---"
grep -a 'raw HCI advertisement received' "$LOG" | head -5
echo "--- 事件推送节奏（EVENT push） ---"
grep -a 'EVENT push#' "$LOG" | head -15
echo "--- push 总数 ---"
grep -ac 'EVENT push#' "$LOG"
echo "--- decoded vendor frame（确认真实门状态字节） ---"
grep -a 'decoded vendor frame' "$LOG" | tail -10
echo "--- snapshot 变化（门状态真值） ---"
grep -a 'snapshot level=' "$LOG" | tail -5

echo "===== 7. 真机截图（P1 顶栏门磁 / P6 门磁维护页） ====="
snap0(){ DISPLAY=:0 ffmpeg -y -loglevel error -f x11grab -video_size 1280x800 -i :0 -frames:v 1 "$1" >/dev/null 2>&1; }
click0(){ DISPLAY=:0 xdotool mousemove "$1" "$2" click 1; }
click0 44 92; sleep 2; snap0 "$OUT/R6_P1.png"
click0 44 532; sleep 2; snap0 "$OUT/R6_P6_DOOR.png"
click0 44 92; sleep 1
ls -l "$OUT"/R6_*.png

echo "===== 8. 清掉 DOOR_DEBUG，恢复干净生产实例 ====="
OLD=$PID
C=()
if [ -n "$OLD" ]; then
  if [ -r "/proc/$OLD/environ" ]; then
    mapfile -d '' -t E < "/proc/$OLD/environ"
    for kv in "${E[@]}"; do
      case "$kv" in
        DISPLAY=*|RAILHMI_DOOR_DEBUG=*|RAILHMI_DOOR_SIM_FRAME=*|RAILHMI_DOOR_SIM_FOLLOWUP=*|RAILHMI_DUMP_LAYOUT=*|RAILHMI_WINDOWED=*) ;;
        *) C+=("$kv");; esac
    done
  fi
  kill "$OLD" 2>/dev/null || true
  for i in 1 2 3 4 5 6 7 8 9 10; do kill -0 "$OLD" 2>/dev/null || break; sleep 1; done
fi
env -i "${C[@]}" DISPLAY=:0 setsid nohup ./build-target/railhmi \
    --config "$APP/config" > "$LOG.clean" 2>&1 < /dev/null &
sleep 12
echo "--- 干净实例状态 ---"
pgrep -af '^\./build-target/railhmi --config' | head -2
grep -aE 'door-sensor: (enabled|raw HCI|I/O mode=)|video workers started|nvr-status:' "$LOG.clean" | head -10
echo "--- 干净实例 QML 错误（应为 0 行） ---"
grep -a -iE 'qml.*error|TypeError|ReferenceError|Cannot assign|is not defined' "$LOG.clean" | head -8 || true
echo "===== DONE ====="
