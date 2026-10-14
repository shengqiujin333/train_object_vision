#!/usr/bin/env python3
"""Dahua PTZ zoom/focus control via CGI (192.168.1.65).

Interactive (run in terminal on the board, watch the demo window while pressing):
  python3 ptz_control.py
    i = zoom in (拉近) 0.5s      o = zoom out (拉远) 0.5s
    I = zoom in 2s               O = zoom out 2s
    n = focus near (近焦)        f = focus far (远焦)
    1-8 = set zoom speed         q = quit

One-shot mode (from SSH):
  python3 ptz_control.py in 1        # zoom in 1 second
  python3 ptz_control.py out 2       # zoom out 2 seconds
  python3 ptz_control.py focusnear 0.5
  python3 ptz_control.py focusfar 0.5
"""
import sys
import time

import requests
from requests.auth import HTTPDigestAuth

HOST = "192.168.1.65"
USER = "admin"
PASS = "ipc12345"
SPEED = 4  # 1-8, higher = faster
BURST = 0.5  # seconds per keypress in interactive mode

CODES = {
    "in": "ZoomTele",
    "out": "ZoomWide",
    "focusnear": "FocusNear",
    "focusfar": "FocusFar",
}


def ptz(code, action, speed=SPEED):
    url = (f"http://{HOST}/cgi-bin/ptz.cgi?action={action}&code={code}"
           f"&channel=0&arg1={speed}&arg2=0&arg3=0")
    r = requests.get(url, auth=HTTPDigestAuth(USER, PASS), timeout=5)
    if r.status_code != 200 or "OK" not in r.text:
        print(f"  ERROR: HTTP {r.status_code} {r.text[:100]}")
        return False
    return True


def burst(cmd, seconds):
    code = CODES[cmd]
    if not ptz(code, "start"):
        return
    time.sleep(seconds)
    ptz(code, "stop")


def interactive():
    global SPEED
    print(__doc__.split("One-shot")[0])
    print(f"host={HOST} speed={SPEED} burst={BURST}s  (press keys, q to quit)")
    import termios
    import tty
    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd)
    tty.setraw(fd)
    try:
        while True:
            ch = sys.stdin.read(1)
            if ch in ("q", "Q", "\x03"):
                break
            elif ch == "i":
                print(f"\rzoom in  {BURST}s (speed {SPEED})")
                burst("in", BURST)
            elif ch == "o":
                print(f"\rzoom out {BURST}s (speed {SPEED})")
                burst("out", BURST)
            elif ch == "I":
                print(f"\rzoom in  2s (speed {SPEED})")
                burst("in", 2)
            elif ch == "O":
                print(f"\rzoom out 2s (speed {SPEED})")
                burst("out", 2)
            elif ch == "n":
                print(f"\rfocus near {BURST}s")
                burst("focusnear", BURST)
            elif ch == "f":
                print(f"\rfocus far {BURST}s")
                burst("focusfar", BURST)
            elif ch.isdigit() and ch != "0":
                SPEED = int(ch)
                print(f"\rspeed = {SPEED}")
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old)
        print("\nbye")


def main():
    if len(sys.argv) == 1:
        interactive()
        return
    if len(sys.argv) == 3 and sys.argv[1] in CODES:
        try:
            seconds = float(sys.argv[2])
        except ValueError:
            print("seconds must be a number, e.g. 0.5 / 1 / 2")
            return
        print(f"{sys.argv[1]} for {seconds}s ...")
        burst(sys.argv[1], seconds)
        print("done")
        return
    print(__doc__)


if __name__ == "__main__":
    main()
