#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-012 验证侧独立解析比对（software_verification，VERIFY-20261017-v1）。

比对对象:
  实现方 r5 归档 docs/video/test_evidence/ITEM-012/clip-…item012-r5.core-evidence_clip-solidify-…r5.{log,json}
  验证侧复跑 docs/video/test_evidence/ITEM-012/clip-…item012-verify-v1.core-evidence_clip-solidify-…verify-v1.{log,json}

判据全部独立重推导，不采信交付自身 acceptance 行:
  语义判据（覆盖数学/缺口区间推导/三态绑定键/幂等/缓冲界）逐轮独立推导；
  稳定行（config 有效字段/acceptance 八旗）跨轮逐字段恒等。
TSMedia: 真实 .ts 片段结构（188B 包/0x47 同步/封装头）在比对面外独立核对。
"""
import json
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE / "_verify_compare_result.json"

CONFIG_RX = re.compile(
    r"type=config config_pre_seconds=(\d+) config_post_seconds=(\d+) effective_pre_seconds=(\d+) "
    r"effective_post_seconds=(\d+) buffer_seconds=(\d+) outdir=(\S+)")

ACC_RX = re.compile(
    r"type=acceptance real_media=(true|false) coverage_matches=(true|false) keyframe_aligned=(true|false) "
    r"gap_recorded=(true|false) availability_tristate=(true|false) idempotent=(true|false) "
    r"publish_after_rename=(true|false) no_stray_temp=(true|false) result=(\w+)")

CASE_KEYS = {
    "complete": ["requested", "actual", "packets", "keyframe_aligned", "availability",
                 "size_bytes", "published", "path", "reason"],
    "incomplete": ["requested", "actual", "availability", "has_gap", "gap_reason", "gap"],
    "unavailable": ["packets", "availability", "has_gap", "reason"],
    "mux_failed": ["availability", "has_gap", "reason"],
}


def parse_log(path):
    d = {"config": None, "acc": None, "cases": {}, "idem": None, "buffer": None}
    text = path.read_text(encoding="utf-8", errors="replace")
    m = CONFIG_RX.search(text)
    if m:
        d["config"] = {"config_pre": int(m.group(1)), "config_post": int(m.group(2)),
                       "eff_pre": int(m.group(3)), "eff_post": int(m.group(4)),
                       "buffer_s": int(m.group(5)), "outdir": m.group(6)}
    m = ACC_RX.search(text)
    if m:
        d["acc"] = {"real_media": m.group(1), "coverage_matches": m.group(2),
                    "keyframe_aligned": m.group(3), "gap_recorded": m.group(4),
                    "availability_tristate": m.group(5), "idempotent": m.group(6),
                    "publish_after_rename": m.group(7), "no_stray_temp": m.group(8),
                    "result": m.group(9)}
    for case in CASE_KEYS:
        m = re.search(r"^type=clip case=%s (.*)$" % case, text, re.M)
        if m:
            d["cases"][case] = m.group(1)
    m = re.search(r"^type=idempotency (.*)$", text, re.M)
    if m:
        d["idem"] = dict(re.findall(r"(\w+)=([A-Za-z]+)", m.group(1)))
    m = re.search(r"^type=buffer packets=(\d+) span_ms=(\d+) last_ts_ms=(-?\d+) "
                  r"display_frames=(\d+) trigger_ts_ms=(-?\d+)", text, re.M)
    if m:
        d["buffer"] = {"packets": int(m.group(1)), "span_ms": int(m.group(2)),
                       "last_ts_ms": int(m.group(3)), "display_frames": int(m.group(4)),
                       "trigger_ts_ms": int(m.group(5))}
    return d


def parse_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def independent_semantics(tag, d, jb, pre, post, problems):
    """独立重推导判据。probe 参数 pre/post 由 stamp 外部注入（缺省 3/2）。"""
    if not (d["config"] and d["acc"] and d["cases"] and d["idem"] and d["buffer"]):
        problems.append("%s missing structured lines: %s" %
                        (tag, [k for k, v in d.items() if not v]))
        return
    cfg = d["config"]
    if not (cfg["config_pre"] == 30 and cfg["config_post"] == 60 and
            cfg["eff_pre"] == pre and cfg["eff_post"] == post and
            cfg["buffer_s"] == pre + post + 2):
        problems.append("%s config echo mismatch: %s (expect eff %d/%d buffer %d)" %
                        (tag, cfg, pre, post, pre + post + 2))

    # idempotency
    idem = d["idem"]
    if not (idem.get("first") == "added" and idem.get("repeat") == "duplicate" and
            idem.get("different_params") == "conflict"):
        problems.append("%s idempotency chain wrong: %s" % (tag, idem))

    # 天然键集对齐: each case line keys (含窗口/availability/gap)
    for case, tail in d["cases"].items():
        keys = set(re.findall(r"(\w+)=", "type=clip case=%s %s" % (case, tail)))
        expect = set(CASE_KEYS[case]) | {"type", "case"}
        if keys != expect:
            problems.append("%s case=%s key mismatch: %s vs %s" %
                            (tag, case, sorted(keys), sorted(expect)))
    c = d["cases"]
    if "availability=complete" not in c.get("complete", ""):
        problems.append("%s complete availability: %s" % (tag, c.get("complete")))
    if "keyframe_aligned=true" not in c.get("complete", ""):
        problems.append("%s complete keyframe_aligned not true: %s" % (tag, c.get("complete")))
    if not c.get("complete", "").rstrip().endswith("reason="):
        problems.append("%s complete reason not empty" % tag)
    if not ("availability=incomplete" in c.get("incomplete", "") and
            "gap_reason=POST_ROLL_SHORTAGE" in c.get("incomplete", "")):
        problems.append("%s incomplete row: %s" % (tag, c.get("incomplete")))
    if not ("availability=unavailable" in c.get("unavailable", "") and
            "reason=NO_MEDIA" in c.get("unavailable", "")):
        problems.append("%s unavailable row: %s" % (tag, c.get("unavailable")))
    if not ("availability=unavailable" in c.get("mux_failed", "") and
            "reason=MKDIR_FAILED" in c.get("mux_failed", "")):
        problems.append("%s mux_failed row: %s" % (tag, c.get("mux_failed")))

    # 覆盖数学（complete 窗口 = [trig-pre, trig+post)）
    trig = d["buffer"]["trigger_ts_ms"]
    m = re.search(r"requested=\[(-?\d+),(-?\d+)\)", c.get("complete", ""))
    if not m:
        problems.append("%s complete requested missing" % tag)
        return
    rt0, rt1 = int(m.group(1)), int(m.group(2))
    if not (rt0 == trig - pre * 1000 and rt1 == trig + post * 1000):
        problems.append("%s complete requested window [%d,%d) != [%d,%d)" %
                        (tag, rt0, rt1, trig - pre * 1000, trig + post * 1000))
    m = re.search(r"actual=\[(-?\d+),(-?\d+)\]", c.get("complete", ""))
    if not m:
        problems.append("%s complete actual missing" % tag)
        return
    at0, at1 = int(m.group(1)), int(m.group(2))
    if at0 > rt0 + 1000:
        problems.append("%s complete actual_t0 %d beyond tolerance %d > %d" %
                        (tag, at0, at0, rt0 + 1000))
    if at1 < rt1 - 1000:
        problems.append("%s complete actual_t1 %d below tolerance %d < %d" %
                        (tag, at1, at1, rt1 - 1000))

    # incomplete 缺口数学: gap=[actual_t1, req_t1)
    m = re.search(r"actual=\[(-?\d+),(-?\d+)\] availability", c.get("incomplete", ""))
    if m:
        iat1 = int(m.group(2))
    else:
        problems.append("%s incomplete actual missing" % tag)
        iat1 = None
    m = re.search(r"requested=\[(-?\d+),(-?\d+)\)", c.get("incomplete", ""))
    if m:
        irt1 = int(m.group(2))
    else:
        problems.append("%s incomplete requested missing" % tag)
        irt1 = None
    m = re.search(r"gap=\[(-?\d+),(-?\d+)\)", c.get("incomplete", ""))
    if not m:
        problems.append("%s incomplete gap missing" % tag)
    elif iat1 is not None and irt1 is not None:
        if int(m.group(1)) != iat1 or int(m.group(2)) != irt1:
            problems.append("%s incomplete gap [%s,%s) ≠ [actual_t1=%d,req_t1=%d)" %
                            (tag, m.group(1), m.group(2), iat1, irt1))

    # buffer: span <= buffer_s*1000
    buf = d["buffer"]
    if buf["span_ms"] > cfg["buffer_s"] * 1000 + 100:
        problems.append("%s buffer span %d > %d" % (tag, buf["span_ms"], cfg["buffer_s"] * 1000))
    if not (0 < buf["display_frames"] < buf["packets"]):
        problems.append("%s buffer display vs packets: %s" % (tag, buf))

    # acceptance 八旗全真
    acc = d["acc"]
    for k in ("real_media", "coverage_matches", "keyframe_aligned", "gap_recorded",
              "availability_tristate", "idempotent", "publish_after_rename", "no_stray_temp"):
        if acc[k] != "true":
            problems.append("%s acceptance %s=%s" % (tag, k, acc[k]))
    if acc["result"] != "PASS":
        problems.append("%s acceptance result=%s" % (tag, acc["result"]))

    # JSON/日志交叉（complete 行三字段）
    if jb is not None:
        cp = jb.get("clips", [{}])[0]
        if cp.get("availability") != "complete" or cp.get("published") is not True:
            problems.append("%s json complete tri/published: %s" % (tag, cp))
        if cp.get("size_bytes") != int(re.search(r"size_bytes=(\d+)", c["complete"]).group(1)):
            problems.append("%s json size mismatch" % tag)
        if cp.get("actual_t0_ms") != at0 or cp.get("actual_t1_ms") != at1:
            problems.append("%s json actual window mismatch" % tag)
        if jb.get("acceptance") is not True:
            problems.append("%s json acceptance flag not true" % tag)
        nclips = len(jb.get("clips", []))
        if nclips != 3:
            problems.append("%s json clips count %d != 3" % (tag, nclips))


def main():
    import sys
    problems = []
    checks = []

    def mark(name, ok, detail=""):
        checks.append({"check": name, "ok": bool(ok), "detail": detail})
        print("%s %s %s" % (name, "PASS" if ok else "FAIL", detail))

    impl = {
        "log": parse_log(HERE / "clip-192_168_0_103-20261017-item012-r5.core-evidence_clip-solidify-20261017-item012-r5.log"),
        "json": parse_json(HERE / "clip-192_168_0_103-20261017-item012-r5.core-evidence_clip-solidify-20261017-item012-r5.json"),
    }
    ver = {
        "log": parse_log(HERE / "clip-192_168_0_103-20261017-item012-verify-v1.core-evidence_clip-solidify-20261017-item012-verify-v1.log"),
        "json": parse_json(HERE / "clip-192_168_0_103-20261017-item012-verify-v1.core-evidence_clip-solidify-20261017-item012-verify-v1.json"),
    }

    for tag, d, jb in (("impl-r5", impl["log"], impl["json"]),
                       ("verify-v1", ver["log"], ver["json"])):
        pre = len(problems)
        independent_semantics(tag, d, jb, 3, 2, problems)
        mark("semantics_" + tag, len(problems) == pre,
             "; ".join(problems[pre:]) or "independent re-derivation holds")

    # 稳定行跨轮恒等: config + idem + acceptance
    a, b = impl["log"], ver["log"]
    for k in ("config", "idem", "acc"):
        mark("stable_%s" % k, a[k] == b[k] and a[k] is not None,
             "%s vs %s" % (str(a[k])[:80], str(b[k])[:80]))

    # 语义域波动窗: packets/received/size 差 <= 15%（x264enc 码率自然波动）
    for key, cond in (("packets", None),):
        pass
    cc, cv = impl["log"]["cases"].get("complete", ""), ver["log"]["cases"].get("complete", "")
    mp = re.search(r"packets=(\d+)", cc)
    mv = re.search(r"packets=(\d+)", cv)
    ms_i = re.search(r"size_bytes=(\d+)", cc)
    ms_v = re.search(r"size_bytes=(\d+)", cv)
    if mp and mv:
        pi, pv = int(mp.group(1)), int(mv.group(1))
        diff = abs(pi - pv)
        mark("packets_window", diff <= max(8, pi // 10),
             "impl=%d verify=%d (|%d|<=win)" % (pi, pv, diff))
    if ms_i and ms_v:
        si, sv = int(ms_i.group(1)), int(ms_v.group(1))
        dmk = abs(si - sv) / max(si, sv)
        mark("size_window", dmk <= 0.15, "impl=%d verify=%d (|Δ%%|=%.3f<=15%%)" % (si, sv, dmk * 100))

    result = {"checks": checks, "problems": problems,
              "pass": all(c["ok"] for c in checks)}
    OUT.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print("SUMMARY", "PASS" if result["pass"] else "FAIL", "checks=%d" % len(checks))
    if problems:
        print("PROBLEMS:")
        for p in problems:
            print(" -", p)
    return 0 if result["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
