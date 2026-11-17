#!/usr/bin/env python3
"""Root probe: does a periodic raw-HCI filter_dup=0 command break BlueZ discovery?

Starts our OWN long-lived discovery client, then samples Discovering / device count
/ T1 presence every second while re-issuing the HCI command at a configurable cadence.
"""
import subprocess
import sys
import time

import dbus
import dbus.mainloop.glib

CADENCE = int(sys.argv[1]) if len(sys.argv) > 1 else 5
DURATION = int(sys.argv[2]) if len(sys.argv) > 2 else 40
dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
bus = dbus.SystemBus()
ADAPTER = "/org/bluez/hci0"
obj = bus.get_object("org.bluez", ADAPTER)
adapter = dbus.Interface(obj, "org.bluez.Adapter1")
props = dbus.Interface(obj, "org.freedesktop.DBus.Properties")
om = dbus.Interface(bus.get_object("org.bluez", "/"), "org.freedesktop.DBus.ObjectManager")


def hci():
    r = subprocess.run(["hcitool", "cmd", "0x08", "0x000c", "01", "00"],
                       capture_output=True, text=True)
    return "Send failed" not in (r.stdout + r.stderr)


try:
    adapter.SetDiscoveryFilter({"Transport": dbus.String("le", variant_level=1),
                                "DuplicateData": dbus.Boolean(True, variant_level=1)})
    print("SetDiscoveryFilter OK", flush=True)
except Exception as exc:
    print("SetDiscoveryFilter FAILED %s" % exc, flush=True)
try:
    adapter.StopDiscovery()
except Exception:
    pass
try:
    adapter.StartDiscovery()
    print("StartDiscovery OK (we own the client)", flush=True)
except Exception as exc:
    print("StartDiscovery FAILED %s" % exc, flush=True)

start = time.time()
last_hci = -100
while time.time() - start < DURATION:
    t = time.time() - start
    if t - last_hci >= CADENCE:
        last_hci = t
        print("  t=%5.1f hci_cmd=%s" % (t, hci()), flush=True)
    try:
        discovering = bool(props.Get("org.bluez.Adapter1", "Discovering"))
        objects = om.GetManagedObjects()
        devices = sum(1 for p, i in objects.items() if "org.bluez.Device1" in i)
        t1 = any("dev_C0_6D_9A_AC_C3_87" in str(p) for p in objects)
        mfr = ""
        for p, i in objects.items():
            if "dev_C0_6D_9A_AC_C3_87" in str(p):
                m = i.get("org.bluez.Device1", {}).get("ManufacturerData", {})
                for c, v in m.items():
                    mfr = bytes(int(x) for x in v).hex()[-8:]
        print("  t=%5.1f Discovering=%s devices=%d T1=%s tail=%s"
              % (t, discovering, devices, t1, mfr), flush=True)
    except Exception as exc:
        print("  t=%5.1f read error %s" % (t, exc), flush=True)
    time.sleep(1)
print("=== END ===", flush=True)
