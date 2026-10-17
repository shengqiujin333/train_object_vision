#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""EXT-I 精化判据本地独立复检（不连板端的纯本地扫描）。"""
import re
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
NAMES = ("TRAIN_RK3588_SSH_CREDENTIAL", "TRAIN_FRONT_RTSP_CREDENTIAL",
         "TRAIN_SIDE_RTSP_CREDENTIAL")


def ps_user_var(name):
    r = subprocess.run(["powershell.exe", "-NoProfile", "-Command",
                        '[Environment]::GetEnvironmentVariable("%s","User")' % name],
                       capture_output=True, text=True, timeout=30)
    return (r.stdout or "").strip()


def cred_semantics(text, value):
    out = []
    rv = re.escape(value)
    for m in re.finditer(r"user-pw[\s\"']*%s|password[\s\"'=]*%s|%s@[\w.\-]*"
                         % (rv, rv, rv), text):
        out.append(text[max(0, m.start() - 30):m.end() + 15].replace("\n", " ")[:70])
    return out


def main():
    secrets = [(n, ps_user_var(n)) for n in NAMES]
    secrets = [(n, v) for n, v in secrets if v and len(v) >= 4]
    hits, ssh_note, scanned = [], [], 0
    for p in HERE.rglob("*"):
        if not p.is_file():
            continue
        try:
            t = p.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        scanned += 1
        for name, v in secrets:
            if v not in t:
                continue
            ctxs = cred_semantics(t, v)
            if ctxs:
                hits.append((p.name, name, ctxs[:2]))
            elif name != "TRAIN_RK3588_SSH_CREDENTIAL":
                hits.append((p.name, name, "bare occurrence"))
            else:
                ssh_note.append(p.name)
    print("files_scanned=%d" % scanned)
    print("hits=%s" % (hits or "none"))
    print("ssh username-form-only files=%d" % len(ssh_note))
    print("EXT-I refinded PASS =", not hits)
    return 0 if not hits else 1


if __name__ == "__main__":
    raise SystemExit(main())
