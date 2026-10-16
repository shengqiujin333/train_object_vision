# -*- coding: utf-8 -*-
"""ITEM-025 verification — offline self-check 2 (board-free).

Simulates the on-board scan invocation path used by the fixtures driver
(--app src with packaging/ alongside, i.e. the /tmp/railhmi_build upload
layout) against a synthetic copy, and dry-runs the ext fixture compile
arguments against a synthetic tree layout (no compile — just verifies the
files exist locally that will be uploaded).
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


with tempfile.TemporaryDirectory() as td:
    root = Path(td) / "railhmi_build"
    shutil.copytree(REPO / "packaging", root / "packaging")
    shutil.copytree(REPO / "app" / "src", root / "src")
    # board_build.py TestRun uploads design_tokens.yaml + PERMISSION_MATRIX.md
    # at top level too; not needed by the scan.
    scan_src = (BASE / "verify_packaging25_scan.py").read_text(encoding="utf-8")
    (root / "verify_scan25.py").write_text(scan_src, encoding="utf-8")
    r = subprocess.run([sys.executable, "verify_scan25.py",
                        "--app", "src"], capture_output=True, text=True,
                       encoding="utf-8", errors="replace",
                       cwd=str(root), timeout=120)
    out = (r.stdout + r.stderr)
    step("scan runs clean under board layout", r.returncode == 0,
         (r.stdout + r.stderr).strip().splitlines()[-1] if (r.stdout + r.stderr).strip() else "")
    obs = dict(re.findall(r"^([PD]\w+)=(\S+)$", r.stdout, re.M))
    POSITIVE = ["P1_unit_restart_always", "P2_unit_display", "P3_no_cpu_performance",
                "P4_install_runtime_dirs", "P5_install_enables_unit",
                "P6_version_upgrade_resources", "P7_fault_catalog_six_kinds",
                "P8_p6_selfcheck_card", "P9_main_injects_faults",
                "P13_unit_named_railhmi", "P14_execstart_binary_installed",
                "P15_sim_inject_cli_built", "P16_fault_probe_built"]
    step("board-layout scan: positives all True",
         all(obs.get(k) == "True" for k in POSITIVE),
         "failing=%s" % [k for k in POSITIVE if obs.get(k) != "True"])
    step("board-layout scan: D-01/D-01b/D-02 all False",
         obs.get("D01_execstartpre_provenance") == "False"
         and obs.get("D01b_upgrade_doc_bin_tools") == "False"
         and obs.get("D02_config_arg_consumed") == "False",
         "d01=%s d01b=%s d02=%s" % (obs.get("D01_execstartpre_provenance"),
                                    obs.get("D01b_upgrade_doc_bin_tools"),
                                    obs.get("D02_config_arg_consumed")))

# files the driver uploads / compiles must exist locally
for f in ("verify_packaging25_ext.cpp", "verify_packaging25_scan.py"):
    step("driver upload source exists: %s" % f, (BASE / f).is_file())
step("ext include paths match board layout",
     re.search(r"-Isrc/core/include -Isrc/platform/include",
               (BASE / "verify_packaging25_fixtures.py").read_text(encoding="utf-8"),
               ) is not None)
step("ext links fresh static libs",
     "build-core/src/core/librailhmi_core.a" in
     (BASE / "verify_packaging25_fixtures.py").read_text(encoding="utf-8")
     and "build-core/src/platform/librailhmi_platform.a" in
     (BASE / "verify_packaging25_fixtures.py").read_text(encoding="utf-8"))

result = "PASS" if not fails else "FAIL"
print("OFFLINE-2 RESULT: %s (%d failed)" % (result, len(fails)))
raise SystemExit(0 if not fails else 1)
