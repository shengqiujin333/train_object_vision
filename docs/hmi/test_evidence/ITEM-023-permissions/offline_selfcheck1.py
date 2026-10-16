# -*- coding: utf-8 -*-
"""ITEM-023 offline self-check 1: local dry-run of the embedded verify_scan23.py
scanner against the actual delivered sources (path-remapped), output parsed the
same way the board step parses it. Catches scanner regex mismatches BEFORE the
board round (ITEM-021 lesson: exec + redirect_stdout, single namespace)."""
import io
import re
import sys
from contextlib import redirect_stdout
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(HERE))
import verify_perm23_fixtures as F  # noqa: E402

ns = {"__name__": "verify_scan23_local"}
# remap open()/paths via chdir-rooted monkeypatch: run with cwd=fake board root
import os

board_root = HERE / "board_local_dryrun"
if not board_root.exists():
    # build the board-layout view: src/... + config/ + PERMISSION_MATRIX.md
    (board_root / "src" / "ui" / "qml" / "pages").mkdir(parents=True, exist_ok=True)
    (board_root / "src" / "ui" / "src").mkdir(parents=True, exist_ok=True)
    (board_root / "config").mkdir(parents=True, exist_ok=True)
    (board_root / "tools").mkdir(parents=True, exist_ok=True)
import shutil

app_root = ROOT / "app"
mapping = {
    "src/ui/qml/pages/P3Replay.qml": app_root / "src/ui/qml/pages/P3Replay.qml",
    "src/ui/qml/pages/P6Maintenance.qml": app_root / "src/ui/qml/pages/P6Maintenance.qml",
    "src/ui/src/permissions_model.cpp": app_root / "src/ui/src/permissions_model.cpp",
    "src/ui/src/permissions_model.h": app_root / "src/ui/src/permissions_model.h",
    "src/ui/src/main.cpp": app_root / "src/ui/src/main.cpp",
    "config/app.yaml": app_root / "config/app.yaml",
    "tools/board_build.py": app_root / "tools/board_build.py",
}
for rel, srcp in mapping.items():
    dst = board_root / rel
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(srcp, dst)
shutil.copyfile(ROOT / "docs/hmi/PERMISSION_MATRIX.md", board_root / "PERMISSION_MATRIX.md")

old_cwd = os.getcwd()
os.chdir(board_root)
buf = io.StringIO()
code = compile(F.SCAN_PY, "verify_scan23.py", "exec")
g = {"__name__": "scan"}
try:
    with redirect_stdout(buf):
        exec(code, g)
finally:
    os.chdir(old_cwd)
out = buf.getvalue()
print(out)
observed = {}
for name in F.SCAN_KEYS:
    m = re.search(r"%s\s*=\s*(\S+)" % re.escape(name), out)
    observed[name] = m.group(1) if m else None
print("---")
print("observed:", observed)
mismatch = sorted(k for k, v in F.SCAN_KEYS if observed.get(k) != "True") if False else \
    sorted(k for k in F.SCAN_KEYS if observed.get(k) != "True")
print("mismatch:", mismatch)
if mismatch:
    print("SELF1 FAIL: scanner keys not all True offline")
    raise SystemExit(1)
print("SELF1 PASS: all scanner keys True against real sources (local dry-run)")
