#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-011 验证侧独立解析比对（software_verification，VERIFY-20261017-v1）。

比对对象:
  实现方 r2 归档 docs/video/test_evidence/ITEM-011/rec-…item011-r2.core-evidence_recording-tap(-byte)-…r2.{log,json}
  验证侧复跑 docs/video/test_evidence/ITEM-011/rec-…item011-verify-v1.core-evidence_recording-tap(-byte)-…verify-v1.{log,json}

判据全部独立重推导，不采信交付自身 acceptance 行:
  语义判据（双上限/淘汰计数守恒/慢显示独立/来源显式回落）逐轮独立推导；
  稳定行（source/acceptance/config 有效字段）跨轮逐字段恒等。
"""
import json
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE / "_verify_compare_result.json"

CONFIG_RX = re.compile(
    r"type=config config_pre_seconds=(\d+) effective_pre_seconds=(\d+) max_bytes=(\d+) "
    r"duration_s=(\d+) fps=(\d+) display_pull_ms=(\d+)")
SOURCE_RX = re.compile(
    r"type=source requested=(\w+) nvr_configured=(true|false) effective=(\w+) local=(true|false) "
    r"nvr=(true|false)(?: reason=(\S+))?")
TAP_RX = re.compile(
    r"type=tap_records received=(\d+) expected=(\d+) keyframe_last=(true|false) display_frames=(\d+)")
ROLLING_RX = re.compile(
    r"type=rolling packets=(\d+) bytes=(\d+) first_ts_ms=(-?\d+) last_ts_ms=(-?\d+) span_ms=(-?\d+) "
    r"keyframes=(\d+) pushed=(\d+) evicted_duration=(\d+) evicted_bytes=(\d+)")
ACC_RX = re.compile(
    r"type=acceptance bytes_bounded=(true|false) duration_bounded=(true|false) evicted=(true|false) "
    r"record_rate_ok=(true|false) display_independent=(true|false) source_status_explicit=(true|false) "
    r"result=(\w+)")


def parse_log(path):
    d = {"config": None, "source": None, "tap": None, "rolling": None, "acc": None}
    text = path.read_text(encoding="utf-8", errors="replace")
    m = CONFIG_RX.search(text)
    if m:
        d["config"] = {"config_pre": int(m.group(1)), "effective_pre": int(m.group(2)),
                       "max_bytes": int(m.group(3)), "duration_s": int(m.group(4)),
                       "fps": int(m.group(5)), "display_pull_ms": int(m.group(6))}
    m = SOURCE_RX.search(text)
    if m:
        d["source"] = {"requested": m.group(1), "nvr_configured": m.group(2),
                       "effective": m.group(3), "local": m.group(4), "nvr": m.group(5),
                       "reason": m.group(6) or ""}
    m = TAP_RX.search(text)
    if m:
        d["tap"] = {"received": int(m.group(1)), "expected": int(m.group(2)),
                    "keyframe_last": m.group(3), "display_frames": int(m.group(4))}
    m = ROLLING_RX.search(text)
    if m:
        d["rolling"] = {"packets": int(m.group(1)), "bytes": int(m.group(2)),
                        "first_ts_ms": int(m.group(3)), "last_ts_ms": int(m.group(4)),
                        "span_ms": int(m.group(5)), "keyframes": int(m.group(6)),
                        "pushed": int(m.group(7)), "evicted_duration": int(m.group(8)),
                        "evicted_bytes": int(m.group(9))}
    m = ACC_RX.search(text)
    if m:
        d["acc"] = {"bytes_bounded": m.group(1), "duration_bounded": m.group(2),
                    "evicted": m.group(3), "record_rate_ok": m.group(4),
                    "display_independent": m.group(5), "source_status_explicit": m.group(6),
                    "result": m.group(7)}
    return d


def parse_json_budget(path):
    j = json.loads(path.read_text(encoding="utf-8"))
    return {"records": j.get("records"), "expected": j.get("expected"),
            "bytes": j.get("bytes"), "span_ms": j.get("span_ms"),
            "evicted_duration": j.get("evicted_duration"),
            "effective_pre_seconds": j.get("effective_pre_seconds"),
            "acceptance": j.get("acceptance")}


def independent_semantics(tag, d, jb, problems):
    """独立重推导的验收判据（不采信 acceptance 行）。"""
    if not (d["config"] and d["source"] and d["tap"] and d["rolling"] and d["acc"]):
        problems.append("%s missing structured lines: %s" % (tag, sorted(k for k, v in d.items() if not v)))
        return
    cfg, src, tap, roll = d["config"], d["source"], d["tap"], d["rolling"]

    # (1) 双上限: bytes <= max_bytes, span <= effective_pre*1000 (+1s 探针容差, 独立收紧到 +100ms)
    if roll["bytes"] > cfg["max_bytes"]:
        problems.append("%s bytes %d > max %d" % (tag, roll["bytes"], cfg["max_bytes"]))
    if roll["span_ms"] > cfg["effective_pre"] * 1000 + 100:
        problems.append("%s span %d > pre %d (+100ms)" % (tag, roll["span_ms"], cfg["effective_pre"]))

    # (2) 淘汰守恒: pushed == packets + evicted_duration + evicted_bytes
    if roll["pushed"] != roll["packets"] + roll["evicted_duration"] + roll["evicted_bytes"]:
        problems.append("%s eviction conservation broken: %s" % (tag, roll))

    # (3) 阶段绑定: 时长绑定阶段 dur>=pre 时必有 evicted_duration>0；字节绑定阶段(last_bytes<max)必有 evicted_bytes>0
    #     阶段身份由档案参数 max_bytes 判别: 1 MiB 档=时长绑定, 200 KiB 档=字节绑定
    if cfg["max_bytes"] == 1048576:
        if roll["evicted_duration"] <= 0:
            problems.append("%s duration-bound phase evicted_duration=%d" % (tag, roll["evicted_duration"]))
        if roll["evicted_bytes"] != 0:
            problems.append("%s duration-bound phase unexpected evicted_bytes=%d" % (tag, roll["evicted_bytes"]))
        if roll["span_ms"] < cfg["effective_pre"] * 1000 - 100:
            problems.append("%s duration-bound phase span %d below window" % (tag, roll["span_ms"]))
    elif cfg["max_bytes"] == 204800:
        if roll["evicted_bytes"] <= 0:
            problems.append("%s byte-bound phase evicted_bytes=%d" % (tag, roll["evicted_bytes"]))
        if roll["bytes"] > cfg["max_bytes"] * 0.98:
            problems.append("%s byte-bound phase bytes=%d too close to cap %d" % (tag, roll["bytes"], cfg["max_bytes"]))
    else:
        problems.append("%s unexpected max_bytes=%d" % (tag, cfg["max_bytes"]))

    # (4) 录像率: received >= 85% expected（源 25fps）
    if tap["received"] < tap["expected"] * 85 // 100:
        problems.append("%s received %d < 85%% of %d" % (tag, tap["received"], tap["expected"]))

    # (5) 独立性: 慢显示(display_pull=200ms → 期望 ~duration/0.2=40) 远小于录像包数
    if not (tap["display_frames"] < tap["received"] // 3):
        problems.append("%s display %d not much less than records %d" % (tag, tap["display_frames"], tap["received"]))

    # (6) 来源显式: requested=both + nvr_configured=false -> effective=local_fallback + reason 非空
    if not (src["requested"] == "both" and src["nvr_configured"] == "false"
            and src["effective"] == "local_fallback" and src["reason"] == "NVR_UNCONFIGURED"):
        problems.append("%s source fallback not explicit: %s" % (tag, src))
    if not (src["local"] == "true" and src["nvr"] == "false"):
        problems.append("%s source flags: %s" % (tag, src))

    # (7) JSON 与结构化行交叉核验
    if jb:
        xkeys = [("records", tap["received"]), ("expected", tap["expected"]),
                 ("bytes", roll["bytes"]), ("span_ms", roll["span_ms"]),
                 ("evicted_duration", roll["evicted_duration"])]
        for k, v in xkeys:
            if jb.get(k) != v:
                problems.append("%s json/log mismatch %s: json=%s log=%d" % (tag, k, jb.get(k), v))
        if jb.get("acceptance") is not True:
            problems.append("%s json acceptance flag not true" % tag)


def main():
    problems = []
    checks = []

    def mark(name, ok, detail=""):
        checks.append({"check": name, "ok": bool(ok), "detail": detail})
        print("%s %s %s" % (name, "PASS" if ok else "FAIL", detail))

    impl_r2 = {"dur": parse_log(HERE / "rec-192_168_0_103-20261017-item011-r2.core-evidence_recording-tap-20261017-item011-r2.log"),
               "byte": parse_log(HERE / "rec-192_168_0_103-20261017-item011-r2.core-evidence_recording-tap-byte-20261017-item011-r2.log"),
               "jdur": parse_json_budget(HERE / "rec-192_168_0_103-20261017-item011-r2.core-evidence_recording-tap-20261017-item011-r2.json"),
               "jbyte": parse_json_budget(HERE / "rec-192_168_0_103-20261017-item011-r2.core-evidence_recording-tap-byte-20261017-item011-r2.json")}
    ver_v1 = {"dur": parse_log(HERE / "rec-192_168_0_103-20261017-item011-verify-v1.core-evidence_recording-tap-20261017-item011-verify-v1.log"),
              "byte": parse_log(HERE / "rec-192_168_0_103-20261017-item011-verify-v1.core-evidence_recording-tap-byte-20261017-item011-verify-v1.log"),
              "jdur": parse_json_budget(HERE / "rec-192_168_0_103-20261017-item011-verify-v1.core-evidence_recording-tap-20261017-item011-verify-v1.json"),
              "jbyte": parse_json_budget(HERE / "rec-192_168_0_103-20261017-item011-verify-v1.core-evidence_recording-tap-byte-20261017-item011-verify-v1.json")}

    # 自有独立判据（双阶段）
    for tag, d, jb in (("impl-r2-dur", impl_r2["dur"], impl_r2["jdur"]),
                       ("impl-r2-byte", impl_r2["byte"], impl_r2["jbyte"]),
                       ("verify-v1-dur", ver_v1["dur"], ver_v1["jdur"]),
                       ("verify-v1-byte", ver_v1["byte"], ver_v1["jbyte"])):
        pre = len(problems)
        independent_semantics(tag, d, jb, problems)
        mark("semantics_" + tag, len(problems) == pre, "; ".join(problems[pre:]) or "independent re-derivation holds")

    # 稳定行跨轮恒等（config 有效字段/source/acceptance）—— duration 8/fps 25/pull 200/pre 3 稳定参数
    for phase in ("dur", "byte"):
        a, b = impl_r2[phase], ver_v1[phase]
        stable = all(a[k] == b[k] for k in ("source", "acc"))
        cf_ok = (a["config"] and b["config"] and
                 all(a["config"][k] == b["config"][k] for k in ("config_pre", "effective_pre", "max_bytes", "duration_s", "fps", "display_pull_ms")))
        mark("stable_source_acc_%s" % phase, stable,
             "SOURCE: %s" % (a["source"] == b["source"]))
        mark("stable_config_%s" % phase, bool(cf_ok), "config fields identical across rounds")

    # 接收量波动窗: 合成源 25fps·8s=200 包, 双轮 received 差 ≤ 8 (管线启动/停机边界容差)
    for phase in ("dur", "byte"):
        ra, rb = impl_r2[phase]["tap"]["received"], ver_v1[phase]["tap"]["received"]
        mark("received_window_%s" % phase,
             abs(ra - rb) <= 8, "impl=%d verify=%d (|diff|=%d <= 8)" % (ra, rb, abs(ra - rb)))

    result = {"checks": checks, "problems": problems,
              "pass": all(c["ok"] for c in checks)}
    OUT.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print("SUMMARY", "PASS" if result["pass"] else "FAIL", "checks=%d" % len(checks))
    return 0 if result["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
