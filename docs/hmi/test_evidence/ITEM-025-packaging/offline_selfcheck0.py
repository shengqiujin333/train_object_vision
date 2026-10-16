# -*- coding: utf-8 -*-
"""ITEM-025 verification — offline self-check 0 (board-free).

Pre-flight on locally archived implementer artifacts:
  - implementer r1 core-tests log: 663 [PASS] / 0 [FAIL] / RESULT PASS
  - the 31 fault/injection case names exist verbatim in the log
  - JUnit XML (r1): 663/0 + the 31 exact case names
  - fault-probe log: full 16-line oracle match
  - sim-inject log: oracle lines (onset/recovery/summary) + byte-identity sanity
  - sim-inject json: parseable, kinds complete
  - implementer r1 driver log: all pipeline exits 0
  - ext fixture static audit: check signature, RESULT once, brace balance,
    include list, no duplicate check names
"""
import ast
import json
import re
from collections import Counter
from pathlib import Path

BASE = Path(__file__).resolve().parent
CORE = BASE / "build-TestRun-192_168_0_103-20261016-ITEM025-r1.core-evidence_core-tests-20261016-ITEM025-r1.log"
XML = BASE / "build-TestRun-192_168_0_103-20261016-ITEM025-r1.core-evidence_core-tests-20261016-ITEM025-r1.xml"
PROBE = BASE / "build-TestRun-192_168_0_103-20261016-ITEM025-r1.core-evidence_fault-probe-20261016-ITEM025-r1.log"
SIM = BASE / "build-TestRun-192_168_0_103-20261016-ITEM025-r1.core-evidence_sim-inject-20261016-ITEM025-r1.log"
SIMJ = BASE / "build-TestRun-192_168_0_103-20261016-ITEM025-r1.core-evidence_sim-inject-20261016-ITEM025-r1.json"
DRIVER = BASE / "build-TestRun-192_168_0_103-20261016-ITEM025-r1.log"

import importlib.util
spec = importlib.util.spec_from_file_location(
    "fx", BASE / "verify_packaging25_fixtures.py")
# cannot import the whole driver (needs paramiko + argv); replicate the two
# oracle tables by extracting them textually instead.
fx_src = (BASE / "verify_packaging25_fixtures.py").read_text(encoding="utf-8")

fails = []


def step(desc, ok, detail=""):
    print(("PASS" if ok else "FAIL"), desc, detail)
    if not ok:
        fails.append(desc + " " + detail)


def parse_pass(text):
    return [l.strip() for l in text.splitlines() if l.startswith("[PASS]")]


def parse_fail(text):
    return [l.strip() for l in text.splitlines() if l.startswith("[FAIL]")]


# 0. oracle tables in the fixtures driver parse out cleanly
fault_block = re.search(r"FAULT_ORACLE = \[(.*?)\n\]", fx_src, re.S).group(1)
fault_oracle = ast.literal_eval("[" + fault_block + "]")
step("fault oracle extracted", len(fault_oracle) == 16, "n=%d" % len(fault_oracle))
cases_block = re.search(r"ITEM025_CASES = \[(.*?)\n\]", fx_src, re.S).group(1)
item025_cases = ast.literal_eval("[" + cases_block + "]")
step("31 case names extracted", len(item025_cases) == 31, "n=%d" % len(item025_cases))

# 1. implementer r1 core-tests log
u = CORE.read_text(encoding="utf-8", errors="replace")
step("impl core-tests 663/0", len(parse_pass(u)) == 663 and not parse_fail(u)
     and "RESULT: PASS passed=663 failed=0" in u,
     "pass=%d fail=%d" % (len(parse_pass(u)), len(parse_fail(u))))
names_in_log = {l[len("[PASS]"):].strip() for l in parse_pass(u)}
missing_cases = [c for c in item025_cases if c not in names_in_log]
step("all 32 case names verbatim in log", not missing_cases, "missing=%s" % missing_cases)

# 2. JUnit XML
x = XML.read_text(encoding="utf-8", errors="replace")
m = re.search(r'tests="(\d+)"[^>]*failures="(\d+)"', x, re.S)
xml_names = [n.replace("&gt;", ">").replace("&lt;", "<").replace("&amp;", "&")
             .replace("&apos;", "'").replace("&quot;", '"')
             for n in re.findall(r'name="([^"]+)"', x)]
step("junit 663/0", m and m.group(1) == "663" and m.group(2) == "0",
     "tests=%s failures=%s" % (m.group(1), m.group(2)) if m else "no match")
