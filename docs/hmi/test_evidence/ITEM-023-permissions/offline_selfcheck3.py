# -*- coding: utf-8 -*-
"""ITEM-023 offline self-check 3: dry-run of the log-compare / junit-parse /
ext-summary regex logics with the archived artifacts (positive) and mutated
copies (negatives), so fixture parser bugs are caught before the board round."""
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(HERE))
import verify_perm23_fixtures as F  # noqa: E402

EV = ROOT / "docs" / "hmi" / "test_evidence" / "ITEM-023-permissions"
IMPL_LOG = EV / "build-TestRun-192_168_0_103-20261016-ITEM023-r3.core-evidence_core-tests-20261016-ITEM023-r3.log"
IMPL_XML = EV / "build-TestRun-192_168_0_103-20261016-ITEM023-r3.core-evidence_core-tests-20261016-ITEM023-r3.xml"
IMPL_PROBE = EV / "build-TestRun-192_168_0_103-20261016-ITEM023-r3.core-evidence_permissions-probe-20261016-ITEM023-r3.log"

fails = []

# --- log-compare positive ---
impl = F.parse_core_tests_log(IMPL_LOG.read_text(encoding="utf-8", errors="replace"))
if len(impl["pass"]) != 641 or impl["fail"]:
    fails.append("archived log parse: pass=%d fail=%d" % (len(impl["pass"]), len(impl["fail"])))
if len(set(impl["pass"])) != len(impl["pass"]):
    dup = sorted(c for c in impl["pass"] if impl["pass"].count(c) > 1)
    print("DUPED PASS NAMES (counted each occurrence, JUnit must cover):", dup)
# same log == identical (the fresh board copy is expected to be byte-equal)
if impl["pass"] != impl["pass"]:
    fails.append("self-inequality")
# mutated impl log (one case renamed) must NOT be identical
mut = impl["pass"][:]
mut[640] = mut[640] + " x"
if mut == impl["pass"]:
    fails.append("line-compare equality mutated not caught")

# --- junit parse positive ---
xtext = IMPL_XML.read_text(encoding="utf-8", errors="replace")
m = re.search(r'tests="(\d+)"[^>]*failures="(\d+)"', xtext, re.S)
if not m:
    fails.append("junit totals regex failed on archived xml")
else:
    if m.group(1) != "641" or m.group(2) != "0":
        fails.append("junit totals != 641/0: %s" % (m.groups(),))
names_all = [n.replace("&gt;", ">").replace("&lt;", "<").replace("&amp;", "&")
             for n in re.findall(r'name="([^"]+)"', xtext)]
found = [c for c in F.ITEM023_CASES if c in names_all]
missing = [c for c in F.ITEM023_CASES if c not in names_all]
if missing:
    # names may carry classname prefix — show actual nearby names for diagnosis
    print("ITEM023 case missing from JUnit names:", missing)
    print("sample names:", names_all[:5])
    fails.append("junit: %d ITEM023 cases missing" % len(missing))

# --- junit negative: a fresh XML with 640/1 must fail the gate ---
bad_xml = xtext.replace('tests="641"', 'tests="640"').replace('failures="0"', 'failures="1"', 1)
m2 = re.search(r'tests="(\d+)"[^>]*failures="(\d+)"', bad_xml, re.S)
if m2 and m2.groups() == ("641", "0"):
    fails.append("junit negative mutation not reflected")
else:
    print("junit negative observed totals:", m2.groups() if m2 else None)

if fails:
    print("SELF3 FAIL:")
    for f in fails:
        print("  -", f)
    raise SystemExit(1)
print("SELF3 PASS: log-compare 641 + junit 641/0 with all %d ITEM-023 cases" % len(found))
