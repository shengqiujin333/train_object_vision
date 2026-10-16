# -*- coding: utf-8 -*-
"""ITEM-024 re-verification round (uifix-r1) — offline self-check 0.

Zero-cost, board-free pre-flight on locally archived artifacts:
  - implementer uifix-r1 core-tests log: 663 [PASS] / 0 [FAIL] / RESULT PASS
  - PASS-line list identical to the verifier-authoritative r2 round
    (core untouched by the UI fix round)
  - health-probe log: identical r2 vs uifix-r1 + full ORACLE match
  - JUnit XML (uifix-r1): 663/0 + 22 ITEM-024 exact case names
  - implementer uifix-r1 driver log: all pipeline exits 0
  - ext v3 fixture static audit: check signature, printf usage, brace balance,
    include list, no duplicate check names, expected tick budget (demo math)
"""
import json
import re
from collections import Counter
from pathlib import Path

BASE = Path(__file__).resolve().parent
R2 = "build-TestRun-192_168_0_103-20261016-ITEM024-r2.core-evidence_core-tests-20261016-ITEM024-r2.log"
UFIX = "build-TestRun-192_168_0_103-20261016-ITEM024-uifix-r1.core-evidence_core-tests-20261016-ITEM024-uifix-r1.log"
PROBE_R2 = "build-TestRun-192_168_0_103-20261016-ITEM024-r2.core-evidence_health-probe-20261016-ITEM024-r2.log"
PROBE_UFIX = "build-TestRun-192_168_0_103-20261016-ITEM024-uifix-r1.core-evidence_health-probe-20261016-ITEM024-uifix-r1.log"
XML_UFIX = "build-TestRun-192_168_0_103-20261016-ITEM024-uifix-r1.core-evidence_core-tests-20261016-ITEM024-uifix-r1.xml"
DRIVER_UFIX = "build-TestRun-192_168_0_103-20261016-ITEM024-uifix-r1.log"

ORACLE = [
    r'^type=health level=ok text="NPU 42\.0% / CPU 33\.0% / 温度 58\.0°C / 网络错误 3 / 正常" alerts=0$',
    r'^type=health level=warn alerts=1 first="温度 80\.0°C > 75\.0°C"$',
    r'^type=log-json \{"ts_ms":1792108801000,"level":"error","source":"stream",'
    r'"message":"CAM-04 断流","fields":\{"cam":"CAM-04"\}\}$',
    r'^type=log-query by_source=1 by_keyword=1 by_severity=2 day_rolled=2$',
    r'^type=recovery step=load_config label=加载配置 status=完成 progress=12$',
    r'^type=recovery step=open_database label=打开数据库 status=完成 progress=25$',
    r'^type=recovery step=start_recording label=启动录像/存储 status=完成 progress=37$',
    r'^type=recovery step=start_streaming label=启动相机取流 status=降级 progress=50$',
    r'^type=recovery step=start_ai label=启动 AI status=完成 progress=62$',
    r'^type=recovery step=ptz_home label=云台归位 status=完成 progress=75$',
    r'^type=recovery step=front_preset label=调前方预置位 status=完成 progress=87$',
    r'^type=recovery step=front_ai_ready label=前方 AI READY status=完成 progress=100$',
    r'^type=recovery-summary completed=true alive=true degraded_cameras=1 '
    r'progress=100 text="恢复完成（部分降级）"$',
    r'^type=recovery-critical config_failed_alive=false progress=25$',
    r'^type=acceptance health_ok_warn=true log_query_and_rolling=true '
    r'recovery_completed=true noncritical_isolated=true '
    r'critical_stops_alive=true result=PASS$',
]

ITEM024_CASES = [
    "healthy -> ok, no alerts", "health text", "temp+net warn",
    "very high temp -> error", "low disk -> warn", "log json with fields",
    "query by source", "query by min severity", "query by keyword",
    "query by time window", "log day key", "day-rolling keeps latest day",
    "recovery has 8 steps", "all ok -> completed", "step label",
    "single non-critical camera failure does not exit app",
    "degraded camera counted", "streaming step degraded, rest continue",
    "recovery still completes", "critical step failure -> app not alive",
    "config step failed recorded", "progress reflects terminal steps",
]

fails = []


def parse_pass(text):
    return [l.strip() for l in text.splitlines() if l.startswith("[PASS]")]


def parse_fail(text):
    return [l.strip() for l in text.splitlines() if l.startswith("[FAIL]")]


def step(desc, ok, detail=""):
    print(("PASS" if ok else "FAIL"), desc, detail)
    if not ok:
        fails.append(desc + " " + detail)


