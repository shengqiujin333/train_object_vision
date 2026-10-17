#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""验证侧逐行/结构一致性比对（impl r6 vs verify v1），正式夹具组件之一。

比对口径：
  * type=injection 行: duration_s 差异归一（r6 用 6s、verify 用 5s，两个独立会话）
  * 每行: t=/fps=/last_frame_ms 三类时间量归一（跨会话瞬时值，业务结构不变）
  * r6 的 >=5000ms 槽位剔除后与其余行逐行比对（verify 全部 <5000ms）
判据: 两侧 state 序列、presentable 位、reconnect_count、summary 行结构逐行相等。
"""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
KINDS = ("test", "file", "rtsp-auto", "rtsp-h265")


def norm(lines, drop_t_ge=None):
    out = []
    for l in lines:
        if drop_t_ge is not None and "type=stream_state" in l:
            m = re.match(r"^t=(\d+)", l)
            if m and int(m.group(1)) >= drop_t_ge:
                continue
        if l.startswith("type=injection"):
            l = re.sub(r"duration_s=\d+", "duration_s=X", l)
        l = re.sub(r"last_frame_ms=\d+", "last_frame_ms=X", l)
        l = re.sub(r"^t=\d+", "t=X", l)
        l = re.sub(r"fps=[\d.]+", "fps=X", l)
        out.append(l)
    return out


def main() -> int:
    all_ok = True
    report = {}
    for kind in KINDS:
        a = (HERE / ("pipe-192_168_0_103-20261017-item002-r6.core-evidence_probe-%s-"
                     "20261017-item002-r6.log" % kind)).read_text(errors="replace").splitlines()
        b = (HERE / ("pipe-192_168_0_103-20261017-item002-verify-v1.core-evidence_probe-%s-"
                     "20261017-item002-verify-v1.log" % kind)).read_text(errors="replace").splitlines()
        An = norm(a, drop_t_ge=5000)
        Bn = norm(b)
        same = An == Bn
        report[kind] = {"identical": same, "impl_lines": len(An), "verify_lines": len(Bn)}
        print("%s aligned_structure_identical=%s (impl=%d verify=%d lines)"
              % (kind, same, len(An), len(Bn)))
        if not same:
            for i, (x, y) in enumerate(zip(An, Bn)):
                if x != y:
                    print("  first diff line %d: %r || %r" % (i, x, y))
                    break
            if len(An) != len(Bn):
                print("  length difference: %d vs %d" % (len(An), len(Bn)))
            all_ok = False
    print("COMPARISON_IDENTICAL=%s" % all_ok)
    return 0 if all_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
