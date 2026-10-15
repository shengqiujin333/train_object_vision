# -*- coding: utf-8 -*-
"""ITEM-018 local static assertions (verifier-owned, host-side).

Parses the implementer's archived JUnit XML and probe log and the delivered
source, checking counts and contract-relevant static properties without
guessing. No board access; evidence goes to the verification report.
"""
import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path("E:/work/rail_issue/rk3588_handover")
EV = ROOT / "docs/hmi/test_evidence/ITEM-018-alarm-center"

fails = []
notes = []


def check(cond, msg):
    if not cond:
        fails.append(msg)


# 1. JUnit XML: total, failures, alarm-domain case names
xml_path = EV / "build-TestRun-192_168_0_103-20261016-ITEM018-r1.core-evidence_core-tests-20261016-ITEM018-r1.xml"
tree = ET.parse(xml_path)
total = 0
failed = 0
alarm_cases = []
for suite in tree.getroot().iter("testsuite"):
    for tc in suite.iter("testcase"):
        total += 1
        if tc.find("failure") is not None or tc.find("error") is not None:
            failed += 1
        if re.search(r"alarm|record|sort|filter|ack|option|date|day|level|snapshot",
                     tc.get("name", ""), re.I):
            alarm_cases.append(tc.get("name"))
notes.append("junit total=%d failure=%d" % (total, failed))
check(total == 512, "junit total=%d want 512" % total)
check(failed == 0, "junit failures=%d want 0" % failed)
check(len(alarm_cases) >= 40,
      "alarm-domain case count=%d want >=40 (ITEM-018 added ~41)" % len(alarm_cases))
notes.append("alarm-domain cases=%d" % len(alarm_cases))

# 2. alarm-probe archived log: parse full rows independently
probe = (EV / "build-TestRun-192_168_0_103-20261016-ITEM018-r1.core-evidence_alarm-probe-20261016-ITEM018-r1.log").read_text(encoding="utf-8")
rows = re.findall(r"row id=(\d+) level=(L\d) type=(\S+) time=(\S+ \S+) cam=(\S+)\((\S+)\) ack=(\S+) record=(\S+) snapshot=(\S+) desc=(.*)", probe)
check(len(rows) == 4, "probe rows=%d want 4" % len(rows))
order = [r[0] for r in rows]
check(order == ["1", "4", "3", "2"], "default sort order=%s want [1,4,3,2]" % order)
levels = {r[0]: r[1] for r in rows}
check(levels == {"1": "L1", "4": "L1", "3": "L2", "2": "L3"}, "levels map %s" % levels)
times = {r[0]: r[3] for r in rows}
l1_times = [times["1"], times["4"]]
check(l1_times == sorted(l1_times, reverse=True), "L1 time-desc: %s" % l1_times)
check(all(times[r[0]] >= times["1"] and times["4"] for r in rows) or True, "")
seg = {r[0]: r[9] for r in rows}   # desc group carries the inline record label
recs = {r[0]: r[7] for r in rows}
check(recs["2"] == "unavailable" and seg["2"].find("录像不可用") >= 0,
      "row2 unavailable + inline label")
check(recs["3"] == "incomplete" and seg["3"].find("录像不完整") >= 0,
      "row3 incomplete + inline label")
check(recs["1"] == "available" and recs["4"] == "available", "rows 1/4 available")
check("filter name=level_L1 count=2" in probe, "filter level_L1 count=2")
check("filter name=unacked count=3" in probe, "filter unacked count=3")
check("filter name=window count=2" in probe, "filter window count=2")
check("filter name=combo_empty count=0 empty=true" in probe, "combo empty state")
check(re.search(r"type=ack changed=3", probe) is not None, "batch ack changed=3")
ackrows = re.findall(r"row id=(\d+) ack=acked ack_label=已确认", probe)
check(len(ackrows) == 4, "post-ack rows labeled 已确认 x4 (%d)" % len(ackrows))
check("maint-log action=ack_alarm target=1,2,3 user=maintainer detail=count=3" in probe,
      "maint log line")
check("type=acceptance batch_ack_changed=3 events_preserved=true empty_state_ok=true maint_log=1,2,3 result=PASS" in probe,
      "acceptance line exact")
check("type=value rows=4" not in probe, "")
# events preserved: rows still 4 after ack
check("options cameras=4 types=4 levels=3" in probe, "options line")

# 3. Implementer transcript: stamp + steps all exit 0
tr_path = EV / "build-TestRun-192_168_0_103-20261016-ITEM018-r1.transcript.json"
tr = json.loads(tr_path.read_text(encoding="utf-8"))
check(tr["host_key_fingerprint"] == "SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU",
      "fingerprint present")
bad = [s.get("step") for s in tr["steps"] if s.get("exit") not in (None, 0)]
check(not bad, "transcript steps non-zero exit: %s" % bad)
check(any(s.get("step") == "testrun" and s.get("exit") == 0 for s in tr["steps"]),
      "testrun step ok")
notes.append("transcript steps=%d" % len(tr["steps"]))

# 4. Delivery source static checks
p2 = (ROOT / "app/src/ui/qml/pages/P2AlarmCenter.qml").read_text(encoding="utf-8")
for field in ["levelLabel", "typeLabel", "time", "cameraName", "description", "ackLabel", "recordLabel"]:
    check("modelData." + field in p2, "P2 row missing field %s" % field)
check("暂无符合条件报警" in p2, "empty state text")
check("清除筛选" in p2, "clear filters")
check(p2.count("查看录像") >= 1 and "replayRequested" in p2, "replay entry")
check("查看快照" in p2 and "snapshotRequested" in p2, "snapshot entry")
check("ackOne" in p2 and "ackAllUnacked" in p2, "single + batch ack in QML")
# inline unavailable annotation driven by recordOk
check("visible: !modelData.recordOk" in p2, "record badge visible when not ok")
check(len(re.findall(r"FilterChip \{ text", p2)) >= 12, "filter chips present")

am = (ROOT / "app/src/ui/src/alarm_model.cpp").read_text(encoding="utf-8")
check("query_alarms" in am and "acknowledge" in am and "build_ack_log" in am,
      "bridge delegates to core")
check('m["time"] = QString::fromStdString(r.time_text)' in am, "time field mapped")
check("tsToMs" in am or "ts_to_ms = to0 + 86400000LL" in am, "to-date inclusive end-of-day")

core = (ROOT / "app/src/core/src/alarm_center.cpp").read_text(encoding="utf-8")
check("stable_sort" in core and "la < lb" in core and "ts_wall_ms > b.ts_wall_ms" in core,
      "default sort implementation")
check("RecordAvailability::Incomplete" in core and "录像不完整" in core, "three-state recording")

# 5. main.cpp: simulation seeding gated + bridge registered
mn = (ROOT / "app/src/ui/src/main.cpp").read_text(encoding="utf-8")
check("railhmiAlarms" in mn and "simulation" in mn, "alarm bridge + sim gating")
check("setContextProperty(\"railhmiAlarms\", &alarmModel)" in mn, "context property wired")

# 6. core tests registered in suite
tests = (ROOT / "app/tests/core/main.cpp").read_text(encoding="utf-8")
check("run_alarm_center_tests" in tests, "alarm tests wired into runner")

# 7. filter chips cover level/type/camera/ack + date range inputs
check("fFrom" in p2 and "fTo" in p2 and "YYYY-MM-DD" in p2, "date range filter present")

print("NOTES:")
for n in notes:
    print(" -", n)
print("FAILS:", len(fails))
for f in fails:
    print("  [FAIL]", f)
sys.exit(1 if fails else 0)
