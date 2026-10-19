#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-007 验证独立比对 oracle（software_tester.software_verification）。

不采信交付 RESULT 行；全部判据独立推导：
  C1  transcript 结构：fresh 全部步骤 exit=0、主机指纹、--keep-remote 无 cleanup
  C2  fresh vs impl r1：core-tests 判例名多重集恒等 + selftest/11 fixtures/config 日志逐行恒等
  C3  语义重推导（独立解析 fixture JSON 输出，不读交付 RESULT）：
      TC-T-SEM-01 person 语义与级别、TC-T-SEM-02 未知 label 拦截零报警、
      TC-T-SEM-03 多目标各自独立事件、离轨不误清、TC-T-SEM-04 generation 重确认、
      无 track 禁 0 冒充、低置信不下发、不可用无判定、聚合、config 阈值来源
  C4  selftest 独立统计（PASS/FAIL 行计数，不读 RESULT）

用法:
  python verify_item007_compare.py                    # fresh vs impl r1
  python verify_item007_compare.py <stamp>            # 自检模式（fresh=impl=stamp）
"""
import json
import os
import re
import sys
from collections import Counter
from html import unescape

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", "..", "..", ".."))
EVID = os.path.join(REPO, "docs", "vision", "test_evidence", "ITEM-007")
FRESH = sys.argv[1] if len(sys.argv) > 1 else "verify-20261019-item007-v1"
IMPL = "20261019-item007-r1"
FP = "SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU"
FIXTURES = ["person_on_rail", "unknown_label", "multi_target", "off_rail", "generation_reset",
            "no_track", "low_confidence", "obstacle", "unavailable", "aggregate", "sorted"]
SELFTEST_N = 19  # 实现 r1 实测判例数（实现文档写 30，与实测不符，见 VERIFY 报告观察项）

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
    p = os.path.join(EVID, "vision-item007-192_168_0_103-%s.transcript.json" % stamp)
    with open(p, encoding="utf-8") as fh:
        return json.load(fh)


def load_fixture(stamp, fx):
    p = os.path.join(EVID, "vision-item007-192_168_0_103-%s.app_core-evidence_item007-fixture-%s-%s.json"
                     % (stamp, fx, stamp))
    with open(p, encoding="utf-8") as fh:
        return json.load(fh)["lines"]


def load_log(stamp, kind):
    p = os.path.join(EVID, "vision-item007-192_168_0_103-%s.app_core-evidence_item007-%s-%s.log"
                     % (stamp, kind, stamp))
    with open(p, encoding="utf-8") as fh:
        return fh.read()


def ev_lines(lines):
    return [l for l in lines if l.startswith("event ")]


def total_events(lines):
    for l in lines:
        if l.startswith("type=events total="):
            return int(l.split("total=")[1])
    return None


def result_line(lines):
    for l in lines:
        if l.startswith("type=result events="):
            return l
    return ""


def sem_line(lines):
    # aggregate/sorted/unavailable 家族输出 res_line（type=result type=semantic ok=...），
    # 与事件型 fixture 的 type=result events=... 是两个输出面（同 ITEM-012 kv 分表教训）。
    for l in lines:
        if "type=semantic ok=" in l:
            return l
    return ""


def rval(lines, key):
    m = re.search(r"(?:^|\s)%s=(-?\d+(?:\.\d+)?)" % re.escape(key), result_line(lines) or "")
    return m.group(1) if m else None


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
    check(all(v == 0 for v in step_exits.values()), "C1_step_exits_zero", step_exits)
    exit_tokens = {k: v for k, v in
                   [(s["step"], (s.get("output") or "")) for s in tr["steps"]]
                   if re.search(r"(BUILD|TEST|SELFTEST|CONFIG)_EXIT=-?\d+", v)}
    toks_ok = all(re.search(r"%s_EXIT=0\b" % k.upper(), v)
                  for k, v in [("build", exit_tokens.get("build-core", "")),
                               ("test", exit_tokens.get("core-tests", "")),
                               ("selftest", exit_tokens.get("selftest", "")),
                               ("selftest_json", exit_tokens.get("selftest", "")),
                               ("config", exit_tokens.get("config", ""))])
    check(toks_ok, "C1_real_exit_tokens_zero", sorted(exit_tokens))
    fx_exit_ok = all(re.search(r"FIX_%s_EXIT=0\b" % fx.upper(), steps["fixture-" + fx]["output"])
                     for fx in FIXTURES)
    check(fx_exit_ok, "C1_fixture_exit_tokens_zero", FIXTURES)
    check("cleanup" not in steps, "C1_keep_remote_no_cleanup", sorted(steps))

    # ---- C2 fresh vs impl r1 恒等 ----
    def junit_xml(stamp):
        p = os.path.join(EVID, "vision-item007-192_168_0_103-%s.app_core-evidence_item007-core-tests-%s.xml"
                         % (stamp, stamp))
        with open(p, encoding="utf-8", errors="replace") as fh:
            return fh.read()

    fresh_xml, impl_xml = junit_xml(FRESH), junit_xml(IMPL)

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
    sem_cases = [n for n in fc if re.search(r"person on rail|unknown label|aggregated|invalidate|"
                                            r"generation|null track|low confidence|off-rail|sorted", n)]
    check(len(sem_cases) >= 20, "C2_junit_semantic_domain_present", len(sem_cases))

    kinds = ["selftest"] + ["fixture-" + fx for fx in FIXTURES] + ["config"]
    diffs = []
    for kind in kinds:
        a = load_log(FRESH, kind)
        b = load_log(IMPL, kind)
        if a != b:
            diffs.append(kind)
    check(not diffs, "C2_log_lines_identical_fresh_vs_impl_r1", diffs or "all %d identical" % len(kinds))

    # ---- C3 独立语义重推导（fresh fixture JSON，不读交付 RESULT 行）----
    # TC-T-SEM-01: person 在轨 3 帧确认 → PersonOnRail L1（独立期望：person→PersonOnRail、L1、track1）
    lines = load_fixture(FRESH, "person_on_rail")
    evs = ev_lines(lines)
    check(total_events(lines) == 1 and len(evs) == 1 and
          evs[0] == "event type=person_on_rail level=1 track=1",
          "C3_TC-T-SEM-01_person_on_rail_L1_track1", evs)
    check(rval(lines, "events") == "1" and rval(lines, "mapped") == "3",
          "C3_TC-T-SEM-01_confirmed_at_3rd_frame", result_line(lines))

    # TC-T-SEM-02: 未知 label（car 不在 front 白名单）→ 零报警 + 拦截计数
    lines = load_fixture(FRESH, "unknown_label")
    check(total_events(lines) == 0 and not ev_lines(lines) and
          rval(lines, "events") == "0" and rval(lines, "unknown") == "3" and rval(lines, "mapped") == "0",
          "C3_TC-T-SEM-02_unknown_label_blocked", result_line(lines))

    # TC-T-SEM-03: 多目标每 track 独立事件（track 1/2 各一，不重复开事件）
    lines = load_fixture(FRESH, "multi_target")
    evs = ev_lines(lines)
    tracks = sorted(re.search(r"track=(\d+)", e).group(1) for e in evs)
    check(total_events(lines) == 2 and len(evs) == 2 and tracks == ["1", "2"] and
          all("person_on_rail level=1" in e for e in evs),
          "C3_TC-T-SEM-03_multi_target_one_event_per_track", evs)
    check(rval(lines, "mapped") == "6" and rval(lines, "det_last") == "2",
          "C3_TC-T-SEM-03_six_boxes_two_tracks", result_line(lines))

    # 离轨目标不误清在轨目标确认（同帧 track2 离轨，track1 第 3 帧报警）
    lines = load_fixture(FRESH, "off_rail")
    evs = ev_lines(lines)
    check(total_events(lines) == 1 and len(evs) == 1 and "track=1" in evs[0],
          "C3_TC-T-SEM-03_off_rail_does_not_clear_track1", evs)
    check(rval(lines, "track_reset") == "0",
          "C3_off_rail_first_seen_no_phantom_reset", result_line(lines))

    # TC-T-SEM-04: generation 变化清确认（新代重新累计 3 帧才报警）
    lines = load_fixture(FRESH, "generation_reset")
    m = re.search(r"^type=gen gen_reset_keys=(\d+) events=(\d+)/(\d+)/(\d+)$", "\n".join(lines), re.M)
    check(m is not None and m.group(1) == "1" and (m.group(2), m.group(3), m.group(4)) == ("0", "0", "1"),
          "C3_TC-T-SEM-04_generation_reset_reconfirm", m.groups() if m else None)

    # 无 track 禁 0 冒充：丢弃并计数，不产生任何事件（含 track=0）
    lines = load_fixture(FRESH, "no_track")
    check(total_events(lines) == 0 and not ev_lines(lines) and
          rval(lines, "no_track") == "3" and rval(lines, "det_last") == "0",
          "C3_no_track_dropped_never_fabricated", result_line(lines))

    # 在轨低置信不下发（映射但非候选）
    lines = load_fixture(FRESH, "low_confidence")
    check(total_events(lines) == 0 and not ev_lines(lines) and
          rval(lines, "low_conf") == "3" and rval(lines, "det_last") == "1",
          "C3_low_confidence_mapped_not_dispatched", result_line(lines))

    # obstacle 白名单 → FrontObstacle L1（track3）
    lines = load_fixture(FRESH, "obstacle")
    evs = ev_lines(lines)
    check(total_events(lines) == 1 and len(evs) == 1 and
          evs[0] == "event type=front_obstacle level=1 track=3",
          "C3_obstacle_front_obstacle_L1", evs)

    # envelope 不可用 → 无判定/无报警（独立解析 res_line 字段）
    lines = load_fixture(FRESH, "unavailable")
    m = re.search(r"type=semantic ok=(\d+) available=(\d+) reason=(\S+) det=(\d+)",
                  sem_line(lines) or "")
    check(m is not None and m.group(1) == "0" and m.group(2) == "0" and
          m.group(3) == "SEMANTIC_UNAVAILABLE" and m.group(4) == "0",
          "C3_unavailable_no_judgement_no_alarm", m.groups() if m else None)

    # 同帧同 track 聚合（3 框 2 track，agg=1）＋ 不可用/聚合分支语义独立解析
    lines = load_fixture(FRESH, "aggregate")
    m = re.search(r"ok=(\d+) available=(\d+) reason=(\S+) det=(\d+) cand=(\d+) in=(\d+) mapped=(\d+)"
                  r".*agg=(\d+)", sem_line(lines) or "")
    check(m is not None and (m.group(1), m.group(2), m.group(3), m.group(4), m.group(5),
                             m.group(6), m.group(7), m.group(8)) ==
          ("1", "1", "SEMANTIC_OK", "2", "2", "3", "2", "1"),
          "C3_same_track_aggregated", m.groups() if m else None)

    # 按 track 排序：探针内建序判定 + 与 aggregate 统计面一致（det=2/agg=1）
    lines = load_fixture(FRESH, "sorted")
    m2 = re.search(r"ok=(\d+) available=(\d+) reason=(\S+) det=(\d+) .*agg=(\d+)",
                   sem_line(lines) or "")
    check(m2 is not None and (m2.group(1), m2.group(4), m2.group(5)) == ("1", "2", "1") and
          m2.group(3) == "SEMANTIC_OK",
          "C3_sorted_stats_consistent", m2.groups() if m2 else None)

    # config：阈值来自 thresholds.yaml（独立读取配置文件交叉核对）
    cfg = load_log(FRESH, "config")
    m = re.search(r"type=config dir=\S+ confirm_frames=(\d+) confidence_min=([\d.]+) "
                  r"suppress_window_ms=(\d+)", cfg)
    th = {}
    with open(os.path.join(REPO, "app", "config", "thresholds.yaml"), encoding="utf-8") as fh:
        for ln in fh:
            mm = re.match(r"\s*(confirm_frames|confidence_min|suppress_window_ms):\s*([0-9.]+)", ln)
            if mm:
                th[mm.group(1)] = mm.group(2)
    check(m is not None and m.group(1) == th.get("confirm_frames") and
          float(m.group(2)) == float(th.get("confidence_min", "-1")) and
          m.group(3) == th.get("suppress_window_ms"),
          "C3_config_thresholds_from_yaml", m.groups() if m else th)

    # ---- C4 selftest 独立统计（不读 RESULT）----
    sl = load_log(FRESH, "selftest")
    n_pass = sl.count("[PASS]")
    n_fail = sl.count("[FAIL]")
    check(n_pass == SELFTEST_N and n_fail == 0, "C4_selftest_case_lines",
          "pass=%d fail=%d (expect %d)" % (n_pass, n_fail, SELFTEST_N))
    check("RESULT: PASS failures=0" in sl, "C4_selftest_result_line", sl.strip().splitlines()[-1:])

    summary = {"passed": g_pass, "failed": g_fail, "fail_names": g_fail_names,
               "fresh": FRESH, "impl": IMPL}
    with open(os.path.join(HERE, "item007-compare-%s.json" % FRESH), "w", encoding="utf-8") as fh:
        json.dump(summary, fh, ensure_ascii=False, indent=2)
    print("COMPARE SUMMARY passed=%d/%d" % (g_pass, g_pass + g_fail), flush=True)
    return 0 if g_fail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
