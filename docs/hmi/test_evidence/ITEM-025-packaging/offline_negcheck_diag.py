# -*- coding: utf-8 -*-
"""Instrumented NEG repro: capture the mutated run's stderr to explain exit 2."""
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(".").resolve()
BASE = REPO / "docs/hmi/test_evidence/ITEM-025-packaging"
fx_src = (BASE / "verify_packaging25_fixtures.py").read_text(encoding="utf-8")
neg_src = re.search(r"NEG_PY = r'''\n(.*?)'''", fx_src, re.S).group(1)
neg_src2 = neg_src.replace(
    'neg = "/tmp/railhmi_verify025/negpkg"', 'neg = "/tmp/negcheck_diag/negpkg"')
neg_src2 = neg_src2.replace(
    'print("NEG_MUTATED_EXIT=%d" % r1.returncode)',
    'print("NEG_MUTATED_EXIT=%d" % r1.returncode)\n'
    'sys.stdout.write("stderr tail: %r\\n" % r1.stderr[-400:])\n'
    'sys.stdout.write("stdout tail: %r\\n" % r1.stdout[-400:])')
td = tempfile.mkdtemp()
root = Path(td) / "railhmi_build"
shutil.copytree(REPO / "packaging", root / "packaging")
shutil.copytree(REPO / "app" / "src", root / "src")
(root / "tools").mkdir()
shutil.copy(REPO / "app" / "tools" / "check_packaging.py", root / "tools" / "check_packaging.py")
(root / "verify_neg25d.py").write_text(neg_src2, encoding="utf-8")
r = subprocess.run([sys.executable, "verify_neg25d.py"], capture_output=True,
                   text=True, encoding="utf-8", errors="replace",
                   cwd=str(root), timeout=120)
print("RC=%d" % r.returncode)
print(r.stdout)
print("driver stderr:", r.stderr[-400:])
