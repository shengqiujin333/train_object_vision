#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-008 验证独立比对 oracle（software_tester.software_verification）。

不采信交付 RESULT 行；全部判据独立推导：
  C1  transcript 结构：fresh 全部步骤 exit=0、真实 exit token、主机指纹、--keep-remote 无 cleanup
  C2  fresh vs impl r4：core-tests 判例名多重集恒等 + selftest/8 fixtures/config 日志逐行恒等
      + JUnit 跟踪域判例在场
  C3  语义重推导（独立解析 fixture/selftest/config 行，不读 RESULT 行）：
      TC-T-TRK-01 同目标 id 稳定单事件、多目标互不干扰、label_guard、miss_drop、
      generation_reset、camera_level(-1 非 0)、envelope_only 回归、max_tracks 上限、
      id 永不为 0、config 阈值来源
  C4  selftest 独立统计（PASS/FAIL 行计数 + 判例名与源 check( 第二实参恒等，不手抄））

用法:
  python verify_item008_compare.py                    # fresh vs impl r4
  python verify_item008_compare.py <stamp>            # 自检模式（fresh=impl=stamp）
"""
import json
import os
import re
import sys
from collections import Counter
from html import unescape

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", "..", "..", ".."))
EVID = os.path.join(REPO, "docs", "vision", "test_evidence", "ITEM-008")
FRESH = sys.argv[1] if len(sys.argv) > 1 else "verify-20261019-item008-v1"
IMPL = "20261019-item008-r4"
FP = "SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU"
FIXTURES = ["same_target", "two_targets", "label_guard", "miss_drop", "generation_reset",
            "camera_level", "envelope_only", "max_tracks"]
SELFTEST_N = 23  # 探针 --selftest 判例数（独立计数；交付证据/文档一致口径）

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
    p = os.path.join(EVID, "vision-item008-192_168_0_103-%s.transcript.json" % stamp)
    with open(p, encoding="utf-8") as fh:
        return json.load(fh)


def load_fixture(stamp, fx):
    p = os.path.join(EVID, "vision-item008-192_168_0_103-%s.app_core-evidence_item008-fixture-%s-%s.log"
                     % (stamp, fx, stamp))
    with open(p, encoding="utf-8") as fh:
        return [ln.rstrip("\n") for ln in fh]


def load_json_lines(stamp, kind):
    p = os.path.join(EVID, "vision-item008-192_168_0_103-%s.app_core-evidence_item008-%s-%s.json"
                     % (stamp, kind, stamp))
    with open(p, encoding="utf-8") as fh:
        d = json.load(fh)
    return d.get("lines", [])


def load_log(stamp, kind):
    p = os.path.join(EVID, "vision-item008-192_168_0_103-%s.app_core-evidence_item008-%s-%s.log"
                     % (stamp, kind, stamp))
    with open(p, encoding="utf-8") as fh:
        return fh.read()


def result_line(lines):
    for ln in lines:
        if ln.startswith("RESULT:"):
            return ln
    return ""


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
    exit_tokens = {s["step"]: (s.get("output") or "") for s in tr["steps"]
                   if re.search(r"(BUILD|TEST|SELFTEST|CONFIG)_EXIT=-?\d+", s.get("output") or "")}
    token_map = {"build-core": "BUILD_EXIT", "core-tests": "TEST_EXIT",
                 "selftest": "SELFTEST_EXIT", "config": "CONFIG_EXIT"}
    toks_ok = {}
    for k, tok in token_map.items():
        toks_ok[tok] = re.search(r"%s=0\b" % tok, exit_tokens.get(k, "")) is not None
    # selftest 步骤同帧调用两次（log + json），两个退出 token 都须为 0
    toks_ok["SELFTEST_JSON_EXIT"] = re.search(r"SELFTEST_JSON_EXIT=0\b",
                                              exit_tokens.get("selftest", "")) is not None
    check(all(toks_ok.values()), "C1_real_exit_tokens_zero", toks_ok)
    fx_exit_ok = all(re.search(r"FIX_%s_EXIT=0\b" % fx.upper(), steps["fixture-" + fx]["output"])
                     for fx in FIXTURES)
    check(fx_exit_ok, "C1_fixture_exit_tokens_zero", FIXTURES)
    n_dl = sum(1 for s in tr["steps"] if s["step"] == "download" and s.get("exit") == 0)
    check(n_dl == 21, "C1_downloads_21", "downloaded=%d" % n_dl)
    # keep-remote 与 cleanup 一致性（两个方向都判）：交付 runner clean ⇔ 未用 --keep-remote
    kr = tr.get("invocation", {}).get("keep_remote")
    check(kr is True and "cleanup" not in steps,
          "C1_keep_remote_no_cleanup", sorted(steps))

    # ---- C2 fresh vs impl r4 恒等 ----
    def junit_xml(stamp):
        p = os.path.join(EVID, "vision-item008-192_168_0_103-%s.app_core-evidence_item008-core-tests-%s.xml"
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
    trk_cases = [n for n in fc if re.search(r"track|lightweight|camera-level|generation|null", n, re.I)]
    check(len(trk_cases) >= 10, "C2_junit_tracking_domain_present", len(trk_cases))

    kinds = ["selftest"] + ["fixture-" + fx for fx in FIXTURES] + ["config"]
    diffs = []
    for kind in kinds:
        if load_log(FRESH, kind) != load_log(IMPL, kind):
            diffs.append(kind)
    check(not diffs, "C2_log_lines_identical_fresh_vs_impl_r4", diffs or "all %d identical" % len(kinds))

    # ---- C3 独立语义重推导（fresh fixture 行，不读 RESULT 行）----
    # TC-T-TRK-01: 同目标连续命中 → 稳定 track=1、第 3 帧报警、第 4 帧不重复
    lines = load_fixture(FRESH, "same_target")
    fr = [ln for ln in lines if ln.startswith("frame=")]
    tracks = [re.search(r"track=(-?\d+)", ln).group(1) for ln in fr]
    totals = [int(re.search(r"events_total=(\d+)", ln).group(1)) for ln in fr]
    check(len(fr) == 4 and tracks == ["1"] * 4 and totals == [0, 0, 1, 1],
          "C3_TC-T-TRK-01_same_track_single_event", "tracks=%s totals=%s" % (tracks, totals))

    # 多目标互不干扰：每 track 独立事件
    lines = load_fixture(FRESH, "two_targets")
    m = re.search(r"^events=(\d+)$", "\n".join(lines), re.M)
    check(m is not None and m.group(1) == "2",
          "C3_TC-T-TRK-03_two_targets_two_events", m.group(0) if m else None)

    # 标签变化不串 id：person→obstacle 在同位置开新 id 2
    lines = load_fixture(FRESH, "label_guard")
    m = re.search(r"^ids=(\d+)$", "\n".join(lines), re.M)
    check(m is not None and m.group(1) == "2",
          "C3_label_change_gets_new_id_not_reused", m.group(0) if m else None)

    # 丢失后重现不复用 id：超过 max_misses 删除 + 新 id
    lines = load_fixture(FRESH, "miss_drop")
    m = re.search(r"^dropped=(\d) new_id=(\d+)$", "\n".join(lines), re.M)
    check(m is not None and m.group(1) == "1" and m.group(2) == "2",
          "C3_miss_drop_then_new_id", m.group(0) if m else None)

    # generation 变化：先 reset 跟踪器再映射；新代重新累计 3 帧
    lines = load_fixture(FRESH, "generation_reset")
    m = re.search(r"^tracker_resets=(\d+) gen_keys=(\d+) new_id=(\d+) events=(\d+)/(\d+)/(\d+)$",
                  "\n".join(lines), re.M)
    check(m is not None and (m.group(1), m.group(2)) == ("1", "1") and
          int(m.group(3)) >= 1 and int(m.group(3)) != 1 and
          (m.group(4), m.group(5), m.group(6)) == ("0", "0", "1"),
          "C3_TC-T-TRK-04_generation_reset_reconfirm", m.groups() if m else None)

    # 相机级适配：显式共享 -1（非 0）
    lines = load_fixture(FRESH, "camera_level")
    m = re.search(r"\bpolicy=(\w+)", "\n".join(lines))
    mid = re.search(r"^det_id=(-?\d+) events=(\d+)$", "\n".join(lines), re.M)
    rt_pairs = [(mid.group(1), mid.group(2))] if mid is not None else []
    check(m is not None and m.group(1) == "camera_level" and rt_pairs == [("-1", "1")],
          "C3_camera_level_explicit_minus1_not_0",
          (m.group(0) if m else "") + " " + (mid.group(0) if mid else ""))

    # 跟踪不可用：null 丢弃（回归）
    lines = load_fixture(FRESH, "envelope_only")
    m = re.search(r"^det=(\d+) no_track=(\d+)$", "\n".join(lines), re.M)
    check(m is not None and m.group(1) == "0" and m.group(2) == "1",
          "C3_TC-T-TRK-02_null_track_dropped_never_0", m.group(0) if m else None)

    # 容量上限：超限显式 -1，不伪造 id
    lines = load_fixture(FRESH, "max_tracks")
    m = re.search(r"^ids=(-?\d+),(-?\d+) untracked=(\d+)$", "\n".join(lines), re.M)
    check(m is not None and m.group(1) == "1" and m.group(2) == "-1" and m.group(3) == "1",
          "C3_max_tracks_surplus_explicitly_untracked", m.group(0) if m else None)

    # id 永不 0：全部 fixture/selftest 行内无 "id 0" / "id=0" 呈证
    ids_all = []
    for fx in FIXTURES:
        ids_all.extend(load_fixture(FRESH, fx))
    st = load_log(FRESH, "selftest")
    ids_all.append(st)
    bad = re.findall(r"ids=\b0\b|det_id=(0)(?!-)|track=0", "\n".join(ids_all))
    check(not bad, "C3_no_zero_track_id_anywhere", bad[:3])

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
    mcfg = re.search(r"^events=(\d+)$", cfg, re.M)
    check(mcfg is not None and mcfg.group(1) == "1",
          "C3_config_lightweight_confirms_within_budget", mcfg.group(0) if mcfg else None)

    # log 与 JSON 两个输出面逐行恒等（同 ITEM-012 kv 分表教训）
    jdiffs = []
    for kind in ["selftest"] + ["fixture-" + fx for fx in FIXTURES] + ["config"]:
        jl = load_json_lines(FRESH, kind)
        log_lines = [ln.rstrip("\n") for ln in load_log(FRESH, kind).splitlines()]
        if jl != log_lines:
            jdiffs.append(kind)
    check(not jdiffs, "C3_json_lines_match_log", jdiffs or "all %d identical" % (len(FIXTURES) + 2))

    # ---- C4 selftest 独立统计（判例名与探针源 check( 第二实参恒等，不手抄）----
    sl = load_log(FRESH, "selftest")
    n_pass = sl.count("[PASS]")
    n_fail = sl.count("[FAIL]")
    check(n_pass == SELFTEST_N and n_fail == 0, "C4_selftest_case_lines",
          "pass=%d fail=%d (expect %d)" % (n_pass, n_fail, SELFTEST_N))
    src_names = []
    srcp = os.path.join(REPO, "app", "src", "core", "tools", "tracking_probe.cpp")
    with open(srcp, encoding="utf-8") as fh:
        vsrc = fh.read()
    for mm in re.finditer(r"\bcheck\(", vsrc):
        if vsrc[max(0, mm.start() - 7):mm.start()].endswith("static "):
            continue
        i = mm.end()
        d = 0
        while i < len(vsrc):
            ch = vsrc[i]
            if ch in "([{":
                d += 1
                i += 1
            elif ch in ")]}":
                if d == 0:
                    break
                d -= 1
                i += 1
            elif ch == '"':
                i += 1
                while i < len(vsrc) and vsrc[i] != '"':
                    if vsrc[i] == "\\":
                        i += 1
                    i += 1
                i += 1
            elif ch == "," and d == 0:
                break
            else:
                i += 1
        j = i + 1
        while j < len(vsrc) and vsrc[j] in " \t\r\n":
            j += 1
        if vsrc[j] == '"':
            j += 1
            k = j
            while k < len(vsrc) and vsrc[k] != '"':
                k += 1
            src_names.append(vsrc[j:k])
    src_set = set(src_names)
    log_names = set()
    for ln in sl.splitlines():
        if ln.startswith("[PASS] ") or ln.startswith("[FAIL] "):
            body = ln[len("[PASS] "):]
            log_names.add(body.split(" :: ", 1)[0])
    check(src_set == log_names and n_fail == 0,
          "C4_selftest_names_match_source", "src=%d log=%d only_src=%s only_log=%s" %
          (len(src_set), len(log_names), sorted(src_set - log_names)[:3],
           sorted(log_names - src_set)[:3]))

    summary = {"passed": g_pass, "failed": g_fail, "fail_names": g_fail_names,
               "fresh": FRESH, "impl": IMPL}
    with open(os.path.join(HERE, "item008-compare-%s.json" % FRESH), "w", encoding="utf-8") as fh:
        json.dump(summary, fh, ensure_ascii=False, indent=2)
    print("COMPARE SUMMARY passed=%d/%d" % (g_pass, g_pass + g_fail), flush=True)
    return 0 if g_fail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
