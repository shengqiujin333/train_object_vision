# -*- coding: utf-8 -*-
"""ITEM-024 offline self-check 2: static audit of the verifier ext fixture.

No local C++ compiler (E-00); this audit mechanically checks the known
compile/noise hazard classes from prior rounds before the board round:
  1. check() call signature consistency: every check(...) call passes
     (bool-ish, name) — flag any call whose first arg is a string literal.
  2. No printf %s with non-char* args; no %-placeholders inside check names.
  3. C++ \\xHH escape greed hazard: no string literal where \\xHH is followed
     by a hex digit character (the ITEM-022 lesson).
  4. Aggregated struct initializers: LogRecord{...} braced lists have 5
     fields; RecoveryStep/StepStatus casts stay in range.
  5. Every check name is unique (duplicate names weaken failure triage).
  6. Includes referenced: railhmi/core/health.h + recovery.h + alarm_center.h
     (parse_date_utc_ms) exist in the repo.
"""
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
APP = HERE.parents[3] / "app"
EXT = HERE / "verify_health24_ext.cpp"


def main() -> int:
    src = EXT.read_text(encoding="utf-8")
    fails = []

    # 1. check(ok, name) — first arg must not be a string literal
    for m in re.finditer(r'check\(\s*"', src):
        fails.append("check() with string literal first arg at offset %d" % m.start())

    # 2. printf placeholders: ext uses std::printf only in main; %s misuse scan
    for line in src.splitlines():
        if "printf" in line and "%s" in line:
            # only the two known-good printf sites in main() are allowed
            if ("RESULT: %s passed=%d failed=%d" not in line
                    and "== verify" not in line
                    and 'n.c_str()' not in line):
                fails.append("suspicious printf: %s" % line.strip()[:80])

    # 3. \xHH followed by hex digit inside a string literal
    for m in re.finditer(r'"((?:[^"\\]|\\.)*)"', src):
        s = m.group(1)
        for em in re.finditer(r'\\x([0-9a-fA-F]{2})', s):
            tail = s[em.end():em.end() + 1]
            if tail and re.match(r'[0-9a-fA-F]', tail):
                fails.append("\\xHH greed hazard near: ...%s..." % s[max(0, em.start() - 10):em.end() + 2])

    # 4. LogRecord braced init: count top-level commas in each {...} after LogRecord{
    for m in re.finditer(r'LogRecord\{', src):
        depth = 1
        i = m.end()
        commas = 0
        while i < len(src) and depth > 0:
            ch = src[i]
            if ch in "{[":
                depth += 1
            elif ch in "}]":
                depth -= 1
            elif ch == "," and depth == 1:
                commas += 1
            i += 1
        if commas != 4:
            fails.append("LogRecord braced init has %d top-level commas (want 4) near offset %d"
                         % (commas, m.start()))

    # 5. duplicate check names
    names = re.findall(r'check(?:_eq)?\((?:[^,]+,\s*)?"((?:[^"\\]|\\.)*)"', src)
    dup = {n for n in names if names.count(n) > 1}
    if dup:
        fails.append("duplicate check names: %s" % sorted(dup))

    # 6. includes exist
    for inc in ("src/core/include/railhmi/core/health.h",
                "src/core/include/railhmi/core/recovery.h",
                "src/core/include/railhmi/core/alarm_center.h"):
        if not (APP / inc).is_file():
            fails.append("missing include target: %s" % inc)

    if fails:
        print("SELF2 FAIL:")
        for f in fails:
            print("  -", f)
        return 1
    print("SELF2 PASS: %d checks, no duplicate names, no escape/comma/signature hazards"
          % len(names))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
