# -*- coding: utf-8 -*-
"""ITEM-024 offline self-check: derive verifier expectations from the archived
implementer r2 artifacts + authoritative docs, BEFORE any board round.

Zero board access, finite run, read-only. Checks:
  1. JUnit XML parses, 663/0, and the ITEM-024 case names the verifier will
     assert on the fresh round are present in the implementer archive.
  2. The archived health-probe log matches the verifier's independently
     computed oracle (health eval / log query / day rolling / recovery
     progress math recomputed by hand, not copied from the probe).
  3. The ext fixture oracle tokens match the probe lines.
"""
import html
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
EV = ROOT / "docs/hmi/test_evidence/ITEM-024-health"
JUNIT = EV / "build-TestRun-192_168_0_103-20261016-ITEM024-r2.core-evidence_core-tests-20261016-ITEM024-r2.xml"
PROBE = EV / "build-TestRun-192_168_0_103-20261016-ITEM024-r2.core-evidence_health-probe-20261016-ITEM024-r2.log"
EXT = Path(__file__).resolve().parent / "verify_health24_ext.cpp"

ITEM024_CASES = [
    "healthy -> ok, no alerts",
    "health text",
    "temp+net warn",
    "very high temp -> error",
    "low disk -> warn",
    "log json with fields",
    "query by source",
    "query by min severity",
    "query by keyword",
    "query by time window",
    "log day key",
    "day-rolling keeps latest day",
    "recovery has 8 steps",
    "all ok -> completed",
    "step label",
    "single non-critical camera failure does not exit app",
    "degraded camera counted",
    "streaming step degraded, rest continue",
    "recovery still completes",
    "critical step failure -> app not alive",
    "config step failed recorded",
    "progress reflects terminal steps",
]


def main() -> int:
    fails = []

    # 1. JUnit parse + case enumeration
    t = JUNIT.read_text(encoding="utf-8", errors="replace")
    m = re.search(r'tests="(\d+)"[^>]*failures="(\d+)"', t, re.S)
    if not m:
        fails.append("junit: no tests/failures attributes")
        n_tests, n_fail = None, None
    else:
        n_tests, n_fail = m.group(1), m.group(2)
    names = [html.unescape(n) for n in re.findall(r'name="([^"]+)"', t)]
    missing = [c for c in ITEM024_CASES if c not in names]
    print("junit tests=%s failures=%s item024_cases=%d/%d missing=%s"
          % (n_tests, n_fail, len(ITEM024_CASES) - len(missing),
             len(ITEM024_CASES), missing))
    if n_tests != "663" or n_fail != "0":
        fails.append("junit counts not 663/0: %s/%s" % (n_tests, n_fail))
    if missing:
        fails.append("junit missing ITEM-024 cases: %s" % missing)

    # 2. independent oracle for the health-probe log (patterns use real UTF-8
    # chars: the probe text is decoded str, byte escapes would not match)
    p = PROBE.read_text(encoding="utf-8", errors="replace")
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
    for rx in ORACLE:
        if not re.search(rx, p, re.M):
            fails.append("probe oracle missing: %s" % rx[:60])

    # independent progress math (verifier-derived, not from the probe):
    # 8 steps, each terminal step adds ceil(100/8)=12.5 -> int division:
    # after step i (1-based): i*100//8 -> 12,25,37,50,62,75,87,100
    want_progress = [i * 100 // 8 for i in range(1, 9)]
    got_progress = [int(x) for x in re.findall(r'^type=recovery step=\S+ .*?progress=(\d+)$', p, re.M)]
    print("progress math verifier=%s probe=%s" % (want_progress, got_progress))
    if got_progress != want_progress:
        fails.append("progress math mismatch")

    # 3. ext oracle tokens present in the ext source
    et = EXT.read_text(encoding="utf-8")
    for token in ["evaluate_health", "query_logs", "log_record_json",
                  "roll_health_logs_by_day", "RecoveryTracker", "camera_result",
                  "recovery_step_critical", "log_day_key", "min_severity",
                  "LogSeverity::Error", "StepStatus::Degraded", "degraded_cameras"]:
        if token not in et:
            fails.append("ext oracle missing token %s" % token)

    if fails:
        print("SELF FAIL:")
        for f in fails:
            print("  -", f)
        return 1
    print("SELF PASS: junit 663/0 + 22 ITEM-024 cases + probe oracle + progress math + ext tokens")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
