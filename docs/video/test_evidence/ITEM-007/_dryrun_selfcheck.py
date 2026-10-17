#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-007 EXT 夹具离线自检（上板前）：预构造 C++ 判例表与判据逻辑的期望先审。"""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

src = (HERE / "_verify_ext.py").read_text(encoding="utf-8")
problems = []

# 1) C++ 夹具 vs python names 表逐一对应（防静默少跑/名字漂移）
m = re.search(r"VCODE = r'''(.*?)'''", src, re.S)
vcode = m.group(1)
cpp_names = []
for cm in re.finditer(r"check\(", vcode):
    tail = vcode[cm.end():cm.end() + 400]
    nm = re.search(r'"(g\d\d_[a-z0-9_]+)"', tail)
    if nm:
        cpp_names.append(nm.group(1))
names_m = re.search(r"names = \[(.*?)\]", src, re.S)
py_names = re.findall(r'"([^"]+)"', names_m.group(1))
if sorted(cpp_names) != sorted(py_names):
    problems.append("cpp names != py names: cpp_only=%s py_only=%s"
                    % (sorted(set(cpp_names) - set(py_names)), sorted(set(py_names) - set(cpp_names))))

# 2) g10 期望自查: restart 在 200ms 单次 pull 上限下重建需多轮 loop 完成；
#    800ms 后检查 g10_others_* —— 其余五路 800ms 内 last_frame_ms 必须已推进 ≥1 帧（25fps 源）
if "sleep_ms(800)" not in vcode:
    problems.append("g10 rebuild window missing")
# others_last 取快照在 restart 前 1500ms —— 此时 25fps 源已出多帧
if "others_last[i]" not in vcode:
    problems.append("g10 baseline capture missing")

# 3) g06 frozen 检查在 cut 后 1200+600ms —— cut 路状态机 2s 默认冻结窗内 f1 不变成立
if "g06_cut_path_frame_frozen" not in vcode:
    problems.append("g06 frozen check missing")

# 4) EX-A fps 阈值 20 与"串行泵每路 ≤~4fps"的对照是否写进判据
if "20.0" not in src:
    problems.append("EX-A fps threshold missing")

# 5) EX-D --count 0 期望 exit=3 —— probe 源码: started==0 → return 3 ✓；未知 arg → return 2 ✓
if "exit=3" not in src or "all channels failed" not in src:
    problems.append("EX-D expectation wording missing")

# 6) EX-E 确定性判据不要求 fps/last_frame_ms 跨轮恒等（单调钟不同）
sex = re.search(r"def ext_e.*?return ok", src, re.S).group(0)
if 'a["last"] == 0' not in sex or 'a["fps"]' in sex:
    problems.append("EX-E semantics must exclude fps/last values")

# 7) 恒真断言审计: check(x==x) 形态
if re.search(r"check\(\s*\w+\[[^\]]+\]\.\w+ == \w+\[[^\]]+\]\.\w+\s*,", vcode):
    problems.append("possible tautological check")

# 8) 聚合初始化顺序: StreamStats 字段只在直驱读取用，无聚合初始化 —— 确认 VCODE 无 {}
braces_init = re.findall(r"=\s*\{[^}]*\}", vcode)
if braces_init:
    problems.append("unexpected aggregate init in VCODE: %s" % braces_init[:3])

print("problems=%d" % len(problems))
for p in problems:
    print(" -", p)
sys.exit(1 if problems else 0)
