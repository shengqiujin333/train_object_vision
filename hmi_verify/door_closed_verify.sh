#!/usr/bin/env bash
# 真实关门（磁铁贴合）状态下的门磁链路验收：零仿真帧，纯广播。
set -u
APP=/home/admin/railhmi-ui-fix/app
OUT=/home/admin/door_verify
mkdir -p "$OUT"

echo "===== 0. 验证开始时的真实门磁广播 ====="
python3 - <<'PY'
import dbus
bus = dbus.SystemBus()
om = dbus.Interface(bus.get_object('org.bluez','/'), 'org.freedesktop.DBus.ObjectManager')
for _, ifs in om.GetManagedObjects().items():
    d = ifs.get('org.bluez.Device1')
    if not d: continue
    if 'T1' not in str(d.get('Name','')).upper(): continue
    for k, v in d.get('ManufacturerData', {}).items():
        h = ''.join('%02X' % (int(x)&0xFF) for x in v)
        print('T1', d.get('Address'), 'RSSI', d.get('RSSI'), 'company', hex(k), 'value=', h)
PY

if ! xdpyinfo -display :99 >/dev/null 2>&1; then
  setsid Xvfb :99 -screen 0 1280x800x24 -nolisten tcp > "$OUT/xvfb.log" 2>&1 < /dev/null &
  sleep 2
fi
snap99(){ DISPLAY=:99 ffmpeg -y -loglevel error -f x11grab -video_size 1280x800 -i :99 -frames:v 1 "$1" >/dev/null 2>&1; }
cd "$APP" || exit 1
DISPLAY=:99 QT_QUICK_BACKEND=software RAILHMI_DOOR_DEBUG=1 \
  timeout 30 ./build-target/railhmi --config "$APP/config" > /home/admin/door_closed_verify.log 2>&1 &
pid=$!
sleep 18
DISPLAY=:99 xdotool mousemove 44 92 click 1;  sleep 2; snap99 "$OUT/CLOSED_p1.png"
DISPLAY=:99 xdotool mousemove 44 532 click 1; sleep 2; snap99 "$OUT/CLOSED_p6.png"
wait "$pid" || true

echo "===== 3. 真实关门状态的门磁日志 ====="
grep -a 'door-sensor' /home/admin/door_closed_verify.log | grep -aE 'decoded vendor frame|snapshot|SYNTHETIC|SetDiscoveryFilter|StartDiscovery' | head -14
echo "===== 4. QML 错误 ====="
grep -a -iE 'qml.*error|TypeError|ReferenceError' /home/admin/door_closed_verify.log | head -6

snap0(){ DISPLAY=:0 ffmpeg -y -loglevel error -f x11grab -video_size 1280x800 -i :0 -frames:v 1 "$1" >/dev/null 2>&1; }
DISPLAY=:0 xdotool mousemove 44 92 click 1;  sleep 2; snap0 "$OUT/CLOSED_LIVE_p1.png"
DISPLAY=:0 xdotool mousemove 44 532 click 1; sleep 2; snap0 "$OUT/CLOSED_LIVE_p6.png"
DISPLAY=:0 xdotool mousemove 44 92 click 1;  sleep 1
echo "===== 5. 截图 ====="
ls -l "$OUT"/CLOSED_*.png
