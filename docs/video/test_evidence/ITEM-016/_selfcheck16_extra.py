import json
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
d = json.load(open(HERE / 'diag-ext-192_168_0_103-20261017-item016-verify-v1.transcript.json', encoding='utf-8'))
src = (HERE / '_verify16_vext.cpp').read_text(encoding='utf-8')
names = set(re.findall(r'check\([^;]*?,\s*"([^"]+)"', src, re.S))
for s in d['steps']:
    if s['step'] == 'EXC-run':
        passes = re.findall(r'^vext PASS (\S+)', s.get('output', ''), re.M)
        print('total:', len(passes))
        named = [p for p in passes if p in names]
        dynamic = [p for p in passes if p not in names]
        print('named:', len(named), 'dynamic:', len(dynamic))
        print('dynamic:', dynamic)
