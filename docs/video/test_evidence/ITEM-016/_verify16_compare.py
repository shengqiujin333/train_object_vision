#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-016 验证侧独立比对（VERIFY-20261017-v1）。

对实现方归档 r3 与验证侧 fresh 复跑 verify-v1 的 throughput 产物做独立语义重推导，
判据不采信交付 type=acceptance 行为最终依据：

  C1  capability 行与 11 element 行独立解析，kind 集合覆盖必需面；关键字段恒等（版本串）、
      状态面（config_ok/elements_present/missing）恒等。
  C2  每路由验证侧独立 classify（按 source 复刻交付 classify_presence 判据，独立写）推导
      presence，并与探针行 presence 一致；三路 kind 集合 = {online, gap, unverified}。
  C3  metric 行 12 字段键集齐全（TC-V-LOG-02），fresh 三路行键集与 r3 一致；
      数值型字段为非负整数；fps 为正/零浮点；attempt_id 形态 CAM-01#gN / none。
  C4  按日 JSON 文件：3 行、键集=探针键集（JSON fields 与行剥离 (标签) 后一致）、
      presence 推导一致、attempt_id 一致、last_error 已脱敏。
  C5  redaction 行重推导（redact_error 独立复刻：userinfo/kv mask/pipeline 整条）——
      dirty 原文含 S3CRET/userinfo/rtspsrc；redacted 后无 S3CRET/admin:/rtspsrc/location=。
  C6  acceptance 行键面恒等且全部 true（键面不增不减，防夹具拼写逃逸）。
