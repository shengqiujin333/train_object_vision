#!/usr/bin/env python3
"""Controlled experiment: raw HCI filter_dup=0 cadence vs BlueZ device visibility.

Modes:
  none       - BlueZ discovery only (baseline)
  once       - issue LE Set Scan Enable(filter_dup=0) once, right after StartDiscovery
  every2     - issue it every 2s (round-5 behaviour)
  every5     - issue it every 5s

BlueZ owns discovery in every mode; this script is the ONLY discovery client.
It records both D-Bus PropertiesChanged pushes and 1s GetManagedObjects polls
for the real T1 door sensor, so we can tell whether dedup-off survives and
whether BlueZ keeps exposing Device1 objects at all.
"""
import os
import socket
import struct
import sys
import time

import dbus
import dbus.mainloop.glib
from gi.repository import GLib

MODE = sys.argv[1] if len(sys.argv) > 1 else "once"
DURATION = int(sys.argv[2]) if len(sys.argv) > 2 else 45
TARGET = "C0:6D:9A:AC:C3:87"
DEV_PATH_SUFFIX = "dev_C0_6D_9A_AC_C3_87"

AF_BLUETOOTH = 31
BTPROTO_HCI = 1
HCI_CHANNEL_RAW = 0
CMD = bytes([0x01, 0x0c, 0x20, 0x02, 0x01, 0x00])  # LE Set Scan Enable, enable=1, filter_dup=0

dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
bus = dbus.SystemBus()
obj = bus.get_object("org.bluez", "/org/bluez/hci0")
adapter = dbus.Interface(obj, "org.bluez.Adapter1")
props = dbus.Interface(obj, "org.freedesktop.DBus.Properties")
om = dbus.Interface(bus.get_object("org.bluez", "/"), "org.freedesktop.DBus.ObjectManager")

import subprocess

hci_ok = MODE != "none"


def hci_disable_dup():
    """Equivalent of the C++ raw-HCI helper; hcitool is just the probe's transport."""
    if not hci_ok:
        return False
    try:
        r = subprocess.run(["sudo", "-S", "hcitool", "cmd", "0x08", "0x000c", "01", "00"],
                           input=b"admin\n", stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL, timeout=3)
        return r.returncode == 0
    except Exception as exc:
        print("  hci cmd err=%s" % exc, flush=True)
        return False


def frame_from_mfg(mfg):
    for company, value in mfg.items():
        b = bytes(int(x) for x in value)
        if len(b) >= 6:
            return (b[-1], b[4] if len(b) > 4 else -1,
                    " ".join("%02x" % x for x in b))
    return None


def read_cached():
    objects = om.GetManagedObjects()
    devices = sum(1 for p, i in objects.items() if "org.bluez.Device1" in i)
    cur = None
    for p, i in objects.items():
        if DEV_PATH_SUFFIX in str(p):
            cur = frame_from_mfg(i.get("org.bluez.Device1", {}).get("ManufacturerData", {}))
    return devices, cur


push_times = []
last_push_frame = [None]


def on_props(iface, changed, invalidated, path=None):
    if iface != "org.bluez.Device1" or DEV_PATH_SUFFIX not in str(path):
        return
    if "ManufacturerData" not in changed:
        return
    cur = frame_from_mfg(changed["ManufacturerData"])
    if cur is not None and cur != last_push_frame[0]:
        last_push_frame[0] = cur
        push_times.append(time.time())


bus.add_signal_receiver(
    on_props,
    signal_name="PropertiesChanged",
    dbus_interface="org.freedesktop.DBus.Properties",
    path_keyword="path",
    arg0="org.bluez.Device1",
)

try:
    adapter.SetDiscoveryFilter({"Transport": dbus.String("le", variant_level=1),
                                "DuplicateData": dbus.Boolean(True, variant_level=1)})
except Exception as exc:
    print("SetDiscoveryFilter err=%s" % exc, flush=True)
try:
    adapter.StopDiscovery()
except Exception:
    pass
time.sleep(0.5)
try:
    adapter.StartDiscovery()
    print("StartDiscovery OK (we own discovery)", flush=True)
except Exception as exc:
    print("StartDiscovery FAILED %s" % exc, flush=True)

start = time.time()
if MODE == "once":
    time.sleep(1.0)
    print("t=%.1f HCI filter_dup=0 once -> %s" % (time.time() - start, hci_disable_dup()), flush=True)

last_hci = 0.0
period = {"every2": 2.0, "every5": 5.0}.get(MODE, 0.0)
poll_changes = 0
last_poll_frame = None
loop = GLib.MainLoop()
ticks = [0]


def tick():
    global last_hci, poll_changes, last_poll_frame
    t = time.time() - start
    ticks[0] += 1
    if period and t - last_hci >= period:
        last_hci = t
        hci_disable_dup()
    try:
        devices, cur = read_cached()
        discovering = bool(props.Get("org.bluez.Adapter1", "Discovering"))
        if cur is not None and cur != last_poll_frame:
            poll_changes += 1
            last_poll_frame = cur
            print("  t=%5.1f poll  Discovering=%-5s devices=%2d counter=%3d door=%d (change#%d) [%s]"
                  % (t, discovering, devices, cur[0], cur[1], poll_changes, cur[2]), flush=True)
        if ticks[0] % 10 == 0:
            print("  t=%5.1f hb    Discovering=%-5s devices=%2d T1=%-5s pushes=%d"
                  % (t, discovering, devices, cur is not None, len(push_times)), flush=True)
    except Exception as exc:
        print("  t=%5.1f read err=%s" % (t, exc), flush=True)
    if t >= DURATION:
        loop.quit()
        return False
    return True


GLib.timeout_add(1000, tick)
loop.run()
print("=== mode=%s duration=%ds poll_changes=%d pushes=%d ===" %
      (MODE, DURATION, poll_changes, len(push_times)), flush=True)
if len(push_times) > 1:
    gaps = [b - a for a, b in zip(push_times, push_times[1:])]
    gaps.sort()
    print("    push gaps min=%.2fs median=%.2fs max=%.2fs  <=5s: %d/%d"
          % (gaps[0], gaps[len(gaps) // 2], gaps[-1],
             sum(1 for g in gaps if g <= 5.0), len(gaps)), flush=True)
try:
    adapter.StopDiscovery()
except Exception:
    pass
