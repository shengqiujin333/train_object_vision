#!/usr/bin/env python3
"""Board-side: can a 1s GetManagedObjects poll read the newest door byte?

BlueZ caches Device1.ManufacturerData even when PropertiesChanged is throttled.
This probe samples the cached value every second and reports the counter/door byte
transitions, so we can prove end-to-end latency without touching the HMI.
"""
import sys
import time
import datetime

import dbus
import dbus.mainloop.glib

DURATION = int(sys.argv[1]) if len(sys.argv) > 1 else 45
INTERVAL = float(sys.argv[2]) if len(sys.argv) > 2 else 1.0
dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
bus = dbus.SystemBus()
om = bus.get_object("org.bluez", "/")
TOKEN = "dev_C0_6D_9A_AC_C3_87"
start = time.time()
last = None
changes = 0


def sample():
    global last, changes
    try:
        objects = om.GetManagedObjects(dbus_interface="org.freedesktop.DBus.ObjectManager")
    except Exception as exc:
        print("poll error %s" % exc, flush=True)
        return
    for path, ifaces in objects.items():
        if TOKEN not in str(path):
            continue
        dev = ifaces.get("org.bluez.Device1", {})
        mfr = dev.get("ManufacturerData", {})
        for company, payload in mfr.items():
            try:
                b = bytes(int(x) for x in payload)
            except Exception:
                continue
            counter = b[-1] if len(b) else -1
            door = b[4] if len(b) > 4 else -1
            key = (counter, door)
            if key != last:
                changes += 1
                now = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
                print("%s t=%6.2fs counter=%3d door=%d  (change#%d)"
                      % (now, time.time() - start, counter, door, changes), flush=True)
                last = key
        break


print("poll-probe: %.1fs interval, %ds duration" % (INTERVAL, DURATION), flush=True)
while time.time() - start < DURATION:
    sample()
    time.sleep(INTERVAL)
print("poll-probe: total state transitions seen = %d" % changes, flush=True)
