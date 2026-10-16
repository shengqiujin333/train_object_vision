# offline dry-run of the ITEM-021 fixture oracles (no board, no paramiko)
# catches expectation-construction errors before spending a board round.
import re
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[4]
ev = root / "docs/hmi/test_evidence/ITEM-021-ai-debug"

sys.path.insert(0, str(ev))
import verify_ai21_fixtures as fx  # noqa: E402

problems = []

# 1. ai-probe oracle vs the implementer's r2 archived log (formulas must match)
text = (ev / "build-TestRun-192_168_0_103-20261016-ITEM021-r2.core-evidence_ai-probe-20261016-ITEM021-r2.log").read_text(encoding="utf-8")
fails = fx.assert_ai_probe(text)
if fails:
    problems.append("ai-probe oracle fails on r2 archive: %s" % fails)

# 2. ai-probe oracle vs MY fresh VERIFY-20261016-v1 log (already produced)
text2 = (ev / "build-TestRun-192_168_0_103-VERIFY-20261016-v1.core-evidence_ai-probe-VERIFY-20261016-v1.log").read_text(encoding="utf-8")
fails2 = fx.assert_ai_probe(text2)
if fails2:
    problems.append("ai-probe oracle fails on v1 fresh: %s" % fails2)

# 3. core-tests parser + case-set vs the r2 archived log and junit xml
ct = (ev / "build-TestRun-192_168_0_103-20261016-ITEM021-r2.core-evidence_core-tests-20261016-ITEM021-r2.log").read_text(encoding="utf-8")
parsed = fx.parse_core_tests_log(ct)
if len(parsed["pass"]) != 586 or parsed["fail"] or "PASS" not in parsed["result"]:
    problems.append("core-tests parse wrong: %d %s %s" % (len(parsed["pass"]), parsed["fail"][:2], parsed["result"]))
xml = (ev / "build-TestRun-192_168_0_103-20261016-ITEM021-r2.core-evidence_core-tests-20261016-ITEM021-r2.xml").read_text(encoding="utf-8")
m = re.search(r'tests="(\d+)"[^>]*failures="(\d+)"', xml, re.S)
names_all = [n.replace("&gt;", ">").replace("&lt;", "<").replace("&amp;", "&")
             for n in re.findall(r'name="([^"]+)"', xml)]
found = [c for c in fx.ITEM021_CASES if c in names_all]
if m.group(1) != "586" or m.group(2) != "0" or len(found) != len(fx.ITEM021_CASES):
    problems.append("junit/case-set mismatch: %s %s %d/%d" % (m.group(1), m.group(2), len(found), len(fx.ITEM021_CASES)))

# 4. wiring-scan source oracles vs the local delivered sources
app = root / "app"
sources = {
    "p5": (app / "src/ui/qml/pages/P5AiDebug.qml").read_text(encoding="utf-8"),
    "main": (app / "src/ui/qml/main.qml").read_text(encoding="utf-8"),
    "p1": (app / "src/ui/qml/pages/P1Realtime.qml").read_text(encoding="utf-8"),
    "mc": (app / "src/ui/src/main.cpp").read_text(encoding="utf-8"),
    "theme": (app / "src/ui/qml/Theme.qml").read_text(encoding="utf-8"),
    "tokens_qml": (app / "src/ui/qml/Tokens.qml").read_text(encoding="utf-8"),
    "tokens_json": (app / "resources/tokens.json").read_text(encoding="utf-8"),
    "tokens_yaml": (root / "docs/hmi/design_tokens.yaml").read_text(encoding="utf-8"),
    "bb": (app / "tools/board_build.py").read_text(encoding="utf-8"),
    "nav": (app / "src/core/src/navigation.cpp").read_text(encoding="utf-8"),
    "inf": (app / "src/core/src/inference.cpp").read_text(encoding="utf-8"),
    "check_tools": (app / "tools/check_ai_debug_wiring.py").read_text(encoding="utf-8"),
}
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
printed = dict(re.findall(r"(T\d+_[A-Za-z0-9_]+)= (\S+)", buf.getvalue()))
for key in fx.SCAN_KEYS:
    got_str = printed.get(key)
    got = {"True": True, "False": False}.get(got_str)
    if got is not True:
        problems.append("scan %s = %r want True" % (key, got))

if problems:
    print("OFFLINE_SELFCHECK: FAIL")
    for p in problems:
        print("  -", p)
    raise SystemExit(1)
print("OFFLINE_SELFCHECK: PASS (ai-probe oracle r2+fresh, core-tests/junit/case-set, %d wiring checks)" % len(fx.SCAN_KEYS))
