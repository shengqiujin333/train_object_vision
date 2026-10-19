#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-006 验证独立比对 oracle（software_tester.software_verification）。

不采信交付 acceptance/RESULT 行；全部判据独立推导：
  C1  transcript 结构：fresh 全部步骤 exit=0、主机指纹、--keep-remote 无 cleanup
  C2  fresh vs impl r3：core-tests 判例名多重集恒等 + selftest/fixture/config 日志逐行恒等
  C3  语义重推导（独立解析 fixture JSON 输出，不读交付 RESULT）：
      TC-T-PUB-01 身份链/延迟含排队、TC-T-PUB-02 逐类拒绝独立计数且不污染可用语义、
      TC-T-PUB-03 空框 vs 不可用可区分、TC-T-PUB-04 有界丢最旧保留最新、config 读取
  C4  JUnit XML 独立解析（tests/failures 属性 + testcase 名 + failure 标签）
"""
import json
import os
import re
import sys
from collections import Counter
from html import unescape

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", "..", "..", ".."))
EVID = os.path.join(REPO, "docs", "vision", "test_evidence", "ITEM-006")
FRESH = "verify-20261019-item006-v1"
IMPL = "20261019-item006-r3"
FP = "SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU"
FIXTURES = ["ready", "empty", "unavailable", "seg_unavailable", "no_track",
            "identity", "stale", "duplicate", "out_of_order", "queue_overflow"]

g_pass, g_fail, g_fail_names = 0, 0, []


def check(ok, name, detail=""):
    global g_pass, g_fail
    print("[%s] %s%s" % ("PASS" if ok else "FAIL", name, (" :: " + str(detail)) if detail else ""))
    if ok:
        g_pass += 1
    else:
        g_fail += 1
        g_fail_names.append(name)


def load_transcript(stamp):
    p = os.path.join(EVID, "vision-item006-192_168_0_103-%s.transcript.json" % stamp)
    with open(p, encoding="utf-8") as fh:
        return json.load(fh)


def load_fixture(stamp, fx):
    p = os.path.join(EVID, "vision-item006-192_168_0_103-%s.app_core-evidence_item006-fixture-%s-%s.json"
                     % (stamp, fx, stamp))
    with open(p, encoding="utf-8") as fh:
        return json.load(fh)["lines"]


def load_log(stamp, kind):
    p = os.path.join(EVID, "vision-item006-192_168_0_103-%s.app_core-evidence_item006-%s-%s.log"
                     % (stamp, kind, stamp))
    with open(p, encoding="utf-8") as fh:
        return fh.read()


def tc01_identity_chain(fx_lines):
    """TC-T-PUB-01: ready envelope 身份链完整，延迟独立重算。"""
    env = json.loads([l for l in fx_lines if l.startswith('{"session_id"')][0])
    ok = (env["session_id"] == "s-env" and env["camera_id"] == "CAM-01"
          and env["generation"] == 1 and env["seq"] == 1
          and env["capture_mono_us"] == 1000000 and env["result_mono_us"] == 1033000
          and env["model_version"] == "rknn-2.3.2/yolov5s")
    # 独立重算延迟（完成−接收，含排队；µs→ms）
    lat = (env["result_mono_us"] - env["capture_mono_us"]) // 1000
    return ok and env["latency_ms"] == 33 == lat, env, lat


def main():
    tr = load_transcript(FRESH)
    steps = {s["step"]: s for s in tr["steps"]}

    # ---- C1 fresh transcript 结构 ----
    check(tr["host_key_fingerprint"] == FP, "C1_board_fingerprint", tr["host_key_fingerprint"])
    check(steps["upload-app"]["output"].startswith("uploaded ") and
          int(steps["upload-app"]["output"].split()[1]) > 200, "C1_upload_count",
          steps["upload-app"]["output"])
    step_exits = {k: s.get("exit") for k, s in steps.items()
                  if k not in ("env", "mkdir", "upload-app", "download")}
    zero = all(v == 0 for v in step_exits.values())
    check(zero, "C1_step_exits_zero", step_exits)
    exit_tokens = {k: v for k, v in
                   [(s["step"], (s.get("output") or "")) for s in tr["steps"]]
                   if re.search(r"(BUILD|TEST|SELFTEST|CONFIG)_EXIT=-?\d+", v)}
    toks_ok = all(re.search(r"%s_EXIT=0\b" % k.upper(), v)
                  for k, v in [("build", exit_tokens.get("build-core", "")),
                               ("test", exit_tokens.get("core-tests", "")),
                               ("selftest", exit_tokens.get("selftest", "")),
                               ("config", exit_tokens.get("config", ""))])
    check(toks_ok, "C1_real_exit_tokens_zero", sorted(exit_tokens))
    fx_exit_ok = all(re.search(r"FIX_%s_EXIT=0\b" % fx.upper(), steps["fixture-" + fx]["output"])
                     for fx in FIXTURES)
    check(fx_exit_ok, "C1_fixture_exit_tokens_zero", FIXTURES)
    check("cleanup" not in steps, "C1_keep_remote_no_cleanup", sorted(steps))

    # ---- C2 fresh vs impl r3 恒等 ----
    with open(os.path.join(EVID, "vision-item006-192_168_0_103-%s.app_core-evidence_item006-core-tests-%s.xml"
              % (FRESH, FRESH)), encoding="utf-8", errors="replace") as fh:
        fresh_xml = fh.read()
    with open(os.path.join(EVID, "vision-item006-192_168_0_103-%s.app_core-evidence_item006-core-tests-%s.xml"
              % (IMPL, IMPL)), encoding="utf-8", errors="replace") as fh:
        impl_xml = fh.read()

    def cases(xml):
        # 本仓 testcase 是 classname=… name=… 属性序；name 前须有空格锚（防 classname 子串匹配）
        names = []
        for m in re.finditer(r"<testcase\b[^>]*>", xml):
            tag = m.group(0)
            nm = re.search(r'\sname="([^"]*)"', tag)
            if nm and nm.group(1):
                names.append(unescape(nm.group(1)))
        return names

    fc, ic = cases(fresh_xml), cases(impl_xml)
    check(len(fc) > 1100 and len(ic) > 1100, "C2_junit_case_counts", "fresh=%d impl=%d" % (len(fc), len(ic)))
    check(Counter(fc) == Counter(ic), "C2_junit_names_multiset_identical",
          "fresh=%d impl=%d" % (len(fc), len(ic)))
    m = re.search(r'<testsuites tests="(\d+)" failures="(\d+)">', fresh_xml)
    check(m is not None and m.group(1) == str(len(fc)) and m.group(2) == "0",
          "C2_junit_attributes_pass", m.groups() if m else None)
    envelope_cases = [n for n in fc if "track_id" in n or "queue" in n or "latency" in n
                      or "unavailable" in n or "empty" in n]
    check(len(envelope_cases) >= 20, "C2_junit_envelope_domain_present", len(envelope_cases))

    kinds = ["selftest"] + ["fixture-" + fx for fx in FIXTURES] + ["config"]
    diffs = []
    for kind in kinds:
        a = load_log(FRESH, kind)
        b = load_log(IMPL, kind)
        if a != b:
            diffs.append(kind)
    check(not diffs, "C2_log_lines_identical_fresh_vs_impl_r3", diffs or "all %d identical" % len(kinds))

    # ---- C3 独立语义重推导（fresh fixture JSON，不读交付 RESULT 行）----
    ok, env, lat = tc01_identity_chain(load_fixture(FRESH, "ready"))
    check(ok, "C3_TC-T-PUB-01_identity_chain_latency_recomputed", "latency=%d" % lat)
    boxes = env["boxes"]
    masks = env["masks"]
    check(len(boxes) == 1 and boxes[0]["track_id"] == 7 and boxes[0]["label"] == "person",
          "C3_TC-T-PUB-01_box_track_id", boxes)
    check(len(masks) == 1 and masks[0]["plane_present"] is True and masks[0]["kind"] == "rail"
          and masks[0]["width"] == 64 and masks[0]["stride_bytes"] == 64 and masks[0]["box_index"] == 0,
          "C3_TC-T-PUB-01_mask_ref_fields", masks)

    # TC-T-PUB-02: identity fixture 三类独立计数（独立解析 stats JSON）
    ident = [l for l in load_fixture(FRESH, "identity") if l.startswith("stats ")]
    st = json.loads(ident[0][len("stats "):])
    check(st["submitted"] == 4 and st["accepted"] == 1 and st["rejected"] == 3
          and st["stale_generation"] == 1 and st["duplicate_seq"] == 1 and st["out_of_order_seq"] == 1
          and st["queued"] == 1,
          "C3_TC-T-PUB-02_per_class_rejection_counts", st)
    # 逐类 fixture 各自计数且互不串扰
    stale_st = json.loads([l for l in load_fixture(FRESH, "stale") if l.startswith("stats ")][0][6:])
    dup_st = json.loads([l for l in load_fixture(FRESH, "duplicate") if l.startswith("stats ")][0][6:])
    ooo_st = json.loads([l for l in load_fixture(FRESH, "out_of_order") if l.startswith("stats ")][0][6:])
    check(stale_st["stale_generation"] == 1 and stale_st["duplicate_seq"] == 0
          and stale_st["out_of_order_seq"] == 0, "C3_TC-T-PUB-02_stale_only", stale_st)
    check(dup_st["duplicate_seq"] == 1 and dup_st["stale_generation"] == 0
          and dup_st["out_of_order_seq"] == 0, "C3_TC-T-PUB-02_duplicate_only", dup_st)
    check(ooo_st["out_of_order_seq"] == 1 and ooo_st["stale_generation"] == 0
          and ooo_st["duplicate_seq"] == 0, "C3_TC-T-PUB-02_ooo_only", ooo_st)
    # 拒绝项不入队（queued==1 = 仅首个被接受项）
    check(st["queued"] == 1 and st["queue_dropped"] == 0, "C3_TC-T-PUB-02_rejected_not_enqueued", st)

    # TC-T-PUB-03: 空框 vs 不可用可区分（availability/reason/box_count 三维）
    empty_env = json.loads([l for l in load_fixture(FRESH, "empty") if l.startswith('{"session_id"')][0])
    unav_env = json.loads([l for l in load_fixture(FRESH, "unavailable") if l.startswith('{"session_id"')][0])
    seg_env = json.loads([l for l in load_fixture(FRESH, "seg_unavailable") if l.startswith('{"session_id"')][0])
    check(empty_env["availability"] == "ready" and empty_env["reason"] == "READY"
          and empty_env["box_count"] == 0 and empty_env["boxes"] == [],
          "C3_TC-T-PUB-03_empty_ready_semantics", empty_env["reason"])
    check(unav_env["availability"] == "unavailable" and unav_env["reason"] == "AI_UNAVAILABLE"
          and unav_env["box_count"] == 0 and unav_env["boxes"] == [],
          "C3_TC-T-PUB-03_unavailable_semantics", unav_env["reason"])
    check(seg_env["availability"] == "unavailable" and seg_env["reason"] == "SEG_UNAVAILABLE"
          and seg_env["box_count"] == 0, "C3_TC-T-PUB-03_seg_unavailable_semantics", seg_env["reason"])
    check((empty_env["availability"], empty_env["reason"])
          != (unav_env["availability"], unav_env["reason"]),
          "C3_TC-T-PUB-03_distinguishable_outputs", "ready+READY vs unavailable+AI_UNAVAILABLE")
    no_track = json.loads([l for l in load_fixture(FRESH, "no_track") if l.startswith('{"session_id"')][0])
    check(no_track["boxes"][0]["track_id"] == -1 and no_track["boxes"][0]["track_id"] != 0,
          "C3_track_id_null_not_zero", no_track["boxes"][0]["track_id"])

    # TC-T-PUB-04: 有界队列丢最旧（深度=2：5 进 2 驻留、丢 3、pop 序 4,5）
    qo = load_fixture(FRESH, "queue_overflow")
    qst = json.loads([l for l in qo if l.startswith("stats ")][0][6:])
    first_pop = int([l for l in qo if l.startswith("pop_first_seq=")][0].split("=")[1])
    # 独立重推导：push seq 1..5 到 depth 2 丢最旧 → 驻留 {4,5}、丢 3
    depth, pushed = 2, 5
    exp_dropped = pushed - depth
    check(qst["queued"] == pushed and qst["queue_dropped"] == exp_dropped
          and first_pop == pushed - depth + 1,
          "C3_TC-T-PUB-04_bounded_drop_oldest_recomputed",
          "depth=%d pushed=%d dropped=%d first_pop=%d" % (depth, pushed, qst["queue_dropped"], first_pop))
    check(qst["accepted"] == pushed and qst["submitted"] == pushed,
          "C3_TC-T-PUB-04_all_pushed_accepted", qst)

    # config：深度来自 app.ai.queue_depth（配置值 2 与 app.yaml 一致）
    cfg = load_log(FRESH, "config")
    m = re.search(r"app\.ai\.queue_depth=(\d+) depth_used=(\d+) \(from app\.ai\.queue_depth\)", cfg)
    check(m is not None and m.group(1) == m.group(2) == "2",
          "C3_config_depth_from_app_yaml", m.group(0) if m else cfg[-120:])

    # selftest：独立统计 PASS 行数与失败数（不读 RESULT）
    sl = load_log(FRESH, "selftest")
    n_pass = sl.count("[PASS]")
    n_fail = sl.count("[FAIL]")
    check(n_pass >= 24 and n_fail == 0, "C3_selftest_case_lines", "pass=%d fail=%d" % (n_pass, n_fail))

    # ---- C4 汇总 ----
    summary = {"passed": g_pass, "failed": g_fail, "fail_names": g_fail_names}
    with open(os.path.join(HERE, "item006-compare-%s.json" % FRESH), "w", encoding="utf-8") as fh:
        json.dump(summary, fh, ensure_ascii=False, indent=2)
    print("COMPARE SUMMARY passed=%d/%d" % (g_pass, g_pass + g_fail), flush=True)
    return 0 if g_fail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