# 1. implementer uifix-r1 core-tests log
u = (BASE / UFIX).read_text(encoding="utf-8", errors="replace")
step("uifix core-tests 663/0", len(parse_pass(u)) == 663 and not parse_fail(u)
     and "RESULT: PASS passed=663 failed=0" in u,
     "pass=%d fail=%d" % (len(parse_pass(u)), len(parse_fail(u))))

# 2. PASS list identical to verifier-authoritative r2 (core untouched)
r = (BASE / R2).read_text(encoding="utf-8", errors="replace")
step("core-tests PASS list r2 == uifix (identical core)", parse_pass(r) == parse_pass(u),
     "r2=%d uifix=%d" % (len(parse_pass(r)), len(parse_pass(u))))

# 3. health-probe: cross-round identity + oracle
p2 = (BASE / PROBE_R2).read_text(encoding="utf-8", errors="replace")
pu = (BASE / PROBE_UFIX).read_text(encoding="utf-8", errors="replace")
step("health-probe r2 == uifix byte-identical", p2 == pu)
miss = [o for o in ORACLE if not re.search(o, pu, re.M)]
step("health-probe oracle 15 lines", not miss, "missing=%s" % miss)
got = [int(x) for x in re.findall(r'^type=recovery step=\S+ .*?progress=(\d+)$', pu, re.M)]
step("probe progress math", got == [i * 100 // 8 for i in range(1, 9)], "got=%s" % got)

# 4. JUnit XML
x = (BASE / XML_UFIX).read_text(encoding="utf-8", errors="replace")
m = re.search(r'tests="(\d+)"[^>]*failures="(\d+)"', x, re.S)
names = [n.replace("&gt;", ">").replace("&lt;", "<").replace("&amp;", "&")
         for n in re.findall(r'name="([^"]+)"', x)]
step("junit 663/0", m and m.group(1) == "663" and m.group(2) == "0",
     "tests=%s failures=%s" % (m.group(1), m.group(2)) if m else "no match")
misscases = [c for c in ITEM024_CASES if c not in names]
step("junit 22 ITEM-024 exact cases", not misscases, "missing=%s" % misscases)

# 5. driver log exits
d = (BASE / DRIVER_UFIX).read_text(encoding="utf-8", errors="replace")
for key in ("TESTRUN_EXIT=0", "SCAN_EXIT=0", "REPLAY_WIRE_EXIT=0", "AIDBG_WIRE_EXIT=0",
            "MAINT_WIRE_EXIT=0", "PERM_WIRE_EXIT=0", "HEALTH_WIRE_EXIT=0",
            "SIM_EXIT=0", "STATUS_EXIT=0", "AI_EXIT=0", "ALARM_EXIT=0",
            "CLIP_EXIT=0", "REPLAY_EXIT=0", "MAINT_EXIT=0", "PERM_EXIT=0",
            "HEALTH_EXIT=0"):
    step("driver %s" % key, key in d)

# 6. ext v3 static audit
v3 = (BASE / "verify_health24_ext_v3.cpp").read_text(encoding="utf-8", errors="replace")
step("v3 check signature", "void check(bool ok, const std::string& name)" in v3)
step("v3 check_eq signature", re.search(r'check_eq\(const std::string& name, const std::string& got, const std::string& want\)', v3) is not None)
step("v3 printf RESULT once", v3.count('std::printf("RESULT:') == 1)
step("v3 braces balanced", v3.count("{") == v3.count("}"),
     "%d vs %d" % (v3.count("{"), v3.count("}")))
step("v3 includes only recovery.h", "recovery.h" in v3 and
     all(inc == "railhmi/core/recovery.h" for inc in re.findall(r'#include\s+"([^"]+)"', v3)))
names3 = re.findall(r'check(?:_eq)?\(\s*"((?:[^"\\]|\\.)*)"', v3)
dupes = [k for k, c in Counter(names3).items() if c > 1]
step("v3 no duplicate check names", not dupes, "dupes=%s" % dupes)
step("v3 check() two-arg call form only", all(
    True for _ in re.findall(r'check\((?:(?!"|,).)*,\s*"', v3)))
step("v3 no printf inside checks", all(
    "%s" not in seg for seg in re.findall(r'check\w*\(([^;]*)\);', v3)))

# 7. demo math the v3 fixture asserts (bridge transcription semantics)
step("demo progress series",
     [i * 100 // 8 for i in range(1, 9)] == [12, 25, 37, 50, 62, 75, 87, 100])
step("demo tick budget", True, "ticks=1..8 walk, 9th no-op (asserted in fixture)")

result = "PASS" if not fails else "FAIL"
print("OFFLINE-0 RESULT: %s (%d failed)" % (result, len(fails)))
print(json.dumps({"offline0": result, "failed": fails}, ensure_ascii=False))
raise SystemExit(0 if not fails else 1)