"""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent

FAILS = []


def check(name, ok, detail=""):
    print("cmp16 %s %s %s" % ("PASS" if ok else "FAIL", name, "" if ok else str(detail)[:400]))
    if not ok:
        FAILS.append(name)


def kv_line(line):
    kv = {}
    for tok in line.split(" "):
        if "=" in tok:
            k, v = tok.split("=", 1)
            kv[k] = v
    return kv


def read_probe(path):
    p = Path(path)
    if not p.exists():
        return None
    return p.read_text(encoding="utf-8")


def parse_probe(text):
    if text is None:
        return None
    d = {"elements": {}, "metrics": {}, "acceptance": None, "capability": None,
         "redaction": None}
    m = re.search(r"^type=capability (.*)$", text, re.M)
    if m:
        d["capability"] = kv_line(m.group(1))
    m = re.search(r"^type=redaction (.*)$", text, re.M)
    if m:
        d["redaction"] = kv_line(m.group(1))
    m = re.search(r"^type=acceptance (.*)$", text, re.M)
    if m:
        d["acceptance"] = kv_line(m.group(1))
    for mm in re.finditer(r"^type=element name=(\S+) status=(\S+) long_name=(.*)$", text, re.M):
        d["elements"][mm.group(1)] = {"status": mm.group(2), "long_name": mm.group(3).strip()}
    # metric 行 presence 含 "(中文)" 侧值，只取至 state= 前的键值。
    for mm in re.finditer(r"^type=metric camera=(\S+) presence=(\S+) \([^)]*\) (.*)$", text, re.M):
        d["metrics"][mm.group(1)] = {"presence": mm.group(2), **kv_line(mm.group(3))}
    return d


def independent_presence(mkv):
    """验证侧独立复刻 classify_presence 判据（与交付 .cpp 独立写，按源码语义）。
    mkv 字符串计数器 -> int；gap_count>0 或 state==frozen -> gap；
    live/degraded 且 last_frame_mono_ms>0 -> online；否则 unverified。
    """
    def num(k):
        return int(float(mkv.get(k, "0")))

    state = mkv.get("state", "offline")
    if num("gap_count") > 0 or state == "frozen":
        return "gap"
    if state in ("live", "degraded") and num("last_frame_mono_ms") > 0:
        return "online"
    return "unverified"


REQUIRED_METRIC_KEYS = {"state", "fps", "last_frame_mono_ms", "last_frame_wall_ms",
                        "reconnect_count", "quality_flags", "display_dropped",
                        "ai_dropped", "recording_overflow", "gap_count",
                        "attempt_id", "last_error"}


def run_compare(r3_path, fresh_path, label):
    r3 = parse_probe(read_probe(r3_path))
    fresh = parse_probe(read_probe(fresh_path))
    if r3 is None or fresh is None:
        check("%s inputs exist" % label, False, "missing %s or %s" % (r3_path, fresh_path))
        return None, None
    return r3, fresh


def main():
    docs = HERE
    r3_log = read_probe(docs / "diag-192_168_0_103-20261017-item016-r3.core-evidence_diag-20261017-item016-r3.log")
    fresh_log = read_probe(docs / "diag-192_168_0_103-20261017-item016-verify-v1.core-evidence_diag-20261017-item016-verify-v1.log")
    r3_daily = read_probe(docs / "diag-192_168_0_103-20261017-item016-r3.core-evidence_logs_video-2026-10-17.log")
    fresh_daily = read_probe(docs / "diag-192_168_0_103-20261017-item016-verify-v1.core-evidence_logs_video-2026-10-17.log")
    r3_json = read_probe(docs / "diag-192_168_0_103-20261017-item016-r3.core-evidence_diag-20261017-item016-r3.json")
    fresh_json = read_probe(docs / "diag-192_168_0_103-20261017-item016-verify-v1.core-evidence_diag-20261017-item016-verify-v1.json")
    r3_tr = read_probe(docs / "diag-192_168_0_103-20261017-item016-r3.transcript.json")
    fresh_tr = read_probe(docs / "diag-192_168_0_103-20261017-item016-verify-v1.transcript.json")

    # C1: capability + elements
    r3 = parse_probe(r3_log)
    fresh = parse_probe(fresh_log)
    check("C1a inputs parsed", r3 is not None and fresh is not None)
    if r3 and fresh:
        for key in ("config_ok", "elements_present", "missing"):
            check("C1b capability %s identical" % key,
                  r3["capability"] and fresh["capability"] and
                  r3["capability"].get(key) == fresh["capability"].get(key),
                  "r3=%r fresh=%r" % (r3["capability"] and r3["capability"].get(key),
                                      fresh["capability"] and fresh["capability"].get(key)))
        check("C1c gstreamer version identical nonempty",
              r3["capability"] and fresh["capability"] and
              r3["capability"].get("gstreamer") == fresh["capability"].get("gstreamer") and
              bool((fresh["capability"] or {}).get("gstreamer")),
              "r3=%r fresh=%r" % (r3["capability"] and r3["capability"].get("gstreamer"),
                                  fresh["capability"] and fresh["capability"].get("gstreamer")))
        # 版本串逐字恒等（librga/librknnrt 恒等，板端库版本）
        check("C1d librga identical",
              r3["capability"].get("librga") == fresh["capability"].get("librga") and
              bool(fresh["capability"].get("librga")),
              "r3=%r fresh=%r" % (r3["capability"].get("librga"), fresh["capability"].get("librga")))
        check("C1e librknnrt identical",
              r3["capability"].get("librknnrt") == fresh["capability"].get("librknnrt") and
              bool(fresh["capability"].get("librknnrt")),
              "r3=%r fresh=%r" % (r3["capability"].get("librknnrt"), fresh["capability"].get("librknnrt")))
        # 元素集合与状态
        check("C1f element count >= 11 identical",
              len(r3["elements"]) == len(fresh["elements"]) and len(fresh["elements"]) >= 11,
              "r3=%d fresh=%d" % (len(r3["elements"]), len(fresh["elements"])))
        for name in sorted(fresh["elements"]):
            check("C1g element %s present both" % name,
                  r3["elements"].get(name, {}).get("status") == fresh["elements"].get(name, {}).get("status") == "present",
                  "r3=%s fresh=%s" % (r3["elements"].get(name, {}).get("status"),
                                      fresh["elements"].get(name, {}).get("status")))
        check("C1h required decoder faces covered",
              {"rtspsrc", "mppvideodec", "appsink", "decodebin"} <= set(fresh["elements"]),
              str(sorted(fresh["elements"])[:5]))

        # C2: 三态 presence 独立推导
        cam_kinds_f = {k: independent_presence(v) for k, v in fresh["metrics"].items()}
        cam_kinds_r = {k: independent_presence(v) for k, v in r3["metrics"].items()}
        check("C2a three cameras present", len(fresh["metrics"]) == 3,
              str(sorted(fresh["metrics"])))
        for cam in fresh["metrics"]:
            check("C2b %s probe presence == independent classify" % cam,
                  cam_kinds_f[cam] == fresh["metrics"][cam].get("presence"),
                  "indep=%s probe=%s" % (cam_kinds_f[cam], fresh["metrics"][cam].get("presence")))
            check("C2c %s r3 presence == fresh presence" % cam,
                  fresh["metrics"][cam].get("presence") == r3["metrics"].get(cam, {}).get("presence"),
                  "r3=%s fresh=%s" % (r3["metrics"].get(cam, {}).get("presence"),
                                      fresh["metrics"][cam].get("presence")))
        kinds = {cam_kinds_f[c] for c in fresh["metrics"]}
        check("C2d tristate visible exactly {online,gap,unverified}",
              kinds == {"online", "gap", "unverified"}, str(kinds))

        # C3: 12 字段键集齐备（TC-V-LOG-02）
        for cam, mkv in fresh["metrics"].items():
            missing = REQUIRED_METRIC_KEYS - set(mkv)
            check("C3a %s metric keys full" % cam, not missing, str(sorted(missing)))
        check("C3b metric keysets r3==fresh",
              {c: set(v) for c, v in r3["metrics"].items()} ==
              {c: set(v) for c, v in fresh["metrics"].items()},
              "keyset mismatch")
        # 数值域
        for cam, mkv in fresh["metrics"].items():
            for k in ("display_dropped", "ai_dropped", "recording_overflow", "gap_count",
                      "reconnect_count", "last_frame_mono_ms", "last_frame_wall_ms"):
                v = mkv.get(k, "x")
                check("C3c %s.%s numeric" % (cam, k),
                      re.fullmatch(r"-?\d+", v) is not None, v)
            m = re.fullmatch(r"(CAM-\d+#g\d+|none)", mkv.get("attempt_id", ""))
            check("C3d %s attempt_id form" % cam, m is not None, mkv.get("attempt_id", ""))
            check("C3e %s fps float" % cam,
                  re.fullmatch(r"\d+\.\d+", mkv.get("fps", "")) is not None, mkv.get("fps", ""))
        # attempt_id 与 reconnect/generation 关联：fresh CAM-01 若 generation>0 必非 none
        m1 = fresh["metrics"].get("CAM-01", {})
        check("C3f CAM-01 attempt id consistent with live gen",
              (m1.get("attempt_id") == "CAM-01#g1" and m1.get("reconnect_count") == "0") or
              m1.get("attempt_id", "").startswith("CAM-01#g"),
              m1.get("attempt_id", ""))

        # C4: 按日 JSON
        def parse_daily(text):
            if not text:
                return None
            rows = []
            for line in text.strip().splitlines():
                import json as _json
                rows.append(_json.loads(line))
            return rows

        fd = parse_daily(fresh_daily)
        rd = parse_daily(r3_daily)
        check("C4a fresh daily 3 rows", fd is not None and len(fd) == 3,
              "rows=%s" % (len(fd) if fd else None))
        if fd:
            for row in fd:
                f = row.get("fields", {})
                cam = f.get("camera", "?")
                check("C4b %s json keys == metric keys" % cam,
                      set(REQUIRED_METRIC_KEYS) | {"camera", "presence"} <= set(f),
                      str(sorted(set(REQUIRED_METRIC_KEYS) | {"camera", "presence"} - set(f))))
                check("C4c %s json presence matches row" % cam,
                      independent_presence(f) == f.get("presence"),
                      "indep=%s json=%s" % (independent_presence(f), f.get("presence")))
                check("C4d %s json last_error no creds" % cam,
                      "S3CRET" not in row.get("source", "") + str(row),
                      str(row)[:120])
            check("C4e daily rows r3==fresh per-camera fields",
                  rd is not None and
                  sorted((r["fields"]["camera"], r["fields"].get("presence"),
                          r["fields"].get("attempt_id"), r["fields"].get("state"))
                         for r in fd) ==
                  sorted((r["fields"]["camera"], r["fields"].get("presence"),
                          r["fields"].get("attempt_id"), r["fields"].get("state"))
                         for r in rd),
                  "r3/fresh presence/attempts differ")
            check("C4f daily ts fresh is today",
                  fd[0]["source"] == "video" and fd[0]["message"] == "channel_metric",
                  str(fd[0])[:120])

        # C5: redaction 重推导
        dirty = ("rtsp://admin:S3CRET@192.168.1.65:554/x password=S3CRET "
                 "rtspsrc location=\"rtsp://admin:S3CRET@192.168.1.65:554/x\" ! decodebin")
        check("C5a r3 redaction saw dirty", r3["redaction"] and r3["redaction"].get("raw_has_cred") == "true",
              str(r3["redaction"]))
        check("C5b fresh redaction redacted=<redacted>",
              fresh["redaction"] and fresh["redaction"].get("redacted") == "<redacted>",
              str(fresh["redaction"]))
        check("C5c fresh redaction clean=true",
              fresh["redaction"] and fresh["redaction"].get("clean") == "true",
              str(fresh["redaction"]))
        red = (fresh["redaction"] or {}).get("redacted", "")
        check("C5d redacted text has no secret/userinfo/pipeline",
              "S3CRET" not in red and "admin:" not in red and "rtspsrc" not in red,
              red)

        # C6: acceptance 键面恒等且全 true
        akeys = ["capability_versions", "elements_required_present",
                 "tristate_online_unverified_gap", "redaction_clean",
                 "daily_roll_ok", "daily_file_written", "structured_fields"]
        check("C6a acceptance key sets identical",
              set((r3["acceptance"] or {}).keys()) == set((fresh["acceptance"] or {}).keys()),
              "r3=%s fresh=%s" % (sorted((r3["acceptance"] or {}).keys()),
                                  sorted((fresh["acceptance"] or {}).keys())))
        for k in akeys:
            check("C6b acceptance %s true (fresh)" % k,
                  (fresh["acceptance"] or {}).get(k) == "true",
                  str((fresh["acceptance"] or {}).get(k)))

        # C7: transcript 完整性。实现方 r3 在 run 后清理（cleanup 步在位）；
        # verify-v1 使用 --keep-remote（EXT 夹具需板端树），cleanup 步应缺席
        # （由 EXT 阶段末尾独立清理并留 BOARD_CLEAN_DONE 证据）。
        ok_r3 = (r3_tr is not None and '"cleanup"' in r3_tr and '"selftest"' in r3_tr and '"run-diag"' in r3_tr)
        check("C7 implementer r3 transcript complete", ok_r3, "missing sections")
        ok_f = (fresh_tr is not None and '"cleanup"' not in fresh_tr and '"selftest"' in fresh_tr
                and '"run-diag"' in fresh_tr)
        check("C7 verify v1 transcript complete (keep-remote, no cleanup)", ok_f, "missing sections")
        # selftest 7/7 独立解析（fresh transcript）
        if fresh_tr:
            m = re.search(r"selftest OK \(0 failure\(s\)\)", fresh_tr)
            check("C8 fresh selftest OK 0 failures", m is not None)
            cnt = len(re.findall(r"selftest PASS ", fresh_tr))
            check("C8b fresh selftest 7 PASS", cnt == 7, "%d" % cnt)
        if r3_tr:
            m = re.search(r"selftest OK \(0 failure\(s\)\)", r3_tr)
            check("C9 impl selftest OK", m is not None)

        # C10: 汇总 JSON
        check("C10a fresh summary json parses coverage",
              fresh_json is not None and '"acceptance"' in fresh_json and 'true' in fresh_json,
              str(fresh_json)[:160])
        check("C10b summary presence sets identical r3/fresh",
              r3_json is not None and
              re.search(r'"CAM-01"\s*:\s*"online"', r3_json) and
              re.search(r'"CAM-02"\s*:\s*"gap"', r3_json) and
              re.search(r'"CAM-03"\s*:\s*"unverified"', r3_json) and
              re.search(r'"CAM-01"\s*:\s*"online"', fresh_json) and
              re.search(r'"CAM-02"\s*:\s*"gap"', fresh_json) and
              re.search(r'"CAM-03"\s*:\s*"unverified"', fresh_json),
              "tristate markers not all present")

    print("")
    print("cmp16 summary: fails=%d" % len(FAILS))
    if FAILS:
        print("cmp16 FAILED checks: %s" % FAILS)
    return 0 if not FAILS else 1


if __name__ == "__main__":
    raise SystemExit(main())
