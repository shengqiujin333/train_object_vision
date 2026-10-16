# offline dry-run of the ITEM-022 fixture oracles (no board, no paramiko)
# catches expectation-construction errors before spending a board round.
import re
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[4]
ev = root / "docs/hmi/test_evidence/ITEM-022-maintenance"

sys.path.insert(0, str(ev))
import verify_maint22_fixtures as fx  # noqa: E402

problems = []

# 1. maintenance-probe oracle vs the implementer's r1 archived log (formulas)
text = (ev / "build-TestRun-192_168_0_103-20261016-ITEM022-r1.core-evidence_maintenance-probe-20261016-ITEM022-r1.log").read_text(encoding="utf-8")
fails = fx.assert_maintenance_probe(text)
if fails:
    problems.append("maint-probe oracle fails on r1 archive: %s" % fails)

# 2. core-tests parser + case-set vs the r1 archived log and junit xml
ct = (ev / "build-TestRun-192_168_0_103-20261016-ITEM022-r1.core-evidence_core-tests-20261016-ITEM022-r1.log").read_text(encoding="utf-8")
parsed = fx.parse_core_tests_log(ct)
if len(parsed["pass"]) != 612 or parsed["fail"] or "PASS" not in parsed["result"]:
    problems.append("core-tests parse wrong: %d %s %s" % (len(parsed["pass"]), parsed["fail"][:2], parsed["result"]))
xml = (ev / "build-TestRun-192_168_0_103-20261016-ITEM022-r1.core-evidence_core-tests-20261016-ITEM022-r1.xml").read_text(encoding="utf-8")
m = re.search(r'tests="(\d+)"[^>]*failures="(\d+)"', xml, re.S)
names_all = [n.replace("&gt;", ">").replace("&lt;", "<").replace("&amp;", "&")
             for n in re.findall(r'name="([^"]+)"', xml)]
found = [c for c in fx.ITEM022_CASES if c in names_all]
missing_cases = [c for c in fx.ITEM022_CASES if c not in names_all]
if m.group(1) != "612" or m.group(2) != "0" or found != fx.ITEM022_CASES:
    problems.append("junit/case-set mismatch: tests=%s failures=%s found=%d/%d missing=%s"
                    % (m.group(1), m.group(2), len(found), len(fx.ITEM022_CASES), missing_cases))

# 3. wiring-scan source oracles vs the local delivered sources
# exec the scan body with the same variable names, then verify all True
# (on the board the scan runs from /tmp/railhmi_build where sources sit at
# src/... and design_tokens.yaml at the root; locally mirror that by remapping
# the open() paths to the repo layout — app/src/..., docs/hmi/design_tokens.yaml)
import contextlib
scan_src = fx.SCAN_PY
for old, new in (("open('src/", "open('app/src/"),
                 ("open('resources/", "open('app/resources/"),
                 ("open('tools/", "open('app/tools/"),
                 ("open('design_tokens.yaml'", "open('docs/hmi/design_tokens.yaml'")):
    scan_src = scan_src.replace(old, new)
scan_locals = {}
import io
buf = io.StringIO()
with contextlib.chdir(str(root)), contextlib.redirect_stdout(buf):
    exec(compile(scan_src, "scan", "exec"), scan_locals)  # noqa: S102
printed = dict(re.findall(r"([A-Za-z0-9_]+)= (\S+)", buf.getvalue()))
for key in fx.SCAN_KEYS:
    got_str = printed.get(key)
    got = {"True": True, "False": False}.get(got_str)
    if got is not True:
        problems.append("scan %s = %r want True" % (key, got))
d1 = printed.get("D1_manual_reconnect_entry_present")
d2 = printed.get("D2_log_day_rolling_present")
print("offline D-probes: D1_manual_reconnect_entry_present=%s D2_log_day_rolling_present=%s" % (d1, d2))
if d1 != "False":
    problems.append("D-probe 1 unexpectedly True=%s (would mask the missing-reconnect finding)" % d1)
if d2 != "False":
    problems.append("D-probe 2 unexpectedly True=%s (would mask the no-day-rolling finding)" % d2)
# additional offline facts recorded for the report
print("offline facts: T8_reconnect_text_bridged_in_main=%s T8_reconnect_text_consumed_in_p6=%s"
      % (printed.get("T8_reconnect_text_bridged_in_main"),
         printed.get("T8_reconnect_text_consumed_in_p6")))

if problems:
    print("OFFLINE_SELFCHECK: FAIL")
    for p in problems:
        print("  -", p)
    raise SystemExit(1)
print("OFFLINE_SELFCHECK: PASS (maint-probe oracle r1, core-tests/junit/case-set 612, %d wiring checks)" % len(fx.SCAN_KEYS))