missxml = [c for c in item025_cases if c not in xml_names]
step("junit 31 ITEM-025 exact cases", not missxml, "missing=%s" % missxml)

# 3. fault-probe oracle on the implementer log
p = PROBE.read_text(encoding="utf-8", errors="replace")
miss = [rx for rx in fault_oracle if not re.search(rx, p, re.M)]
step("fault-probe oracle 16 lines on impl log", not miss, "missing=%s" % miss)

# 4. sim-inject log + json
s = SIM.read_text(encoding="utf-8", errors="replace")
sim_checks = [
    (r'^t=0 type=injection camera=CAM-01 profile=front '
     r'faults=FI-01,FI-02,FI-03,FI-04,FI-05,FI-10 onset_ms=1500 recovery_ms=5000 '
     r'duration_ms=6000 step_ms=500$', "injection params"),
    (r'^t=1500 type=stream_state state=reconnecting reconnect_count=1 presentable=false cam=CAM-01$',
     "reconnect@1500"),
    (r'^t=2500 type=stream_state state=offline reconnect_count=3 presentable=false cam=CAM-01$',
     "offline@2500"),
    (r'^t=5000 type=stream_state state=live reconnect_count=0 presentable=true cam=CAM-01$',
     "live@5000"),
    (r'^t=6000 type=summary final_state=live reconnect_count=0 alarms=2 gaps=1 events_recorded=2$',
     "summary"),
]
for rx, name in sim_checks:
    step("sim log %s" % name, re.search(rx, s, re.M) is not None)
sj = json.loads(SIMJ.read_text(encoding="utf-8"))
kinds = {x.get("type") for x in sj}
step("sim json kinds complete",
     {"stream_state", "ai", "storage", "ptz", "gap", "summary", "alarm"} <= kinds,
     "kinds=%s" % sorted(kinds))

# 5. driver log exits
d = DRIVER.read_text(encoding="utf-8", errors="replace")
for key in ("TESTRUN_EXIT=0", "SCAN_EXIT=0", "REPLAY_WIRE_EXIT=0", "AIDBG_WIRE_EXIT=0",
            "MAINT_WIRE_EXIT=0", "PERM_WIRE_EXIT=0", "HEALTH_WIRE_EXIT=0",
            "PACK_EXIT=0", "SIM_EXIT=0", "STATUS_EXIT=0", "AI_EXIT=0", "ALARM_EXIT=0",
            "CLIP_EXIT=0", "REPLAY_EXIT=0", "MAINT_EXIT=0", "PERM_EXIT=0",
            "HEALTH_EXIT=0", "FAULT_EXIT=0"):
    step("driver %s" % key, key in d)

# 6. ext fixture static audit
v = (BASE / "verify_packaging25_ext.cpp").read_text(encoding="utf-8", errors="replace")
step("ext check signature", "void check(bool ok, const std::string& name)" in v)
step("ext check_eq signature",
     "void check_eq(const std::string& name, const std::string& got, const std::string& want)" in v)
step("ext printf RESULT once", v.count('std::printf("RESULT:') == 1)
step("ext braces balanced", v.count("{") == v.count("}"),
     "%d vs %d" % (v.count("{"), v.count("}")))
step("ext includes core only",
     sorted(re.findall(r'#include\s+"([^"]+)"', v)) ==
     ["railhmi/core/fault_catalog.h", "railhmi/core/retention.h", "railhmi/core/simulation.h"],
     str(re.findall(r'#include\s+"([^"]+)"', v)))
names_e = re.findall(r'check(?:_eq)?\(\s*"((?:[^"\\]|\\.)*)"', v)
dupes = [k for k, c in Counter(names_e).items() if c > 1]
step("ext no duplicate check names", not dupes, "dupes=%s" % dupes)

# 7. scan fixture static audit
sc = (BASE / "verify_packaging25_scan.py").read_text(encoding="utf-8", errors="replace")
for key in ("P1_unit_restart_always", "D01_execstartpre_provenance",
            "D01b_upgrade_doc_bin_tools", "D02_config_arg_consumed"):
    step("scan key %s" % key, re.search(r'print\("%s=' % key, sc) is not None)

result = "PASS" if not fails else "FAIL"
print("OFFLINE-0 RESULT: %s (%d failed)" % (result, len(fails)))
print(json.dumps({"offline0": result, "failed": fails}, ensure_ascii=False))
raise SystemExit(0 if not fails else 1)
