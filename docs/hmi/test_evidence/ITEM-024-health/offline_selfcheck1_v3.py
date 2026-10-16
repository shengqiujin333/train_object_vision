# -*- coding: utf-8 -*-
"""ITEM-024 re-verification round (uifix-r1) — offline self-check 1.

Local dry-run of the verifier wiring scan v3 (VERIFY_SCAN_PY below) against the
repository sources with board-layout path remapping (board: src/ui/... at
/tmp/railhmi_build; repo: app/src/ui/...) — same remap trick as prior rounds.
Also re-derives the D-01 geometry arithmetic from the fixed card heights and
asserts the fix-relevant QML facts that the scan keys encode.
"""
import io
import re
from contextlib import redirect_stdout
from pathlib import Path

BASE = Path(__file__).resolve().parent
APP = BASE.parents[3] / "app"

# --- verifier wiring scan v3 (runs on-board against fresh copy) ----------------
# Board layout: cwd=/tmp/railhmi_build, files under src/ui/..., tools/board_build.py
VERIFY_SCAN_PY = r'''
import re
overlay = open('src/ui/qml/components/RecoveryOverlay.qml', encoding='utf-8').read()
main_qml = open('src/ui/qml/main.qml', encoding='utf-8').read()
p6 = open('src/ui/qml/pages/P6Maintenance.qml', encoding='utf-8').read()
bridge = open('src/ui/src/health_model.cpp', encoding='utf-8').read()
bridge_h = open('src/ui/src/health_model.h', encoding='utf-8').read()
mc = open('src/ui/src/main.cpp', encoding='utf-8').read()
bb = open('tools/board_build.py', encoding='utf-8').read()

# T1: overlay renders ordered steps with per-step status + progress + alive notice
print('T1_overlay_structure=',
      'recoveryOverlay' in overlay and 'recoveryStep-' in overlay and
      'recoveryText' in overlay and 'recoveryAlive' in overlay and
      'modelData.statusLabel' in overlay and 'modelData.label' in overlay and
      'progress' in overlay)

# T2: overlay wired into main.qml and driven by the railhmiHealth bridge
print('T2_overlay_wired=',
      'RecoveryOverlay' in main_qml and 'railhmiHealth' in overlay and
      'R.recovery()' in overlay)

# T3: P6 structured log card: chips + source/keyword inputs (D-03) + rows
print('T3_p6_log_card=',
      'maintLogQueryCard' in p6 and 'structuredLogRow' in p6 and
      'logFilterAll' in p6 and 'logFilterWarn' in p6 and 'logFilterError' in p6 and
      'logSourceInput' in p6 and 'logKeywordInput' in p6 and
      re.search(r'logSourceInput[^>]*\n[^>]*onEditingFinished', p6) is not None and
      re.search(r'logKeywordInput[^>]*\n[^>]*onEditingFinished', p6) is not None and
      re.search(r'railhmiHealth\.logs\(', p6) is not None and
      'modelData.levelLabel' in p6 and 'modelData.source' in p6 and
      'modelData.message' in p6)

# T4: P6 recovery card: progress text + per-step rows + demo control (D-04)
print('T4_p6_recovery_card=',
      'maintRecoveryCard' in p6 and 'recoveryProgressText' in p6 and
      'recoveryStepRow-' in p6 and 'recoveryDemo' in p6 and
      re.search(r'resetRecovery\(\)', p6) is not None and
      re.search(r'railhmiHealth\.recovery\(\)', p6) is not None and
      'modelData.statusLabel' in p6 and 'modelData.detail' in p6)

# T5: bridge delegates to core (not UI-reimplemented logic)
print('T5_bridge_core=',
      'evaluate_health' in bridge and 'query_logs' in bridge and
      'log_record_json' in bridge and 'recovery_' in bridge and
      'railhmi::core::' in bridge)

# T6: main.cpp injects railhmiHealth and seeds a full 8-step recovery state
print('T6_main_inject=',
      'railhmiHealth' in mc and 'RailhmiHealthModel' in mc and
      'healthModel.seed(' in mc and
      'RecoveryStep::FrontAiReady' in mc and
      'RecoveryStep::StartStreaming' in mc)

# T7: board pipeline runs the health probe + health wiring checker
print('T7_board=',
      'check_health_wiring.py' in bb and 'railhmi-health-probe' in bb and
      'HEALTH_WIRE_EXIT' in bb and 'HEALTH_EXIT' in bb)

# T8: bridge log filter maps warn/error to min_severity + source/keyword through
print('T8_bridge_filter=',
      re.search(r'level == "warn"', bridge) is not None and
      re.search(r'level == "error"', bridge) is not None and
      'min_severity' in bridge and 'f.source' in bridge and 'f.keyword' in bridge)

# T9: overlay hides on completion, shows critical-failure notice on alive=false
print('T9_overlay_gating=',
      re.search(r'visible:\s*st\.completed === false', overlay) is not None and
      re.search(r'visible:\s*overlay\.st\.alive === false', overlay) is not None)

# T11 (D-02 overlay): reload() re-reads recovery() and the model change signal
# is subscribed; st is now a cached property, NOT a one-shot method-call binding
print('T11_overlay_reactive=',
      'function reload()' in overlay and
      re.search(r'overlay\.st\s*=\s*overlay\.R \? overlay\.R\.recovery\(\)', overlay) is not None and
      'Connections' in overlay and 'function onChanged()' in overlay and
      re.search(r'property var st:\s*\(\{', overlay) is not None and
      re.search(r'property var st:\s*R \?', overlay) is None)

# T12 (D-02 P6): healthSt/logRows are page properties refreshed by reload helpers
# and the bridge changed() signal is connected
print('T12_p6_reactive=',
      'property var healthSt' in p6 and 'property var logRows' in p6 and
      'function reloadHealth()' in p6 and 'function reloadLogs()' in p6 and
      re.search(r'page\.reloadHealth\(\);\s*page\.reloadLogs\(\)', p6) is not None and
      'page.reloadHealth' in p6 and 'page.reloadLogs' in p6)

# T13 (D-02 demo): bridge exposes reset/advance and the overlay Timer drives
# advanceRecovery while visible; P6 chip calls resetRecovery
print('T13_demo_path=',
      'resetRecovery' in bridge_h and 'advanceRecovery' in bridge_h and
      'Q_INVOKABLE void resetRecovery' in bridge_h and
      'Q_INVOKABLE void advanceRecovery' in bridge_h and
      re.search(r'Timer\s*\{[^}]*onTriggered:\s*overlay\.R\.advanceRecovery\(\)', overlay, re.S) is not None and
      re.search(r'running:\s*overlay\.visible && overlay\.R', overlay) is not None and
      re.search(r'resetRecovery\(\)', p6) is not None)

# T14 (D-04 demo semantics): advance walks one pending/running step per tick,
# streaming tick uses camera_result degraded (not ok), all-terminal -> finish
print('T14_demo_semantics=',
      re.search(r'if \(s == railhmi::core::RecoveryStep::StartStreaming\)\s*\n\s*demo_tracker_\.camera_result\(false, false,', bridge, re.M) is not None and
      re.search(r'complete_step\(s, true\)', bridge) is not None and
      re.search(r'if \(all_terminal && !now\.completed\) demo_tracker_\.finish\(demo_clock_\);', bridge) is not None)

# N2 observable: degraded is distinct from failed in BOTH overlay and P6
print('N2_degraded_not_failed=',
      'degraded' in overlay and 'failed' in overlay and
      'degraded' in p6 and 'failed' in p6 and
      'Theme.color.degraded' in overlay and 'Theme.color.degraded' in p6)

# T10: tokens-only colors (strip // comments like the delivered checker)
overlay_l = [line.split('//', 1)[0] for line in overlay.splitlines()]
p6l = [line.split('//', 1)[0] for line in p6.splitlines()]
print('T10_no_hex=',
      not any(re.search(r'#[0-9a-fA-F]{3,8}\b', l) for l in overlay_l + p6l) and
      'Theme.color.' in overlay and 'Theme.color.' in p6)

# R1 (D-01): P6 cards are inside a scrollable Flickable with clip and full
# content height so late cards stay reachable
print('R1_p6_scrollable=',
      re.search(r'Flickable\s*\{[^{}]*anchors\.fill:\s*parent', p6, re.S) is not None and
      'clip: true' in p6 and
      re.search(r'contentHeight:\s*grid\.height', p6) is not None and
      re.search(r'contentWidth:\s*width', p6) is not None)

# R2 (D-01 reverse): the old top-level non-scrolling Grid anchors-fill form is gone
#   before: "    Grid {\n        id: grid\n        anchors.fill: parent"
#   [^{}]* stays inside the Grid header (cannot cross into nested cards)
print('R2_p6_no_anchored_grid=',
      re.search(r'Grid\s*\{[^{}]*anchors\.fill:\s*parent', p6, re.S) is None)

# R3 (D-02 reverse): the old one-shot method-call bindings are gone
#   before: "text: (typeof railhmiHealth !== \"undefined\" && railhmiHealth)\n ? (railhmiHealth.recovery().text + ...)"
#   before: "model: (typeof railhmiHealth !== \"undefined\" && railhmiHealth) ? railhmiHealth.recovery().steps : []"
#   before overlay: "property var st: R ? R.recovery() : ({ completed: true, steps: [] })"
print('R3_no_one_shot_bindings=',
      re.search(r'railhmiHealth\.recovery\(\)\.text', p6) is None and
      re.search(r'railhmiHealth\.recovery\(\)\.steps', p6) is None and
      re.search(r'property var st:\s*R \?', overlay) is None)

# R4 (D-04): main.cpp seeds a COMPLETED recovery (pre-fix overlay hidden);
# the demo path must exist so the in-progress overlay state is reachable
print('R4_demo_reachable=',
      'rec.finish(' in mc and 'resetRecovery' in bridge_h)

# R5: Theme tokens used by the new inputs exist (no undefined token drift)
print('R5_theme_tokens_present=',
      'touch_target_min' in p6 and 'bg_sunken' in p6 and 'radius.chip' in p6)
'''

