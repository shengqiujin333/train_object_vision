#!/usr/bin/env python3
"""Board-side probe: does BlueZ actually PUSH Device1 changes for the real door sensor?

Uses a normal D-Bus match rule (not eavesdropping), so no root/eavesdrop policy needed.
Counts PropertiesChanged / InterfacesAdded signals for the real BWD1 (T1) sensor and
reports which property keys are pushed. Result decides whether the HMI can react to
an advertisement immediately instead of waiting for the next 10s GetManagedObjects poll.

Usage: python3 ble_signal_probe.py [seconds]
"""
import sys
import time

import dbus
import dbus.mainloop.glib
from gi.repository import GLib

DURATION = int(sys.argv[1]) if len(sys.argv) > 1 else 45
dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
bus = dbus.SystemBus()

DOOR_PATH = "dev_C0_6D_9A_AC_C3_87"
counts = {"props": 0, "added": 0, "props_door": 0, "added_door": 0, "removed": 0}
keys = {}
door_events = []
start = time.time()


def on_props(iface, changed, invalidated, path=None):
    counts["props"] += 1
    p = str(path or "")
    if DOOR_PATH not in p:
        return
    counts["props_door"] += 1
    now = time.time() - start
    names = sorted(str(k) for k in changed.keys())
    for k in names:
        keys[k] = keys.get(k, 0) + 1
    if len(door_events) < 30:
        door_events.append((round(now, 2), p, names))


def on_added(path, ifaces):
    counts["added"] += 1
    if DOOR_PATH in str(path):
        counts["added_door"] += 1
        door_events.append((round(time.time() - start, 2), str(path),
                            ["InterfacesAdded:" + ",".join(sorted(str(k) for k in ifaces.keys()))]))


def on_removed(path, ifaces):
    counts["removed"] += 1


bus.add_signal_receiver(on_props, signal_name="PropertiesChanged",
                        dbus_interface="org.freedesktop.DBus.PropertiesChanged",
                        bus_name="org.bluez", path_keyword="path")
bus.add_signal_receiver(on_added, signal_name="InterfacesAdded",
                        dbus_interface="org.freedesktop.DBus.ObjectManager",
                        bus_name="org.bluez")
bus.add_signal_receiver(on_removed, signal_name="InterfacesRemoved",
                        dbus_interface="org.freedesktop.DBus.ObjectManager",
                        bus_name="org.bluez")

print("probe: listening %ds for org.bluez signals (match-rule, no eavesdrop)" % DURATION,
      flush=True)
loop = GLib.MainLoop()
GLib.timeout_add(DURATION * 1000, loop.quit)
loop.run()

print("=== COUNTS === %s" % counts, flush=True)
print("=== DOOR PROPERTY KEYS === %s" % keys, flush=True)
print("=== DOOR TIMELINE (t_sec, path, keys) ===", flush=True)
for t, p, k in door_events:
    print("  %.2f  %s  %s" % (t, p, k), flush=True)
if counts["props_door"] > 1:
    gaps = [door_events[i][0] - door_events[i - 1][0]
            for i in range(1, len(door_events)) if door_events[i][0] >= 0]
    if gaps:
        print("=== DOOR EVENT GAP min=%.2fs max=%.2fs avg=%.2fs ===" %
              (min(gaps), max(gaps), sum(gaps) / len(gaps)), flush=True)
print("=== END ===", flush=True)
