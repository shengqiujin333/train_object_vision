#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-010 验证侧独立解析比对（software_verification，VERIFY-20261017-v1）。

比对对象:
  实现方归档 docs/video/test_evidence/ITEM-010/ai-…item010-r2.core-evidence_ai-dispatch-…r2.log
  验证侧复跑 docs/video/test_evidence/ITEM-010/ai-…item010-verify-v1.core-evidence_ai-dispatch-…verify-v1.log

判据全部独立重推导，不采信交付自身 acceptance 行；结构化逐判据对齐实现方/复跑两轮：
  逐拍窗 (enq,drop,skip,rej,depth) 语义序列 + summary 行 + popped 身份 + acceptance 行交叉核验。
"""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent

INJ_RX = re.compile(
    r"type=injection source=(\S+) channels=(\d+) started=(\d+) "
    r"inference_interval=(\d+) queue_depth=(\d+) consume_hz=([\d.]+)")
TICK_RX = re.compile(
    r"t=(\d+) type=ai \[CAM-01 enq=(\d+) drop=(\d+) skip=(\d+) rej=(\d+) depth=(\d+)\] "
    r"\[CAM-02 enq=(\d+) drop=(\d+) skip=(\d+) rej=(\d+) depth=(\d+)\] "
    r"\[CAM-03 enq=(\d+) drop=(\d+) skip=(\d+) rej=(\d+) depth=(\d+)\] "
    r"\[CAM-04 enq=(\d+) drop=(\d+) skip=(\d+) rej=(\d+) depth=(\d+)\] "
    r"\[CAM-05 enq=(\d+) drop=(\d+) skip=(\d+) rej=(\d+) depth=(\d+)\] "
    r"\[CAM-06 enq=(\d+) drop=(\d+) skip=(\d+) rej=(\d+) depth=(\d+)\] popped=(\d+)")
SUM_RX = re.compile(
    r"type=ai_summary camera=(CAM-\d+) enabled=(true|false) published=(\d+) enqueued=(\d+) "
    r"dropped=(\d+) skipped=(\d+) rejected=(\d+) depth=(\d+)")
POP_RX = re.compile(
    r"type=popped camera=(CAM-\d+) session=(\S+) generation=(\d+) seq=(\d+) profile=(\S+) "
    r"capture_mono_us=(\d+)")
ACC_RX = re.compile(
    r"type=acceptance enabled_only_dispatch=(true|false) disabled_never_enqueued=(true|false) "
    r"interval_applied=(true|false) bounded=(true|false) overflow_counted=(true|false) "
    r"identity_present=(true|false) result=(\w+)")
ENABLED = {"CAM-01", "CAM-04", "CAM-05", "CAM-06"}
PROFILES = {"CAM-01": "front", "CAM-04": "pantograph", "CAM-05": "wheel_rail", "CAM-06": "cab_door"}


def parse_log(text):
    d = {"inj": None, "ticks": [], "summary": {}, "popped": None, "acc": None}
    for line in text.splitlines():
        m = TICK_RX.search(line)
        if m:
            g = [int(x) for x in m.groups()]
            tick = {"t": g[0], "popped": g[-1]}
            for i, cam in enumerate(["CAM-01", "CAM-02", "CAM-03", "CAM-04", "CAM-05", "CAM-06"]):
                b = 1 + i * 5
                tick[cam] = {"enq": g[b], "drop": g[b + 1], "skip": g[b + 2],
                             "rej": g[b + 3], "depth": g[b + 4]}
            d["ticks"].append(tick)
            continue
        m = SUM_RX.search(line)
        if m:
            d["summary"][m.group(1)] = {
                "enabled": m.group(2), "published": int(m.group(3)), "enq": int(m.group(4)),
                "drop": int(m.group(5)), "skip": int(m.group(6)), "rejected": int(m.group(7)),
                "depth": int(m.group(8))}
            continue
        m = POP_RX.search(line)
        if m:
            d["popped"] = {"camera": m.group(1), "session": m.group(2), "generation": int(m.group(3)),
                           "seq": int(m.group(4)), "profile": m.group(5),
                           "capture_mono_us": int(m.group(6))}
            continue
        m = ACC_RX.search(line)
        if m:
            d["acc"] = {"enabled_only_dispatch": m.group(1), "disabled_never_enqueued": m.group(2),
                        "interval_applied": m.group(3), "bounded": m.group(4),
                        "overflow_counted": m.group(5), "identity_present": m.group(6),
                        "result": m.group(7)}
            continue
        m = INJ_RX.search(line)
        if m:
            d["inj"] = {"source": m.group(1), "channels": int(m.group(2)),
                        "started": int(m.group(3)), "interval": int(m.group(4)),
                        "depth": int(m.group(5)), "consume_hz": float(m.group(6))}
    return d


def check_semantics(tag, d, problems):
    """独立重推导判据（fps/t/popped 具体帧为自然波动非判据）。"""
    if not d["inj"]:
        problems.append("%s no injection line" % tag)
        return
    inj = d["inj"]
    if not (inj["source"] == "test" and inj["channels"] == 6 and inj["started"] == 6):
        problems.append("%s injection source/channels/started=%s" % (tag, inj))
    if not (inj["interval"] == 5 and inj["depth"] == 2 and inj["consume_hz"] == 2.0):
        problems.append("%s injection interval/depth/consume=%s" % (tag, inj))
    ticks = d["ticks"]
    if len(ticks) < 10:
        problems.append("%s ticks=%d too few" % (tag, len(ticks)))
        return
    # 逐窗深度上限（有界）
    for tick in ticks:
        for cam in ("CAM-01", "CAM-02", "CAM-03", "CAM-04", "CAM-05", "CAM-06"):
            if tick[cam]["depth"] > inj["depth"]:
                problems.append("%s tick t=%d %s depth=%d>%d"
                                % (tag, tick["t"], cam, tick[cam]["depth"], inj["depth"]))
                break
    # 启用路: enq/skip/drop 单调非降；禁用路: rej 单调非降 enq==0
    for cam in ("CAM-01", "CAM-02", "CAM-03", "CAM-04", "CAM-05", "CAM-06"):
        keys = ["enq", "skip", "drop"] if cam in ENABLED else ["rej"]
        for key in keys:
            vals = [tick[cam][key] for tick in ticks]
            if any(vals[i + 1] < vals[i] for i in range(len(vals) - 1)):
                problems.append("%s %s %s not monotone" % (tag, cam, key))
        if cam not in ENABLED:
            for tick in ticks:
                if tick[cam]["enq"] != 0:
                    problems.append("%s disabled %s enq!=0 t=%d" % (tag, cam, tick["t"]))
                    break
    # summary 独立重算（不采信行内自洽）
    if len(d["summary"]) != 6:
        problems.append("%s summaries=%d != 6" % (tag, len(d["summary"])))
        return
    pub_set = set(s["published"] for s in d["summary"].values())
    if len(pub_set) != 1:
        problems.append("%s published not uniform: %s" % (tag, pub_set))
        return
    published = pub_set.pop()
    expect_enq = published // inj["interval"]
    for cam, s in sorted(d["summary"].items()):
        is_en = cam in ENABLED
        if s["enabled"] != ("true" if is_en else "false"):
            problems.append("%s %s enabled flag=%s" % (tag, cam, s["enabled"]))
        if is_en:
            # enq≈published/interval（±2 边界：采样窗/队列剔除时序差）
            if not (s["enq"] + 2 <= expect_enq <= s["enq"] + 2 or abs(s["enq"] - expect_enq) <= 2):
                problems.append("%s %s enq=%d expect~%d" % (tag, cam, s["enq"], expect_enq))
            if s["enq"] + s["skip"] > published:
                problems.append("%s %s enq+skip=%d>published=%d"
                                % (tag, cam, s["enq"] + s["skip"], published))
            if s["rejected"] != 0:
                problems.append("%s %s rejected!=0" % (tag, cam))
            if s["drop"] == 0:
                problems.append("%s %s no overflow counted (slow consumer)" % (tag, cam))
        else:
            if s["enq"] != 0 or s["drop"] != 0 or s["skip"] != 0:
                problems.append("%s %s disabled non-zero enq/drop/skip" % (tag, cam))
            if s["rejected"] == 0:
                problems.append("%s %s disabled rejected==0" % (tag, cam))
    # popped 身份可回联
    p = d["popped"]
    if not p:
        problems.append("%s no popped line" % tag)
    else:
        if p["camera"] not in ENABLED:
            problems.append("%s popped camera=%s not ai-enabled" % (tag, p["camera"]))
        if p["profile"] != PROFILES.get(p["camera"]):
            problems.append("%s popped profile=%s expected~%s"
                            % (tag, p["profile"], PROFILES.get(p["camera"])))
        if not p["session"] or p["generation"] < 1 or p["seq"] < 1:
            problems.append("%s popped identity incomplete: %s" % (tag, p))
    # acceptance 交叉核验
    a = d["acc"]
    if not a:
        problems.append("%s no acceptance line" % tag)
    elif a["result"] != "PASS" or any(a[k] != "true" for k in
                                      ("enabled_only_dispatch", "disabled_never_enqueued",
                                       "interval_applied", "bounded", "overflow_counted",
                                       "identity_present")):
        problems.append("%s acceptance=%s" % (tag, a))


def main():
    impl = (HERE / "ai-192_168_0_103-20261017-item010-r2.core-evidence_ai-dispatch-20261017-item010-r2.log").read_text(
        encoding="utf-8", errors="replace")
    ver = (HERE / "ai-192_168_0_103-20261017-item010-verify-v1.core-evidence_ai-dispatch-20261017-item010-verify-v1.log").read_text(
        encoding="utf-8", errors="replace")
    di, dv = parse_log(impl), parse_log(ver)
    problems, checks = [], []

    def ck(ok, name):
        checks.append((name, ok))
        if not ok:
            problems.append("CHECK %s" % name)

    # 1) 两轮各自语义独立判据
    for tag, d in (("impl-r2", di), ("verify-v1", dv)):
        p = []
        check_semantics(tag, d, p)
        ck(not p, "%s independent-semantics" % tag)
        for x in p[:6]:
            problems.append("  %s" % x)

    # 2) 两轮结构一致性（逐拍窗语义序列 (enq,drop,skip,rej,depth,popped)；
    #    t= 时间轴与 popped capture_mono_us 为自然波动排除）
    n = min(len(di["ticks"]), len(dv["ticks"]))
    ck(n >= 10 and abs(len(di["ticks"]) - len(dv["ticks"])) <= 1,
       "tick-count-aligned %d/%d" % (len(di["ticks"]), len(dv["ticks"])))
    def deltas(tick_a, tick_b):
        out = {}
        reorder = False
        for c in ("CAM-01", "CAM-02", "CAM-03", "CAM-04", "CAM-05", "CAM-06"):
            for key in ("enq", "drop", "skip", "rej"):
                out[(c, key)] = tick_b[c][key] - tick_a[c][key]
            if tick_a[c]["depth"] != tick_b[c]["depth"]:
                reorder = True
        return out, reorder
    jitter, hard = [], []
    for k in range(n):
        dlt, reorder = deltas(di["ticks"][k], dv["ticks"][k])
        if reorder:
            hard.append((k, "depth-changed"))
            continue
        mx = max(abs(v) for v in dlt.values())
        if mx == 0:
            continue
        if mx <= 1:
            jitter.append((di["ticks"][k]["t"], dlt))
        else:
            hard.append((k, dlt))
    ck(len(hard) == 0, "tick-semantic-sequence-identical (n=%d windows; %d jitter<=1-frame windows)" % (n, len(jitter)))
    if hard:
        problems.append("first tick hard diffs: %s" % hard[:5])
    if len(jitter) > 3:
        problems.append("tick jitter windows=%d (expected <=3 sampling-boundary windows)" % len(jitter))
    # 3) summary 表逐字段 identical
    ck(di["summary"] == dv["summary"] and len(di["summary"]) == 6, "summary-identical-6-rows")
    # 4) acceptance 行逐字段 identical
    ck(di["acc"] == dv["acc"] and di["acc"] is not None, "acceptance-identical")
    # 5) popped 结构域 identical（session/时刻可自然波动）
    pi, pv = di["popped"], dv["popped"]
    ck(pi is not None and pv is not None and
       pi["camera"] == pv["camera"] and pi["profile"] == pv["profile"] and
       pi["generation"] == pv["generation"], "popped-structure-identical")
    # 6) 汇总 JSON 双轮逐字段 identical（interval/queue_depth/total_enqueued/acceptance）
    #    total_enqueued=131 = summary 32x4=128 + 停机前边界帧 3（双轮同值，登记 O-2 观察项非缺陷）
    import json as _json
    ji = _json.loads((HERE / "ai-192_168_0_103-20261017-item010-r2.core-evidence_ai-dispatch-20261017-item010-r2.json").read_text())
    jv = _json.loads((HERE / "ai-192_168_0_103-20261017-item010-verify-v1.core-evidence_ai-dispatch-20261017-item010-verify-v1.json").read_text())
    ck(ji == jv, "summary-json-identical %s" % (jv,))

    passed = sum(1 for _, ok in checks if ok)
    out = {"checks": [{"name": k, "ok": bool(v)} for k, v in checks],
           "pass": passed, "fail": len(checks) - passed, "problems": problems[:20]}
    import json as _json
    (HERE / "_verify_compare_result.json").write_text(
        _json.dumps(out, ensure_ascii=False, indent=2), encoding="utf-8")
    print("compare: %d/%d checks PASS" % (passed, len(checks)))
    for p in problems:
        print("  PROBLEM:", p)
    return 0 if not problems else 1


if __name__ == "__main__":
    raise SystemExit(main())
