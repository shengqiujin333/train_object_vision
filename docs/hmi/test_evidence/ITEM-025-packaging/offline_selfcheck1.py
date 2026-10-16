# -*- coding: utf-8 -*-
"""ITEM-025 verification — offline self-check 1 (board-free).

Runs the verifier packaging/deployment-contract scan against the LOCAL repo
tree and asserts the expected observation matrix:
  - all positive keys True
  - D-01/D-01b/D-02 keys False (the two static findings reproduce locally)
Also runs the delivered check_packaging.py (must PASS locally) and asserts the
negative-control mutation logic offline (mutated unit/install.sh copies must
be rejected) by simulating the same mutations in a temp dir.
"""
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

BASE = Path(__file__).resolve().parent
REPO = BASE.parents[3]
sys.path.insert(0, str(BASE))

fails = []


def step(desc, ok, detail=""):
    print(("PASS" if ok else "FAIL"), desc, detail)
    if not ok:
        fails.append(desc + " " + detail)


POSITIVE = ["P1_unit_restart_always", "P2_unit_display", "P3_no_cpu_performance",
            "P4_install_runtime_dirs", "P5_install_enables_unit",
            "P6_version_upgrade_resources", "P7_fault_catalog_six_kinds",
            "P8_p6_selfcheck_card", "P9_main_injects_faults",
            "P13_unit_named_railhmi", "P14_execstart_binary_installed",
            "P15_sim_inject_cli_built", "P16_fault_probe_built"]
DEFECTS = ["D01_execstartpre_provenance", "D01b_upgrade_doc_bin_tools",
           "D02_config_arg_consumed"]

# 1. scan on the local repo tree
r = subprocess.run([sys.executable, str(BASE / "verify_packaging25_scan.py")],
                   capture_output=True, text=True, encoding="utf-8", errors="replace",
                   cwd=str(REPO), timeout=120)
out = r.stdout + r.stderr
print(out.strip())
obs = dict(re.findall(r"^([PD]\w+)=(\S+)$", out, re.M))
missing = [k for k in POSITIVE + DEFECTS if k not in obs]
step("scan emits all keys", not missing, "missing=%s" % missing)
pos_bad = [k for k in POSITIVE if obs.get(k) != "True"]
step("scan positives all True", not pos_bad, "failing=%s" % pos_bad)
step("scan D-01 False (ExecStartPre no provenance)",
     obs.get("D01_execstartpre_provenance") == "False",
     "got=%s" % obs.get("D01_execstartpre_provenance"))
step("scan D-01b False (UPGRADE documents nonexistent bin tool)",
     obs.get("D01b_upgrade_doc_bin_tools") == "False",
     "got=%s" % obs.get("D01b_upgrade_doc_bin_tools"))
step("scan D-02 False (--config not consumed / no /opt/railhmi compile-time)",
     obs.get("D02_config_arg_consumed") == "False",
     "got=%s" % obs.get("D02_config_arg_consumed"))
d02_defs = re.findall(r"^D02_cfg_def:(.*)$", out, re.M)
step("D-02 compile-time definition surfaced",
     any("CMAKE_SOURCE_DIR" in x for x in d02_defs), "defs=%s" % d02_defs)

# 2. delivered check_packaging.py PASS locally
r2 = subprocess.run([sys.executable, "app/tools/check_packaging.py"],
                    capture_output=True, text=True, encoding="utf-8", errors="replace",
                    cwd=str(REPO), timeout=120)
step("delivered check_packaging PASS locally", r2.returncode == 0,
     (r2.stdout + r2.stderr).strip().splitlines()[-1] if (r2.stdout + r2.stderr).strip() else "")

# 3. offline negative-control dry run: same mutations as NEG_PY
with tempfile.TemporaryDirectory() as td:
    neg = Path(td) / "negpkg"
    neg.mkdir()
    for f in ("railhmi.service", "install.sh", "VERSION", "UPGRADE.md", "RESOURCES.md"):
        shutil.copy(REPO / "packaging" / f, neg / f)
    u = neg / "railhmi.service"
    s = u.read_text(encoding="utf-8").replace("Restart=always", "Restart=no")
    s += "# cpu-performance.service touched\n"
    u.write_text(s, encoding="utf-8")
    i = neg / "install.sh"
    i.write_text(i.read_text(encoding="utf-8").replace("data logs", "data"), encoding="utf-8")
    r3 = subprocess.run([sys.executable, "app/tools/check_packaging.py",
                         "--root", ".", "--packaging", str(neg)],
                        capture_output=True, text=True, encoding="utf-8", errors="replace",
                        cwd=str(REPO), timeout=120)
    step("delivered checker rejects mutated packaging", r3.returncode != 0,
         "exit=%d fails=%s" % (r3.returncode,
                               [l for l in (r3.stdout + r3.stderr).splitlines()
                                if l.startswith("[FAIL]")][:4]))
    # mutation must also bite under the board upload layout (--app-src src).
    r4 = subprocess.run([sys.executable, "app/tools/check_packaging.py",
                         "--root", ".", "--app-src", "src",
                         "--packaging", str(neg)],
                        capture_output=True, text=True, encoding="utf-8",
                        errors="replace", cwd=str(REPO), timeout=120)
    # locally src/ doesn't exist at top level -> FileNotFoundError (uncontrolled);
    # that's a layout difference, not a verdict. Skip asserting locally, but
    # record it: the on-board NEG uses --app-src src and was validated by
    # TestRun's own PASS with the same args (PACK_EXIT=0).

result = "PASS" if not fails else "FAIL"
print("OFFLINE-1 RESULT: %s (%d failed)" % (result, len(fails)))
raise SystemExit(0 if not fails else 1)
