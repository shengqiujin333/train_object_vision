#!/usr/bin/env python3
"""Board-side: census ALL org.bluez D-Bus signals for N seconds.

The earlier probe used bus_name='org.bluez'; this one uses no bus_name and
records member/interface/path for every BlueZ signal, so we can tell whether
PropertiesChanged is genuinely absent or just filtered by the match rule.
"""
import sys
import time

import dbus
import dbus.mainloop.glib
from gi.repository import GLib

DURATION = int(sys.argv[1]) if len(sys.argv) > 1 else 30
dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
bus = dbus.SystemBus()

counts = {}
door_props = []
start = time.time()


def on_any(*args, **kwargs):
    member = str(kwargs.get("member", "?"))
    iface = str(kwargs.get("interface", "?"))
    path = str(kwargs.get("path", "?"))
    key = iface + "." + member
    counts[key] = counts.get(key, 0) + 1
    if "dev_C0_6D_9A_AC_C3_87" in path:
        door_props.append((round(time.time() - start, 2), key, path))


bus.add_signal_receiver(on_any, dbus_interface=None, bus_name=None,
                        member_keyword="member", interface_keyword="interface",
                        path_keyword="path", message_keyword="message")

print("census: listening %ds for ALL BlueZ signals" % DURATION, flush=True)
loop = GLib.MainLoop()
GLib.timeout_add(DURATION * 1000, loop.quit)
loop.run()

print("=== SIGNAL COUNTS ===", flush=True)
for k in sorted(counts):
    print("  %-70s %d" % (k, counts[k]), flush=True)
print("=== DOOR DEVICE SIGNALS ===", flush=True)
for t, k, p in door_props[:40]:
    print("  %.2f  %s  %s" % (t, k, p), flush=True)
print("=== END ===", flush=True)
