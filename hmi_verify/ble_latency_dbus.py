#!/usr/bin/env python3
"""Board-side: timestamp every BlueZ PropertiesChanged for the real T1 door sensor.

Prints the full ManufacturerData hex so the last-byte advertisement counter can be
compared against the raw HCI capture. A counter jump > 1 between two D-Bus signals
means BlueZ/kernel dropped advertisements -> software-side latency is fixable.
"""
import datetime
import sys
import time

import dbus
import dbus.mainloop.glib
from gi.repository import GLib

DURATION = int(sys.argv[1]) if len(sys.argv) > 1 else 70
DOOR_TOKEN = "C0_6D_9A_AC_C3_87"
dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
bus = dbus.SystemBus()
start = time.time()
count = 0


def on_props(iface, changed, invalidated, path=None, sender=None):
    global count
    if DOOR_TOKEN not in str(path):
        return
    count += 1
    now = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
    hexstr = ""
    mfr = changed.get("ManufacturerData")
    if mfr:
        for company, payload in mfr.items():
            try:
                hexstr = bytes(int(x) for x in payload).hex()
            except Exception as exc:  # diagnostic only
                hexstr = "unparsed(%s)" % exc
    rssi = changed.get("RSSI", "")
    door = hexstr[6:12] if len(hexstr) >= 12 else ""
    counter = hexstr[-2:] if hexstr else ""
    print("%s t=%7.2fs rssi=%-4s counter=%-2s doorbytes=%-6s mfr=%s"
          % (now, time.time() - start, rssi, counter, door, hexstr), flush=True)


bus.add_signal_receiver(on_props, signal_name="PropertiesChanged",
                        dbus_interface="org.freedesktop.DBus.Properties",
                        path_keyword="path", sender_keyword="sender")
print("dbus-probe: listening %ds for %s" % (DURATION, DOOR_TOKEN), flush=True)
loop = GLib.MainLoop()
GLib.timeout_add(DURATION * 1000, loop.quit)
loop.run()
print("dbus-probe: total=%d events" % count, flush=True)
