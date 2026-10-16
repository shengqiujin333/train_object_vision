# -*- coding: utf-8 -*-
"""ITEM-023 offline self-check 2: assert_permissions_probe oracle — positive on
the archived implementer r3 probe log; negatives on mutated copies (flag flips
must be caught) to prove the oracle is not a constant-true."""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(HERE))
import verify_perm23_fixtures as F  # noqa: E402

ARCHIVE = (ROOT / "docs" / "hmi" / "test_evidence" / "ITEM-023-permissions" /
           "build-TestRun-192_168_0_103-20261016-ITEM023-r3.core-evidence_permissions-probe-20261016-ITEM023-r3.log")

fails = []
text = ARCHIVE.read_text(encoding="utf-8")
r = F.assert_permissions_probe(text)
if r:
    fails.append("positive on archived probe produced fails: %s" % r)

negatives = [
    ("driver export flipped Allow", lambda t: t.replace(
        "row perm=export_recording label=录像导出 driver=-",
        "row perm=export_recording label=录像导出 driver=Y")),
    ("maintainer export flipped on", lambda t: t.replace(
        "row perm=export_recording label=录像导出 driver=- maintainer=-",
        "row perm=export_recording label=录像导出 driver=- maintainer=Y")),
    ("system config opened to maintainer", lambda t: t.replace(
        "row perm=modify_system_config label=系统配置修改 driver=- maintainer=-",
        "row perm=modify_system_config label=系统配置修改 driver=- maintainer=Y")),
    ("gate confirm reason dropped", lambda t: t.replace(
        'type=gate admin_unconfirmed reason="需二次确认"',
        'type=gate admin_unconfirmed reason=""')),
    ("maint-log dropped", lambda t: t.replace(
        'type=maint-log action=export_recording user=admin result=ok '
        'detail="录像导出 CAM-01 [1000,61000]"', "type=noise x=1")),
    ("acceptance verdict flipped", lambda t: t.replace(
        "config_system_admin_only=true result=PASS",
        "config_system_admin_only=true result=FAIL")),
    ("row count dropped", lambda t: t.replace(
        "row perm=ptz_control", "row-noise")),
    ("export-gate maintainer_on dropped", lambda t: t.replace(
        "maintainer_on=Y", "maintainer_on=-")),
]
for name, mut in negatives:
    r2 = F.assert_permissions_probe(mut(text))
    if not r2:
        fails.append("negative NOT caught: %s" % name)
    else:
        print("caught negative [%s]: %s" % (name, r2[0]))

if fails:
    print("SELF2 FAIL:")
    for f in fails:
        print("  -", f)
    raise SystemExit(1)
print("SELF2 PASS: oracle accepts archived probe and catches all %d mutations" % len(negatives))
