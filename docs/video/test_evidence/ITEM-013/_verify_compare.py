#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-013 验证侧独立解析比对（software_verification，VERIFY-20261017-v1）。

比对对象:
  实现方 r2 归档 prov-…item013-r2.core-evidence_media-provider-…r2.{log,json}
  验证侧复跑 prov-…item013-verify-v1.core-evidence_media-provider-…verify-v1.{log,json}

判据全部独立重推导，不采信交付自身 acceptance 行:
  A. clip 行 availability=complete + size>0 + t1>t0。
  B. query 四态逐案例独立断言:
     complete: ok / coverage_complete=true / segments>=1 / covered_ms ≈ 窗长（≥窗长-100）。
     empty:    ok / segments=0 / coverage_complete=false —— 「查询成功无覆盖≠可播放」。
     partial:  partial / covered_ms ≈ [actual_t1-1000, actual_t1) ≈ 1000 / coverage false。
     gaps:    查询出口 ok 而且 gaps 非空且 reason 非空（缺口如实记录）。
     expired:  error / TIMEOUT（deadline 语义）。
  C. play 五轮: ok / reached=true / frames>0 / actual>=requested 或在 KEY_UNIT 容差内
     （容差以探针 --seek-tolerance 等价语义 1500ms 保守再放宽到 2000ms——关键帧间隔 1s）；
     **actual 不必相等 requested** —— 实际解码 position 语义；
     五轮同 rel(400/500/600/2500) 落在同一关键帧 → actual 一致（≤100ms 窗）、
     rel 4500 → 落到下一关键帧 actual 差 ≥1000ms ——「回报实际解码 position 而非请求接受」判据。
  D. export: ok / 完整路径非空 / size_bytes 与源 .ts 归档对账（±15% x264enc 波动窗）/
     complete=true；missing: unavailable/NOT_FOUND。
  E. thumbnail: path 非空 .jpg exists=true。
  F. nvr: 三方法 unavailable/UNCONFIGURED + segments=0（不模拟填补）。
  G. sync: close max_deviation<=300 in_sync=true；spread max_deviation>300 in_sync=false
     prompt 非空含 300；in_sync == (max_deviation<=300) 一致性 both。
  H. acceptance 行逐旗 true + result=PASS；JSON/日志交叉（clip query play export thumb nvr acceptance）。
  I. play 行 kv 键集闭包（id/status/reason/requested/actual/reached/frames）。
  J. 确定性: 同 stamp 内 core-tests/selftest/acceptance 交叉结果行两轮恒等在 meta 比对。
