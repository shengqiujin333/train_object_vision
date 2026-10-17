import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
s = (HERE / '_verify16_vext.cpp').read_text(encoding='utf-8')
bad = []
for line in s.splitlines():
    # const char* + const char* chains: string literal followed by + string literal directly
    if re.search(r'"\s*\+\s*"', line):
        # std::string anywhere on the left of the chain makes it legal; check first operand
        first = line.split('+')[0]
        if 'std::string' not in line and not re.search(r'check\([^,]*,\s*"[^"]*"\s*,\s*std::string', line):
            bad.append(line.strip()[:120])
print('potential illegal chains:', len(bad))
for b in bad:
    print('  ', b)
