#!/usr/bin/env bash
# 现场定稿 P3 历史录像 / P2 报警中心：板端构建 + 实机重启 + 基础截图验收
# 在 RK3588 上执行：cd /home/admin/railhmi-ui-fix && bash hmi_verify/round3_verify.sh
set -u
ROOT=/home/admin/railhmi-ui-fix
APP=$ROOT/app
OUT=/home/admin/round3_verify
LOG=/home/admin/hmi_live_round3.log
mkdir -p "$OUT"

echo "===== 0. 本机时间 / 私有凭据部署与键名（绝不打印值） ====="
date '+local=%Y-%m-%d %H:%M:%S %Z'
date -u '+utc  =%Y-%m-%d %H:%M:%S UTC'
# 程序只从 <configDir> 与 /opt/railhmi/config 两处读私有凭据；若都不存在，
# 把现场已有的私有文件复制一份到 configDir（0600，只复制不删除、不打印值）。
# 板子 11-14 重启后环境变量全部丢失，RTSP 与 NVR 两份都要补齐。
CRED_SRC=/home/admin/rk3588_handover_live_20261020
for name in camera-credentials.env nvr-credentials.env; do
  if [ ! -f "$APP/config/$name" ] && [ ! -f "/opt/railhmi/config/$name" ] && [ -f "$CRED_SRC/$name" ]; then
    cp "$CRED_SRC/$name" "$APP/config/$name"
    chmod 600 "$APP/config/$name"
    echo "CRED_DEPLOYED=$APP/config/$name"
  fi
done
for f in "$APP/config/camera-credentials.env" "$APP/config/nvr-credentials.env" \
         /opt/railhmi/config/camera-credentials.env /opt/railhmi/config/nvr-credentials.env; do
  [ -f "$f" ] || continue
  echo -n "$f  perms=$(stat -c '%a' "$f")  keys="
  sed -n 's/^\([A-Za-z_][A-Za-z0-9_]*\)=.*/\1/p' "$f" | tr '\n' ' '
  echo
done

echo "===== 1. 全部静态守卫 ====="
cd "$ROOT" || exit 1
python3 app/tools/check_qml_qt_validity.py --root app/src/ui \
  --headers app/src/core/include app/src/platform/include 2>&1 | tail -2
for w in replay ai_debug maintenance permissions health alarm_center stream_recovery; do
  case "$w" in
    ai_debug) python3 app/tools/check_ai_debug_wiring.py --root app/src/ui --tokens docs/hmi/design_tokens.yaml 2>&1 | tail -1 ;;
    permissions) python3 app/tools/check_permissions_wiring.py --root app/src/ui --matrix docs/hmi/PERMISSION_MATRIX.md 2>&1 | tail -1 ;;
    *) python3 app/tools/check_${w}_wiring.py --root app/src/ui 2>&1 | tail -1 ;;
  esac
done
python3 app/tools/check_no_hardcoded_ui.py --root app/src/ui 2>&1 | tail -1
python3 app/tools/check_packaging.py --root . --app-src app/src --packaging packaging 2>&1 | tail -1

echo "===== 2. CoreTests + 全量 Qt 目标构建 ====="
cd "$APP" || exit 1
./build.sh CoreTests > "$OUT/coretests_build.log" 2>&1; echo "CORETESTS_BUILD_EXIT=$?"
./build-core/tests/railhmi-core-tests 2>&1 | tail -3
./build.sh Release > "$OUT/qt_build.log" 2>&1; echo "QT_BUILD_EXIT=$?"
tail -6 "$OUT/qt_build.log"

echo "===== 3. 重启 :0 实机进程（不带任何模拟/调试变量） ====="
OLD=$(pgrep -f '^\./build-target/railhmi --config' | head -1)
C=()
if [ -n "$OLD" ]; then
  mapfile -d '' -t E < "/proc/$OLD/environ"
  for kv in "${E[@]}"; do
    case "$kv" in
      RAILHMI_DOOR_DEBUG=*|RAILHMI_DOOR_SIM_FRAME=*|RAILHMI_DOOR_SIM_FOLLOWUP=*|DISPLAY=*|RAILHMI_DUMP_LAYOUT=*|RAILHMI_WINDOWED=*|RAILHMI_DOOR_SIM_FOLLOWUP_MS=*) ;;
      *) C+=("$kv");; esac
  done
  kill "$OLD" 2>/dev/null || true
  for i in 1 2 3 4 5 6 7 8 9 10; do kill -0 "$OLD" 2>/dev/null || break; sleep 1; done
fi
env -i "${C[@]}" DISPLAY=:0 setsid nohup ./build-target/railhmi \
    --config "$APP/config" > "$LOG" 2>&1 < /dev/null &
echo "RESTARTED :0 old_pid=${OLD:-none} new_pid=$!"
sleep 14
echo "--- 进程 ---"
pgrep -af '^\./build-target/railhmi --config' | head -3
echo "--- 凭据补齐 / 自动登录 / NVR 状态 ---"
grep -aE 'credentials:|nvr-status:|录像机已连接|缺少录像机|TRAIN_NVR_CREDENTIAL' "$LOG" | head -12
echo "--- 视频 / 门磁 ---"
grep -aE 'video workers started|door-sensor: enabled|video watchdog: rebuild' "$LOG" | head -6
echo "--- QML 运行期错误（应为 0 行） ---"
grep -a -iE 'qml.*error|TypeError|ReferenceError|Cannot assign|is not defined' "$LOG" | head -10 || true

echo "===== 4. 基础截图：P3 / P2 / P1 ====="
snap0(){ DISPLAY=:0 ffmpeg -y -loglevel error -f x11grab -video_size 1280x800 -i :0 -frames:v 1 "$1" >/dev/null 2>&1; }
click0(){ DISPLAY=:0 xdotool mousemove "$1" "$2" click 1; }
click0 44 268; sleep 3; snap0 "$OUT/R3_P3_DEFAULT.png"
click0 44 180; sleep 2; snap0 "$OUT/R3_P2_DEFAULT.png"
click0 44 92;  sleep 2; snap0 "$OUT/R3_P1_DEFAULT.png"
ls -l "$OUT"/R3_*.png
echo "===== DONE ====="
