#!/usr/bin/env python3
"""Board-side: capture the exact PropertiesChanged keys BlueZ pushes for the T1 door sensor.

This decides whether the HMI can decode the door byte straight from the pushed signal
(no extra D-Bus read), which is what makes an open-door event show up immediately.
"""
import sys
import time

import dbus
import dbus.mainloop.glib
from gi.repository import GLib

DURATION = int(sys.argv[1]) if len(sys.argv) > 1 else 45
dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
bus = dbus.SystemBus()
DOOR = "/org/bluez/hci0/dev_C0_6D_9A_AC_C3_87"
start = time.time()
events = []


def on_props(iface, changed, invalidated, path=None, sender=None):
    if str(path) != DOOR:
        return
    t = round(time.time() - start, 2)
    detail = {}
    for k, v in changed.items():
        key = str(k)
        if key == "ManufacturerData":
            try:
                vals = []
                for company, payload in v.items():
                    vals.append("company=0x%04x bytes=%s" %
                                (int(company), "".join("%02x" % b for b in payload)))
                detail[key] = "; ".join(vals)
            except Exception as exc:  # pragma: no cover - diagnostic only
                detail[key] = "unparsed(%s): %r" % (exc, v)
        else:
            detail[key] = str(v)
    events.append((t, str(iface or ""), detail, [str(x) for x in invalidated]))


bus.add_signal_receiver(on_props, signal_name="PropertiesChanged",
                        dbus_interface="org.freedesktop.DBus.Properties",
                        path=DOOR, path_keyword="path", sender_keyword="sender")

print("door-keys: listening %ds on %s" % (DURATION, DOOR), flush=True)
loop = GLib.MainLoop()
GLib.timeout_add(DURATION * 1000, loop.quit)
loop.run()
print("=== %d DOOR PROPERTIESCHANGED EVENTS ===" % len(events), flush=True)
for t, iface, detail, invalid in events:
    print("  t=%6.2fs iface=%s keys=%s" % (t, iface, detail), flush=True)
    if invalid:
        print("        invalidated=%s" % invalid, flush=True)
print("=== END ===", flush=True)
