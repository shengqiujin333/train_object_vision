# -*- coding: utf-8 -*-
"""ITEM-024 offline self-check 1: wiring scan dry-run on the LOCAL repo copy.

Extracts the SCAN_PY body from verify_health24_fixtures.py, remaps open()
paths to the local repo layout (app/src/... + app/tools/...), executes it in
a single namespace with redirected stdout, and prints the observed key=values.
Catches scan-pattern drift BEFORE spending a board round. Zero board access.
"""
import io
import re
import sys
from contextlib import redirect_stdout
from pathlib import Path

HERE = Path(__file__).resolve().parent
SRC = HERE / "verify_health24_fixtures.py"


def main() -> int:
    text = SRC.read_text(encoding="utf-8")
    m = re.search(r"SCAN_PY = r'''(.*?)'''", text, re.S)
    if not m:
        print("no SCAN_PY found")
        return 2
    body = m.group(1)

    # remap board paths to local repo layout: rewrite open('<rel>' with the
    # local absolute path, leaving the rest of the call untouched
    app_root = HERE.parents[3] / "app"
    remap = {
        "src/ui/qml/components/RecoveryOverlay.qml": app_root / "src/ui/qml/components/RecoveryOverlay.qml",
        "src/ui/qml/main.qml": app_root / "src/ui/qml/main.qml",
        "src/ui/qml/pages/P6Maintenance.qml": app_root / "src/ui/qml/pages/P6Maintenance.qml",
        "src/ui/src/health_model.cpp": app_root / "src/ui/src/health_model.cpp",
        "src/ui/src/health_model.h": app_root / "src/ui/src/health_model.h",
        "src/ui/src/main.cpp": app_root / "src/ui/src/main.cpp",
        "tools/board_build.py": app_root / "tools/board_build.py",
    }
    for rel, local in remap.items():
        if not local.is_file():
            print("missing local file: %s" % local)
            return 2
        body = body.replace("open('%s'" % rel,
                            "open(r'%s'" % str(local).replace("\\", "/"))

    ns = {}
    exec(compile(body, "scan24", "exec"), ns)  # noqa: S102 - verifier-owned scan
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
