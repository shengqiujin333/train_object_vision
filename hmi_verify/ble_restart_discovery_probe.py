#!/usr/bin/env python3
"""Pure D-Bus alternative to raw HCI: restart BlueZ discovery every N seconds.

Each StopDiscovery/StartDiscovery cycle makes the controller re-issue
LE Set Scan Enable, which clears its duplicate-filter table, so a fresh
advertisement from the 1s-interval door sensor gets through. No raw HCI
command, no root, no capability, no interference with BlueZ's mgmt state.
"""
import sys
import time

import dbus
import dbus.mainloop.glib

PERIOD = float(sys.argv[1]) if len(sys.argv) > 1 else 2.0
DURATION = int(sys.argv[2]) if len(sys.argv) > 2 else 40
dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
bus = dbus.SystemBus()
obj = bus.get_object("org.bluez", "/org/bluez/hci0")
adapter = dbus.Interface(obj, "org.bluez.Adapter1")
props = dbus.Interface(obj, "org.freedesktop.DBus.Properties")
om = dbus.Interface(bus.get_object("org.bluez", "/"), "org.freedesktop.DBus.ObjectManager")

adapter.SetDiscoveryFilter({"Transport": dbus.String("le", variant_level=1),
                            "DuplicateData": dbus.Boolean(True, variant_level=1)})
try:
    adapter.StopDiscovery()
except Exception:
    pass
try:
    adapter.StartDiscovery()
    print("StartDiscovery OK", flush=True)
except Exception as exc:
    print("StartDiscovery FAILED %s" % exc, flush=True)

start = time.time()
last_restart = -100.0
last = None
changes = 0
while time.time() - start < DURATION:
    t = time.time() - start
    if t - last_restart >= PERIOD:
        last_restart = t
        try:
            adapter.StopDiscovery()
        except Exception:
            pass
        try:
            adapter.StartDiscovery()
        except Exception as exc:
            if "InProgress" not in str(exc):
                print("  t=%5.1f StartDiscovery err=%s" % (t, exc), flush=True)
    try:
        objects = om.GetManagedObjects()
        discovering = bool(props.Get("org.bluez.Adapter1", "Discovering"))
        devices = sum(1 for p, i in objects.items() if "org.bluez.Device1" in i)
        cur = None
        for p, i in objects.items():
            if "dev_C0_6D_9A_AC_C3_87" in str(p):
                m = i.get("org.bluez.Device1", {}).get("ManufacturerData", {})
                for c, v in m.items():
                    b = bytes(int(x) for x in v)
                    cur = (b[-1], b[4] if len(b) > 4 else -1)
        if cur != last and cur is not None:
            changes += 1
            print("  t=%5.1f Discovering=%s devices=%d counter=%3d door=%d (change#%d)"
                  % (t, discovering, devices, cur[0], cur[1], changes), flush=True)
            last = cur
        elif int(t) % 10 == 0 and t - int(t) < 0.2:
            print("  t=%5.1f Discovering=%s devices=%d T1=%s"
                  % (t, discovering, devices, cur is not None), flush=True)
    except Exception as exc:
        print("  t=%5.1f read error %s" % (t, exc), flush=True)
    time.sleep(1)
print("=== total frame changes=%d in %ds (period=%ss) ===" % (changes, DURATION, PERIOD), flush=True)
