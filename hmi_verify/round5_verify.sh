#!/usr/bin/env bash
# 板端执行：cd /home/admin/railhmi-ui-fix && bash hmi_verify/round5_verify.sh
# 验收「门磁 5 秒内识别新状态」：关控制器去重 + 1s 全量读取 + 事件推送。
set -u
ROOT=/home/admin/railhmi-ui-fix
APP=$ROOT/app
OUT=/home/admin/round5_verify
LOG=/home/admin/hmi_live_round5.log
mkdir -p "$OUT"

echo "===== 0. 厂家 1s 广播 / 权限基线 ====="
date '+local=%Y-%m-%d %H:%M:%S %Z'
echo "before setcap: $(getcap "$APP/build-target/railhmi" 2>/dev/null || echo none)"

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

echo "===== 2. CoreTests + 全量 Qt 构建 ====="
cd "$APP" || exit 1
./build.sh CoreTests > "$OUT/coretests_build.log" 2>&1; echo "CORETESTS_BUILD_EXIT=$?"
./build-core/tests/railhmi-core-tests 2>&1 | tail -3
./build.sh Release > "$OUT/qt_build.log" 2>&1; echo "QT_BUILD_EXIT=$?"
tail -6 "$OUT/qt_build.log"

echo "===== 3. 给二进制授予 HCI 能力（关闭控制器去重的唯一权限点） ====="
echo admin | sudo -S /usr/sbin/setcap cap_net_admin,cap_net_raw+ep "$APP/build-target/railhmi" 2>/dev/null
echo "after setcap: $(getcap "$APP/build-target/railhmi" 2>/dev/null || echo FAILED)"

echo "===== 4. 重启 :0（带 DOOR_DEBUG 只为取帧节奏证据，末尾会清掉） ====="
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
echo "--- 关键启动日志 ---"
grep -aE 'door-sensor: (enabled|HCI filter_dup|warning: cannot disable|PropertiesChanged subscribed|SetDiscoveryFilter OK|StartDiscovery OK)|system-metrics:|nvr-status:|video workers started' "$LOG" | head -20
echo "--- QML 错误（应为 0 行） ---"
grep -a -iE 'qml.*error|TypeError|ReferenceError|Cannot assign|is not defined' "$LOG" | head -8 || true

echo "===== 5. 30s 并行取证：HCI 广播节奏 / BlueZ 缓存节奏 / HMI 读取节奏 ====="
B=$OUT/round5_btmon.txt
P=$OUT/round5_poll.txt
echo admin | sudo -S timeout 32 btmon -t > "$B" 2>&1 &
BPID=$!
python3 "$ROOT/hmi_verify/ble_poll_probe.py" 30 1 > "$P" 2>&1 &
PPID=$!
wait "$PPID" 2>/dev/null || true
wait "$BPID" 2>/dev/null || true
echo "--- 窗口内 Filter duplicates 设置（应全为 Disabled） ---"
grep -A2 'LE Set Scan En' "$B" | grep -i 'Filter duplicates' | sort | uniq -c
echo "--- HCI 层 T1 广播时间戳与相邻间隔（厂家标称 1s） ---"
awk '/> HCI Event: LE Meta Event/{ts=$NF} /Address: C0:6D:9A:AC:C3:87/{print ts}' "$B" \
  | sort -n | uniq > "$OUT/t1_adv_times.txt"
python3 - "$OUT/t1_adv_times.txt" <<'PY'
import sys
from datetime import datetime
rows = [l.strip() for l in open(sys.argv[1]) if l.strip()]
def sec(s):
    h, m, rest = s.split(':'); return int(h)*3600+int(m)*60+float(rest)
d = []
for a, b in zip(rows, rows[1:]):
    gap = sec(b)-sec(a)
    if gap > 0.05: d.append(gap)
print("  ADV events=%d distinct_gaps=%d" % (len(rows), len(d)))
if d:
    s = sorted(d)
    print("  gap min=%.3fs median=%.3fs max=%.3fs  (<=5s: %d/%d)"
          % (s[0], s[len(s)//2], s[-1], sum(1 for x in d if x <= 5.0), len(d)))
PY
echo "--- BlueZ 缓存中门状态帧变化节奏（1s 轮询探针） ---"
cat "$P"
echo "--- HMI 侧真实解码帧节奏（DOOR_DEBUG） ---"
grep -a 'decoded vendor frame' "$LOG" | tail -18
echo "--- HMI 侧 1s 轮询节奏（poll# 计数） ---"
grep -a 'scan objects' "$LOG" | tail -6
echo "--- 事件推送节奏（EVENT push） ---"
grep -a 'EVENT push#' "$LOG" | tail -8

echo "===== 6. 真机截图（P1 / P6 门磁新鲜度） ====="
snap0(){ DISPLAY=:0 ffmpeg -y -loglevel error -f x11grab -video_size 1280x800 -i :0 -frames:v 1 "$1" >/dev/null 2>&1; }
click0(){ DISPLAY=:0 xdotool mousemove "$1" "$2" click 1; }
click0 44 92; sleep 2; snap0 "$OUT/R5_P1.png"
click0 44 532; sleep 2; snap0 "$OUT/R5_P6_DOOR.png"
click0 44 92; sleep 1
ls -l "$OUT"/R5_*.png

echo "===== 7. 清掉 DOOR_DEBUG，恢复干净生产实例 ====="
OLD=$PID
C=()
if [ -n "$OLD" ]; then
  mapfile -d '' -t E < "/proc/$OLD/environ"
  for kv in "${E[@]}"; do
    case "$kv" in
      DISPLAY=*|RAILHMI_DOOR_DEBUG=*|RAILHMI_DOOR_SIM_FRAME=*|RAILHMI_DOOR_SIM_FOLLOWUP=*|RAILHMI_DUMP_LAYOUT=*|RAILHMI_WINDOWED=*) ;;
      *) C+=("$kv");; esac
  done
  kill "$OLD" 2>/dev/null || true
  for i in 1 2 3 4 5 6 7 8 9 10; do kill -0 "$OLD" 2>/dev/null || break; sleep 1; done
fi
env -i "${C[@]}" DISPLAY=:0 setsid nohup ./build-target/railhmi \
    --config "$APP/config" > "$LOG.clean" 2>&1 < /dev/null &
sleep 12
echo "--- 干净实例状态 ---"
pgrep -af '^\./build-target/railhmi --config' | head -2
grep -aE 'door-sensor: (enabled|HCI filter_dup|warning: cannot disable)|video workers started|nvr-status:' "$LOG.clean" | head -10
echo "--- 干净实例 QML 错误（应为 0 行） ---"
grep -a -iE 'qml.*error|TypeError|ReferenceError|Cannot assign|is not defined' "$LOG.clean" | head -8 || true
echo "===== DONE ====="
