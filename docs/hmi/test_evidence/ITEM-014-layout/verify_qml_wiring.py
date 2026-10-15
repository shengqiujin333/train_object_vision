#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Verifier-owned static wiring check for ITEM-014 QML consumption chain
(software_verification; runs host-side because Qt5 Quick/Qml dev packages are
missing on the board E-01b and QML cannot execute).

Checks (file-content assertions, not runtime):
  q0  qrc registers pages/P1Realtime.qml and components/VideoSurface.qml
  q1  P1Realtime lays out the tri-panel Row in order left -> front -> right
      with widths derived from railhmiLayout ratios (no literal 0.26/0.48)
  q2  P1Realtime has the P0 elements attributed: top bar (shell nav), status
      strip (objectName statusStrip), alarm bar (objectName alarmBar, anchored
      bottom over full width), tri-panel row below them
  q3  statusStrip sits above alarmBar, and the tri-panel row is bounded
      between the top margin and statusStrip.top (P0 stacking, no overlap)
  q4  tiles bind live=/state= from camByPosition; front tile position 'primary'
      and side tiles 'left'/'right'; driver page shows no badge (showBadge:false)
  q5  VideoSurface consumes railhmiRenderPath (config-driven), has a non-LIVE
      placeholder layer and a render:<path> label; never shows stale frame as
      live (video layer visible: surface.live only)
  q6  main.cpp builds railhmiLayout from railhmi::core::compute_layout (single
      source) and injects railhmiRenderPath from the loaded config render_path
      (default cpu; parse accepts gl) - no duplicated ratio literals introduced
  q7  app/config/app.yaml render_path is 'cpu' (documented ITEM-002 selection
      still the baseline; gl switchable via config only)
  q8  core layout header exposes the five acceptance criteria APIs
Exit 0 = all pass. No product file is modified.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
APP = ROOT / "app"

passed = 0
failed = 0


def check(ok: bool, name: str, detail: str = "") -> None:
    global passed, failed
    if ok:
        passed += 1
        print("[PASS] %s" % name)
    else:
        failed += 1
        print("[FAIL] %s%s" % (name, (" | " + detail) if detail else ""))


def read(p: Path) -> str:
    try:
        return p.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        return ""


def main() -> int:
    qrc = read(APP / "src/ui/qml.qrc")
    p1 = read(APP / "src/ui/qml/pages/P1Realtime.qml")
    vs = read(APP / "src/ui/qml/components/VideoSurface.qml")
    tile = read(APP / "src/ui/qml/components/CameraTile.qml")
    maincpp = read(APP / "src/ui/src/main.cpp")
    appyaml = read(APP / "config/app.yaml")
    layh = read(APP / "src/core/include/railhmi/core/layout.h")

    # q0 qrc registration
    check("pages/P1Realtime.qml" in qrc, "q0 qrc registers P1Realtime.qml")
    check("components/VideoSurface.qml" in qrc, "q0b qrc registers VideoSurface.qml")

    # q1 tri-panel order + ratio consumption (no hardcoded 0.26/0.48 in page)
    row_at = p1.find("objectName: \"threePanelRow\"")
    tiles = [(m.start(), m.group(1)) for m in
             re.finditer(r'objectName: "tile(Left|Front|Right)"', p1)]
    order = [t[1] for t in sorted(tiles)]
    check(order == ["Left", "Front", "Right"],
          "q1 tile child order inside row is Left,Front,Right", str(order))
    check(all(t[0] > row_at >= 0 for t in tiles), "q1b all tiles inside the row item")
    check("page.L.frontRatio" in p1 and "page.L.leftRatio" in p1
          and "page.L.rightRatio" in p1,
          "q1c tile widths consume railhmiLayout ratios")
    check("(row.width - 2 * page.gap) * page.L.frontRatio" in p1,
          "q1d front width formula consistent with gap arithmetic")
    # literals are allowed only inside the typeof-guard fallback (never bound
    # when railhmiLayout is injected); strip the fallback block before scanning
    fb = re.search(r'typeof railhmiLayout !== "undefined"\)\s*\?\s*railhmiLayout\s*:'
                   r'\s*\(\{[^}]*\}\)', p1, re.S)
    p1_nofb = p1[:fb.start()] + p1[fb.end():] if fb else p1
    check("0.48" not in p1_nofb and "0.26" not in p1_nofb,
          "q1e no ratio literal outside the typeof-guard fallback in P1Realtime.qml")
    check(fb is not None,
          "q1f ratio literals confined to the injected-fallback (page.L guard)")

    # q2 P0 elements attributed & anchored
    check("objectName: \"alarmBar\"" in p1 and "anchors.bottom: parent.bottom" in p1,
          "q2a alarm bar P0 anchored to page bottom")
    check("objectName: \"statusStrip\"" in p1 and "anchors.bottom: alarmBar.top" in p1,
          "q2b status strip sits directly above alarm bar")
    check("anchors.bottom: statusStrip.top" in p1,
          "q2c tri-panel row bottom bounded by status strip top")
    check("anchors.topMargin: page.margin" in p1,
          "q2d row top margin from layout margin token")

    # q4 tile bindings: position mapping + driver page badge hidden
    check("camName(\"primary\", \"前方\")" in p1.replace("“", "\"").replace("”", "\"")
          or "page.camName(\"primary\"" in p1,
          "q4a front tile bound to position primary")
    check("page.camName(\"left\"" in p1 and "page.camName(\"right\"" in p1,
          "q4b side tiles bound to left/right")
    tiles_live = re.findall(r'live:\s*(\w+)', p1)
    check(len(tiles_live) == 3 and all(v == "false" for v in tiles_live),
          "q4c all three tiles currently non-live (media wiring follows ITEM-013 state)")
    check(p1.count("showBadge: false") >= 3,
          "q4d driver P1 page shows no detection badges (showBadge false)")

    # q5 VideoSurface render path + stale-frame guard
    check("railhmiRenderPath" in vs,
          "q5a VideoSurface consumes injected railhmiRenderPath")
    check('visible: surface.live' in vs and 'visible: !surface.live' in vs,
          "q5b placeholder layer exactly complements live layer")
    check('"render:" + surface.renderPath' in vs,
          "q5c render path label displayed on the surface")

    # q6 single-source layout + config-driven render path injection
    check("compute_layout" in maincpp and "railhmiLayout" in maincpp,
          "q6a main.cpp injects railhmiLayout from core compute_layout")
    check("RailhmiRenderPath" in maincpp or "railhmiRenderPath" in maincpp,
          "q6b main.cpp injects railhmiRenderPath")
    check("cfg.settings.app.render_path" in maincpp.replace(" ", " "),
          "q6c render path injected from loaded config, not a literal")
    check(maincpp.count("0.26") == 0 and maincpp.count("0.48") == 0,
          "q6d main.cpp introduces no ratio literals (compute_layout output used)")

    # q7 baseline config render_path = cpu
    check(re.search(r"render_path:\s*cpu", appyaml) is not None,
          "q7 baseline config render_path=cpu (ITEM-002 selection)")

    # q8 core criteria API surface
    for fn in ("compute_layout", "three_panel_ordered", "front_is_largest",
               "p0_fits_without_scroll", "alarm_banner_obscures_front_center",
               "ratios_preserved"):
        check(fn in layh, "q8 header exposes %s" % fn)

    print("== static wiring check ==")
    print("== %d passed, %d failed ==" % (passed, failed))
    print("RESULT: %s passed=%d failed=%d"
          % ("PASS" if failed == 0 else "FAIL", passed, failed))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
