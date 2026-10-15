# -*- coding: utf-8 -*-
"""Offline dry-run for v3 fixtures: probe parser (v3 line format) + JUnit case
check vs the implementer's uifix-r1 archives (verifier self-check, no board)."""
import ast
import re

path = r'docs/hmi/test_evidence/ITEM-020-replay/verify_replay20_fixtures_v3.py'
src = open(path, encoding='utf-8').read()
ast.parse(src)
mod = {}
mod['__name__'] = 'selfcheck_mod'
exec(compile(src, path, 'exec'), mod)
print('P_DAY', mod['P_DAY'], 'P_ALARM', mod['P_ALARM'])
parse = mod['parse_replay_probe']
assert_fn = mod['assert_replay_probe']
text = open(r'docs/hmi/test_evidence/ITEM-020-replay/build-TestRun-192_168_0_103-'
            r'20261016-ITEM020-uifix-r1.core-evidence_replay-probe-20261016-ITEM020-uifix-r1.log',
            encoding='utf-8').read()
p = parse(text)
fails = assert_fn(p)
print('flags', p['flags'])
print('detail', p['detail'])
print('segments', p['segments'])
print('parsed_keys_nonnull', sorted(k for k in p if p[k]))
print('PROBE FAILS:', fails)
print('cases', len(mod['ITEM020_CASES']))
xml = open(r'docs/hmi/test_evidence/ITEM-020-replay/build-TestRun-192_168_0_103-'
           r'20261016-ITEM020-uifix-r1.core-evidence_core-tests-20261016-ITEM020-uifix-r1.xml',
           encoding='utf-8').read()
names = re.findall(r'name="([^"]+)"', xml)
names = [n.replace('&gt;', '>').replace('&lt;', '<').replace('&amp;', '&') for n in names]
missing = [c for c in mod['ITEM020_CASES'] if c not in names]
m = re.search(r'tests="(\d+)"[^>]*failures="(\d+)"', xml, re.S)
print('junit tests/failures', m.group(1), m.group(2), 'missing', missing)

# core-tests log line-count check vs archived impl log
def pass_lines(f):
    return [l.strip() for l in open(f, encoding='utf-8', errors='replace').readlines()
            if l.startswith('[PASS]')]
import os
base = r'docs/hmi/test_evidence/ITEM-020-replay'
impl = pass_lines(os.path.join(base, 'build-TestRun-192_168_0_103-20261016-ITEM020-uifix-r1'
                                      '.core-evidence_core-tests-20261016-ITEM020-uifix-r1.log'))
print('impl PASS lines', len(impl), 'RESULT PASS:',
      'RESULT: PASS' in open(os.path.join(base, 'build-TestRun-192_168_0_103-20261016-'
                                          'ITEM020-uifix-r1.core-evidence_core-tests-'
                                          '20261016-ITEM020-uifix-r1.log'),
                             encoding='utf-8').read())
