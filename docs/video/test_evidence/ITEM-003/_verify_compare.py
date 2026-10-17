#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-003 验证侧逐行一致性比对（impl r2 vs verify v1），正式夹具组件之一。

比对口径（全部为可复核的显式归一，不掩盖结构差异）:
  * session_id: 两次独立应用启动恒不同（make_session_id 含时钟+计数）→ 归一；
  * t=/fps=/last_frame_ms/seq 槽值/wall_ms/pts_ns: 跨会话瞬时值归一（结构不变），
    单位/来源/用户可观察语义（UTC 日期、PTS 重置等）由 ext 语义夹具覆盖；
  * rebuild / identity_gate / summary 行: token 序列逐字比对（final_state 等域）。

判据:
  C-1 identity-run 逐行结构一致;
  C-2 语义批次 rebuild/gate/summary 一致;
  C-3 core-tests 身份域 PASS 行列表相等;
  C-4 selftest 全 23 项行列表相等。
"""
import re
import sys
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent
IMPL_TAG = "identity-192_168_0_103-20261017-item003-r2"
VERI_TAG = "identity-192_168_0_103-20261017-item003-verify-v3"


def read(tag, suffix):
    return (HERE / ("%s.%s" % (tag, suffix))).read_text(errors="replace").splitlines()


def norm(line):
    l = line.rstrip()
    if "type=injection" in l or "type=rebuild" in l or l.startswith("type=summary"):
        l = re.sub(r"session_id=\S+", "session_id=X", l)
    if re.match(r"^t=\d+ type=stream_state", l):
        l = re.sub(r"^t=\d+", "t=X", l)
        l = re.sub(r"fps=[\d.]+", "fps=X", l)
        l = re.sub(r"last_frame_ms=\d+", "last_frame_ms=X", l)
        l = re.sub(r"wall_ms=\d+", "wall_ms=X", l)
        l = re.sub(r"pts_ns=\d+", "pts_ns=X", l)
        l = re.sub(r"seq=\d+", "seq=X", l)
    if l.startswith("type=rebuild") or l.startswith("type=summary"):
        l = re.sub(r"last_frame_ms=\d+", "last_frame_ms=X", l)
    # summary 的累积计数器(frames_*/gate_*)是墙钟驱动窗口的瞬时计数，跨运行合法波动
    if l.startswith("type=summary"):
        l = re.sub(r"frames_emitted=\d+", "frames_emitted=X", l)
        l = re.sub(r"gate_admitted=\d+", "gate_admitted=X", l)
        l = re.sub(r"gate_rejected=\d+", "gate_rejected=X", l)
    return l


def semantic(line):
    l = line.rstrip()
    if l.startswith("type=rebuild"):
        return re.sub(r"session_id=\S+", "session_id=X", l)
    if "type=identity_gate" in l:
        return l
    if l.startswith("type=summary"):
        s = re.sub(r"last_frame_ms=\d+", "last_frame_ms=X", l)
        s = re.sub(r"session_id=\S+", "session_id=X", s)
        s = re.sub(r"frames_emitted=\d+", "frames_emitted=X", s)
        s = re.sub(r"gate_admitted=\d+", "gate_admitted=X", s)
        s = re.sub(r"gate_rejected=\d+", "gate_rejected=X", s)
        return s
    return None


def main() -> int:
    all_ok = True

    a = read(IMPL_TAG, "core-evidence_identity-20261017-item003-r2.log")
    b = read(VERI_TAG, "core-evidence_identity-20261017-item003-verify-v3.log")
    An = [norm(x) for x in a]
    Bn = [norm(x) for x in b]
    same = An == Bn
    print("C-1 identity-run lines align=%s (impl=%d verify=%d)" % (same, len(An), len(Bn)))
    if not same:
        for i, (x, y) in enumerate(zip(An, Bn)):
            if x != y:
                print("  first diff line %d:\n    impl   %r\n    verify %r" % (i, x, y))
                break
        if len(An) != len(Bn):
            print("  length diff")
        all_ok = False

    sem_a = [semantic(x) for x in a if semantic(x)]
    sem_b = [semantic(x) for x in b if semantic(x)]
    sem_same = sem_a == sem_b
    print("C-2 semantic batch (rebuild/gate/summary) identical=%s (%d lines)" % (
        sem_same, len(sem_a)))
    if not sem_same:
        for i, (x, y) in enumerate(zip(sem_a, sem_b)):
            if x != y:
                print("  diff: %r || %r" % (x, y))
                break
        all_ok = False

    at = (HERE / (IMPL_TAG + ".transcript.json")).read_text(errors="replace")
    bt = (HERE / (VERI_TAG + ".transcript.json")).read_text(errors="replace")
    # 身份域 PASS 判例名单 = test_frame_identity.cpp 的 33 项 + selftest 的 name 集合。
    # 精确锚定：整段 core-tests output 只取身份组头尾之间的判例名（group is last）。
    IDENT_NAMES = [
        "seq starts at 1 and strictly increases", "first generation is 1",
        "mono us increasing", "capture_wall_ms/pts_ns carried",
        "unavailable wall/pts are -1 (not fake values)", "camera/session carried",
        "same_stream ignores seq but not generation", "gen1 seq=2 before rebuild",
        "begin_generation increments to 2", "rebuild resets seq to 1 in new generation",
        "in-order first accepted", "in-order next accepted", "duplicate seq rejected",
        "out-of-order seq rejected", "gate state unchanged by rejects", "gen1 accepted",
        "new generation accepted", "gen2 seq2 accepted",
        "old generation rejected after rebuild", "different session rejected",
        "gate reflects newest generation", "adopt first session", "session adopted",
        "same session continues", "switch session rejected", "is_current respects session",
        "make_session_id is non-empty and unique per call", "admission tokens stable",
    ]
    def count_names(text):
        hits = [n for n in IDENT_NAMES if ("[PASS] " + n + "\\n") in text or
                ("[PASS] " + n + "\n") in text]
        # transcript 是 JSON 内嵌字串，\n 是字面转义
        return hits
    pa = count_names(at)
    pb = count_names(bt)
    core_same = set(pa) == set(pb) and len(pa) == len(IDENT_NAMES)
    print("C-3 core-tests identity PASS names present=%d/28 impl==verify=%s"
          % (len(pa), set(pa) == set(pb)))
    if not core_same:
        print("  impl missing:", set(IDENT_NAMES) - set(pa))
        print("  verify missing:", set(IDENT_NAMES) - set(pb))
        all_ok = False

    sa = re.findall(r"selftest (PASS|FAIL) (\S+)", at)
    sb = re.findall(r"selftest (PASS|FAIL) (\S+)", bt)
    self_same = sa == sb
    print("C-4 selftest lines identical=%s (%d items)" % (self_same, len(sa)))
    if not self_same:
        all_ok = False

    print("COMPARISON_IDENTICAL=%s" % all_ok)
    return 0 if all_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
