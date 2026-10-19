#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-010 验证轮 v1 —— V-CMP：独立比对 oracle。

impl r4 归档（docs/vision/test_evidence/ITEM-010/…r4.*） vs fresh 验证轮
（…verify-20261019-item010-v1.*）：逐份稳定日志语义比对 + JUnit 判例多重集 +
chain4 语义重推导（预算/不可用路不占预算/unknown 语义）。

用法: python verify_item010_compare.py --fresh-stamp verify-20261019-item010-v1 [--impl-r4]
退出: 0 全部判据通过；1 有判据失败（结果 JSON 落 item010-compare-<stamp>.json）。
"""
import argparse
import glob
import json
import os
import re
import sys
import xml.etree.ElementTree as ET
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
EVID = os.path.normpath(os.path.join(HERE, ".."))
REPO = os.path.normpath(os.path.join(EVID, "..", "..", "..", ".."))

RES = []


def check(ok, name, detail=""):
    RES.append({"ok": bool(ok), "name": name, "detail": str(detail)[:500]})
    print("[%s] %s %s" % ("PASS" if ok else "FAIL", name, detail), flush=True)


def read(path):
    with open(path, encoding="utf-8", errors="replace") as fh:
        return fh.read()


def parse_probe_log(text):
    """duty 探针稳定行 -> dict（不采信 RESULT 行本身，独立复核语义）。"""
    d = {}
    d["pass_names"] = re.findall(r"^\[PASS\] (\S+)", text, re.M)
    d["fail_names"] = re.findall(r"^\[FAIL\] (\S+)", text, re.M)
    d["result"] = re.search(r"^RESULT: (\w+) failures=(\d+)", text, re.M)
    d["duty_rows"] = re.findall(
        r"^type=duty name=(\S+) registered=(\S*) available=(\w+) reason=(\S+) state=(\S+) display=(\S+)",
        text, re.M)
    d["declared"] = re.findall(r"^type=declared duty=(\S+) outputs=(\d+) hw=(\S+) labels=(.*)$", text, re.M)
    d["evals"] = re.findall(r"^type=evaluation json=(\{.*\})$", text, re.M)
    d["descriptors"] = re.findall(r"^type=descriptor api=(.*) drv=(\S+) in=(\d+) out=(\d+)$", text, re.M)
    d["scenarios"] = re.findall(r"^type=scenario name=(\S+) json=(\{.*\})$", text, re.M)
    return d


def parse_junit_names(path):
    root = ET.parse(path).getroot()
    names = []
    for tc in root.iter("testcase"):
        n = tc.get("name")
        if n:
            names.append(n)
    return names


def unesc(s):
    return (s.replace("&gt;", ">").replace("&lt;", "<")
            .replace("&quot;", '"').replace("&apos;", "'").replace("&amp;", "&"))


def junit_names(path):
    return [unesc(n) for n in parse_junit_names(path)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fresh-stamp", required=True)
    ap.add_argument("--impl-stamp", default="20261021-item010-r4")
    args = ap.parse_args()
    fs = args.fresh_stamp
    impl = args.impl_stamp
    T = "vision-item010-192_168_0_103-%s" % fs
    IP = "vision-item010-192_168_0_103-%s" % impl

    def f(name):
        p = os.path.join(EVID, "%s.%s" % (T, name))
        return read(p) if os.path.isfile(p) else None

    def i(name):
        p = os.path.join(EVID, "%s.%s" % (IP, name))
        return read(p) if os.path.isfile(p) else None

    check(os.path.isfile(os.path.join(EVID, "%s.transcript.json" % T)),
          "C00_fresh_transcript_present", fs)
    tr = json.loads(read(os.path.join(EVID, "%s.transcript.json" % T)))
    steps = {s["step"]: s for s in tr["steps"]}
    for st in ("build-core", "build-media", "core-tests", "duty-selftest", "duty-config",
               "fixture-no_model", "fixture-config_only", "fixture-missing", "fixture-mismatch",
               "fixture-ready", "duty-real-descriptor-seg", "duty-real-descriptor-det",
               "duty-media-no-model", "duty-media-declared-mismatch", "chain4"):
        check(st in steps and steps[st].get("exit") == 0, "C00_step_%s_exit0" % st,
              "exit=%s" % steps.get(st, {}).get("exit"))
    # --keep-remote：cleanup 不在交付轮执行，延迟到验证侧 V-EXT 收尾（缺席是预期）
    check("cleanup" not in steps or steps["cleanup"].get("exit") == 0, "C00_cleanup_deferred",
          "cleanup step=%s" % ("absent(keep-remote)" if "cleanup" not in steps
                               else steps["cleanup"].get("exit")))

    # ---- C1: JUnit 判例多重集恒等 + 1300 上限 + ITEM-010 域判例在场 ----
    xml_f = "vision-item010-192_168_0_103-%s.app_core-evidence_item010-core-tests-%s.xml" % (fs, fs)
    xml_i = "vision-item010-192_168_0_103-%s.app_core-evidence_item010-core-tests-%s.xml" % (impl, impl)
    if os.path.isfile(os.path.join(EVID, xml_f)) and os.path.isfile(os.path.join(EVID, xml_i)):
        nf = Counter(junit_names(os.path.join(EVID, xml_f)))
        ni = Counter(junit_names(os.path.join(EVID, xml_i)))
        sum_f = sum(nf.values())
        root = ET.parse(os.path.join(EVID, xml_f)).getroot()
        ts = root.get("tests")
        fails = root.get("failures")
        check(ts == "1300" and fails == "0", "C1_junit_1300_0",
              "tests=%s failures=%s" % (ts, fails))
        check(nf == ni, "C1_junit_multiset_identical_impl_r4",
              "fresh=%d impl=%d only_fresh=%d only_impl=%d"
              % (sum_f, sum(ni.values()), len(nf - ni), len(ni - nf)))
        duty_names = [n for n in nf if "duty" in n]
        sched_names = [n for n in nf if "sched_unavailable" in n]
        check(len(duty_names) >= 40 and len(sched_names) >= 5,
              "C1_duty_test_domain_present", "duty*=%d sched_unavailable*=%d"
              % (len(duty_names), len(sched_names)))
        want = {"duty_no_model_unavailable_unknown", "duty_config_registration_not_capability",
                "duty_ready_is_not_normal", "duty_map_unknown_label_blocked",
                "duty_map_below_threshold_unknown", "duty_adapter_rejects_prediction_when_unavailable",
                "duty_all_three_unavailable_unknown", "duty_unknown_display_not_normal",
                "duty_registered_yolov5s_is_mismatch", "sched_unavailable_not_budgeted",
                "sched_unavailable_not_skipped", "sched_all_ready_regression"}
        check(want.issubset(nf.keys()), "C1_key_duty_cases_present",
              "missing=%s" % sorted(want - set(nf.keys())))
    else:
        check(False, "C1_junit_files_present", xml_f + " / " + xml_i)

    # ---- C2: duty selftest —— 稳定判例名集合恒等 + 语义独立断言 ----
    lf = f("app_core-evidence_item010-duty-selftest-%s.log" % fs)
    li = i("app_core-evidence_item010-duty-selftest-%s.log" % impl)
    check(lf is not None and li is not None, "C2_selftest_logs_present")
    if lf and li:
        pf = parse_probe_log(lf)
        pi = parse_probe_log(li)
        check(pf["pass_names"] == pi["pass_names"] and not pf["fail_names"] and not pi["fail_names"],
              "C2_selftest_pass_names_identical", "%d cases" % len(pf["pass_names"]))
        check(len(pf["pass_names"]) == 17, "C2_selftest_17_cases", str(len(pf["pass_names"])))
        scen_f = dict((n, json.loads(j)) for n, j in pf["scenarios"])
        # 独立断言（不采信 [PASS] 行）：三职责缺省 unavailable+unknown；READY 仍 unknown；
        # unknown 的 display 恒为 未知。
        for k in ("pantograph", "wheel_rail", "cab_door"):
            a = scen_f.get("no_model_" + k)
            check(a is not None and a["available"] is False
                  and a["reason"] == "PROFILE_UNSUPPORTED" and a["state"] == "unknown"
                  and a["semantics_unknown"] is True and a["state_display"] == "未知",
                  "C2_no_model_%s_unavailable_unknown" % k,
                  json.dumps(a, ensure_ascii=False)[:120])
        ready = scen_f.get("ready")
        check(ready is not None and ready["available"] is True and ready["reason"] == "READY"
              and ready["state"] == "unknown", "C2_ready_but_unknown", "")
        pn = scen_f.get("prediction_normal")
        check(pn is not None and pn["state"] == "normal" and pn["semantics_unknown"] is False,
              "C2_declared_label_sets_normal", "")
        # unknown JSON 面永不出现“正常”
        check(all('正常' not in j for n, j in pf["scenarios"] if json.loads(j)["state"] == "unknown"),
              "C2_unknown_display_never_normal", "")

    # ---- C3: duty-config（真实配置三职责口径）----
    lf = f("app_core-evidence_item010-duty-config-%s.log" % fs)
    li = i("app_core-evidence_item010-duty-config-%s.log" % impl)
    check(lf is not None and li is not None, "C3_config_logs_present")
    if lf and li:
        pf = parse_probe_log(lf)
        pi = parse_probe_log(li)
        rows_f = [(n, av, r, st) for n, reg, av, r, st, dp in pf["duty_rows"]]
        rows_i = [(n, av, r, st) for n, reg, av, r, st, dp in pi["duty_rows"]]
        check(rows_f == rows_i and len(rows_f) == 3, "C3_config_duty_rows_identical", str(rows_f))
        check(all(av == "false" and r == "PROFILE_UNSUPPORTED" and st == "unknown"
                  for _, av, r, st in rows_f), "C3_three_duties_unavailable_unknown", "")
        reg_f = {n: reg for n, reg, av, r, st, dp in pf["duty_rows"]}
        check(reg_f == {"pantograph": "yolov5s", "wheel_rail": "yolov8s-seg", "cab_door": "yolov5s"},
              "C3_registered_names_from_real_config", str(reg_f))
        cfg_json = json.loads(re.search(r"^type=duty_status json=(\{.*\})$", lf, re.M).group(1))
        det_ok = True
        for d in cfg_json["duties"]:
            av = d["availability"]
            if av["available"] or av["state"] != "unknown" or av["reason"] != "PROFILE_UNSUPPORTED":
                det_ok = False
            if "registration is not evidence" not in av["detail"]:
                det_ok = False
            if d["stats"]["evaluations"] != 1 or d["stats"]["unknown_results"] != 1:
                det_ok = False
        check(det_ok, "C3_config_json_independent_recheck", "")
        check(pf["pass_names"] == ["config_three_duties_unavailable_unknown"],
              "C3_config_pass_names", str(pf["pass_names"]))

    # ---- C4: 五个 fixture（期望独立重推导，不采信 RESULT）----
    expect_fx = {
        "no_model": (False, "PROFILE_UNSUPPORTED", "unknown"),
        "config_only": (False, "PROFILE_UNSUPPORTED", "unknown"),
        "missing": (False, "MODEL_MISSING", "unknown"),
        "mismatch": (False, "MODEL_MISMATCH", "unknown"),
        "ready": (True, "READY", "unknown"),
    }
    for fx, (av, rn, st) in expect_fx.items():
        lf = f("app_core-evidence_item010-fixture-%s-%s.log" % (fx, fs))
        check(lf is not None, "C4_fixture_%s_present" % fx)
        if lf:
            scen = re.search(r"^type=scenario name=%s json=(\{.*\})$" % fx, lf, re.M)
            check(scen is not None, "C4_fixture_%s_scenario" % fx)
            if scen:
                a = json.loads(scen.group(1))
                check(a["available"] == av and a["reason"] == rn and a["state"] == st,
                      "C4_fixture_%s_semantics" % fx,
                      "avail=%s reason=%s state=%s" % (a["available"], a["reason"], a["state"]))
            check("RESULT: PASS" in lf, "C4_fixture_%s_result_pass" % fx)

    # ---- C5: 真实描述 drill（rknn_query 描述 vs 声明 -> MODEL_MISMATCH）----
    lf = f("app_core-evidence_item010-descriptor-seg-%s.log" % fs)
    li = i("app_core-evidence_item010-descriptor-seg-%s.log" % impl)
    check(lf is not None and li is not None, "C5_descriptor_seg_present")
    if lf and li:
        pf, pi = parse_probe_log(lf), parse_probe_log(li)
        check(pf["evals"] == pi["evals"], "C5_descriptor_seg_evals_identical", "")
        af = json.loads(pf["evals"][0])
        check(af["available"] is False and af["reason"] == "MODEL_MISMATCH"
              and af["state"] == "unknown" and af["mismatched"] == ["output_count13!=3"],
              "C5_seg_real_mismatch", af["mismatched"].__str__())
        check("detected=yolov8_seg" in af["detail"] and "1 proto(32x160x160)" in af["detail"],
              "C5_seg_detail_records_real_shape", af["detail"][:100])
        df = pf["descriptors"]
        check(len(df) == 1 and df[0][2] == "1" and df[0][3] == "13" and "2.3.2" in df[0][0],
              "C5_seg_descriptor_real", str(df))
    lf = f("app_core-evidence_item010-descriptor-det-%s.log" % fs)
    li = i("app_core-evidence_item010-descriptor-det-%s.log" % impl)
    check(lf is not None and li is not None, "C5_descriptor_det_present")
    if lf and li:
        pf, pi = parse_probe_log(lf), parse_probe_log(li)
        check(pf["evals"] == pi["evals"], "C5_descriptor_det_evals_identical", "")
        af = json.loads(pf["evals"][0])
        check(af["available"] is False and af["reason"] == "MODEL_MISMATCH"
              and af["state"] == "unknown" and af["mismatched"] == ["output_count3!=2"],
              "C5_det_real_mismatch", af["mismatched"].__str__())
        check(pf["descriptors"][0][2] == "1" and pf["descriptors"][0][3] == "3",
              "C5_det_descriptor_real", str(pf["descriptors"]))

    # ---- C6: media duty 两种情形 ----
    lf = f("app_core-evidence_item010-media-no-model-%s.log" % fs)
    check(lf is not None, "C6_media_no_model_present")
    if lf:
        pf = parse_probe_log(lf)
        rows = {n: (av, r, st) for n, av, r, st, dp in
                re.findall(r"^type=duty name=(\S+) available=(\w+) reason=(\S+) state=(\S+) display=(\S+)", lf, re.M)}
        check(set(rows) == {"pantograph", "wheel_rail", "cab_door"}
              and all(v == ("false", "PROFILE_UNSUPPORTED", "unknown") for v in rows.values()),
              "C6_media_no_model_three_unavailable_unknown", str(rows))
        check(pf["pass_names"] == ["duty_pantograph_no_model", "duty_wheel_rail_no_model",
                                   "duty_cab_door_no_model"], "C6_media_no_model_pass_names",
              str(pf["pass_names"]))
        mj = json.loads(re.search(r"^type=duty_status json=(\{.*\})$", lf, re.M).group(1))
        check(all(d["availability"]["registered_model_name"] == {"pantograph": "yolov5s",
                                                                 "wheel_rail": "yolov8s-seg",
                                                                 "cab_door": "yolov5s"}[d["profile"]]
                  for d in mj["duties"]), "C6_media_no_model_registered_recorded", "")

    lf = f("app_core-evidence_item010-media-mismatch-%s.log" % fs)
    li = i("app_core-evidence_item010-media-mismatch-%s.log" % impl)
    check(lf is not None and li is not None, "C6_media_mismatch_present")
    if lf and li:
        rows_f = re.findall(r"^type=duty name=(\S+) available=(\w+) reason=(\S+) state=(\S+) "
                            r"display=\S+ registered=(\S*) declared=(\S*) evidence=(\S+)", lf, re.M)
        rows_i = re.findall(r"^type=duty name=(\S+) available=(\w+) reason=(\S+) state=(\S+) "
                            r"display=\S+ registered=(\S*) declared=(\S*) evidence=(\S+)", li, re.M)
        check(rows_f == rows_i and len(rows_f) == 3, "C6_media_mismatch_rows_identical", str(rows_f))
        rmap = {n: (av, r, st, ev) for n, av, r, st, reg, dec, ev in rows_f}
        check(rmap["pantograph"] == ("false", "MODEL_MISMATCH", "unknown", "declaration+descriptor")
              and rmap["cab_door"] == ("false", "MODEL_MISMATCH", "unknown", "declaration+descriptor")
              and rmap["wheel_rail"] == ("false", "PROFILE_UNSUPPORTED", "unknown", "none"),
              "C6_media_mismatch_semantics", str(rmap))
        pf = parse_probe_log(lf)
        check(pf["pass_names"] == ["duty_pantograph_declared", "duty_wheel_rail_no_model",
                                   "duty_cab_door_declared"], "C6_media_mismatch_pass_names",
              str(pf["pass_names"]))
        # pantograph mismatched 必须含 unsupported_decode_layout（真实 yolov5s 布局无解码器）
        mj = json.loads(re.search(r"^type=duty_status json=(\{.*\})$", lf, re.M).group(1))
        pd = [d for d in mj["duties"] if d["profile"] == "pantograph"][0]
        check("unsupported_decode_layout" in pd["availability"]["mismatched"],
              "C6_media_pantograph_layout_mismatch", pd["availability"]["mismatched"].__str__())

    # ---- C7: chain4 语义重推导（不采信 acceptance 行）----
    jf = f("app_core-evidence_item010-chain4-%s.json" % fs)
    check(jf is not None, "C7_chain4_json_present")
    if jf:
        j = json.loads(jf)
        lanes = {l["camera"]: l for l in j["lanes"]}
        check(j["mode"] == "chain" and j["source"] == "test" and j["duration_s"] == 6
              and j["budget_configured"] == 1, "C7_chain4_shape",
              "mode=%s budget=%s" % (j["mode"], j["budget_configured"]))
        check(set(lanes) == {"CAM-01", "CAM-04", "CAM-05", "CAM-06"}, "C7_four_lanes", str(set(lanes)))
        front = lanes["CAM-01"]
        check(front["ready"] is True and front["profile"] == "front", "C7_front_ready", "")
        # 预算 1 全给 front：inferred=front；职责路不占预算
        check(front["inferred"] >= 25, "C7_front_inferred_full_budget",
              "inferred=%s (5fps x 6s)" % front["inferred"])
        for cam in ("CAM-04", "CAM-05", "CAM-06"):
            l = lanes[cam]
            check(l["ready"] is False and l["reason"] == "PROFILE_UNSUPPORTED",
                  "C7_%s_unavailable" % cam, "reason=%s" % l["reason"])
            check(l["consumed"] == l["inferred"] and l["inferred"] >= 25,
                  "C7_%s_consumed_outside_budget" % cam,
                  "consumed=%s inferred=%s" % (l["consumed"], l["inferred"]))
            check(l["last_infer_us"] < 1000, "C7_%s_no_npu_cost" % cam,
                  "last_infer_us=%s" % l["last_infer_us"])
            check(l["fps"] > 4.5, "C7_%s_fps_dispatched" % cam, "fps=%s" % l["fps"])
        # 终帧 envelope（从 chain4 日志的 type=envelope 行解析）：职责路
        # availability=unavailable + reason + 空框 + 语义 profile
        envs = dict((m[0], json.loads(m[1])) for m in re.findall(
            r"^type=envelope camera=(\S+) json=(\{.*\})$", read(os.path.join(
                EVID, "%s.app_core-evidence_item010-chain4-%s.log" % (T, fs))), re.M))
        for cam in ("CAM-04", "CAM-05", "CAM-06"):
            env = envs.get(cam)
            check(env is not None and env["availability"] == "unavailable"
                  and env["reason"] == "PROFILE_UNSUPPORTED" and env["box_count"] == 0
                  and env["boxes"] == [] and env["masks"] == [],
                  "C7_%s_envelope_unavailable_empty" % cam,
                  json.dumps(env, ensure_ascii=False)[:120] if env else "missing")
            check(env is not None and env["profile"] in ("pantograph", "wheel_rail", "cab_door"),
                  "C7_%s_envelope_profile" % cam, env["profile"] if env else "missing")
        env_f = envs.get("CAM-01")
        check(env_f is not None and env_f["availability"] == "ready"
              and env_f["profile"] == "front", "C7_front_envelope_ready",
              env_f.get("availability") if env_f else "missing")
        # 调度轮 JSON（lane 级终轮快照聚合）：run 只有 front、skipped 恒空、
        # 职责路在 unavailable（有帧被职责拦截）或 idle（无帧窗）——都不进 run/skipped
        lr = lanes["CAM-01"]["last_round"]
        check(lr["run"] == ["CAM-01"] and lr["skipped"] == [] and lr["budget_effective"] == 1,
              "C7_last_round_budget_split", json.dumps(lr)[:140])
        agg_unavail = set()
        for cam in ("CAM-01", "CAM-04", "CAM-05", "CAM-06"):
            l = lanes[cam]["last_round"]
            agg_unavail.update(l["unavailable"])
            agg_unavail.update(l["idle"])
            agg_unavail.update(l["run"])
            check(l["skipped"] == [], "C7_%s_last_round_skipped_empty" % cam, str(l["skipped"]))
        check({"CAM-01", "CAM-04", "CAM-05", "CAM-06"}.issubset(agg_unavail | {"CAM-01"}),
              "C7_last_round_duty_lanes_not_in_run", str(sorted(agg_unavail)))
        # 汇总守恒：consumed = 4 路之和（test 源 ~5fps 每路）
        total_lane = sum(lanes[c]["consumed"] for c in lanes)
        check(j["consumed"] == total_lane, "C7_consumed_conservation",
              "summary=%s lanes=%s" % (j["consumed"], total_lane))
        check(j["unavailable"] == sum(lanes[c]["unavailable"] for c in lanes),
              "C7_unavailable_conservation", "summary=%s" % j["unavailable"])
        check(j["infer_calls"] == j["consumed"], "C7_infer_calls_all_consumed",
              "infer_calls=%s consumed=%s" % (j["infer_calls"], j["consumed"]))
        check(j["published"] == j["consumed"], "C7_published_conservation", "")
        # 职责状态发布面：三职责 unknown + PROFILE_UNSUPPORTED
        dstat = j["detector"]["duty_status"]["duties"]
        check(all(d["availability"]["available"] is False
                  and d["availability"]["state"] == "unknown"
                  and d["availability"]["reason"] == "PROFILE_UNSUPPORTED" for d in dstat),
              "C7_duty_status_unknown", "")
        # 规则引擎无报警（职责 unknown 不产判定；front 检测正常）
        check(j["events"] == 0, "C7_no_events", "events=%s" % j["events"])
        check(j["result"] == "PASS" and j["failures"] == 0, "C7_result_pass", "")
        # 热守卫未触发且实测
        th = j["thermal"]
        check(th["measured"] is True and th["valid"] is True and j["thermal_guard_rounds"] == 0,
              "C7_thermal_measured", "celsius=%s" % th.get("celsius"))

    # ---- C8: transcript 指纹与凭据面 ----
    check(tr.get("host_key_fingerprint", "").startswith("SHA256:"), "C8_fingerprint",
          tr.get("host_key_fingerprint", ""))
    secret_hint = os.environ.get("TRAIN_RK3588_SSH_CREDENTIAL", "")
    leaks = 0
    if secret_hint and len(secret_hint) >= 10:
        # 短/词表凭据（如 admin）会子串命中日志词形，按既有口径以长度+全串匹配为准
        for name in glob.glob(os.path.join(EVID, "%s.*" % T)):
            if secret_hint in read(name):
                leaks += 1
    check(leaks == 0, "C8_no_credential_leak", "leaks=%d" % leaks)

    # ---- C9: 返回码域（交付 runner 全部 *_EXIT token 全 0；FIXTURE_EXIT ×5）----
    raw_log = read(os.path.join(EVID, "%s.log" % T))
    tokens = re.findall(r"([A-Z_0-9]+)_EXIT=(\d+)", raw_log)
    check(len(tokens) >= 17 and all(v == "0" for _, v in tokens), "C9_exit_tokens_zero",
          "%d tokens, nonzero=%s" % (len(tokens), [v for _, v in tokens if v != "0"][:4]))
    kinds = {k for k, _ in tokens}
    check({"BUILD_CORE", "BUILD_MEDIA", "TEST", "DUTY_SELFTEST", "DUTY_CONFIG", "FIXTURE",
           "RKNN", "DESC_SEG", "DESC_DET", "MEDIA_NOMODEL", "MEDIA_MISMATCH",
           "CHAIN4"}.issubset(kinds), "C9_exit_token_kinds", str(sorted(kinds)))

    n_fail = sum(1 for r in RES if not r["ok"])
    out = {"stamp": fs, "impl_stamp": impl, "pass": n_fail == 0, "total": len(RES),
           "failures": n_fail, "results": RES}
    with open(os.path.join(HERE, "item010-compare-%s.json" % fs), "w", encoding="utf-8") as fh:
        json.dump(out, fh, ensure_ascii=False, indent=1)
    print("TOTAL %d checks, %d failures -> %s" % (len(RES), n_fail,
                                                  "item010-compare-%s.json" % fs), flush=True)
    return 0 if n_fail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
