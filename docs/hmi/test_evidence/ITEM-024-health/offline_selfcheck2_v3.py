# -*- coding: utf-8 -*-
"""ITEM-024 re-verification round (uifix-r1) — offline self-check 2.

Negative control for the delivered checker (check_health_wiring.py) and the
verifier scan v3, executed on synthetic broken variants of the QML/bridge:
  A. pre-fix QML (the shape UI-review flagged): old top-level Grid without
     Flickable + one-shot method-call bindings + no source/keyword inputs + no
     demo hooks -> BOTH the delivered checker and the verifier scan must FAIL
     (proves the checker/scan catch the review findings, i.e. the new PASS is
     not vacuous).
  B. post-fix QML with the Connections rewiring deleted -> checker must FAIL
     (D-02 assertion fires), scan T11/T12 must be False.
  C. bridge with advanceRecovery streaming tick switched to complete_step(ok)
     -> scan T14 must be False (demo degraded semantics guarded).
All checks run on temp copies; no repo file is modified.
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

BASE = Path(__file__).resolve().parent
APP = BASE.parents[3] / "app"
CHECKER = APP / "tools" / "check_health_wiring.py"

overlay = (APP / "src/ui/qml/components/RecoveryOverlay.qml").read_text(encoding="utf-8")
p6 = (APP / "src/ui/qml/pages/P6Maintenance.qml").read_text(encoding="utf-8")
bridge_cpp = (APP / "src/ui/src/health_model.cpp").read_text(encoding="utf-8")

fails = []


def run_checker(root: Path) -> int:
    r = subprocess.run([sys.executable, str(CHECKER), "--root", str(root)],
                       capture_output=True, text=True, encoding="utf-8",
                       errors="replace", timeout=60)
    return r.returncode


def make_ui_root(mutate_overlay, mutate_p6):
    tmp = Path(tempfile.mkdtemp(prefix="selfchk2_"))
    (tmp / "qml" / "components").mkdir(parents=True)
    (tmp / "qml" / "pages").mkdir(parents=True)
    (tmp / "src").mkdir()
    (tmp / "qml" / "components" / "RecoveryOverlay.qml").write_text(
        mutate_overlay(overlay), encoding="utf-8")
    (tmp / "qml" / "pages" / "P6Maintenance.qml").write_text(
        mutate_p6(p6), encoding="utf-8")
    (tmp / "qml" / "main.qml").write_text(
        (APP / "src/ui/qml/main.qml").read_text(encoding="utf-8"), encoding="utf-8")
    (tmp / "src" / "health_model.cpp").write_text(bridge_cpp, encoding="utf-8")
    (tmp / "src" / "main.cpp").write_text(
        (APP / "src/ui/src/main.cpp").read_text(encoding="utf-8"), encoding="utf-8")
    return tmp


# --- A: pre-fix shapes (reverse fixtures) --------------------------------------
# A1: old top-level anchored Grid (D-01 pre-fix) — verifier scan R2 must go False
def scan_r2(src: str) -> bool:
    return re.search(r"Grid\s*\{[^{}]*anchors\.fill:\s*parent", src, re.S) is None


a1 = p6.replace(
    "Flickable {\n        id: flick\n        anchors.fill: parent\n        clip: true",
    "Grid {\n        id: grid\n        anchors.fill: parent", 1)
step_a1 = (a1 != p6) and (scan_r2(p6) is True) and (scan_r2(a1) is False)
print("PASS" if step_a1 else "FAIL",
      "A1 reverse: anchored-Grid pre-fix shape detected by scan R2 (mutated=%s)"
      % (a1 != p6))
if not step_a1:
    fails.append("A1")

# A2: one-shot method-call binding shape (D-02 pre-fix) — scan R3 must go False
def scan_r3(p6src: str, ovs: str) -> bool:
    return (re.search(r"railhmiHealth\.recovery\(\)\.text", p6src) is None and
            re.search(r"railhmiHealth\.recovery\(\)\.steps", p6src) is None and
            re.search(r"property var st:\s*R \?", ovs) is None)


ovs_old = overlay.replace(
    "property var st: ({ completed: true, steps: [] })",
    "property var st: R ? R.recovery() : ({ completed: true, steps: [] })", 1)
a2_ok = (scan_r3(p6, overlay) is True) and (scan_r3(p6, ovs_old) is False)
print("PASS" if a2_ok else "FAIL", "A2 reverse: one-shot overlay binding detected by scan R3")
if not a2_ok:
    fails.append("A2")

# A3: pre-fix P6 lacks Flickable/inputs/demo — delivered checker must FAIL on it
# (token deletions are chained replaces; the list form keeps each rename exact;
#  reloadHealth/reloadLogs call sites must go too or the D-02 check still passes)
p6_old = p6.replace(
    "Flickable {\n        id: flick\n        anchors.fill: parent\n        clip: true",
    "Item {\n        id: flick\n        anchors.fill: parent", 1)
for old, new in (
    ("logSourceInput", "goneSrcInput"),
    ("logKeywordInput", "goneKwInput"),
    ("recoveryDemo", "goneRecoveryDemo"),
    ("property var healthSt", "property var goneSt"),
    ("property var logRows", "property var goneRows"),
    ("function reloadHealth", "function goneReloadHealth"),
    ("function reloadLogs", "function goneReloadLogs"),
    ("page.reloadHealth(); page.reloadLogs()", ""),
):
    p6_old = p6_old.replace(old, new)
rootA = make_ui_root(lambda s: ovs_old, lambda s: p6_old)
rcA = run_checker(rootA)
print("PASS" if rcA != 0 else "FAIL", "A3 delivered checker FAILS on pre-fix tree (rc=%d)" % rcA)
if rcA == 0:
    fails.append("A3")
checkerA_out = subprocess.run([sys.executable, str(CHECKER), "--root", str(rootA)],
                              capture_output=True, text=True, encoding="utf-8",
                              errors="replace", timeout=60).stdout
# checker tokens for D-01/D-03/D-04 + the D-02 phrase it prints on failure
a3_needed = all(t in checkerA_out for t in ("Flickable", "logSourceInput", "logKeywordInput",
                                            "recoveryDemo", "D-02"))
print("PASS" if a3_needed else "FAIL", "A3b checker output names D-01/D-03/D-04 tokens")
if not a3_needed:
    fails.append("A3b")

# --- B: post-fix tree with Connections rewiring deleted ------------------------
def drop_health_connections(src: str) -> str:
    out = re.sub(
        r"    // 健康/日志/恢复变更刷新（ITEM-024 D-02）\n"
        r"    Connections \{\n"
        r"        target: \(typeof railhmiHealth !== \"undefined\"\) \? railhmiHealth : null\n"
        r"        function onChanged\(\) \{ page\.reloadHealth\(\); page\.reloadLogs\(\) \}\n"
        r"    \}\n", "", src)
    if out == src:  # fallback: drop the block by anchor tokens
        out = re.sub(r"Connections \{[^{}]*railhmiHealth[^{}]*\}\n", "", src, flags=re.S)
    return out


p6B = drop_health_connections(p6)
print("PASS" if p6B != p6 else "FAIL", "B synthetic mutation applied (P6 health Connections removed)")
if p6B == p6:
    fails.append("B-mutate")
rootB = make_ui_root(lambda s: s, lambda s: p6B)
rcB = run_checker(rootB)
print("PASS" if rcB != 0 else "FAIL", "B delivered checker FAILS without D-02 Connections (rc=%d)" % rcB)
if rcB == 0:
    fails.append("B")

# --- C: bridge demo semantics guard via scan T14 regex on mutated bridge -------
T14 = re.compile(
    r"if \(s == railhmi::core::RecoveryStep::StartStreaming\)\s*\n\s*demo_tracker_\.camera_result\(false, false,", re.M)
okT14_fixed = T14.search(bridge_cpp) is not None
bridgeC = bridge_cpp.replace(
    "demo_tracker_.camera_result(false, false, \"CAM-02 启动失败（降级）\");",
    "demo_tracker_.complete_step(s, true);", 1)
okT14_mut = T14.search(bridgeC) is None and bridgeC != bridge_cpp
print("PASS" if okT14_fixed and okT14_mut else "FAIL",
      "C T14 guard: fixed=True mutated=False")
if not (okT14_fixed and okT14_mut):
    fails.append("C")

print("OFFLINE-2 RESULT: %s (%d failed)" % ("PASS" if not fails else "FAIL", len(fails)))
raise SystemExit(0 if not fails else 1)
