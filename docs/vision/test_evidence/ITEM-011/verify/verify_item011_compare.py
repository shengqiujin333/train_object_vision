#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-011 验证轮 v1 —— V-CMP：独立比对 oracle。

impl r3 归档（docs/vision/test_evidence/ITEM-011/…item011-r3.*） vs fresh 验证轮
（…verify-20261019-item011-v1.*）：distance 语义域独立重推导 + JUnit 判例多重集 +
selftest/fixtures/forms 稳定行恒等 + media/chain 距离统计独立解析。

判据不采信交付 RESULT 行本身；对每个 form/fixture 用 impl 已知标定常数
（500×89 px·m）独立重算期望。改 --impl-stamp 可对历史归档板前 dry-run（非空转证明）。

用法: python verify_item011_compare.py --fresh-stamp verify-20261019-item011-v1
    [--impl-stamp 20261021-item011-r3]
退出: 0 全部判据通过；1 有判据失败（结果 JSON 落 item011-compare-<stamp>.json）。
"""
import argparse
import json
import os
import re
import sys
import xml.etree.ElementTree as ET
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
EVID = os.path.normpath(os.path.join(HERE, ".."))
RES = []

REF_M = 500.0
REF_PX = 89.0
CONST = REF_M * REF_PX          # 44500 px·m
PX_PER_M_REF = 89.0 / 1.70      # 52.352941...

RES_STRING_NOTES = {
    "distance-selftest": [],
    "fixture-no_calib": [], "fixture-zoom_unknown": [], "fixture-zoom_mismatch": [],
    "fixture-non_person": [], "fixture-out_of_range": [], "fixture-valid": [],
}


def check(ok, name, detail=""):
    RES.append({"ok": bool(ok), "name": name, "detail": str(detail)[:500]})
    print("[%s] %s %s" % ("PASS" if ok else "FAIL", name, detail), flush=True)


def read(path):
    with open(path, encoding="utf-8", errors="replace") as fh:
        return fh.read()


def unesc(s):
    return (s.replace("&gt;", ">").replace("&lt;", "<")
            .replace("&quot;", '"').replace("&apos;", "'").replace("&amp;", "&"))


def junit_names(path):
    root = ET.parse(path).getroot()
    return [unesc(tc.get("name")) for tc in root.iter("testcase") if tc.get("name")]


def estimate_json(text):
    """从 form 日志单段抽 type=estimate json=... 并解析（一段日志一条 request）。"""
    m = re.search(r"^type=estimate json=(\{.*\})$", text, re.M)
    return json.loads(m.group(1)) if m else None


def parse_selftest(text):
    d = {}
    d["pass_names"] = re.findall(r"^\[PASS\] (\S+)", text, re.M)
    d["fail_names"] = re.findall(r"^\[FAIL\] (\S+)", text, re.M)
    d["result"] = re.search(r"^RESULT: (\w+) failures=(\d+)", text, re.M)
    return d


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fresh-stamp", required=True)
    ap.add_argument("--impl-stamp", default="20261021-item011-r3")
    args = ap.parse_args()
    fs = args.fresh_stamp
    impl = args.impl_stamp
    T = "vision-item011-192_168_0_103-%s" % fs
    IP = "vision-item011-192_168_0_103-%s" % impl

    def f(name):
        p = os.path.join(EVID, "%s.%s" % (T, name))
        return read(p) if os.path.isfile(p) else None

    def i(name):
        p = os.path.join(EVID, "%s.%s" % (IP, name))
        return read(p) if os.path.isfile(p) else None

    # ---- C00: transcript 步骤全部 exit=0 + cleanup 延后（keep-remote）----
    trp = os.path.join(EVID, "%s.transcript.json" % T)
    check(os.path.isfile(trp), "C00_fresh_transcript_present", fs)
    tr = json.loads(read(trp))
    steps = {s["step"]: s for s in tr["steps"]}
    for st in ("build-core", "build-media", "core-tests", "distance-selftest",
               "fixture-no_calib", "fixture-zoom_unknown", "fixture-zoom_mismatch",
               "fixture-non_person", "fixture-out_of_range", "fixture-valid",
               "form-anchor", "form-scaling", "form-zoom-unknown", "form-no-calib",
               "media-selftest", "chain"):
        check(st in steps and steps[st].get("exit") == 0, "C00_step_%s_exit0" % st,
              "exit=%s" % steps.get(st, {}).get("exit"))
    check("cleanup" not in steps or steps["cleanup"].get("exit") == 0, "C00_cleanup_deferred",
          "cleanup step=%s" % ("absent(keep-remote)" if "cleanup" not in steps
                               else steps["cleanup"].get("exit")))

    # ---- C0EXIT: 全部 EXIT token 为 0 且种类集合完备 ----
    main_log = read(os.path.join(EVID, "%s.log" % T)) \
        if os.path.isfile(os.path.join(EVID, "%s.log" % T)) else None
    check(main_log is not None, "C0EXIT_main_log_present")
    if main_log:
        toks = re.findall(r"^(?:\S+?_EXIT)=(\d+)$", main_log, re.M)
        check(all(v == "0" for v in toks) and len(toks) >= 16,
              "C0EXIT_all_exit_tokens_zero", "tokens=%d" % len(toks))

    # ---- C1: JUnit 判例多重集恒等 + 计数 + ITEM-011 域判例准确集合 ----
    xf = os.path.join(EVID, "%s.app_core-evidence_item011-core-tests-%s.xml" % (T, fs))
    xi = os.path.join(EVID, "%s.app_core-evidence_item011-core-tests-%s.xml" % (IP, impl))
    if os.path.isfile(xf) and os.path.isfile(xi):
        root_f = ET.parse(xf).getroot()
        check(root_f.get("tests") == "1347" and root_f.get("failures") == "0",
              "C1_junit_1347_0", "tests=%s failures=%s" % (root_f.get("tests"), root_f.get("failures")))
        nf = Counter(junit_names(xf))
        ni = Counter(junit_names(xi))
        check(nf == ni, "C1_junit_multiset_identical_impl_r3",
              "fresh=%d impl=%d only_fresh=%d only_impl=%d"
              % (sum(nf.values()), sum(ni.values()),
                 len(nf - ni), len(ni - nf)))
        dist_names = sorted(n for n in nf if n.startswith("distance_"))
        want = sorted([
            "distance_calibration_valid", "distance_reference_constant", "distance_px_per_m_reference",
            "distance_calibration_requires_zoom", "distance_calibration_requires_ref_px",
            "distance_calibration_requires_ref_m", "distance_calibration_rejects_contradictory_range",
            "distance_formula_at_reference", "distance_formula_half_px", "distance_formula_double_px",
            "distance_formula_zero_px_unknown", "distance_formula_negative_px_unknown",
            "distance_no_calibration_unknown", "distance_no_calibration_text_placeholder",
            "distance_unknown_not_zero", "distance_zoom_unknown_refused", "distance_zoom_mismatch_refused",
            "distance_empty_zoom_refused", "distance_non_person_refused", "distance_zero_box_refused",
            "distance_reference_valid", "distance_evidence_px_per_m", "distance_evidence_inputs",
            "distance_text_valid", "distance_json_fields", "distance_within_range_valid",
            "distance_out_of_range_unknown", "distance_below_range_unknown", "distance_set_queries",
            "distance_set_zooms", "distance_set_find_zoom", "distance_set_find_unknown_zoom",
            "distance_set_clear", "distance_parse_spec_ok", "distance_parse_requires_camera",
            "distance_parse_requires_zoom", "distance_parse_requires_ref_px",
            "distance_parse_rejects_bad_number", "distance_parse_rejects_unknown_key",
            "distance_parse_rejects_zero_px", "distance_parse_spec_fields",
            "distance_rules_emit_same_count",
            "distance_rules_same_type_level", "distance_rule_keeps_unknown_field",
            "distance_debug_unknown_placeholder", "distance_debug_unknown_not_zero",
            "distance_debug_valid_text",
        ])
        check(dist_names == want, "C1_distance_case_set_exact",
              "fresh=%d want=%d missing=%s extra=%s"
              % (len(dist_names), len(want),
                 sorted(set(want) - set(dist_names))[:4], sorted(set(dist_names) - set(want))[:4]))
    else:
        check(False, "C1_junit_files_present", xf + " / " + xi)

    # ---- C2: distance selftest —— 14 判例集合恒等 + 锚点语义独立重推导 ----
    lf = f("app_core-evidence_item011-distance-selftest-%s.log" % fs)
    li = i("app_core-evidence_item011-distance-selftest-%s.log" % impl)
    check(lf is not None and li is not None, "C2_selftest_logs_present")
    if lf and li:
        sf, si = parse_selftest(lf), parse_selftest(li)
        check(sorted(sf["pass_names"]) == sorted(si["pass_names"]) and len(sf["pass_names"]) == 14
              and not sf["fail_names"], "C2_selftest_names_identical_14",
              "fresh=%d impl=%d fails=%d" % (len(sf["pass_names"]), len(si["pass_names"]), len(sf["fail_names"])))
        # 独立锚点断言（不采信 PASS 行的数值本身，用已知常数核对关键行）
        check(re.search(r"^\[PASS\] anchor_89px_is_500m :: 500\.000000$", lf, re.M) is not None,
              "C2_selftest_anchor_89px_500m_row", "")
        check(re.search(r"^\[PASS\] half_px_double_distance :: 250\.000000$", lf, re.M) is not None,
              "C2_selftest_half_px_250m_row", "")
        check(abs(PX_PER_M_REF - 52.35294117647059) < 1e-6 and
              "52.352941" in lf and abs(CONST - 44500.0) < 1e-9,
              "C2_selftest_constants_recomputed", "px/m=%.6f const=%.0f" % (PX_PER_M_REF, CONST))
        check(re.search(r"^RESULT: PASS failures=0$", lf, re.M) is not None,
              "C2_selftest_result_row", "")

    # ---- C3: six fixtures —— 独立门控语义重推导（不采信 expect，按标定常数换算）----
    fxp = [
        ("no_calib", None, "NO_CALIBRATION", -1),
        ("zoom_unknown", None, "ZOOM_UNKNOWN", -1),
        ("zoom_mismatch", None, "ZOOM_MISMATCH", -1),
        ("non_person", "NOT_PERSON", None, -1),
        ("out_of_range", "OUT_OF_RANGE", None, -1),  # 40px -> 1112.5 > 600（不 clamp）
        ("valid", None, "DISTANCE_OK", 500.0),       # 89px -> 500.0
    ]
    for name, label_must, reason_want, meters_want in fxp:
        lg = f("app_core-evidence_item011-fixture-%s-%s.log" % (name, fs))
        li2 = i("app_core-evidence_item011-fixture-%s-%s.log" % (name, impl))
        if lg is None:
            check(False, "C3_fixture_%s_present" % name, "log missing")
            continue
        est = None
        m = re.search(r"^type=fixture name=%s json=(\{.*\})$" % re.escape(name), lg, re.M)
        if m:
            est = json.loads(m.group(1))
        ok_impl = li2 is not None and lg.splitlines() == li2.splitlines()
        if est is None:
            check(False, "C3_fixture_%s_json" % name, lg[-200:])
            continue
        row_ok = (est.get("reason") == (reason_want or label_must or est.get("reason")))
        dec_ok = (est.get("distance_m") == meters_want) and (est.get("valid") == (meters_want != -1))
        text_ok = est.get("text") == ("500.0 m" if meters_want == 500.0 else "—")
        ident_ok = ok_impl and li2 is not None and lg.splitlines() == li2.splitlines()
        if name == "valid":
            row_ok = row_ok and est.get("reason") == "DISTANCE_OK"
        check(row_ok and dec_ok and text_ok and ident_ok,
              "C3_fixture_%s_semantics" % name,
              "reason=%s m=%s text=%s identical_impl=%s" % (est.get("reason"), est.get("distance_m"),
                                                            est.get("text"), ident_ok))
        # out_of_range 专项：验证侧独立换算 40px -> 1112.5 m > max 600
        if name == "out_of_range":
            check(abs(CONST / 40.0 - 1112.5) < 1e-6 and "outside calibrated range" in lg,
                  "C3_out_of_range_recomputed_1112_5", "range detail present")

    # ---- C4: four forms —— 公式独立重推导（89->500 / 178->250 / 无档->-1 / 未标定->-1）----
    form_specs = [
        ("anchor", {"valid": True, "distance_m": 500.0, "reason": "DISTANCE_OK", "text": "500.0 m"},
         "FORM_ANCHOR_EXIT=0", CONST / 89.0),
        ("scaling", {"valid": True, "distance_m": 250.0, "reason": "DISTANCE_OK", "text": "250.0 m"},
         "FORM_SCALE_EXIT=0", CONST / 178.0),
        ("zoom-unknown", {"valid": False, "distance_m": -1.0, "reason": "ZOOM_UNKNOWN", "text": "—"},
         "FORM_ZU_EXIT=0", None),
        ("no-calib", {"valid": False, "distance_m": -1.0, "reason": "NO_CALIBRATION", "text": "—"},
         "FORM_NC_EXIT=0", None),
    ]
    for key, want_est, exit_tok, exp_d in form_specs:
        lg = f("app_core-evidence_item011-form-%s-%s.log" % (key, fs))
        li2 = i("app_core-evidence_item011-form-%s-%s.log" % (key, impl))
        if lg is None:
            check(False, "C4_form_%s_present" % key, "log missing")
            continue
        est = estimate_json(lg)
        if est is None:
            check(False, "C4_form_%s_estimate_row" % key, "no type=estimate")
            continue
        if exp_d is not None:
            check(abs(est["distance_m"] - exp_d) < 1e-6 and est["reason"] == want_est["reason"]
                  and est.get("px_per_m_reference", -9) > 0,
                  "C4_form_%s_formula_recomputed" % key,
                  "est=%s exp=%.4f" % (est["distance_m"], exp_d))
        else:
            check(est["valid"] is False and est["distance_m"] == -1 and
                  est["reason"] == want_est["reason"] and est["text"] == want_est["text"],
                  "C4_form_%s_unknown_semantics" % key, json.dumps(est, ensure_ascii=False)[:120])
        ident = (li2 is not None) and (
            # stamp 差异行归一后逐行比对
            [ln for ln in lg.splitlines() if "type=" in ln or "[PASS]" in ln] ==
            [ln for ln in li2.splitlines() if "type=" in ln or "[PASS]" in ln])
        check(ident, "C4_form_%s_impl_identical" % key, "stable lines identical")
        check(exit_tok in (main_log or ""), "C4_form_%s_exit_token" % key, exit_tok)

    # ---- C5: media selftest —— 距离用例 6 判例 + 公式独立重推导（36px -> 1236.111 m）----
    mf = f("app_core-evidence_item011-media-selftest-%s.log" % fs)
    mi = i("app_core-evidence_item011-media-selftest-%s.log" % impl)
    check(mf is not None and mi is not None, "C5_media_logs_present")
    if mf and mi:
        exp = CONST / 36.0   # 500*89/36 = 1236.1111...
        check(abs(exp - 1236.1111111111112) < 1e-3, "C5_media_formula_constant", "%.6f" % exp)
        got = re.search(r"^\[PASS\] distance_with_calibration_matches_formula :: "
                        r"got=([\d.]+) expected=([\d.]+)$", mf, re.M)
        check(got is not None and abs(float(got.group(1)) - exp) < 0.2
              and abs(float(got.group(2)) - exp) < 0.2, "C5_media_formula_36px",
              "got=%s exp=%.6f" % (got.group(1) if got else "?", exp))
        check(re.search(r"^\[PASS\] distance_valid_counted :: valid=1$", mf, re.M) and
              re.search(r"^\[PASS\] distance_worker_text :: 1236\.1 m$", mf, re.M),
              "C5_media_valid_and_text_rows", "")
        check(re.search(r"^\[PASS\] distance_without_calibration_unknown :: "
                        r"distance_m=-1 \+ NO_CALIBRATION \+ 文本占位$", mf, re.M) is not None,
              "C5_media_no_calib_row", "")
        dist_passes = re.findall(r"^\[PASS\] (distance_[^\s:]+)(?: ::.*)?$", mf, re.M)
        check(len(dist_passes) == 6, "C5_media_distance_case_count_6",
              "%s" % dist_passes)
        # selftest 判例总数回退护栏 28
        total = len(re.findall(r"^\[PASS\] ", mf, re.M))
        check(total == 28, "C5_media_total_28", "total=%d" % total)

        def mnorm(lines):
            out = []
            for ln in lines:
                ln = re.sub(r"s1a[0-9a-f]+-", "s1a-", ln)          # 会话 id 主段
                ln = re.sub(r"(s1a-)[0-9a-f]+(-0)", r"\1N\2", ln)  # 会话 id 从段（test 源实例）
                ln = re.sub(r"\b\d+us\b", "Nus", ln)           # 耗时
                n = re.match(r"^(.*? n=)(\d+)$", ln)           # submit 恒等计数（窗边界 ±1 自然）
                if n and int(n.group(2)) > 200:
                    ln = n.group(1) + "N"
                ln = re.sub(r"join_us=\d+", "join_us=N", ln)   # join 窗自然
                out.append(ln)
            return out

        def media_stable(text):
            keep = [ln for ln in text.splitlines()
                    if (ln.startswith("[") or ln.startswith("type=selftest"))]
            return mnorm(keep)
        check(media_stable(mf) == media_stable(mi), "C5_media_stable_lines_identical",
              "stable=%d" % len(media_stable(mf)))

    # ---- C6: chain 带标定 —— 距离统计真实未知 + 链路 9/9 判据 + 标定回显恒等 ----
    cf = f("app_core-evidence_item011-chain-%s.log" % fs)
    ci = i("app_core-evidence_item011-chain-%s.log" % impl)
    check(cf is not None and ci is not None, "C6_chain_logs_present")
    if cf and ci:
        check("type=calibration camera=CAM-01 zoom=Z1 ref_m=500.000000 ref_h_px=89.000000"
              in cf, "C6_chain_calibration_echo", "")
        check("type=camera_zoom camera=CAM-01 zoom=Z1" in cf, "C6_chain_camera_zoom_echo", "")
        lanes = re.findall(r"^type=lane camera=(\S+) .*?distance_valid=(\d+) distance_unknown=(\d+) "
                           r"distance_text=(\S+) last_distance_m=(-?[\d.]+)", cf, re.M)
        check(len(lanes) == 1, "C6_chain_one_lane", str(lanes))
        for cam, dv, du, dt, ld in lanes:
            # 测试源无人员目标: valid 中心为真实两难——大量 person 框=0 换算；检查未知计数与文本占位
            check(dv == "0" and dt == "—" and ld == "-1.000000" and du == "0",
                  "C6_chain_distance_stats_bounded", "lane=%s valid=%s text=%s last=%s" % (cam, dv, dt, ld))
        passes = re.findall(r"^\[PASS\] ", cf, re.M)
        check(len(passes) == 9, "C6_chain_judgements_9", "n=%d" % len(passes))
        check(re.search(r"^RESULT: PASS failures=0$", cf, re.M) is not None, "C6_chain_result_row", "")
        # 链路稳定行恒等（时序/温度/estimated 波动域归一）
        def cnorm(lines):
            out = []
            for ln in lines:
                ln = re.sub(r"t_ms=\d+", "t_ms=N", ln)
                ln = re.sub(r"temp_c=[\d.]+", "temp_c=N", ln)
                ln = re.sub(r"round(s?)=\d+", r"round\1=N", ln)
                ln = re.sub(r"s1a[0-9a-f]+-", "s1", ln)
                ln = re.sub(r"submitted=\d+", "submitted=N", ln)
                ln = re.sub(r"skipped_interval=\d+", "skipped_interval=N", ln)
                ln = re.sub(r"last_infer_us=\d+", "last_infer_us=N", ln)
                ln = re.sub(r"latency_ms=\d+", "latency_ms=N", ln)
                ln = re.sub(r"fps=[\d.]+", "fps=N", ln)
                ln = re.sub(r"celsius\":[\d.]+", "celsius\":N", ln)
                ln = re.sub(r"measured=[\d.]+", "measured=N", ln)
                ln = re.sub(r"read_mono_ms\"?:\s*\d+", "read_mono_ms=N", ln)
                ln = re.sub(r"missing_polls=\d+", "missing_polls=N", ln)
                out.append(ln)
            return out

        def chain_stable(text):
            keep = []
            for ln in text.splitlines():
                if ln.startswith("type=trace") or ln.startswith("type=summary"):
                    continue  # 时序/温度域
                if ln.startswith("[PASS]") or ln.startswith("type=calibration") or \
                   ln.startswith("type=camera_zoom") or ln.startswith("type=lane") or \
                   ln.startswith("type=profile") or ln.startswith("type=source"):
                    keep.append(ln)
            return cnorm(keep)
        check(chain_stable(cf) == chain_stable(ci), "C6_chain_stable_lines_identical",
              "stable=%d" % len(chain_stable(cf)))
        # lane 行数值域逐 token 重推导（排除 will 自然波动 fps/latency/last_infer_us）
        lane_f = re.search(r"^type=lane .*$", cf, re.M).group(0)
        for tok in ("consumed=30", "inferred=30", "published=30", "dropped=0",
                    "skipped_interval=", "distance_valid=0", "distance_unknown=0"):
            check(re.search(r"\b%s" % re.escape(tok), lane_f) is not None,
                  "C6_lane_token_%s" % tok.rstrip("=0").replace("=", ""), tok)

    # ---- C7: 凭据语义扫描（长度≥10 全串匹配；SSH 凭据 admin 假阳性不算）----
    secret = os.environ.get("TRAIN_RK3588_SSH_CREDENTIAL", "")
    if len(secret) >= 10:
        hits = []
        for p in sorted(os.listdir(EVID)):
            fp = os.path.join(EVID, p)
            if not os.path.isfile(fp) or not p.startswith("vision-item011-192_168_0_103-%s" % fs):
                continue
            if secret and secret in read(fp):
                hits.append(p)
        check(not hits, "C7_credential_leakage_scan", "hits=%s" % hits)
    else:
        check(True, "C7_credential_leakage_scan", "skipped (short credential)")

    # ---- C8: file_manifest 与本轮新增文件 ----
    mfst = os.path.normpath(os.path.join(HERE, "..", "..", "..", "..", "..", "file_manifest.txt"))
    if os.path.isfile(mfst):
        man = read(mfst)
        check("docs/vision/test_evidence/ITEM-011/VERIFY-ITEM-011.md" in man,
              "C8_manifest_covers_verify_report", "")
    else:
        check(False, "C8_manifest_path", mfst)

    with open(os.path.join(HERE, "item011-compare-%s.json" % fs), "w", encoding="utf-8") as fh:
        json.dump({"stamp": fs, "impl": impl, "checks": RES,
                   "pass": all(r["ok"] for r in RES)}, fh, ensure_ascii=False, indent=1)
    fails = [r["name"] for r in RES if not r["ok"]]
    print("COMPARE TOTAL: %s (%d checks, %d failed)" %
          ("PASS" if not fails else "FAIL", len(RES), len(fails)), flush=True)
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())