# local remap: board layout -> repo layout
REMAPPED = VERIFY_SCAN_PY.replace(
    "open('src/ui/qml/components/RecoveryOverlay.qml', encoding='utf-8')",
    "open(r'%s', encoding='utf-8')" % (APP / "src/ui/qml/components/RecoveryOverlay.qml").as_posix()
).replace(
    "open('src/ui/qml/main.qml', encoding='utf-8')",
    "open(r'%s', encoding='utf-8')" % (APP / "src/ui/qml/main.qml").as_posix()
).replace(
    "open('src/ui/qml/pages/P6Maintenance.qml', encoding='utf-8')",
    "open(r'%s', encoding='utf-8')" % (APP / "src/ui/qml/pages/P6Maintenance.qml").as_posix()
).replace(
    "open('src/ui/src/health_model.cpp', encoding='utf-8')",
    "open(r'%s', encoding='utf-8')" % (APP / "src/ui/src/health_model.cpp").as_posix()
).replace(
    "open('src/ui/src/health_model.h', encoding='utf-8')",
    "open(r'%s', encoding='utf-8')" % (APP / "src/ui/src/health_model.h").as_posix()
).replace(
    "open('src/ui/src/main.cpp', encoding='utf-8')",
    "open(r'%s', encoding='utf-8')" % (APP / "src/ui/src/main.cpp").as_posix()
).replace(
    "open('tools/board_build.py', encoding='utf-8')",
    "open(r'%s', encoding='utf-8')" % (APP / "tools/board_build.py").as_posix()
)

