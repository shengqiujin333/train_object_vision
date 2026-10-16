# -*- coding: utf-8 -*-
"""ITEM-022 fix-round verifier offline self-check (no board needed).

1) Dry-run the updated assert_maintenance_probe oracle against the implementer's
   fix-r1 archived maintenance-probe log (deterministic shape).
2) Local remap dry-run of the updated SCAN_PY against the delivered sources.
3) Greedy hex-escape audit of the verifier ext fixture (C89/C++ \\xHH followed
   by hex digit is consumed greedily). Also negative-dry-run: oracle must
   reject the pre-fix r1 archived log (no reconnect/log-days lines).
Run: python offline_selfcheck4.py   (repo root working directory)
"""
from __future__ import annotations

import io
import os
import re
import sys
from contextlib import redirect_stdout
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import verify_maint22_fixtures as fx  # noqa: E402

ROOT = HERE.parents[3]


def check(name: str, cond: bool) -> bool:
    print("selfcheck[fix] %s: %s" % (name, "OK" if cond else "FAIL"))
    return cond


def main() -> int:
    ok = True

    # 1. oracle vs fix-r1 archived probe log
    fix_log = (HERE / "build-TestRun-192_168_0_103-20261016-ITEM022-fix-r1."
                       "core-evidence_maintenance-probe-20261016-ITEM022-fix-r1.log")
    fails = fx.assert_maintenance_probe(fix_log.read_text(encoding="utf-8", errors="replace"))
    ok &= check("oracle accepts fix-r1 archived probe log", not fails)
    if fails:
        for f in fails:
            print("   oracle-fail: %s" % f)

    # 1b. negative dry-run: pre-fix r1 archive must MISS the new lines
    pre_log = (HERE / "build-TestRun-192_168_0_103-20261016-ITEM022-r1."
                       "core-evidence_maintenance-probe-20261016-ITEM022-r1.log")
    fails_pre = fx.assert_maintenance_probe(pre_log.read_text(encoding="utf-8", errors="replace"))
    ok &= check("oracle rejects pre-fix archive (D-01/D-02 lines absent)",
                any("missing line" in f for f in fails_pre))
    print("   pre-fix oracle fails=%d" % len(fails_pre))

    # 2. local remap dry-run of SCAN_PY
    p6 = (ROOT / "app/src/ui/qml/pages/P6Maintenance.qml").read_text(encoding="utf-8")
    mainq = (ROOT / "app/src/ui/qml/main.qml").read_text(encoding="utf-8")
    mc = (ROOT / "app/src/ui/src/main.cpp").read_text(encoding="utf-8")
    bridge = (ROOT / "app/src/ui/src/maintenance_model.cpp").read_text(encoding="utf-8")
    bridge_h = (ROOT / "app/src/ui/src/maintenance_model.h").read_text(encoding="utf-8")
    bb = (ROOT / "app/tools/board_build.py").read_text(encoding="utf-8")
    checker = (ROOT / "app/tools/check_maintenance_wiring.py").read_text(encoding="utf-8")
    mapping = {
        "src/ui/qml/pages/P6Maintenance.qml": p6,
        "src/ui/qml/main.qml": mainq,
        "src/ui/src/main.cpp": mc,
        "src/ui/src/maintenance_model.cpp": bridge,
        "src/ui/src/maintenance_model.h": bridge_h,
        "tools/board_build.py": bb,
        "tools/check_maintenance_wiring.py": checker,
    }

    def remap_open(path, *a, **kw):
        text = mapping.get(str(path))
        if text is not None:
            return io.StringIO(text)
        raise FileNotFoundError(str(path))

    buf = io.StringIO()
    ns: dict = {"__name__": "verify_scan22", "open": remap_open,
                "os": os, "re": re}
    try:
        with redirect_stdout(buf):
            exec(fx.SCAN_PY, ns, ns)  # single namespace (ITEM-021 lesson)
    except Exception as exc:  # noqa: BLE001
        ok &= check("scan exec completed inside local dry-run", False)
        print("   scan exec error: %r" % (exc,))
        print("SELFCHCHK4_RC=1")
        return 1
    out = buf.getvalue()
    # strip before '=' so 'T1_cards= False' parses as key -> 'False'
    observed = {}
    for line in out.splitlines():
        m4 = re.match(r"^(\w+)=(.*)$", line)
        if m4:
            observed[m4.group(1)] = m4.group(2).strip()
    wants = {k: "True" for k in fx.SCAN_KEYS}
    missing_keys = [k for k in wants if k not in observed]
    ok &= check("scan printed all SCAN_KEYS", not missing_keys)
    if missing_keys:
        print("   missing keys: %s" % missing_keys)
    mismatch = sorted(k for k in wants if observed.get(k) != "True")
    ok &= check("scan expected values all True on delivered sources", not mismatch)
    if mismatch:
        for k in mismatch:
            print("   scan-mismatch: %s=%s" % (k, observed.get(k)))
    # fix-round keys must be present and True specifically
    ok &= check("D1_manual_reconnect_entry_present=True",
                observed.get("D1_manual_reconnect_entry_present") == "True")
    ok &= check("D2_log_day_rolling_present=True",
                observed.get("D2_log_day_rolling_present") == "True")
    ok &= check("T8_reconnect_text_consumed_in_p6=True",
                observed.get("T8_reconnect_text_consumed_in_p6") == "True")

    # 3. greedy hex-escape audit of ext fixture: \xHH followed by [0-9a-fA-Fx]
    ext = (HERE / "verify_maint22_ext.cpp").read_text(encoding="utf-8")
    hazards = []
    for m in re.finditer(r"\\x[0-9a-fA-F]{2}", ext):
        tail = ext[m.end():m.end() + 1]
        if tail and re.match(r"[0-9a-fA-Fx]", tail):
            hazards.append("%d: %s" % (ext[:m.start()].count("\n") + 1,
                                       ext[m.start():m.end() + 2]))
    ok &= check("no greedy hex-escape hazards in ext fixture", not hazards)
    for h in hazards:
        print("   hex-hazard %s" % h)

    print("SELFCHCHK4_RC=%d" % (0 if ok else 1))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
