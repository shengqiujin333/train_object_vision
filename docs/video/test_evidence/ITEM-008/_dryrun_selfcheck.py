# r"""ITEM-008 VERIFY 夹具上板前自检：C++ 判例名↔python 表逐一同集、大括号配平、恒真占位审计、
# 交付归档数据对 check_semantics 判据期望的可行性核对。整段脚本零成本预飞，problems>0 即中止上板。
import re
import sys
from collections import Counter
from pathlib import Path

HERE = str(Path(__file__).resolve().parent) + '/'
src_ext = open(HERE + '_verify_ext.py', encoding='utf-8').read()
problems = []

# 1) C++ check 名 vs python names 表同集合
m = re.search(r"VCODE = r'''(.*?)'''", src_ext, re.S)
vcode = m.group(1)
cpp_names = []
for cm in re.finditer(r"check\((?!bool)", vcode):
    tail = vcode[cm.end():cm.end() + 400]
    nm = re.search(r'"(v\d+_[a-z0-9_]+)"', tail)
    if nm:
        cpp_names.append(nm.group(1))
names_m = re.search(r"names = \[(.*?)\]", src_ext, re.S)
py_names = re.findall(r'"(v\d+_[a-z0-9_]+)"', names_m.group(1))
cd, pd = Counter(cpp_names), Counter(py_names)
if sorted(cpp_names) != sorted(py_names):
    problems.append('names differ cpp=%s py=%s' % (sorted(set(cd) ^ set(pd)), sorted(set(pd) - set(cd))))

# 2) 大括号配平
if vcode.count('{') != vcode.count('}'):
    problems.append('brace mismatch %d/%d' % (vcode.count('{'), vcode.count('}')))

# 3) 恒真占位审计
for bad in ('v09_no_hang', '== false || true', 'check(true'):
    if bad in src_ext:
        problems.append('tautology residue: %s' % bad)

# 4) 交付归档（impl r2）复现 check_semantics 判据的期望可行性
BP_RX = re.compile(
    r"t=(\d+) type=bp dispatched=(\d+) fps=(\d+) display_dropped=(\d+) ai_dropped=(\d+) "
    r"ai_depth=(\d+) recording_overflow=(\d+) recording_depth=(\d+) "
    r"display_reads=(\d+) ai_reads=(\d+) recording_reads=(\d+)")
runlog = open(HERE + 'bp-192_168_0_103-20261017-item008-r2.core-evidence_backpressure-20261017-item008-r2.log',
              encoding='utf-8').read()
samples = []
for line in runlog.splitlines():
    mb = BP_RX.search(line)
    if mb:
        samples.append({"t": int(mb.group(1)), "fps": int(mb.group(3)), "dd": int(mb.group(4)),
                        "ad": int(mb.group(5)), "aid": int(mb.group(6)), "ov": int(mb.group(7)),
                        "rd": int(mb.group(8)), "rr": int(mb.group(11))})
stop_ms, depth_cap, rec_cap = 4002, 2, 8
pre = [x for x in samples if x['t'] < stop_ms]
post = [x for x in samples if x['t'] >= stop_ms]
if any(x['ov'] != 0 for x in pre):
    problems.append('sim prestop nonzero')
if post[-1]['rd'] != rec_cap:
    problems.append('sim tail rd')
if max(x['rd'] for x in samples) > rec_cap:
    problems.append('sim rd exceeds')
if max(x['aid'] for x in samples) > depth_cap:
    problems.append('sim aid exceeds')
if any(x['fps'] < 20 for x in samples if x['t'] >= 2000):
    problems.append('sim fps post ramp')
ov = [x['ov'] for x in post]
ups = sum(1 for i in range(len(ov) - 1) if ov[i + 1] > ov[i])
if ups < 2:
    problems.append('sim overflow ups=%d<2' % ups)
rr = [x['rr'] for x in post]
if len(set(rr[1:])) > 1:
    problems.append('sim rr changing')

print('problems=%d' % len(problems))
for p in problems:
    print(' -', p)
print('EX-D cpp checks: %d' % len(cpp_names))
sys.exit(1 if problems else 0)
