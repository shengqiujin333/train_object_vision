# -*- coding: utf-8 -*-
"""Greedy hex-escape audit of the ext fixture (""xB0C"" risk)."""
import re
from pathlib import Path

src = (Path(__file__).resolve().parents[4] /
       "docs/hmi/test_evidence/ITEM-022-maintenance/verify_maint22_ext.cpp").read_text(
           encoding="utf-8")
bad = []
for n, line in enumerate(src.splitlines(), 1):
    for m in re.finditer(r'\\x([0-9a-fA-F]{2})([0-9a-fA-F])', line):
        bad.append((n, m.group(0)))
if bad:
    print("GREEDY_HEX_RISKS: FAIL")
    for b in bad:
        print("  line %d: %s" % b)
    raise SystemExit(1)
print("GREEDY_HEX_RISKS: PASS (no \\xHH followed by a hex digit in the fixture)")
