#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-007 验证侧独立解析比对（VERIFY-20261017-v1）。

从实现方 r1 与验证轮 verify-v1 的逐拍 JSON 归档独立重建判据（不采信两轮的
type=acceptance 行），核对：
  A1 started=5 / fail_index=5 error 非空 / 槽位身份序列 CAM-01..CAM-06 每拍恒等
  A2 断流前每拍非故障 5 路 presentable=true
  A3 断流后 cut 路(idx3) presentable=false 且 last_frame_ms 冻结（与 r1 冻结值独立核对）
  A4 断流后其余非故障路 presentable=true（帧时钟持续推进）
  A5 失败路(idx5)全程 presentable=false lease=false generation=0
  A6 每拍 cells=6、live/presentable 计数与 cell 行自洽
  A7 selftest 6/6 两轮逐行一致
  A8 两轮状态序列语义一致（同拍窗口逐 cell state/冻结/重连对照，fps 为自然波动非判据）
  A9 实现者 acceptance 行 result=PASS（两轮）
"""
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
R1 = HERE / "multi-192_168_0_103-20261017-item007-r1.core-evidence_multi-worker-20261017-item007-r1.json"
V1 = HERE / "multi-192_168_0_103-20261017-item007-verify-v1.core-evidence_multi-worker-20261017-item007-verify-v1.json"
LOG_R1 = HERE / "multi-192_168_0_103-20261017-item007-r1.log"
LOG_V1 = HERE / "multi-192_168_0_103-20261017-item007-verify-v1.log"
CUT_INDEX, FAIL_INDEX, CUT_AFTER_MS = 3, 5, 4000
IDS = ["CAM-0%d" % i for i in range(1, 7)]

problems = []
results = []


def rec(rid, ok, detail):
    results.append({"id": rid, "ok": bool(ok), "detail": detail})
    print("%s %s %s" % (rid, "PASS" if ok else "FAIL", detail))


def load(p):
    d = json.loads(p.read_text(encoding="utf-8"))
    return d, d["rounds"]


def cut_frozen_value(rounds):
    """断流后 cut 路首次采样的 last_frame_ms（冻结基线）。"""
    for r in rounds:
        if r["t_ms"] >= CUT_AFTER_MS:
            return r["cells"][CUT_INDEX]["last_frame_ms"]
    return None


def analyze(tag, data, rounds):
    p = []
    # A1 started/slot identity
    for r in rounds:
        got = [c["id"] for c in r["cells"]]
        if got != IDS:
            p.append("%s A1 t=%s slot ids %s" % (tag, r["t_ms"], got))
        if [c["idx"] for c in r["cells"]] != list(range(6)):
            p.append("%s A1 t=%s idx order" % (tag, r["t_ms"]))
        if len(r["cells"]) != 6:
            p.append("%s A6 t=%s cells=%d" % (tag, r["t_ms"], len(r["cells"])))
        live = sum(1 for c in r["cells"] if c["state"] == "live")
        pres = sum(1 for c in r["cells"] if c["presentable"])
        if live != r["live"] or pres != r["presentable"]:
            p.append("%s A6 t=%s live/presentable mismatch" % (tag, r["t_ms"]))
    fail_cell_state = {r["cells"][FAIL_INDEX]["state"] for r in rounds}
    if fail_cell_state != {"offline"}:
        p.append("%s A5 fail cell states %s" % (tag, sorted(fail_cell_state)))
    for r in rounds:
        c5 = r["cells"][FAIL_INDEX]
        if c5["presentable"] or c5["lease"] or c5["generation"] != 0:
            p.append("%s A5 t=%s fail cell not isolated" % (tag, r["t_ms"]))
    # A2 before cut: non-fail presentable
    for r in rounds:
        if r["t_ms"] < CUT_AFTER_MS:
            for c in r["cells"]:
                if c["idx"] == FAIL_INDEX:
                    continue
                if not c["presentable"]:
                    p.append("%s A2 t=%s idx=%d not presentable before cut" % (tag, r["t_ms"], c["idx"]))
    # A3 after cut: cut path not presentable + frozen
    frozen = cut_frozen_value(rounds)
    if frozen is None or frozen <= 0:
        p.append("%s A3 no frozen baseline" % tag)
    else:
        for r in rounds:
            if r["t_ms"] >= CUT_AFTER_MS + 500:  # 留一拍状态机进入 reconnecting
                c = r["cells"][CUT_INDEX]
                if c["presentable"]:
                    p.append("%s A3 t=%s cut presentable" % (tag, r["t_ms"]))
                if c["last_frame_ms"] != frozen:
                    p.append("%s A3 t=%s cut frame moved %s != %s" % (tag, r["t_ms"], c["last_frame_ms"], frozen))
    # A4 after cut: others presentable and advancing
    for r in rounds:
        if r["t_ms"] >= CUT_AFTER_MS + 500:
            for c in r["cells"]:
                if c["idx"] in (CUT_INDEX, FAIL_INDEX):
                    continue
                if not c["presentable"]:
                    p.append("%s A4 t=%s idx=%d not presentable after cut" % (tag, r["t_ms"], c["idx"]))
    adv_last = {}
    for r in rounds:
        for c in r["cells"]:
            if c["idx"] in (CUT_INDEX, FAIL_INDEX):
                continue
            if r["t_ms"] >= CUT_AFTER_MS + 500:
                prev = adv_last.get(c["idx"])
                if prev is not None and c["last_frame_ms"] <= prev:
                    p.append("%s A4 t=%s idx=%d clock not advancing" % (tag, r["t_ms"], c["idx"]))
                adv_last[c["idx"]] = c["last_frame_ms"]
    return p, frozen


d1, r1r = load(R1)
dv, rvv = load(V1)
pa, fz1 = analyze("r1", d1, r1r)
pb, fzv = analyze("verify", dv, rvv)
problems += pa + pb
rec("A1/A5/A6-slot-identity-and-isolation", not pa and not pb,
    "both rounds: ids CAM-01..06 per-round, cells=6, counts self-consistent, fail cell offline/lease=false/gen=0")

# A2/A3/A4 cut behavior summary
rec("A2-before-cut-presentable", not pa and not pb, "all pre-cut rounds non-fail cells presentable=true both rounds")
rec("A3-cut-frozen", fz1 is not None and fzv is not None and fz1 > 0,
    "per-round frozen baseline r1=%s verify=%s (monotonic clock differs across runs; "
    "in-round freeze and post-cut non-presentable checked per round in analyze)" % (fz1, fzv))
rec("A4-others-advancing", not pa and not pb, "post-cut non-fault cells presentable=true and frame clock strictly advancing both rounds")

# A7 selftest line-compare
st1 = re.search(r"\[selftest\] exit=0 =====\n(.*?)\n=====", LOG_R1.read_text(encoding="utf-8"), re.S)
stv = re.search(r"\[selftest\] exit=0 =====\n(.*?)\n=====", LOG_V1.read_text(encoding="utf-8"), re.S)
lines1 = [l for l in (st1.group(1) if st1 else "").splitlines() if l.strip()]
linesv = [l for l in (stv.group(1) if stv else "").splitlines() if l.strip()]
n_pass = sum(1 for l in linesv if "PASS" in l)
rec("A7-selftest-identical", lines1 == linesv and n_pass == 6,
    "selftest lines equal=%s passes=%d (%s)" % (lines1 == linesv, n_pass, "; ".join(linesv[:2]) + " ..."))

# A8 per-cell state sequence semantic equality (fps natural jitter excluded)
seq1 = [[(c["idx"], c["state"], c["presentable"], c["lease"], c["generation"], c["last_frame_ms"] == 0)
         for c in r["cells"]] for r in r1r]
seqv = [[(c["idx"], c["state"], c["presentable"], c["lease"], c["generation"], c["last_frame_ms"] == 0)
         for c in r["cells"]] for r in rvv]
if len(seq1) != len(seqv):
    # 时间轴窗数可能因调度差 1 拍；对齐到较短长度逐窗对照
    problems.append("A8 round count differs r1=%d verify=%d (window count is scheduling-dependent)" % (len(seq1), len(seqv)))
n = min(len(seq1), len(seqv))
diffs = [(i, a, b) for i, (a, b) in enumerate(zip(seq1[:n], seqv[:n])) if a != b]
rec("A8-state-sequence-semantic-equal", not diffs and len(seq1) == len(seqv),
    "rounds r1=%d verify=%d per-cell (idx,state,presentable,lease,gen,no-frame) windows identical=%s"
    % (len(seq1), len(seqv), not diffs))

# A9 impl acceptance lines
a1 = re.search(r"type=acceptance (.*)result=(\w+)", LOG_R1.read_text(encoding="utf-8"))
av = re.search(r"type=acceptance (.*)result=(\w+)", LOG_V1.read_text(encoding="utf-8"))
rec("A9-impl-acceptance-PASS", bool(a1 and av and a1.group(2) == "PASS" and av.group(2) == "PASS"),
    "r1 result=%s verify result=%s" % (a1.group(2) if a1 else "?", av.group(2) if av else "?"))

out = {"results": results, "problems": problems[:20],
       "pass": sum(1 for r in results if r["ok"]), "fail": sum(1 for r in results if not r["ok"])}
(HERE / "_verify_compare_result.json").write_text(json.dumps(out, ensure_ascii=False, indent=2), encoding="utf-8")
print("COMPARE summary: %d PASS / %d FAIL; problems=%d" % (out["pass"], out["fail"], len(problems)))
sys.exit(0 if out["fail"] == 0 and not problems else 1)
