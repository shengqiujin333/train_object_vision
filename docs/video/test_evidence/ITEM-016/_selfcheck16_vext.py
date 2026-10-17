import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
s = (HERE / '_verify16_vext.cpp').read_text(encoding='utf-8')
depth = 0
min_depth = 0
for ch in s:
    if ch == '{':
        depth += 1
    elif ch == '}':
        depth -= 1
    min_depth = min(min_depth, depth)
print('final depth:', depth, 'min:', min_depth)
checks = re.findall(r'check\((?!bool)', s)
print('check calls:', len(checks))
names = re.findall(r'check\([^;]*?,\s*"([^"]+)"', s, re.S)
print('names found:', len(names))
dups = sorted(n for n in set(names) if names.count(n) > 1)
print('dups:', dups)
if dups:
    for d in dups:
        print('DUP:', d, names.count(d))