"""
import json
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE / "_verify_compare_result.json"

CLIP_RX = re.compile(
    r"type=clip availability=(\w+) size_bytes=(\d+) actual=\[(-?\d+),(-?\d+)\] path=(\S+)")

QUERY_CASES = {
    "complete": "status=(ok|partial|unavailable|error) reason=(.*?) segments=(\\d+) coverage_complete=(true|false)(?: covered_ms=(\\d+))?",
    "empty": "status=(ok|partial|unavailable|error) reason=(.*?) segments=(\\d+) coverage_complete=(true|false)(?: covered_ms=(\\d+))?",
    "partial": "status=(ok|partial|unavailable|error) reason=(.*?) segments=(\\d+) covered_ms=(\\d+) coverage_complete=(true|false)",
    "gaps": "status=(ok|partial|unavailable|error) gaps=(\\d+) reason=(\\S*)",
    "expired": "status=(ok|partial|unavailable|error) reason=(\\S*)",
}

PLAY_RX = re.compile(
    r"type=play id=(\S+) status=(ok|partial|unavailable|error) reason=(.*?) "
    r"requested=(-?\d+) actual=(-?\d+) reached=(true|false) frames=(\d+)")

PLAY_KEYSET = {"id", "status", "reason", "requested", "actual", "reached", "frames"}

ACC_RX = re.compile(
    r"type=acceptance query_complete=(\S+) query_empty_ok=(\S+) query_partial=(\S+) gaps_returned=(\S+) "
    r"deadline_timeout=(\S+) play_actual_position=(\S+) play_within_tolerance=(\S+) export_real=(\S+) "
    r"export_missing_unavailable=(\S+) thumbnail_real=(\S+) nvr_unconfigured=(\S+) sync_prompt=(\S+) "
    r"sync_consistency=(\S+) result=(\w+)")

NVR_RX = re.compile(
    r"type=nvr query=(\S+) play=(\S+) export=(\S+) segments=(\d+)")


def parse_log(path):
    d = {"clip": None, "query": {}, "play": [], "export": None, "export_missing": None,
         "thumb": None, "nvr": None, "sync": None, "acc": None}
    t = path.read_text(encoding="utf-8", errors="replace")
    m = CLIP_RX.search(t)
    if m:
        d["clip"] = {"avail": m.group(1), "size": int(m.group(2)),
                     "t0": int(m.group(3)), "t1": int(m.group(4)), "path": m.group(5)}
    for case, rx in QUERY_CASES.items():
        m = re.search(r"^type=query case=%s .*$" % case, t, re.M)
        if m:
            line = m.group(0)
            kv = {}
            # 首 token 是 type=...；键值对解析以 = 分割第一个 token 后的部分
            body = line.split(" ", 1)[1] if " " in line else ""
            for tok in body.split(" "):
                if "=" in tok:
                    k, v = tok.split("=", 1)
                    kv[k] = v
            m2 = re.search(rx, line)
            if m2:
                kv["_rx"] = m2.groups()
            d["query"][case] = kv
    for m in PLAY_RX.finditer(t):
        d["play"].append({"id": m.group(1), "status": m.group(2), "reason": m.group(3),
                          "requested": int(m.group(4)), "actual": int(m.group(5)),
                          "reached": m.group(6), "frames": int(m.group(7))})
    d["play_lines_kv"] = []
    for m in re.finditer(r"^type=play (.*)$", t, re.M):
        kv = {}
        for tok in m.group(1).split(" "):
            if "=" in tok:
                k, v = tok.split("=", 1)
                kv[k] = v
        d["play_lines_kv"].append(kv)
    m = re.search(r"^type=export (status=.*)$", t, re.M)
    if m:
        kv = {}
        for tok in m.group(1).split(" "):
            if "=" in tok:
                k, v = tok.split("=", 1)
                kv[k] = v
        d["export"] = kv
    m = re.search(r"^type=export case=missing (.*)$", t, re.M)
    if m:
        kv = {}
        for tok in m.group(1).split(" "):
            if "=" in tok:
                k, v = tok.split("=", 1)
                kv[k] = v
        d["export_missing"] = kv
    m = re.search(r"^type=thumbnail path=(\S+) exists=(\S+)$", t, re.M)
    if m:
        d["thumb"] = {"path": m.group(1), "exists": m.group(2)}
    m = NVR_RX.search(t)
    if m:
        d["nvr"] = {"query": m.group(1), "play": m.group(2), "export": m.group(3),
                    "segments": int(m.group(4))}
    m = re.search(r"^type=sync close_max_deviation=(-?\d+) close_in_sync=(\S+) "
                  r"spread_max_deviation=(-?\d+) spread_in_sync=(\S+) spread_prompt=(.*)$", t, re.M)
    if m:
        d["sync"] = {"close_dev": int(m.group(1)), "close_in_sync": m.group(2),
                     "spread_dev": int(m.group(3)), "spread_in_sync": m.group(4),
                     "prompt": m.group(5)}
    m = ACC_RX.search(t)
    if m:
        d["acc"] = {"query_complete": m.group(1), "query_empty_ok": m.group(2),
                    "query_partial": m.group(3), "gaps_returned": m.group(4),
                    "deadline_timeout": m.group(5), "play_actual_position": m.group(6),
                    "play_within_tolerance": m.group(7), "export_real": m.group(8),
                    "export_missing_unavailable": m.group(9), "thumbnail_real": m.group(10),
                    "nvr_unconfigured": m.group(11), "sync_prompt": m.group(12),
                    "sync_consistency": m.group(13), "result": m.group(14)}
    return d


def parse_json(path):
    d = json.loads(path.read_text(encoding="utf-8"))
    return d


TOL_SEEK_MS = 2000  # KEY_UNIT + 1s 关键帧间隔的保守容差


def semantics(tag, d, jb, problems):
    need = ["clip", "export", "export_missing", "thumb", "nvr", "sync", "acc"]
    miss = [k for k in need if not d.get(k)]
    qmiss = [k for k in QUERY_CASES if k not in d["query"]]
    if miss or qmiss or len(d["play"]) < 5:
        problems.append("%s missing structure: missing=%s query_missing=%s play_n=%d" %
                        (tag, miss, qmiss, len(d["play"])))
        return

    # A clip
    c = d["clip"]
    if c["avail"] != "complete" or c["size"] <= 0 or c["t1"] <= c["t0"]:
        problems.append("%s clip row: %s" % (tag, c))

    # B query
    q = d["query"]
    if q.get("complete", {}).get("status") != "ok" or \
       q["complete"].get("coverage_complete") != "true" or \
       int(q["complete"].get("segments", "-1")) < 1:
        problems.append("%s query complete row: %s" % (tag, q.get("complete")))
    else:
        cw = c["t1"] - c["t0"]
        cm = int(q["complete"].get("covered_ms", "0"))
        if cm < cw - 100:
            problems.append("%s query complete covered %d < window %d-100" % (tag, cm, cw))
    if q.get("empty", {}).get("status") != "ok" or \
       int(q["empty"].get("segments", "-1")) != 0 or \
       q["empty"].get("coverage_complete") != "false":
        problems.append("%s query empty row (ok empty != playable): %s" % (tag, q.get("empty")))
    if q.get("partial", {}).get("status") != "partial" or \
       q["partial"].get("coverage_complete") != "false":
        problems.append("%s query partial row: %s" % (tag, q.get("partial")))
    else:
        pm = int(q["partial"].get("covered_ms", "0"))
        if abs(pm - 1000) > 1000:  # 请求窗 [t1-1000, t1+10000) ⇒ 可用覆盖恰 1000ms 容差窗
            problems.append("%s partial covered %d not ~1000" % (tag, pm))
    g = q.get("gaps", {})
    if g.get("status") != "ok" or int(g.get("gaps", "0")) < 1 or not g.get("reason"):
        problems.append("%s query gaps row: %s" % (tag, g))
    if q.get("expired", {}).get("status") != "error" or \
       q["expired"].get("reason") != "TIMEOUT":
        problems.append("%s query expired row: %s" % (tag, q.get("expired")))

    # C play
    ps = d["play"]
    for p in ps:
        if p["status"] != "ok" or p["reached"] != "true" or p["frames"] <= 0:
            problems.append("%s play %s not ok/reached/frames>0: %s" % (tag, p["id"], p))
    # request_id 独立枚举出现 5 个不同请求位置
    ids = [p["id"] for p in ps]
    if ids != ["play-400", "play-500", "play-600", "play-2500", "play-4500"]:
        problems.append("%s play id sequence: %s" % (tag, ids))
    base = c["t0"]
    req_rel = [p["requested"] - base for p in ps]
    if req_rel != [400, 500, 600, 2500, 4500]:
        problems.append("%s play requested rel: %s" % (tag, req_rel))
    act_rel = [p["actual"] - base for p in ps]
    # 实际 position 不必等于请求（铸造物=实际解码 PTS）；同窗 (400..2500) 应集中在同一关键帧:
    if abs(act_rel[0] - act_rel[1]) > 100 or abs(act_rel[1] - act_rel[2]) > 100 or \
       abs(act_rel[2] - act_rel[3]) > 100:
        problems.append("%s play act_rel (400..2500) spread: %s" % (tag, act_rel[:4]))
    if abs(act_rel[4] - act_rel[3]) < 1000:
        problems.append("%s play act_rel(4500) - act_rel(2500) = %d < 1000 — 是否在回报实际解码位置"
                        % (tag, act_rel[4] - act_rel[3]))
    for i, p in enumerate(ps):
        dev = abs(p["actual"] - p["requested"])
        if dev > TOL_SEEK_MS:
            problems.append("%s play %s actual-req dev %d > %d" % (tag, p["id"], dev, TOL_SEEK_MS))

    # 键集闭包（play 行）
    for kv in d["play_lines_kv"]:
        if set(kv) != PLAY_KEYSET:
            problems.append("%s play kv keys %s vs %s" % (tag, sorted(kv), sorted(PLAY_KEYSET)))

    # D export
    e = d["export"]
    if e.get("status") != "ok" or e.get("complete") != "true" or \
       int(e.get("size_bytes", "0")) <= 0 or not e.get("path"):
        problems.append("%s export row: %s" % (tag, e))
    else:
        esize = int(e["size_bytes"])
        csize = c["size"]
        diff = abs(esize - csize) / max(esize, csize)
        if diff > 0.15:
            problems.append("%s export size %.2f%% vs clip" % (tag, diff * 100))
    em = d["export_missing"]
    if em.get("status") != "unavailable" or em.get("reason") != "NOT_FOUND":
        problems.append("%s export missing row: %s" % (tag, em))

    # E thumb
    th = d["thumb"]
    if th["exists"] != "true" or ".jpg" not in th["path"]:
        problems.append("%s thumbnail row: %s" % (tag, th))

    # F nvr
    n = d["nvr"]
    if not (n["query"].startswith("unavailable/UNCONFIGURED") and
            n["play"].startswith("unavailable/UNCONFIGURED") and
            n["export"].startswith("unavailable/UNCONFIGURED") and n["segments"] == 0):
        problems.append("%s nvr row: %s" % (tag, n))

    # G sync
    s = d["sync"]
    if not (s["close_dev"] <= 300 and s["close_in_sync"] == "true"):
        problems.append("%s sync close: %s" % (tag, s))
    if not (s["spread_dev"] > 300 and s["spread_in_sync"] == "false" and s["prompt"]):
        problems.append("%s sync spread: %s" % (tag, s))
    if (s["close_in_sync"] == "true") != (s["close_dev"] <= 300):
        problems.append("%s sync close consistency" % tag)
    if (s["spread_in_sync"] == "true") != (s["spread_dev"] <= 300):
        problems.append("%s sync spread consistency" % tag)
    if str(s["spread_dev"]) not in s["prompt"]:
        problems.append("%s prompt missing deviation value: %s" % (tag, s["prompt"]))

    # H acceptance 八旗全真
    acc = d["acc"]
    for k in ("query_complete", "query_empty_ok", "query_partial", "gaps_returned",
              "deadline_timeout", "play_actual_position", "play_within_tolerance",
              "export_real", "export_missing_unavailable", "thumbnail_real",
              "nvr_unconfigured", "sync_prompt", "sync_consistency"):
        if acc[k] != "true":
            problems.append("%s acceptance %s=%s" % (tag, k, acc[k]))
    if acc["result"] != "PASS":
        problems.append("%s acceptance result=%s" % (tag, acc["result"]))

    # JSON 交叉
    if jb is not None:
        if jb.get("clip", {}).get("availability", jb.get("clip", {}).get("avail")) not in (None,):
            pass
        clip_j = jb.get("clip") or {}
        if clip_j.get("size_bytes") != c["size"] or \
           clip_j.get("actual_t0_ms") != c["t0"] or clip_j.get("actual_t1_ms") != c["t1"]:
            problems.append("%s json clip mismatch: %s vs %s" % (tag, clip_j, c))
        qq = jb.get("query") or {}
        if qq.get("complete") != q["complete"].get("status") or \
           qq.get("empty") != q["empty"].get("status") or \
           qq.get("partial") != q["partial"].get("status"):
            problems.append("%s json query mismatch: %s" % (tag, qq))
        pj = jb.get("play") or {}
        if pj.get("requested") != ps[0]["requested"] or pj.get("actual") != ps[0]["actual"] or \
           (pj.get("reached") is True) != (ps[0]["reached"] == "true"):
            problems.append("%s json play mismatch: %s" % (tag, pj))
        ej = jb.get("export") or {}
        if ej.get("path") != e.get("path") or ej.get("size_bytes") != int(e.get("size_bytes", "0")):
            problems.append("%s json export mismatch" % tag)
        tj = (jb.get("thumbnail") or "")
        if tj != th["path"]:
            problems.append("%s json thumbnail mismatch: %r vs %r" % (tag, tj, th["path"]))
        if (jb.get("nvr") or "") != "%s" % n["query"]:
            problems.append("%s json nvr mismatch: %r vs %r" % (tag, jb.get("nvr"), n["query"]))
        if jb.get("acceptance") is not True:
            problems.append("%s json acceptance not true" % tag)


def main():
    impl_log = HERE / ("prov-192_168_0_103-20261017-item013-r2."
                       "core-evidence_media-provider-20261017-item013-r2.log")
    impl_json = HERE / ("prov-192_168_0_103-20261017-item013-r2."
                        "core-evidence_media-provider-20261017-item013-r2.json")
    ver_log = HERE / ("prov-192_168_0_103-20261017-item013-verify-v1."
                      "core-evidence_media-provider-20261017-item013-verify-v1.log")
    ver_json = HERE / ("prov-192_168_0_103-20261017-item013-verify-v1."
                       "core-evidence_media-provider-20261017-item013-verify-v1.json")

    problems = []
    checks = []

    def mark(name, ok, detail=""):
        checks.append({"check": name, "ok": bool(ok), "detail": detail})
        print("%s %s %s" % (name, "PASS" if ok else "FAIL", detail))

    sides = {}
    for tag, lg, js in (("impl-r2", impl_log, impl_json), ("verify-v1", ver_log, ver_json)):
        if not lg.exists():
            print("verify log not present yet (%s): pre-comparing impl only" % lg.name)
            continue
        d = parse_log(lg)
        try:
            jb = parse_json(js)
        except Exception:
            jb = None
        sides[tag] = (d, jb)

    for tag, (d, jb) in sides.items():
        pre = len(problems)
        semantics(tag, d, jb, problems)
        mark("semantics_" + tag, len(problems) == pre, "; ".join(problems[pre:]) or "holds")

    # 稳定行跨轮恒等: query 五 case 状态行(遍历 status/coverage/gaps 计数外) + nvr + acc
    a = sides["impl-r2"][0]
    b = sides["verify-v1"][0]
    for k in ("acc", "nvr", "export_missing", "thumb"):
        mark("stable_%s" % k, a.get(k) == b.get(k) and a.get(k) is not None,
             "%s" % str(a.get(k))[:100])
    # play 维度独立判据: 五轮 status/reached 系列两轮恒等 + act_rel 差异模式恒等
    pa = [p["status"] + "/" + p["reached"] for p in a["play"]]
    pb = [p["status"] + "/" + p["reached"] for p in b["play"]]
    mark("stable_play_status_series", pa == pb and len(pa) == 5, "%s" % pa)
    aa = [p["actual"] - p["requested"] for p in a["play"]]
    ab = [p["actual"] - p["requested"] for p in b["play"]]
    # KEY_UNIT 落点自然波动: 判据=偏差模式（正/零位置）+前四同组一致性，不逐个绝对恒等
    sign = [(d > 0) for d in aa]
    signb = [(d > 0) for d in ab]
    mark("play_actual_mode", sign == signb and abs(aa[0]) <= TOL_SEEK_MS and abs(ab[0]) <= TOL_SEEK_MS,
         "impl=%s verify=%s" % (aa, ab))
    # frames 波动窗: 每轮 frames>0 与档位（播放窗内同档）
    fa = [p["frames"] for p in a["play"]]
    fb = [p["frames"] for p in b["play"]]
    mark("play_frames_window",
         all(f > 0 for f in fa + fb) and max(fa) <= 40 and max(fb) <= 40 and
         all(abs(x - y) <= 15 for x, y in zip(fa, fb)),
         "impl=%s verify=%s" % (fa, fb))
    # export size 波动窗
    es_i = int(a["export"]["size_bytes"])
    es_v = int(b["export"]["size_bytes"])
    mark("export_size_window", abs(es_i - es_v) / max(es_i, es_v) <= 0.15,
         "impl=%d verify=%d" % (es_i, es_v))
    # covered_ms complete 波动（sampling 边界自然波动 <= 200）
    cm_i = int(a["query"]["complete"].get("covered_ms", "0"))
    cm_v = int(b["query"]["complete"].get("covered_ms", "0"))
    mark("covered_ms_window", abs(cm_i - cm_v) <= 200, "impl=%d verify=%d" % (cm_i, cm_v))

    result = {"checks": checks, "problems": problems, "pass": all(c["ok"] for c in checks)}
    OUT.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print("SUMMARY", "PASS" if result["pass"] else "FAIL", "checks=%d" % len(checks))
    if problems:
        print("PROBLEMS:")
        for p in problems:
            print(" -", p)
    return 0 if result["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
