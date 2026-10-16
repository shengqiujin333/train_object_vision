# -*- coding: utf-8 -*-
"""ITEM-025 verification — offline self-check 3 (board-free).

Covers the two fixture bugs fixed after v2:
  - ext cpp: no block-comment residue (*/ orphan), no stray backticks,
    balanced braces, RESULT once.
  - NEG_PY (extracted from the fixtures driver): executes on a synthetic
    board layout (packaging/ + src/ + tools/) and yields EXIT 0 with
    NEG_HEALTHY_EXIT=0 / NEG_MUTATED_EXIT=1 and named [FAIL] lines.
"""
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

BASE = Path(__file__).resolve().parent
REPO = BASE.parents[3]

fails = []


def step(desc, ok, detail=""):
    print(("PASS" if ok else "FAIL"), desc, detail)
    if not ok:
        fails.append(desc + " " + detail)


# 1. ext cpp structural checks
v = (BASE / "verify_packaging25_ext.cpp").read_text(encoding="utf-8", errors="replace")
step("ext no block-comment residue", "/*" not in v and "*/" not in v)
step("ext no stray backticks", "`" not in v)
step("ext braces balanced", v.count("{") == v.count("}"),
     "%d vs %d" % (v.count("{"), v.count("}")))
step("ext RESULT once", v.count('std::printf("RESULT:') == 1)
step("ext no em-dash outside comments-comment lines",
     all(l.strip().startswith("//") for l in v.splitlines() if "—" in l))

# 2. NEG_PY flow on synthetic board layout
fx_src = (BASE / "verify_packaging25_fixtures.py").read_text(encoding="utf-8")
m = re.search(r"NEG_PY = r'''\n(.*?)'''", fx_src, re.S)
step("NEG_PY block extractable", m is not None)
with tempfile.TemporaryDirectory() as td:
    root = Path(td) / "railhmi_build"
    shutil.copytree(REPO / "packaging", root / "packaging")
    shutil.copytree(REPO / "app" / "src", root / "src")
    (root / "tools").mkdir()
    shutil.copy(REPO / "app" / "tools" / "check_packaging.py",
                root / "tools" / "check_packaging.py")
    (root / "verify_neg25.py").write_text(m.group(1), encoding="utf-8")
    r = subprocess.run([sys.executable, "verify_neg25.py"], capture_output=True,
                       text=True, encoding="utf-8", errors="replace",
                       cwd=str(root), timeout=120)
    step("NEG driver exit 0", r.returncode == 0, "rc=%d stderr=%s"
         % (r.returncode, r.stderr[-200:] if r.stderr else ""))
    step("NEG healthy PASS / mutated FAIL", "NEG_HEALTHY_EXIT=0" in r.stdout
         and "NEG_MUTATED_EXIT=1" in r.stdout, r.stdout[:200])
    step("NEG named failures",
         "[FAIL] systemd unit missing auto-restart" in r.stdout
         and "cpu-performance" in r.stdout)

result = "PASS" if not fails else "FAIL"
print("OFFLINE-3 RESULT: %s (%d failed)" % (result, len(fails)))
raise SystemExit(0 if not fails else 1)
