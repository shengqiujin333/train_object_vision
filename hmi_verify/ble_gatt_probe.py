#!/usr/bin/env python3
"""Board-side: is the T1 door sensor connectable and does it expose a GATT
characteristic carrying the door state? If so, HMI can poll/subscribe instead of
waiting for the next advertisement, which is the only way to guarantee < 5s.

Read-only exploration: connect, enumerate services/characteristics, listen for
notifications for a few seconds, then disconnect. No pairing, no bonding, no writes.
"""
import sys
import time

import dbus
import dbus.mainloop.glib
from gi.repository import GLib

LISTEN = int(sys.argv[1]) if len(sys.argv) > 1 else 8
DEV = "/org/bluez/hci0/dev_C0_6D_9A_AC_C3_87"
dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
bus = dbus.SystemBus()
dev = bus.get_object("org.bluez", DEV)
notes = []


def props(path, ifaces, name):
    return {str(k): v for k, v in ifaces.get(name, {}).items()}


def on_props(iface, changed, invalidated, path=None, sender=None):
    if path and "dev_C0_6D_9A_AC_C3_87" in str(path):
        notes.append("%s %s %s" % (time.strftime("%H:%M:%S"), path, dict(changed)))


bus.add_signal_receiver(on_props, signal_name="PropertiesChanged",
                        dbus_interface="org.freedesktop.DBus.Properties",
                        path_keyword="path", sender_keyword="sender")
print("gatt-probe: device=%s listen=%ds" % (DEV, LISTEN), flush=True)
try:
    dev.Connect(dbus_interface="org.bluez.Device1", timeout=12)
    print("CONNECT=OK", flush=True)
except Exception as exc:
    print("CONNECT=FAILED %s" % exc, flush=True)

time.sleep(2)
try:
    om = bus.get_object("org.bluez", "/")
    objects = om.GetManagedObjects(dbus_interface="org.freedesktop.DBus.ObjectManager")
except Exception as exc:
    objects = {}
    print("GetManagedObjects FAILED %s" % exc, flush=True)

services, chars = [], []
for path, ifaces in objects.items():
    if "dev_C0_6D_9A_AC_C3_87" not in str(path):
        continue
    if "org.bluez.GattService1" in ifaces:
        p = props(path, ifaces, "org.bluez.GattService1")
        services.append((str(path), p))
    if "org.bluez.GattCharacteristic1" in ifaces:
        p = props(path, ifaces, "org.bluez.GattCharacteristic1")
        chars.append((str(path), p))

print("=== SERVICES (%d) ===" % len(services), flush=True)
for path, p in services:
    print("  %s uuid=%s primary=%s" % (path, p.get("UUID"), p.get("Primary")), flush=True)
print("=== CHARACTERISTICS (%d) ===" % len(chars), flush=True)
for path, p in chars:
    val = p.get("Value")
    valhex = ""
    if val is not None:
        try:
            valhex = bytes(int(x) for x in val).hex()
        except Exception:
            valhex = repr(val)
    print("  %s uuid=%s flags=%s value=%s" %
          (path, p.get("UUID"), [str(x) for x in p.get("Flags", [])], valhex), flush=True)

# 尝试在支持 notify 的特征上开通知，观察 LISTEN 秒内是否有即时状态推送
started = 0
for path, p in chars:
    flags = [str(x) for x in p.get("Flags", [])]
    if "notify" in flags or "indicate" in flags:
        try:
            bus.get_object("org.bluez", path).StartNotify(
                dbus_interface="org.bluez.GattCharacteristic1", timeout=5)
            started += 1
            print("StartNotify OK %s" % path, flush=True)
        except Exception as exc:
            print("StartNotify FAILED %s %s" % (path, exc), flush=True)
print("notify subscriptions=%d, listening %ds" % (started, LISTEN), flush=True)
loop = GLib.MainLoop()
GLib.timeout_add(LISTEN * 1000, loop.quit)
loop.run()
print("=== PROPERTIESCHANGED DURING LISTEN (%d) ===" % len(notes), flush=True)
for n in notes[:20]:
    print("  " + n, flush=True)

try:
    dev.Disconnect(dbus_interface="org.bluez.Device1", timeout=8)
    print("DISCONNECT=OK", flush=True)
except Exception as exc:
    print("DISCONNECT=FAILED %s" % exc, flush=True)
print("=== END ===", flush=True)
