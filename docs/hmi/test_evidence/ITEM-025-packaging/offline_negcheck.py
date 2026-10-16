# -*- coding: utf-8 -*-
"""Offline repro of the on-board NEG_PY flow (v2 anomaly: NEG_MUTATED_EXIT=2)."""
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(".").resolve()
BASE = REPO / "docs/hmi/test_evidence/ITEM-025-packaging"

fx_src = (BASE / "verify_packaging25_fixtures.py").read_text(encoding="utf-8")
m = re.search(r"NEG_PY = r'''\n(.*?)'''", fx_src, re.S)
neg_src = m.group(1)

with tempfile.TemporaryDirectory() as td:
    root = Path(td) / "railhmi_build"
    shutil.copytree(REPO / "packaging", root / "packaging")
    shutil.copytree(REPO / "app" / "src", root / "src")
    (root / "tools").mkdir()
    shutil.copy(REPO / "app" / "tools" / "check_packaging.py", root / "tools" / "check_packaging.py")
    (root / "verify_neg25.py").write_text(neg_src, encoding="utf-8")
    r = subprocess.run([sys.executable, "verify_neg25.py"], capture_output=True,
                       text=True, encoding="utf-8", errors="replace",
                       cwd=str(root), timeout=120)
    print("RC=%d" % r.returncode)
    print("STDOUT:\n%s" % r.stdout)
    print("STDERR:\n%s" % r.stderr[:2000])
