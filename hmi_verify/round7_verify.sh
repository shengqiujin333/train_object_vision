#!/usr/bin/env bash
# 板端执行：cd /home/admin/railhmi-ui-fix && bash hmi_verify/round7_verify.sh
#
# 2026-11-17 轮验收：视觉推理接入（三路共用检测模型 + 车前方两阶段）、
# AI 报警闪烁红框、顶栏「AI模型 接入/未接入」、P5 模型替换、P6「日志显示 + 功能操作」、
# P1 实时画面检测框、车前方 1:1 裁剪推理。
#
# 依赖：app 已同步到 $APP/app；板端已安装 Qt5/GStreamer/librga；Xvfb（离屏）或 :0 真屏。
# 注意：生产实例现由 systemd 管理（railhmi.service）。本脚本用 systemctl stop/start，
# 不再用 pkill -x railhmi —— 2026-11-17 曾因脚本里的 pkill 把 :0 上的生产进程一起杀掉。
set -u

APP=${APP:-/home/admin/railhmi-ui-fix}
SHOT=${SHOT:-$APP/hmi_verify/round7}
DISP=${DISP:-:95}
SUDO="sudo -n"
mkdir -p "$SHOT"
PASS=0; FAIL=0
ok(){ PASS=$((PASS+1)); echo "  [PASS] $1"; }
bad(){ FAIL=$((FAIL+1)); echo "  [FAIL] $1"; }

cd "$APP" || exit 2

echo "===== 0. 静态守卫 ====="
for g in check_no_hardcoded_ui check_qml_qt_validity check_ai_debug_wiring check_maintenance_wiring \
         check_alarm_center_wiring check_replay_wiring check_permissions_wiring check_health_wiring \
         check_stream_recovery_wiring check_runtime_resources_wiring; do
  case $g in
    check_qml_qt_validity) python3 app/tools/$g.py --root app/src/ui >/tmp/${g}.log 2>&1 ;;
    *)                     python3 app/tools/$g.py >/tmp/${g}.log 2>&1 ;;
  esac
  if grep -q 'PASS' /tmp/${g}.log; then ok "guard $g"; else bad "guard $g"; fi
done

echo "===== 1. CoreTests ====="
chmod +x app/build.sh
./app/build.sh CoreTests >/tmp/r7_core_build.log 2>&1 && ok "CoreTests build" || bad "CoreTests build"
./app/build-core/tests/railhmi-core-tests > /tmp/r7_core.log 2>&1
grep -E '^RESULT:' /tmp/r7_core.log | tail -1
if grep -q 'RESULT: PASS' /tmp/r7_core.log; then ok "core tests"; else bad "core tests"; fi

echo "===== 2. 目标构建 ====="
./app/build.sh Release >/tmp/r7_release.log 2>&1 && ok "Release build" || bad "Release build"
ls -l app/build-target/railhmi 2>/dev/null

echo "===== 3. 顶栏探针（7 项含 ai_model） ====="
./app/build-core/tools/railhmi-status-probe 2>&1 | grep -E 'item key=ai_model|type=acceptance' | tail -2
./app/build-core/tools/railhmi-status-probe 2>&1 | grep -q 'top_order_ok=true' \
  && ok "top bar has 7 items incl. ai_model" || bad "top bar order"

echo "===== 4. 运行取证（离屏 $DISP，六路真实 RTSP，注入报警验证红框闭环） ====="
$SUDO systemctl stop railhmi 2>/dev/null || true
sleep 1
if ! DISPLAY=$DISP xdpyinfo >/dev/null 2>&1; then
  (Xvfb $DISP -screen 0 1280x800x24 -nolisten tcp >/tmp/xvfb.log 2>&1 &) ; sleep 3
fi
run_page(){
  local page=$1 tag=$2 maint=${3:-}
  local extra=""
  [ -n "$maint" ] && extra=" RAILHMI_MAINT_RUN=$maint"
  env -i PATH="$PATH" HOME=/home/admin DISPLAY=$DISP QT_QPA_PLATFORM=xcb QT_QUICK_BACKEND=software \
      RAILHMI_AI_SIM_ALARM=CAM-01,CAM-04,CAM-05 RAILHMI_START_PAGE=$page$extra \
      setsid nohup ./app/build-target/railhmi --config ./app/config > /tmp/r7_$tag.log 2>&1 < /dev/null &
  sleep 26
  DISPLAY=$DISP ffmpeg -y -loglevel error -draw_mouse 0 -f x11grab -video_size 1280x800 -i $DISP \
      -frames:v 1 "$SHOT/R7_$tag.png" >/dev/null 2>&1
  pkill -x railhmi 2>/dev/null || true   # 只杀本脚本起的离屏实例（生产实例已 stop）
  sleep 1
}
run_page realtime P1
run_page ai_debug P5
run_page maintenance P6 "camera,nvr,system,selfcheck"

grep -q 'ai: worker started' /tmp/r7_P1.log && ok "AI worker started" || bad "AI worker started"
grep -q 'detect_ready=true' /tmp/r7_P1.log && ok "detect model ready" || bad "detect model ready"
grep -q 'pantograph_ready=true' /tmp/r7_P1.log && ok "pantograph lane ready (shared model)" || bad "pantograph ready"
grep -q 'wheel_rail_ready=true' /tmp/r7_P1.log && ok "wheel_rail lane ready (shared model)" || bad "wheel_rail ready"
grep -q 'ai: frame CAM-01 1280x720' /tmp/r7_P1.log && ok "AI uses camera-native 1280x720 (not the screen)" || bad "AI frame size"
grep -q 'person_on_rail cam=CAM-01 level=1' /tmp/r7_P1.log && ok "front L1 person_on_rail" || bad "front alarm"
grep -q 'pantograph_abnormal cam=CAM-04 level=2' /tmp/r7_P1.log && ok "pantograph L2" || bad "pantograph alarm"
grep -q 'wheel_derail cam=CAM-05 level=1' /tmp/r7_P1.log && ok "wheel_rail L1" || bad "wheel_rail alarm"
grep -q '\[maint\] 摄像头检测开始' /tmp/r7_P6.log && ok "P6 camera ping ran" || bad "P6 camera ping"
grep -q '\[maint\] NVR 检测' /tmp/r7_P6.log && ok "P6 NVR check ran" || bad "P6 NVR check"
grep -q '\[maint\] 系统信息' /tmp/r7_P6.log && ok "P6 system info ran" || bad "P6 system info"
for f in /tmp/r7_P1.log /tmp/r7_P5.log /tmp/r7_P6.log; do
  n=$(grep -ciE 'qml|TypeError|ReferenceError' "$f")
  [ "$n" = "0" ] && ok "no QML runtime error in $f" || bad "QML errors in $f ($n)"
done

echo "===== 5. 恢复生产服务 ====="
$SUDO systemctl start railhmi 2>/dev/null || echo "  (未安装 systemd 服务，需手工启动)"
sleep 5
$SUDO systemctl is-active railhmi 2>/dev/null

echo "===== 6. 证据 ====="
cp /tmp/r7_P1.log /tmp/r7_P5.log /tmp/r7_P6.log "$SHOT"/ 2>/dev/null
ls -l "$SHOT" | tail -8
echo "PASS=$PASS FAIL=$FAIL"
[ "$FAIL" = "0" ] && echo "ROUND7 RESULT: PASS" || echo "ROUND7 RESULT: FAIL"
