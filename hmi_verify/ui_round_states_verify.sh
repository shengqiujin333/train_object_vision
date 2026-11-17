#!/usr/bin/env bash
# 本轮真值/交互状态补充验收：
#   完整静态守卫（含头文件与接线扫描）
#   顶栏「车辆状态」人工置位循环
#   放大浮层 60s 自动消失
#   顶栏异常真值：NVR异常 / 门磁 传感器异常 / 0/6 路实时 / 门磁 开门
set -u
ROOT=/home/admin/railhmi-ui-fix
APP=$ROOT/app
OUT=/home/admin/ui_round_verify
LOG=/home/admin/hmi_live_round2.log
mkdir -p "$OUT"

echo "===== A. 完整静态守卫（头文件 + 各页接线） ====="
cd "$ROOT" || exit 1
python3 app/tools/check_qml_qt_validity.py --root app/src/ui \
  --headers app/src/core/include app/src/platform/include 2>&1 | tail -2
for w in replay ai_debug maintenance permissions health; do
  case "$w" in
    ai_debug) python3 app/tools/check_ai_debug_wiring.py --root app/src/ui --tokens docs/hmi/design_tokens.yaml 2>&1 | tail -1 ;;
    permissions) python3 app/tools/check_permissions_wiring.py --root app/src/ui --matrix docs/hmi/PERMISSION_MATRIX.md 2>&1 | tail -1 ;;
    *) python3 app/tools/check_${w}_wiring.py --root app/src/ui 2>&1 | tail -1 ;;
  esac
done
python3 app/tools/check_no_hardcoded_ui.py --root app/src/ui 2>&1 | tail -1
python3 app/tools/check_packaging.py --root . --app-src app/src --packaging packaging 2>&1 | tail -1

snap0(){ DISPLAY=:0 ffmpeg -y -loglevel error -f x11grab -video_size 1280x800 -i :0 -frames:v 1 "$1" >/dev/null 2>&1; }
click0(){ DISPLAY=:0 xdotool mousemove "$1" "$2" click 1; }

echo "===== B. 顶栏「车辆状态」人工置位循环（三次点击） ====="
click0 44 92; sleep 1
for i in 1 2 3; do
  click0 520 24; sleep 1
  snap0 "$OUT/ROUND2_VEHICLE_$i.png"
done
echo "--- vehicle-state 日志 ---"
grep -a 'vehicle-state:' "$LOG" | tail -6

echo "===== C. 放大浮层 60s 自动消失 ====="
click0 44 92; sleep 1
click0 490 293; sleep 2; snap0 "$OUT/ROUND2_AUTO_t0.png"
sleep 30; snap0 "$OUT/ROUND2_AUTO_t30.png"
sleep 33; snap0 "$OUT/ROUND2_AUTO_t63.png"
echo "AUTO_TIMER screenshots captured (t0 / t30 / t63)"

echo "===== D. 真值状态：构造两份临时配置（不改动现场配置） ====="
rm -rf "$OUT/cfg_open" "$OUT/cfg_bad"
cp -a "$APP/config" "$OUT/cfg_open"
cp -a "$APP/config" "$OUT/cfg_bad"
sed -i 's/address: "http:\/\/192.168.1.4"/address: "http:\/\/127.0.0.1:9"/' "$OUT/cfg_bad/nvr.yaml"
sed -i 's/enabled: true/enabled: false/' "$OUT/cfg_bad/app.yaml"
echo "--- cfg_bad nvr/door ---"
grep -n 'address:' "$OUT/cfg_bad/nvr.yaml" | head -2
sed -n '52,54p' "$OUT/cfg_bad/app.yaml"

if ! xdpyinfo -display :99 >/dev/null 2>&1; then
  setsid Xvfb :99 -screen 0 1280x800x24 -nolisten tcp > "$OUT/xvfb99.log" 2>&1 < /dev/null &
  sleep 2
fi
snap99(){ DISPLAY=:99 ffmpeg -y -loglevel error -f x11grab -video_size 1280x800 -i :99 -frames:v 1 "$1" >/dev/null 2>&1; }
run99(){
  local cfg="$1" log="$2" png="$3" sim="$4"
  ( cd "$APP" && env -i PATH="$PATH" HOME=/home/admin DISPLAY=:99 QT_QPA_PLATFORM=xcb \
      QT_QUICK_BACKEND=software QT_XCB_GL_INTEGRATION=none RAILHMI_DOOR_DEBUG=1 \
      RAILHMI_DOOR_SIM_FRAME="$sim" timeout 14 ./build-target/railhmi --config "$cfg" > "$log" 2>&1 ) &
  local pid=$!
  sleep 9
  DISPLAY=:99 xdotool mousemove 44 92 click 1; sleep 2
  snap99 "$png"
  wait "$pid" || true
  echo "--- $cfg : NVR / 门磁 / 摄像头日志 ---"
  grep -aE 'nvr-status:|door-sensor: snapshot|video workers started' "$log" | head -6
}
run99 "$OUT/cfg_open" "$OUT/state_open.log" "$OUT/ROUND2_STATE_open.png" "07785634120801635500"
run99 "$OUT/cfg_bad" "$OUT/state_bad.log" "$OUT/ROUND2_STATE_bad.png" ""
echo "===== E. 截图清单 ====="
ls -l "$OUT"/ROUND2_VEHICLE_*.png "$OUT"/ROUND2_AUTO_*.png "$OUT"/ROUND2_STATE_*.png
