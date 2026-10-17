import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
s = (HERE / '_verify16_vext.cpp').read_text(encoding='utf-8')
gs = sorted(set(re.findall(r'g\d\d_[a-z0-9_]+', s)))
print(len(gs))
for g in gs:
    print(g)