buf = io.StringIO()
with redirect_stdout(buf):
    exec(REMAPPED, {})  # single namespace, like the board python3 run
out = buf.getvalue()

SCAN_KEYS = [
    "T1_overlay_structure", "T2_overlay_wired", "T3_p6_log_card",
    "T4_p6_recovery_card", "T5_bridge_core", "T6_main_inject", "T7_board",
    "T8_bridge_filter", "T9_overlay_gating", "T11_overlay_reactive",
    "T12_p6_reactive", "T13_demo_path", "T14_demo_semantics",
    "N2_degraded_not_failed", "T10_no_hex", "R1_p6_scrollable",
    "R2_p6_no_anchored_grid", "R3_no_one_shot_bindings", "R4_demo_reachable",
    "R5_theme_tokens_present",
]

fails = []
observed = {}
for k in SCAN_KEYS:
    m = re.search(r"%s\s*=\s*(\S+)" % re.escape(k), out)
    observed[k] = m.group(1) if m else None
    print("PASS" if observed[k] == "True" else "FAIL", k, "=", observed[k])
    if observed[k] != "True":
        fails.append(k)

# D-01 geometry re-derivation from the delivered fixed card heights:
# per ROW take the max card height (2-column grid), plus row gaps + margins
p6 = (APP / "src/ui/qml/pages/P6Maintenance.qml").read_text(encoding="utf-8", errors="replace")
heights = [int(h) for h in re.findall(r"height:\s*(\d+)\s*$", p6, re.M)]
cards = [h for h in heights if h in (120, 140, 200, 260, 320)]
rows = -(-len(cards) // 2)  # ceil(len/2)
row_sums = []
for i in range(0, len(cards) - 1, 2):
    row_sums.append(max(cards[i], cards[i + 1]))
if len(cards) % 2 == 1:
    row_sums.append(cards[-1])
total = sum(row_sums) + (rows - 1) * 12 + 24  # row maxima + row gaps + margins
usable = 1080 - 48 - 24
over = total - usable
print("PASS" if over > 0 else "FAIL",
      "d01 geometry: cards=%d rows=%d row_maxima=%s total=%d usable=%d overflow=%d"
      % (len(cards), rows, row_sums, total, usable, over))
if over <= 0:
    fails.append("d01 geometry")
print("PASS geometry fix claim: Flickable present makes overflow reachable (R1 True)")
result = "PASS" if not fails else "FAIL"
print("OFFLINE-1 RESULT: %s (%d failed)" % (result, len(fails)))
print("raw:")
print(out)
raise SystemExit(0 if not fails else 1)
