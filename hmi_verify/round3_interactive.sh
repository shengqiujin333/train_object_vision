#!/usr/bin/env bash
# 现场定稿交互验收（RK3588）：
#   A. 生产 :0 实例：P3/P2 日期点开虚拟键盘、点空白收起、可见「查询」按钮可用
#   B. Xvfb :97 临时实例：注入开门合成帧产生 L2 报警，验证 P2 第二行筛选互斥单选
# 不修改生产进程环境，不写配置，不接触凭据值。
set -u
ROOT=/home/admin/railhmi-ui-fix
APP=$ROOT/app
OUT=/home/admin/round3_verify
LOG0=/home/admin/hmi_live_round3.log
mkdir -p "$OUT"

snap(){ DISPLAY="$1" ffmpeg -y -loglevel error -f x11grab -video_size 1280x800 \
        -i "$1" -frames:v 1 "$2" >/dev/null 2>&1; }
click(){ DISPLAY="$1" xdotool mousemove "$2" "$3" click 1; }

echo "===== A. 生产 :0 —— P3 历史录像 ====="
click :0 44 268; sleep 2; snap :0 "$OUT/R3I_P3_BASE.png"
click :0 199 75; sleep 1; snap :0 "$OUT/R3I_P3_KEYBOARD.png"
# 键盘面板实际覆盖约 x=412..960 / y=480..782；空白点击必须落在面板之外。
click :0 300 600; sleep 1; snap :0 "$OUT/R3I_P3_DISMISSED.png"
click :0 289 75; sleep 3; snap :0 "$OUT/R3I_P3_QUERY.png"
echo "--- P3 查询/NVR 日志（末尾） ---"
grep -aE 'nvr|NVR|queryNvr|录像' "$LOG0" | tail -8

echo "===== A. 生产 :0 —— P2 报警中心 ====="
click :0 44 180; sleep 2; snap :0 "$OUT/R3I_P2_BASE.png"
click :0 204 181; sleep 1; snap :0 "$OUT/R3I_P2_KEYBOARD.png"
click :0 300 600; sleep 1; snap :0 "$OUT/R3I_P2_DISMISSED.png"
click :0 447 181; sleep 2; snap :0 "$OUT/R3I_P2_QUERY.png"
echo "--- QML 运行期错误（应为 0 行） ---"
grep -a -iE 'qml.*error|TypeError|ReferenceError|Cannot assign|is not defined' "$LOG0" | tail -8 || true

echo "===== B. Xvfb :97 —— 合成开门报警后验证 P2 互斥单选 ====="
if ! xdpyinfo -display :97 >/dev/null 2>&1; then
  setsid Xvfb :97 -screen 0 1280x800x24 -nolisten tcp > "$OUT/xvfb97.log" 2>&1 < /dev/null &
  sleep 2
fi
SIMLOG=$OUT/R3I_SIM.log
( cd "$APP" && env -i PATH="$PATH" HOME=/home/admin DISPLAY=:97 QT_QPA_PLATFORM=xcb \
    QT_QUICK_BACKEND=software QT_XCB_GL_INTEGRATION=none RAILHMI_DOOR_DEBUG=1 \
    RAILHMI_DOOR_SIM_FRAME=07785634120801635500 \
    timeout 75 ./build-target/railhmi --config "$APP/config" > "$SIMLOG" 2>&1 ) &
SIMPID=$!
sleep 13
click :97 44 180; sleep 2; snap :97 "$OUT/R3I_P2_CHIPS_DEFAULT.png"
# 第二行：全部类型 ≈133 / 类型 chip ≈211 / 全部摄像头 ≈315 / 摄像头 chip ≈386
click :97 211 130; sleep 1; snap :97 "$OUT/R3I_P2_CHIPS_TYPE.png"
click :97 315 130; sleep 1; snap :97 "$OUT/R3I_P2_CHIPS_CAMERA.png"
click :97 386 130; sleep 1; snap :97 "$OUT/R3I_P2_CHIPS_CAMERA_ONE.png"
click :97 204 181; sleep 1; snap :97 "$OUT/R3I_P2_SIM_KEYBOARD.png"
# 有报警行时 ListView 覆盖空白区：这一下必须能收起（回归点）
click :97 300 600; sleep 1; snap :97 "$OUT/R3I_P2_SIM_DISMISSED.png"
# scrim 不能吞掉下层按钮：开着键盘点「一级」chip，应同时收起键盘并切中筛选
click :97 204 181; sleep 1
click :97 192 81; sleep 1; snap :97 "$OUT/R3I_P2_SIM_PROPAGATE.png"
echo "--- 合成开门报警证据 ---"
grep -aE 'SYNTHETIC|door_ajar event|snapshot level|video workers started' "$SIMLOG" | head -12
echo "--- 合成实例 QML 运行期错误（应为 0 行） ---"
grep -a -iE 'qml.*error|TypeError|ReferenceError|Cannot assign|is not defined' "$SIMLOG" | head -8 || true
kill "$SIMPID" 2>/dev/null || true
wait "$SIMPID" 2>/dev/null || true

echo "===== C. 截图清单 ====="
ls -l "$OUT"/R3I_*.png
# 回到生产 :0 的实时监控页待命
click :0 44 92
echo "===== DONE ====="
