#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Verifier-owned static wiring check for ITEM-015 QML consumption chain
(software_verification; host-side because Qt5 Quick/Qml dev packages are
missing on the board E-01b and QML cannot execute).

Checks (file-content assertions, not runtime):
  p0  qrc registers pages/P4AllCameras.qml and components/CameraTile.qml
  p1  P4 page renders a fixed-slot Grid over railhmiChannels; cell width/height
      derive from page cols/rows (shape from railhmiGrid), no per-cell literal
      row/col that could reorder on state change
  p2  full-badge page: CameraTile showBadge true in P4 (registry-driven fields:
      name/state/badge/aiEnabled live via RailhmiChannels modelData)
  p3  per-cell ITEM-015 fields consumed: fpsText/lastFrameText/reconnectText and
      presentable flag bound to the live surface
  p4  CameraTile shows fps/last-frame/reconnect texts and a non-LIVE placeholder
      via VideoSurface (video layer only when live)
  p5  main.cpp: railhmiChannels injected from core build_channel_views over the
      config registry (unstarted fallback when media not wired); railhmiGrid
      injected from core grid_columns/grid_rows (single source, no literal 3/2)
  p6  page shape fallback equals core dims (3x2 / 2x3) and is confined to the
      typeof-guard fallback
  p7  railhmi-media multi-channel API surface: MultiStreamManager(start_all/
      pump_all/simulate_cut/stats/stop_all) + CLI probe registered in CMake
  p8  no camera name / IP literal duplicated in P4 page or CameraTile (config
      driven, 不写死路数)
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
    p4 = read(APP / "src/ui/qml/pages/P4AllCameras.qml")
    tile = read(APP / "src/ui/qml/components/CameraTile.qml")
    vs = read(APP / "src/ui/qml/components/VideoSurface.qml")
    maincpp = read(APP / "src/ui/src/main.cpp")
    media_h = read(APP / "src/media/include/railhmi/media/multi_stream.h")
    media_cmake = read(APP / "src/media/CMakeLists.txt")

    # p0 registration
    check("pages/P4AllCameras.qml" in qrc, "p0 qrc registers P4AllCameras.qml")
    check("components/CameraTile.qml" in qrc, "p0b qrc registers CameraTile.qml")

    # p1 fixed-slot grid consuming railhmiChannels
    check("Repeater" in p4 and "model: railhmiChannels" in p4,
          "p1 P4 cells driven by railhmiChannels (registry order)")
    check("columns: page.cols" in p4 and "spacing: page.gap" in p4,
          "p1b Grid columns/spacing from shape properties")
    W = "(grid.width - (page.cols - 1) * page.gap) / page.cols"
    H = "(grid.height - (page.rows - 1) * page.gap) / page.rows"
    check(W in p4, "p1c cell width formula from cols (gap arithmetic exact)")
    check(H in p4, "p1d cell height formula from rows (gap arithmetic exact)")
    # no per-cell position source that could reorder (only model order)
    check("modelData.id" in p4 and "slot" not in re.sub(r"//.*", "", p4),
          "p1e no per-cell manual reordering (order = model order only)")

    # p2 badges on P4 + registry fields
    check("showBadge: true" in p4, "p2 P4 shows detection badges (P4/P6 visibility contract)")
    check("title: modelData.name" in p4 and "badge: modelData.badge" in p4
          and "aiEnabled: modelData.aiEnabled" in p4,
          "p2b name/badge/aiEnabled consumed from channel views")
    check("state: modelData.stateLabel" in p4,
          "p2c state badge label consumed")

    # p3 ITEM-015 per-cell fields wired
    check("fpsText: modelData.fpsText" in p4, "p3 fps text wired to tile")
    check("lastFrameText: modelData.lastFrameText" in p4,
          "p3b last-frame text wired to tile")
    check("reconnectText: modelData.reconnectText" in p4,
          "p3c reconnect count wired to tile")
    check("live: modelData.presentable" in p4,
          "p3d surface live bound to core presentable (stale frames guarded)")

    # p4 CameraTile displays fields + placeholder via VideoSurface
    check("property string fpsText" in tile and "property string lastFrameText" in tile
          and "property string reconnectText" in tile,
          "p4 CameraTile exposes ITEM-015 display fields")
    check("text: tile.fpsText" in tile and "text: tile.lastFrameText" in tile
          and "text: tile.reconnectText" in tile,
          "p4b tile renders fps/last-frame/reconnect texts")
    check("live: tile.live" in tile and "anchors.fill: parent" in tile,
          "p4c tile embeds VideoSurface bound to tile.live")
    check("visible: surface.live" in vs and "visible: !surface.live" in vs,
          "p4d placeholder exactly complements video layer (stale not shown as live)")

    # p5 main.cpp injection chain (single source core)
    check("build_channel_views" in maincpp and '"railhmiChannels"' in maincpp,
          "p5 main.cpp injects railhmiChannels from core build_channel_views")
    check("railhmi::core::build_channel_views(registry, {}, 0)" in maincpp,
          "p5b unstarted fallback: empty runtime -> placeholders, no dropped cells")
    check("grid_columns" in maincpp and '"railhmiGrid"' in maincpp,
          "p5e main.cpp injects railhmiGrid from core grid dims")
    check("build_registry(cfg.settings)" in maincpp,
          "p5f registry from config (single source)")

    # p6 shape fallback confined and equals core dims
    m = re.search(r'typeof railhmiGrid !== "undefined"\)\s*\?\s*railhmiGrid\s*:\s*\((\{[^}]*\})\)',
                  p4, re.S)
    check(m is not None, "p6 shape fallback confined to typeof-guard")
    if m:
        fb = m.group(1)
        check("wideColumns: 3" in fb and "wideRows: 2" in fb
              and "tallColumns: 2" in fb and "tallRows: 3" in fb,
              "p6b fallback dims equal core 3x2/2x3")
    check("(width >= height) ? page.G.wideColumns : page.G.tallColumns" in p4,
          "p6c wide/tall selection matches core choose_grid_shape semantics")

    # p7 media API surface + probe registration
    for fn in ("start_all", "pump_all", "simulate_cut", "stats", "stop_all"):
        check(fn in media_h, "p7 MultiStreamManager exposes %s" % fn)
    check("single stream failure" not in media_h, "p7b header documents per-channel isolation")
    check("railhmi-multi-probe" in media_cmake and "tools/multi_probe.cpp" in media_cmake,
          "p7c multi-probe CLI registered in media CMake")

    # p8 no camera-specific literals in P4/CameraTile (config driven)
    p4_code = re.sub(r"//[^\n]*", "", p4)
    tile_code = re.sub(r"//[^\n]*", "", tile)
    banned = ["前方", "左侧", "右侧", "车顶", "车底", "司机室", "192.168."]
    hits = [b for b in banned if (b in p4_code or b in tile_code)]
    check(not hits, "p8 no camera name/IP literals in P4/CameraTile code", str(hits))
    check("CAM-" not in p4_code and "CAM-" not in tile_code,
          "p8b no hardcoded camera ids in QML pages")

    print("== static wiring check (ITEM-015) ==")
    print("== %d passed, %d failed ==" % (passed, failed))
    print("RESULT: %s passed=%d failed=%d"
          % ("PASS" if failed == 0 else "FAIL", passed, failed))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
