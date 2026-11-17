#!/usr/bin/env python3
"""Prototype: read T1 door-sensor advertisements straight from a raw HCI socket.

This is the candidate production path for <=5s door-state detection: it does
not depend on BlueZ Device1 objects, PropertiesChanged throttling, or
GetManagedObjects caching. Usage:

    sudo python3 ble_hci_raw_reader.py <seconds> [rescan_period]

Needs CAP_NET_RAW + CAP_NET_ADMIN (or root).
"""
import socket
import struct
import sys
import time

DURATION = float(sys.argv[1]) if len(sys.argv) > 1 else 30.0
RESCAN = float(sys.argv[2]) if len(sys.argv) > 2 else 3.0

AF_BLUETOOTH = 31
BTPROTO_HCI = 1
SOL_HCI = 0
HCI_FILTER = 2
HCI_EVENT_PKT = 0x04
EVT_LE_META_EVENT = 0x3E
EVT_LE_ADVERTISING_REPORT = 0x02
EVT_LE_EXT_ADVERTISING_REPORT = 0x0D

# LE Set Scan Enable: enable=1, filter_duplicates=0
SCAN_ON_NODUP = bytes([0x01, 0x0c, 0x20, 0x02, 0x01, 0x00])

s = socket.socket(AF_BLUETOOTH, socket.SOCK_RAW, BTPROTO_HCI)
s.bind((0,))
# struct hci_filter: uint32 type_mask; uint32 event_mask[2]; uint16 opcode;
# padded to 16 bytes. Allow HCI event packets, LE Meta events only.
f = struct.pack("IIIHH",
                1 << HCI_EVENT_PKT,
                0,
                1 << (EVT_LE_META_EVENT - 32),
                0,
                0)
s.setsockopt(SOL_HCI, HCI_FILTER, f)
s.setblocking(False)


def scan_on():
    try:
        s.send(SCAN_ON_NODUP)
        return True
    except OSError as exc:
        print("scan_on failed: %s" % exc, flush=True)
        return False


def ad_structures(data):
    out = []
    p = 0
    while p + 1 < len(data):
        length = data[p]
        if length == 0:
            break
        typ = data[p + 1]
        val = data[p + 2:p + 1 + length]
        if len(val) != length - 1:
            break
        out.append((typ, val))
        p += 1 + length
    return out


def frame(data):
    name = ""
    mfg = None
    for typ, val in ad_structures(data):
        if typ in (0x08, 0x09):
            name = val.decode("ascii", errors="replace")
        elif typ == 0xFF and len(val) >= 4:
            company = val[0] | (val[1] << 8)
            if company == 0xFF07:
                mfg = val[2:]
    return name, mfg


scan_on()
start = time.time()
last_rescan = start
events = 0
last_frame = None
changes = 0
gaps = []
last_t = None
print("raw HCI reader started, duration=%.0fs rescan=%.1fs" % (DURATION, RESCAN), flush=True)
while time.time() - start < DURATION:
    now = time.time()
    if RESCAN > 0 and now - last_rescan >= RESCAN:
        last_rescan = now
        scan_on()
    try:
        buf = s.recv(2048)
    except BlockingIOError:
        time.sleep(0.05)
        continue
    except OSError as exc:
        print("recv err=%s" % exc, flush=True)
        break
    if len(buf) < 4 or buf[0] != HCI_EVENT_PKT or buf[1] != EVT_LE_META_EVENT:
        continue
    sub = buf[3]
    if sub == EVT_LE_ADVERTISING_REPORT:
        n = buf[4]
        off = 5
        reports = []
        for _ in range(n):
            if off + 10 > len(buf):
                break
            etype = buf[off]
            addr = ":".join("%02X" % x for x in buf[off + 2:off + 8][::-1])
            dlen = buf[off + 8]
            data = buf[off + 9:off + 9 + dlen]
            off += 10 + dlen
            reports.append((etype, addr, data))
    elif sub == EVT_LE_EXT_ADVERTISING_REPORT:
        reports = []  # not used by this sensor (legacy ADV_IND)
    else:
        continue
    for etype, addr, data in reports:
        name, mfg = frame(data)
        if mfg is None or name != "T1":
            continue
        t = time.time() - start
        events += 1
        cur = bytes(mfg)
        if last_t is not None and t - last_t > 0.05:
            gaps.append(t - last_t)
        last_t = t
        if cur != last_frame:
            changes += 1
            door = cur[4] if len(cur) > 4 else -1
            print("  t=%6.2f ADV#%-3d addr=%s door=%d counter=%3d changes=%d [%s]"
                  % (t, events, addr, door, cur[-1], changes,
                     " ".join("%02x" % x for x in cur)), flush=True)
            last_frame = cur
s.close()
print("=== raw ADV events=%d payload_changes=%d in %.0fs ===" % (events, changes, DURATION), flush=True)
if gaps:
    gaps.sort()
    print("    ADV gap min=%.3fs median=%.3fs max=%.3fs  <=5s: %d/%d"
          % (gaps[0], gaps[len(gaps) // 2], gaps[-1],
             sum(1 for g in gaps if g <= 5.0), len(gaps)), flush=True)
