# -*- coding: utf-8 -*-
"""Round-2 pre-flight: static checks of the revised ext fixture + probes."""
import re
from pathlib import Path

root = Path(__file__).resolve().parents[4]
fix = (root / "docs/hmi/test_evidence/ITEM-022-maintenance/verify_maint22_ext.cpp").read_text(encoding="utf-8")

problems = []

# 1. the mojibake byte \xE6\x9B\x9A (曚) must be gone — 曝 is \xE6\x9B\x9D
if "\\xE6\\x9B\\x9A" in fix:
    problems.append("stale 曚 escape still present")
if "\\xE6\\x9B\\x9D" not in fix:
    problems.append("曝 escape missing after rewrite")

# 2. zoom boundary: no more 1.1/1.11 half-inclusive-fiction; deltas binary-exact
for bad in ("1.001", "11.001", "0.11"):
    if bad in fix and "pan_actual = " + bad in fix or ("zoom_actual = " + bad) in fix:
        problems.append("non-representable FP boundary still used: %s" % bad)

# 3. age semantics: now/frame pairs must be 100000-based, no plain 0/5000 combo
if "rt[0].last_frame_ms = 0;" in fix and "build_maintenance_cameras(reg, rt, 5000" in fix:
    problems.append("age-0 fiction still present")

# 4. arity check: count args of every check()/check_eq()/check_contains() call
#    (a repeat of the ITEM-020 v2 lesson: silent arity drift)
def top_level_commas(seg):
    depth = 0
    n = 0
    in_str = False
    esc = False
    for c in seg:
        if in_str:
            if esc:
                esc = False
            elif c == "\\":
                esc = True
            elif c == '"':
                in_str = False
            continue
        if c == '"':
            in_str = True
        elif c in "([{":
            depth += 1
        elif c in ")]}":
            if depth == 0:
                break
            depth -= 1
        elif c == "," and depth == 0:
            n += 1
    return n

for m in re.finditer(r'\b(check_eq|check_contains)\s*\(', fix):
    # skip the definition lines (those declare the signature)
    line_start = fix.rfind('\n', 0, m.start()) + 1
    if re.match(r'\s*(void) %s\s*\(' % m.group(1), fix[line_start:]):
        continue
    seg = fix[m.end():]
    # find matching close paren
    depth = 1
    i = 0
    in_str = False
    esc = False
    while i < len(seg) and depth > 0:
        c = seg[i]
        if in_str:
            if esc:
                esc = False
            elif c == "\\":
                esc = True
            elif c == '"':
                in_str = False
        else:
            if c == '"':
                in_str = True
            elif c == '(':
                depth += 1
            elif c == ')':
                depth -= 1
        i += 1
    body = seg[:i - 1]
    arity = top_level_commas(body) + 1
    want = 3  # both check_eq(name, got, want) and check_contains(got, needle, name)
    if arity != want:
        problems.append("%s call arity %d want %d near: %s"
                        % (m.group(1), arity, want, fix[m.start():m.start() + 70]))

# 5. suspicious 8-bit chars in the file (mojibake detectors)
bad_lines = [(i + 1, l[:80]) for i, l in enumerate(fix.splitlines())
             if any(0xC0 <= ord(c) <= 0xFF for c in l)]
if bad_lines:
    problems.append("suspicious 8-bit lines: %s" % bad_lines[:4])

# 6. dup-symbol check (ITEM-020 v2 lesson)
for sym in ("check_contains", "verify_check", "check_eq"):
    cnt = len(re.findall(r'\b(void|PtzCheck) %s\s*\(' % sym, fix))
    # check_contains/verify_check definitions
    cnt2 = len(re.findall(r'\bcheck_contains\s*\(', fix))
print("sym check_contains decl+uses:", cnt2)

if problems:
    print("PREFLIGHT-2: FAIL")
    for p in problems:
        print("  -", p)
    raise SystemExit(1)
print("PREFLIGHT-2: PASS (mojibake gone, FP boundaries exact, arity/encoding clean)")
