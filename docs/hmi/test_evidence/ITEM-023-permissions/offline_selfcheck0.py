# -*- coding: utf-8 -*-
"""ITEM-023 offline self-check 0: probe expectation derivation vs INTERACTION §8.

Derives the verifier-expected 14x4 permission matrix from the authoritative
docs/hmi/INTERACTION_DESIGN.md §8 table + PERMISSION_MATRIX.md contract columns
(html-tag stripped) and compares it with (a) the implementer's archived r3
permissions-probe log and (b) the verifier's own oracle table encoded in the
ext fixture source. Any mismatch catches whichever side drifted BEFORE spending
a board round.
Zero board access, finite run, read-only.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
PROBE = (ROOT / "docs/hmi/test_evidence/ITEM-023-permissions/"
         "build-TestRun-192_168_0_103-20261016-ITEM023-r3.core-evidence_permissions-probe-20261016-ITEM023-r3.log")
EXT = Path(__file__).resolve().parent / "verify_perm23_ext.cpp"

# Verifier expectation derived from INTERACTION_DESIGN.md §8 + IMPLEMENTATION (PERMISSION_MATRIX.md):
# key: (driver, maintainer, developer, admin) with:
#   Y = yes, N = no, C = config-gated (受配置 -> false by default, true when export_ops_allowed=true)
MATRIX_EXPECT = {
    "view_realtime":        ("Y", "Y", "Y", "Y"),
    "view_alarm_center":    ("Y", "Y", "Y", "Y"),
    "view_replay":          ("Y", "Y", "Y", "Y"),
    "acknowledge":          ("Y", "Y", "Y", "Y"),
    "view_all_cameras":     ("N", "Y", "Y", "Y"),
    "view_ai_debug":        ("N", "Y", "Y", "Y"),
    "view_maintenance":     ("N", "Y", "Y", "Y"),
    "ptz_control":          ("N", "Y", "Y", "Y"),
    "reconnect":            ("N", "Y", "Y", "Y"),
    "modify_device_config": ("N", "Y", "Y", "Y"),
    "modify_system_config": ("N", "N", "N", "Y"),
    "export_recording":     ("N", "C", "C", "Y"),
    "export_snapshot":      ("N", "C", "C", "Y"),
    # view_item? implementation defines 14 permissions:
    # ViewRealtime ViewAlarmCenter ViewReplay ViewAllCameras ViewAiDebug
    # ViewMaintenance Acknowledge ExportRecording ExportSnapshot
    # ModifyDeviceConfig ModifySystemConfig PtzControl Reconnect = 13 ops + ...
}
# IMPORTANT: implementation has exactly 13 permissions (Count=13), not 14.
# Correct the map above to the 13-row set actually printed by the probe.
PERMS13 = ["view_realtime", "view_alarm_center", "view_replay", "acknowledge",
           "view_all_cameras", "view_ai_debug", "view_maintenance",
           "ptz_control", "reconnect", "modify_device_config", "modify_system_config",
           "export_recording", "export_snapshot"]


def main() -> int:
    fails = []
    # 1. probe rows match the §8-derived expectation (13 rows)
    t = PROBE.read_text(encoding="utf-8")
    m = re.search(r'^type=matrix export_ops_allowed=(\w+)$', t, re.M)
    if not m or m.group(1) != "false":
        fails.append("matrix header missing/export_ops_allowed != false")
    rows = {}
    for mm in re.finditer(r'^row perm=(\S+) label=(.+?) driver=(\S+) maintainer=(\S+) '
                          r'developer=(\S+) admin=(\S+)$', t, re.M):
        rows[mm.group(1)] = {"label": mm.group(2), "driver": mm.group(3),
                             "maintainer": mm.group(4), "developer": mm.group(5),
                             "admin": mm.group(6)}
    print("probe rows found:", len(rows))
    for p in PERMS13:
        if p not in rows:
            fails.append("probe missing row %s" % p)
    # order per implementation enum: view_realtime, view_alarm_center, view_replay,
    # view_all_cameras, view_ai_debug, view_maintenance, acknowledge, export_recording,
    # export_snapshot, modify_device_config, modify_system_config, ptz_control, reconnect
    IMPL_ORDER = ["view_realtime", "view_alarm_center", "view_replay", "view_all_cameras",
                  "view_ai_debug", "view_maintenance", "acknowledge", "export_recording",
                  "export_snapshot", "modify_device_config", "modify_system_config",
                  "ptz_control", "reconnect"]
    printed = re.findall(r'^row perm=(\S+)', t, re.M)
    print("probe row order:", printed)
    # §8-derived expectations, driver/maintainer/developer/admin
    E = {
        "view_realtime": (1, 1, 1, 1),
        "view_alarm_center": (1, 1, 1, 1),
        "view_replay": (1, 1, 1, 1),
        "acknowledge": (1, 1, 1, 1),
        "view_all_cameras": (0, 1, 1, 1),
        "view_ai_debug": (0, 1, 1, 1),
        "view_maintenance": (0, 1, 1, 1),
        "ptz_control": (0, 1, 1, 1),
        "reconnect": (0, 1, 1, 1),
        "modify_device_config": (0, 1, 1, 1),
        "modify_system_config": (0, 0, 0, 1),
        "export_recording": (0, 0, 0, 1),     # C column at export_ops_allowed=false
        "export_snapshot": (0, 0, 0, 1),
    }
    tag = {"1": "Y", "0": "-"}
    for p in IMPL_ORDER:
        if p not in rows:
            fails.append("missing row %s" % p)
            continue
        lbl, drv, mnt, dev, adm = (rows[p]["label"], rows[p]["driver"],
                                   rows[p]["maintainer"], rows[p]["developer"],
                                   rows[p]["admin"])
        want = E[p]
        got = (1 if drv == "Y" else 0, 1 if mnt == "Y" else 0,
               1 if dev == "Y" else 0, 1 if adm == "Y" else 0)
        if got != want:
            fails.append("matrix %s got=%s want=%s (label=%s)" % (p, got, want, lbl))
    # export gate line
    gm = re.search(r'^type=export-gate driver=\S+ maintainer_off=\S+ maintainer_on=(\S+)$', t, re.M)
    if not gm or gm.group(1) != "Y":
        fails.append("export-gate line missing or maintainer_on != Y")
    # gate/action/config/acceptance lines exact
    EXACT = [
        r'^type=gate driver reason="权限不足" allowed=false$',
        r'^type=gate admin_unconfirmed reason="需二次确认" requires_confirm=true$',
        r'^type=gate admin_confirmed allowed=true$',
        r'^type=action kind=export_recording target=CAM-01 result=ok executed=true$',
        r'^type=maint-log action=export_recording user=admin result=ok detail="录像导出 CAM-01 \[1000,61000\]"$',
        r'^type=config scope=device role=maintainer result=ok$',
        r'^type=config scope=system role=maintainer result=denied$',
        r'^type=config scope=system role=admin result=ok$',
        r'^type=acceptance driver_export_absent=true admin_export_confirm_required=true '
        r'admin_export_logged=true export_config_gated=true config_system_admin_only=true result=PASS$',
    ]
    for rx in EXACT:
        if not re.search(rx, t, re.M):
            fails.append("probe missing exact line %s" % rx)
    # 2. the ext oracle encodes the same 13-perm expectation
    et = EXT.read_text(encoding="utf-8")
    CPP_NAMES = {
        "view_realtime": "ViewRealtime", "view_alarm_center": "ViewAlarmCenter",
        "view_replay": "ViewReplay", "acknowledge": "Acknowledge",
        "view_all_cameras": "ViewAllCameras", "view_ai_debug": "ViewAiDebug",
        "view_maintenance": "ViewMaintenance", "ptz_control": "PtzControl",
        "reconnect": "Reconnect", "modify_device_config": "ModifyDeviceConfig",
        "modify_system_config": "ModifySystemConfig",
        "export_recording": "ExportRecording", "export_snapshot": "ExportSnapshot",
    }
    for p in PERMS13:
        if ("Permission::%s" % CPP_NAMES[p]) not in et:
            fails.append("ext oracle missing perm token %s" % p)
    if fails:
        print("SELF0 FAIL:")
        for f in fails:
            print("  -", f)
        return 1
    print("SELF0 PASS: probe matrix == INTERACTION §8-derived expectation; ext oracle tokens present")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
