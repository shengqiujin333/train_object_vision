# -*- coding: utf-8 -*-
"""Offline dry-run v4: probe parser (incl. level-e2e), JUnit case enumeration,
scan regexes against DELIVERED SOURCES, and ext-fixture naming — all against the
implementer's uifix-r2 archives / the current working tree (no board)."""
import ast
import re

path = r'docs/hmi/test_evidence/ITEM-020-replay/verify_replay20_fixtures_v4.py'
src = open(path, encoding='utf-8').read()
ast.parse(src)
mod = {}
mod['__name__'] = 'selfcheck_mod'
exec(compile(src, path, 'exec'), mod)
print('P_DAY', mod['P_DAY'], 'P_ALARM', mod['P_ALARM'])
parse = mod['parse_replay_probe']
assert_fn = mod['assert_replay_probe']
base = r'docs/hmi/test_evidence/ITEM-020-replay'
text = open(base + r'/build-TestRun-192_168_0_103-20261016-ITEM020-uifix-r2'
            r'.core-evidence_replay-probe-20261016-ITEM020-uifix-r2.log',
            encoding='utf-8').read()
p = parse(text)
fails = assert_fn(p)
print('flags', p['flags'])
print('detail', p['detail'])
print('level-e2e', p['level-e2e'])
print('acceptance', p['acceptance'])
print('parsed_keys_nonnull', sorted(k for k in p if p[k]))
print('PROBE FAILS:', fails)
print('cases', len(mod['ITEM020_CASES']))

xml = open(base + r'/build-TestRun-192_168_0_103-20261016-ITEM020-uifix-r2'
           r'.core-evidence_core-tests-20261016-ITEM020-uifix-r2.xml',
           encoding='utf-8').read()
names = re.findall(r'name="([^"]+)"', xml)
names = [n.replace('&gt;', '>').replace('&lt;', '<').replace('&amp;', '&') for n in names]
missing = [c for c in mod['ITEM020_CASES'] if c not in names]
m = re.search(r'tests="(\d+)"[^>]*failures="(\d+)"', xml, re.S)
print('junit tests/failures', m.group(1), m.group(2), 'missing', missing)


def pass_lines(f):
    return [l.strip() for l in open(f, encoding='utf-8', errors='replace').readlines()
            if l.startswith('[PASS]')]


impl = pass_lines(base + r'/build-TestRun-192_168_0_103-20261016-ITEM020-uifix-r2'
                          r'.core-evidence_core-tests-20261016-ITEM020-uifix-r2.log')
print('impl PASS lines', len(impl), 'RESULT PASS:',
      'RESULT: PASS' in open(base + r'/build-TestRun-192_168_0_103-20261016-'
      r'ITEM020-uifix-r2.core-evidence_core-tests-20261016-ITEM020-uifix-r2.log',
      encoding='utf-8').read())

# --- scan regexes host-run against delivered sources ---------------------------------
b = r'app/src/ui/'
p3 = open(b + r'qml/pages/P3Replay.qml', encoding='utf-8').read()
bridge = open(b + r'src/replay_model.cpp', encoding='utf-8').read()
cpp = open('app/src/core/src/replay.cpp', encoding='utf-8').read()
coreh = open('app/src/core/include/railhmi/core/replay.h', encoding='utf-8').read()

print('V4_bridge_marks_from_rows=', 'alarm_marks_from_rows' in bridge)
mb = re.search(r'build_timeline\([^\n]*\n?[^\n]*marks, keyframe_interval_ms\)', bridge)
print('V4_bridge_marks_overload_used=', bool(mb), '->', repr(mb.group(0)) if mb else None)
print('V4_bridge_fallback_kept=', re.search(r'AlarmLevel::L3\}', bridge) is not None)
print('V4_core_helper_declared=', 'alarm_marks_from_rows' in coreh)
print('V4_core_helper_defined=', 'alarm_marks_from_rows' in cpp)
mh = re.search(r'alarm_marks_from_rows[\s\S]{0,400}r\.level', cpp)
print('V4_core_helper_uses_row_level=', bool(mh))
print('V4_cursor_follow_explicit=', bool(re.search(r'x:\s*Math\.max\(0,\s*Math\.min\(parent\.width\s*-\s*width,\s*page\.xOf\(page\.cursor\)\s*-\s*width\s*/\s*2\)\)', p3)))
print('V4_cursor_left_anchor_absent=', re.search(r'id:\s*cursorTime[\s\S]{0,400}anchors\.left:\s*parent\.left', p3) is None)
print('V4_cursor_labelled_current=', bool(re.search(r'text:\s*"当前 "', p3)))
print('V4_panel_chip_name=', 'chipName(modelData)' in p3)
mp = re.search(r'chipName\(modelData\)\s*\+\s*" \(" \+ modelData \+ "\)"', p3)
print('V4_panel_id_suffix=', bool(mp), '->', repr(mp.group(0)) if mp else None)
print('V4_old_bare_label_absent=', bool(re.search(r'text:\s*modelData \+ " "', p3)) is False)
