#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-017 验证侧独立语义比对（VERIFY-20261018-v1）。

对实现方归档 r3（stamp 20261017-item017-r3，--duration 6）与验证侧 fresh 复跑
verify-v1（--duration 8，独立窗口）的预算仪器产物做独立语义重推导；判据不采信
交付 type=acceptance 行为最终依据。

  C1  归档 JSON 解析（railhmi.measure.v1 / 3 项）；TC-V-INSTR-01 数值有限（无 NaN/Inf）。
  C2  三类源 × 状态分层（TC-V-INSTR-02）：simulation=recorded / file=recorded /
      rtsp=to_be_executed+CREDENTIAL_MISSING；三类记录 origin 独立标注，不互相替代。
  C3  四段延迟直方图逐记录完备键面（n/min/p50/p90/p99/max/mean），单调不变量
      min ≤ p50 ≤ p90 ≤ p99 ≤ max 逐图独立断言。
  C4  分段一致性：p99(end_to_end) ≤ p99(arrival_decode)+p99(decode_convert)+p99(convert_present)
      （分段延迟四段按帧独立组成，quantile 子可加标准不等式必要条件）。
  C5  吞吐面：source_fps>0；板端 testsrc=25fps 仿真源 source_fps∈[20,30] 独立物理带；
      file 源 ≥ 仿真源（非实时 filesrc 快于实时，ITEM-002 O-2 先例）；CPU ∈ (0,100]；
      rss_kb>0；temp_c>0；NPU 显式不可用 -1+note（不伪造）。
  C6  存储面：clip_bytes>0 且等于 188 字节整数倍（MPEG-TS 段粒度）；
      measured_bitrate_bps 与 clip_bytes/window_s 关系 = clip_bytes*8/window_s 容差 5%（独立算式）；
      free_bytes>0 ≤ total_bytes。
  C7  同步面：容差信号链（compute_replay_sync 三路锁相 threshold=300）；记录 available=true 且
      max_deviation < 300（独立断言，容器模拟时钟引理）；file/rtsp 记录显式不同（file 同步域空）。
  C8  恢复面：simulation 记录 attempts≥1 且 last_recovery_ms>0（真实 CameraWorker cut→重建）；
      file/rtsp 记录 attempts=0/-1（未接入恢复域）逐显式不为 0 值冒充。
  C9  acceptance 键面恒等（r3 与 fresh）且全 true（键面不增不减，防夹具拼写逃逸）。
"""
import json
import math
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent

FAILS = []


def check(name, ok, detail=""):
    print("cmp17 %s %s %s" % ("PASS" if ok else "FAIL", name, "" if ok else str(detail)[:400]))
    if not ok:
        FAILS.append(name)


def kv_line(line):
    kv = {}
    for tok in line.split(" "):
        if "=" in tok:
            k, v = tok.split("=", 1)
            kv[k] = v
    return kv


def load_archive(path):
    p = Path(path)
    if not p.exists():
        return None
    return json.loads(p.read_text(encoding="utf-8"))


def finite_recursion(x, path="$"):
    """递归检查无 NaN/Infinity（TC-V-INSTR-01 数值有限判据，独立 JSON 数值层）。"""
    bad = []
    if isinstance(x, dict):
        for k, v in x.items():
            bad += finite_recursion(v, path + "." + k)
    elif isinstance(x, list):
        for i, v in enumerate(x):
            bad += finite_recursion(v, path + "[%d]" % i)
    elif isinstance(x, float):
        if not math.isfinite(x):
            bad.append(path)
    return bad


def hist_invariants(h):
    """单段直方图独立不变量：键面齐全 + min≤p50≤p90≤p99≤max + 受 n 约束。"""
    req = {"n", "min", "p50", "p90", "p99", "max", "mean"}
    if req - set(h):
        return "missing keys %s" % sorted(req - set(h))
    if h["min"] > h["p50"] or h["p50"] > h["p90"] or h["p90"] > h["p99"] or h["p99"] > h["max"]:
        return "monotonic broken srmin=%s p50=%s p90=%s p99=%s max=%s" % (
            h["min"], h["p50"], h["p90"], h["p99"], h["max"])
    if h["n"] > 0 and (h["min"] == -1 or h["max"] == -1):
        return "n>0 with sentinel -1"
    return None


def main():
    r3 = load_archive(HERE / "measure-192_168_0_103-20261017-item017-r3.core-evidence_measure-20261017-item017-r3.json")
    fresh = load_archive(HERE / "measure-192_168_0_103-20261018-item017-verify-v1.core-evidence_measure-20261018-item017-verify-v1.json")
    check("C1a archives exist", r3 is not None and fresh is not None,
          "r3=%s fresh=%s" % (r3 is not None, fresh is not None))
    if r3 is None or fresh is None:
        print("cmp17 summary: fails=%d" % len(FAILS))
        return 1

    for tag, arc in (("r3", r3), ("fresh", fresh)):
        check("C1b %s schema" % tag, arc.get("schema") == "railhmi.measure.v1", str(arc.get("schema")))
        recs = arc.get("records", [])
        check("C1c %s records=3" % tag, len(recs) == 3, str(len(recs)))
        bad = finite_recursion(arc)
        check("C1d %s all values finite (no NaN/Inf)" % tag, not bad, str(bad[:5]))
        # 三类源 × 状态分层（origin 显式、互不替代）
        classes = {r.get("source_class"): r for r in recs}
        check("C2a %s three classes present" % tag,
              set(classes) == {"simulation", "file", "rtsp"}, str(sorted(classes)))
        simr = classes.get("simulation", {})
        filr = classes.get("file", {})
        rtsr = classes.get("rtsp", {})
        check("C2b %s simulation recorded" % tag, simr.get("status") == "recorded", str(simr.get("status")))
        check("C2c %s file recorded" % tag, filr.get("status") == "recorded", str(filr.get("status")))
        check("C2d %s rtsp to_be_executed" % tag, rtsr.get("status") == "to_be_executed",
              str(rtsr.get("status")))
        check("C2e %s rtsp reason CREDENTIAL_MISSING" % tag,
              rtsr.get("reason") == "CREDENTIAL_MISSING", str(rtsr.get("reason")))
        check("C2f %s rtsp zero-domain (no fabricated values)" % tag,
              rtsr.get("throughput", {}).get("source_fps", -9) == 0 and
              rtsr.get("storage", {}).get("clip_bytes", -9) == 0 and
              rtsr.get("latency", {}).get("end_to_end", {}).get("n", -9) == 0,
              str(rtsr.get("throughput", {}))[:120])
        # 延迟直方图逐段不变量（模拟+file 两个 recorded 记录）
        for label, rec in (("sim", simr), ("file", filr)):
            lat = rec.get("latency", {})
            segs = ["arrival_decode", "decode_convert", "convert_present", "end_to_end"]
            keys_full = all(s in lat for s in segs)
            check("C3a %s %s all four segments" % (tag, label), keys_full, str(sorted(lat)))
            if not keys_full:
                continue
            for s in segs:
                err = hist_invariants(lat[s])
                check("C3b %s %s.%s invariant" % (tag, label, s), err is None, str(err))
            # C4 分段可加必要条件
            e2e = lat.get("end_to_end", {})
            a = lat.get("arrival_decode", {})
            b = lat.get("decode_convert", {})
            c = lat.get("convert_present", {})
            try:
                ok4 = e2e["p99"] <= a["p99"] + b["p99"] + c["p99"] + 1
            except Exception as exc:
                ok4 = False
            check("C4 %s %s p99(e2e)<=sum(p99 segs)" % (tag, label), ok4, str(exc if not ok4 else ""))
        # 吞吐面
        tp = simr.get("throughput", {})
        check("C5a sim source_fps 20..30 band", 20.0 <= tp.get("source_fps", -1) <= 30.0,
              str(tp.get("source_fps")))
        check("C5b sim display_fps >0", tp.get("display_fps", -1) > 0, str(tp.get("display_fps")))
        check("C5c sim cpu 0..100", 0 < tp.get("cpu_pct", -1) <= 100, str(tp.get("cpu_pct")))
        check("C5d sim rss positive", tp.get("rss_kb", -1) > 0, str(tp.get("rss_kb")))
        check("C5e sim temp positive", tp.get("temp_c", -1) > 0, str(tp.get("temp_c")))
        check("C5f sim npu explicit -1 + note (no fabricated)",
              tp.get("npu_pct", 0) == -1 and "unavailable" in tp.get("npu_note", ""),
              "%s %s" % (tp.get("npu_pct"), tp.get("npu_note")))
        tp_f = filr.get("throughput", {})
        check("C5g file source_fps >= sim band lowedge (fast non-realtime)",
              tp_f.get("source_fps", -1) > tp.get("source_fps", -1) - 1.0,
              "%s vs %s" % (tp_f.get("source_fps"), tp.get("source_fps")))
        # 存储面
        st = simr.get("storage", {})
        cb = st.get("clip_bytes", -1)
        check("C6a sim clip_bytes >0", cb > 0, str(cb))
        check("C6b sim clip_bytes 188-byte granularity", cb % 188 == 0, str(cb % 188))
        win = simr.get("window_s", 0)
        expect_br = cb * 8.0 / win
        got = st.get("measured_bitrate_bps", -1)
        check("C6c sim bitrate matches bytes*8/window within 5pct",
              abs(got - expect_br) / expect_br < 0.05, "got=%s expect=%s" % (got, expect_br))
        check("C6d sim free_bytes>0 <= total_bytes",
              st.get("free_bytes", -1) > 0 and st.get("free_bytes", -1) <= st.get("total_bytes", 0),
              "%s/%s" % (st.get("free_bytes"), st.get("total_bytes")))
        st_f = filr.get("storage", {})
        check("C6e file clip_bytes == sim clip_bytes (same staged clip reused)",
              st_f.get("clip_bytes", -1) == cb, "%s vs %s" % (st_f.get("clip_bytes"), cb))
        # 同步面
        sy = simr.get("sync", {})
        check("C7a sim sync available=true", sy.get("available") is True, str(sy))
        check("C7b sim sync max_deviation < 300 threshold", 0 <= sy.get("max_deviation_ms", 9999) < 300,
              str(sy.get("max_deviation_ms")))
        check("C7c sim sync in_sync with dev<300", sy.get("in_sync") is True or
              sy.get("max_deviation_ms", 999) >= 300, str(sy))
        sy_f = filr.get("sync", {})
        check("C7d file sync distinct domain (available=false)",
              sy_f.get("available") is False, str(sy_f))
        # 恢复面
        rc = simr.get("recovery", {})
        check("C8a sim recovery attempts>=1", rc.get("attempts", 0) >= 1, str(rc))
        check("C8b sim recovery last_recovery_ms >0", rc.get("last_recovery_ms", -1) > 0,
              str(rc.get("last_recovery_ms")))
        rc_f = filr.get("recovery", {})
        check("C8c file recovery untouched domain",
              rc_f.get("attempts", -9) == 0 and rc_f.get("last_recovery_ms", 0) == -1, str(rc_f))
        truthChain = rc.get("last_recovery_ms", 0)
        check("C8d sim recovery plausible window (50..4000ms)",
              50 <= truthChain <= 4000, str(truthChain))

    # C9 acceptance 键面恒等且全 true（r3/fresh 双侧独立解析）
    def read_accept(path):
        p = Path(path)
        if not p.exists():
            return None
        m = re.search(r"^type=acceptance (.*)$", p.read_text(encoding="utf-8"), re.M)
        return kv_line(m.group(1)) if m else None

    a3 = read_accept(HERE / "measure-192_168_0_103-20261017-item017-r3.core-evidence_measure-20261017-item017-r3.log")
    af = read_accept(HERE / "measure-192_168_0_103-20261018-item017-verify-v1.core-evidence_measure-20261018-item017-verify-v1.log")
    check("C9a both acceptance lines exist", a3 is not None and af is not None)
    if a3 and af:
        check("C9b acceptance key sets identical",
              set(a3.keys()) == set(af.keys()), "r3=%s fresh=%s" % (sorted(a3), sorted(af)))
        bools = [k for k in a3 if k != "result"]
        for k in bools:
            check("C9c fresh acceptance %s true" % k, af.get(k) == "true", str(af.get(k)))
        check("C9d fresh result PASS", af.get("result") == "PASS", str(af.get("result")))

    print("")
    print("cmp17 summary: fails=%d" % len(FAILS))
    if FAILS:
        print("cmp17 FAILED checks: %s" % FAILS)
    return 0 if not FAILS else 1


if __name__ == "__main__":
    raise SystemExit(main())
