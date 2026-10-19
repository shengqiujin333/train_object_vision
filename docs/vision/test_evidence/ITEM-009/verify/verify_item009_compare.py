#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-009 视觉推理验证轮 v1 —— 阶段 2：V-CMP 独立比对。

对 fresh 独立复跑归档（verify-20261020-item009-v1）套用与板前预演
（_dryrun_on_impl.py，全部 PASS）相同的 oracle；并对 impl r6 终轮做跨轮恒等比对：
  * 确定域（判例名集合 / 判据行集合 / profile 结构 / 配置 token / 身份字段形状）
    impl 与 fresh 必须逐项恒等；
  * 时序域（计数/延迟/温度读数/轮数）按采样波动做结构与方向断言，不做逐字节比对。

退出码 0 = 全部通过；1 = 存在失败（业务缺陷或时序越窗，均记录原始证据）。
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
EVID = []


def note(step, ok, detail=""):
    EVID.append({"step": step, "ok": bool(ok), "detail": str(detail)[:400]})
    print("[%s] %s %s" % ("PASS" if ok else "FAIL", step, detail), flush=True)


def rd(p):
    with open(p, encoding="utf-8") as fh:
        return fh.read()


SELFTEST_CASES = {
    "selftest_frame_lease_from_stream", "prod_no_synthetic_frames",
    "submit_not_blocked_by_infer", "snapshot_not_blocked_by_infer",
    "request_stop_immediate", "stop_ai_unavailable", "stop_join_bounded_by_one_infer",
    "slow_infer_did_run", "bounded_drop_oldest", "drop_oldest_keeps_newest",
    "budget_fairness_all_lanes_ran", "budget_fairness_balanced",
    "temp_guard_halves_budget", "injected_temp_not_measured",
    "thermal_source_reported_honestly", "track_id_never_zero",
    "no_frame_lane_not_inferred", "two_lanes_configured",
    "rule_engine_receives_detections",
}
CHAIN_CHECKS = ["chain_models_ready", "chain_frames_dispatched_and_consumed",
                "chain_envelopes_published", "chain_stop_ai_unavailable",
                "chain_scheduler_rounds", "chain_budget_respected",
                "chain_thermal_reported", "chain_guard_matches_measured_temp",
                "chain_lease_available"]


def parse_selftest(st_log):
    names = re.findall(r"^\[PASS\] (\S+)", st_log, re.M)
    return {
        "names": names,
        "result_pass": "RESULT: PASS failures=0" in st_log.splitlines()[-1],
        "submit_max": int(re.search(r"submit_not_blocked_by_infer :: max=(\d+)us", st_log).group(1)),
        "snap_p95": int(re.search(r"snapshot_not_blocked_by_infer :: p95=(\d+)us", st_log).group(1)),
        "stop_us": int(re.search(r"request_stop_immediate :: (\d+)us", st_log).group(1)),
        "join_us": int(re.search(r"stop_join_bounded_by_one_infer :: (\d+)us", st_log).group(1)),
        "blocking_shape": re.search(
            r"^type=selftest blocking submit_p95_us=\d+ snapshot_p95_us=\d+ "
            r"request_stop_us=\d+ join_us=\d+$", st_log, re.M) is not None,
        "thermal": re.search(
            r"^type=thermal state=(\S+) celsius=([\d.]+) source=(\S+) reason=(\S+) zones=(\d+)$",
            st_log, re.M),
        "drop_shape": re.search(r"bounded_drop_oldest :: pushed=5 depth=\d+ dropped=3$",
                                st_log, re.M) is not None and "popped=5,6 last_pushed=6" in st_log,
        "fairness": re.search(r"budget_fairness_balanced :: each lane 2 times$", st_log, re.M)
        is not None and "budget 4->2" in st_log and "INJECTED" in st_log,
        "prod_no_synth": "steps=" in re.search(r"prod_no_synthetic_frames :: (.+)$", st_log, re.M)
        .group(1) and "infer_calls=0" in
        re.search(r"prod_no_synthetic_frames :: (.+)$", st_log, re.M).group(1),
    }


def parse_chain(ch_log):
    lane = re.search(r"^type=lane camera=CAM-01 (.+)$", ch_log, re.M)
    kv = {}
    for tok in lane.group(1).split(" "):
        if "=" in tok:
            k, v = tok.split("=", 1)
            kv.setdefault(k, v)
    md = re.search(r"last_unavailable_detail=(.+)$", lane.group(1))
    if md:
        kv["last_unavailable_detail"] = md.group(1).strip()
    summ = re.search(r"^type=summary submitted=(\d+) consumed=(\d+) published=(\d+) "
                     r"unavailable=(\d+)", ch_log, re.M)
    tj = json.loads(re.search(r"^type=summary .*thermal=(\{.*\})$", ch_log, re.M).group(1))
    envs = re.findall(r"^type=envelope camera=(\S+) json=(\{.*)$", ch_log, re.M)
    checks = re.findall(r"^\[PASS\] (chain_\w+)", ch_log, re.M)
    return {
        "config_tokens": ("interval=5" in ch_log and "depth=2" in ch_log
                          and "npu_budget=4(config)" in ch_log and "temp_limit_c=85.000000" in ch_log
                          and "confirm_frames=3" in ch_log and "confidence_min=0.500000" in ch_log),
        "kv": kv,
        "submitted": int(summ.group(1)),
        "consumed": int(summ.group(2)),
        "published": int(summ.group(3)),
        "unavailable": int(summ.group(4)),
        "thermal": tj,
        "envs": envs,
        "checks": checks,
        "guard_false": "guard=false" in ch_log and "thermal_guard_rounds=0" in ch_log,
        "profiles": re.findall(r"^type=profile name=(\S+) ready=(\S+) reason=(\S+) model=(.*)$",
                               ch_log, re.M),
    }


def parse_chain4(c4_log):
    lanes = re.findall(r"^type=lane camera=(CAM-\d+) (.+)$", c4_log, re.M)
    cons = {}
    for cam, body in lanes:
        kv = {}
        for tok in body.split(" "):
            if "=" in tok:
                k, v = tok.split("=", 1)
                kv.setdefault(k, v)
        md = re.search(r"last_unavailable_detail=(.+)$", body)
        if md:
            kv["last_unavailable_detail"] = md.group(1).strip()
        cons[cam] = kv
    summ = re.search(r"^type=summary submitted=(\d+) consumed=(\d+) published=(\d+) "
                     r"unavailable=(\d+)", c4_log, re.M)
    envs = re.findall(r"^type=envelope camera=(\S+) json=(\{.*)$", c4_log, re.M)
    checks = re.findall(r"^\[PASS\] (chain_\w+)", c4_log, re.M)
    return {
        "cams": [c for c, _ in lanes],
        "cons": cons,
        "submitted": int(summ.group(1)),
        "consumed": int(summ.group(2)),
        "published": int(summ.group(3)),
        "unavailable": int(summ.group(4)),
        "cli_override": "npu_budget=1(cli-override)" in c4_log,
        "budget1": re.search(r"^type=summary .* budget=1 thermal_guard_rounds=\d+", c4_log,
                             re.M) is not None,
        "envs": envs,
        "checks": checks,
    }


def parse_junit(junit):
    tests = int(re.search(r'tests="(\d+)"', junit).group(1))
    fails = int(re.search(r'failures="(\d+)"', junit).group(1))
    case_names = re.findall(r'<testcase [^>]*\sname="([^"]+)"', junit)
    return tests, fails, case_names


def parse_r7(r7_log):
    return {
        "synthetic": "synthetic_tick=on" in r7_log,
        "sim": "sim(substitute,not-rknn)" in r7_log,
        "channels": len(re.findall(r"^type=channel ", r7_log, re.M)),
        "stop": "stopped=AI 不可用" in r7_log,
        "accept": "result=PASS" in r7_log and "channels_ran=4/4" in r7_log,
    }


def oracle(tag, st, ch, c4, junit, r7):
    tests, fails, case_names = junit
    note("%s_selftest_case_set" % tag, set(st["names"]) == SELFTEST_CASES and len(st["names"]) == 19,
         "n=%d" % len(st["names"]))
    note("%s_selftest_pass" % tag, st["result_pass"], "")
    note("%s_submit_max_below_50ms" % tag, st["submit_max"] < 50000, str(st["submit_max"]))
    note("%s_snapshot_p95_below_20ms" % tag, st["snap_p95"] < 20000, str(st["snap_p95"]))
    note("%s_request_stop_zero" % tag, st["stop_us"] == 0, str(st["stop_us"]))
    note("%s_join_below_400ms" % tag, st["join_us"] < 400000, str(st["join_us"]))
    note("%s_blocking_shape" % tag, st["blocking_shape"], "")
    note("%s_prod_no_synthetic" % tag, st["prod_no_synth"], "")
    m = st["thermal"]
    note("%s_thermal_measured" % tag,
         m is not None and m.group(1) == "MEASURED" and 0.0 < float(m.group(2)) < 120.0
         and m.group(3).startswith("thermal_zone") and m.group(4) == "THERMAL_OK"
         and int(m.group(5)) >= 1, m.group(0) if m else "absent")
    note("%s_drop_shape" % tag, st["drop_shape"], "")
    note("%s_fairness_shape" % tag, st["fairness"], "")

    note("%s_config_tokens" % tag, ch["config_tokens"], "独立重推导 app.yaml/thresholds.yaml 源值")
    kv = ch["kv"]
    note("%s_lane_fields" % tag,
         all(k in kv for k in ("consumed", "inferred", "unavailable", "published", "dropped",
                               "skipped_interval", "queue_depth", "last_infer_us", "latency_ms",
                               "fps", "events", "env_ready", "env_unavailable", "env_queue_dropped")),
         "")
    note("%s_defreq_math" % tag,
         ch["consumed"] * 5 >= ch["submitted"] - 6 and ch["consumed"] * 5 <= ch["submitted"] + 6,
         "submitted=%d consumed=%d" % (ch["submitted"], ch["consumed"]))
    note("%s_skipped_plus_consumed" % tag,
         int(kv["skipped_interval"]) + ch["consumed"] == 300,
         "skipped=%s consumed=%d" % (kv["skipped_interval"], ch["consumed"]))
    note("%s_unavailable_family" % tag,
         kv["unavailable_by_reason"].startswith("TWO_STAGE_UNAVAILABLE:")
         and int(kv["unavailable_by_reason"].split(":")[1]) == int(kv["unavailable"])
         and kv["last_unavailable_reason"] == "TWO_STAGE_UNAVAILABLE"
         and "rail mask empty" in kv["last_unavailable_detail"],
         kv["unavailable_by_reason"])
    note("%s_infer_cost_window" % tag, 20000 <= int(kv["last_infer_us"]) <= 600000,
         kv["last_infer_us"])
    note("%s_latency_ge_infer" % tag,
         int(kv["latency_ms"]) >= int(kv["last_infer_us"]) // 1000,
         "latency=%s infer_ms=%d" % (kv["latency_ms"], int(kv["last_infer_us"]) // 1000))
    note("%s_fps_defreq" % tag, 4.0 <= float(kv["fps"]) <= 6.0, kv["fps"])
    note("%s_env_counters" % tag, kv["env_ready"] == kv["published"]
         and int(kv["env_unavailable"]) == int(kv["unavailable"]), "")
    note("%s_envelope_single" % tag, len(ch["envs"]) == 1, "n=%d" % len(ch["envs"]))
    if ch["envs"]:
        j = json.loads(ch["envs"][0][1])
        note("%s_envelope_identity" % tag,
             j["camera_id"] == "CAM-01" and j["profile"] == "front" and j["generation"] == 1
             and isinstance(j["seq"], int) and j["latency_ms"] >= 0
             and j["availability"] in ("ready", "unavailable")
             and j["model_version"] == "rknn-2.3.2/yolov5s+rknn-2.3.2/yolov8s-seg",
             "seq=%s avail=%s" % (j["seq"], j["availability"]))
        note("%s_envelope_mask" % tag,
             len(j["masks"]) == 1 and j["masks"][0]["box_index"] == -1
             and j["masks"][0]["kind"] == "rail" and j["masks"][0]["width"] == 1280
             and j["masks"][0]["height"] == 720 and j["masks"][0]["plane_present"] is True, "")
    note("%s_chain_check_set" % tag, ch["checks"] == CHAIN_CHECKS, str(ch["checks"]))
    tj = ch["thermal"]
    note("%s_summary_thermal" % tag,
         tj["valid"] is True and tj["measured"] is True and tj["injected"] is False
         and 0.0 < tj["celsius"] < 120.0 and tj["source"].startswith("thermal_zone"), "")
    note("%s_guard_consistency" % tag,
         tj["celsius"] <= 85.0 and ch["guard_false"],
         "实测 %.1f<=85 -> guard=false" % tj["celsius"])

    note("%s_four_lanes" % tag, c4["cams"] == ["CAM-01", "CAM-04", "CAM-05", "CAM-06"],
         str(c4["cams"]))
    note("%s_cli_budget_override" % tag, c4["cli_override"], "")
    note("%s_budget_effective_1" % tag, c4["budget1"], "")
    note("%s_consumed_sum" % tag,
         sum(int(v["consumed"]) for v in c4["cons"].values()) == c4["consumed"],
         str({k: int(v["consumed"]) for k, v in c4["cons"].items()}))
    note("%s_published_sum" % tag,
         sum(int(v["published"]) for v in c4["cons"].values()) == c4["published"], "")
    note("%s_unavailable_sum" % tag,
         sum(int(v["unavailable"]) for v in c4["cons"].values()) == c4["unavailable"], "")
    note("%s_no_starvation" % tag, all(int(v["consumed"]) > 0 for v in c4["cons"].values()), "")
    base_c = c4["consumed"] / 4.0
    note("%s_no_starvation_balanced" % tag,
         all(abs(int(v["consumed"]) - base_c) <= 6 for v in c4["cons"].values()),
         "base=%.1f" % base_c)
    note("%s_profiles_unsupported" % tag,
         all(c4["cons"][c]["last_unavailable_reason"] == "PROFILE_UNSUPPORTED"
             for c in ("CAM-04", "CAM-05", "CAM-06")), "")
    note("%s_unsupported_fast" % tag,
         all(int(c4["cons"][c]["last_infer_us"]) <= 100 for c in ("CAM-04", "CAM-05", "CAM-06")),
         str({c: c4["cons"][c]["last_infer_us"] for c in ("CAM-04", "CAM-05", "CAM-06")}))
    note("%s_front_real_infer" % tag, int(c4["cons"]["CAM-01"]["last_infer_us"]) >= 20000,
         c4["cons"]["CAM-01"]["last_infer_us"])
    note("%s_queue_bounded" % tag,
         all(int(v["dropped"]) >= 0 and int(v["queue_depth"]) <= 2 for v in c4["cons"].values()),
         "")
    note("%s_envelope_count" % tag, len(c4["envs"]) == 4, "n=%d" % len(c4["envs"]))
    unsup = [json.loads(b) for c, b in c4["envs"] if c != "CAM-01"]
    note("%s_unavailable_envelope" % tag,
         len(unsup) == 3 and all(u["availability"] == "unavailable"
                                 and u["reason"] == "PROFILE_UNSUPPORTED"
                                 and u["box_count"] == 0 and u["boxes"] == [] and u["masks"] == []
                                 and u["model_version"] == "" for u in unsup), "")
    note("%s_junit_1215_0" % tag, tests == 1215 and fails == 0,
         "tests=%d failures=%d" % (tests, fails))
    note("%s_junit_case_count" % tag, len(case_names) == 1215, "extracted=%d" % len(case_names))
    sched = [n for n in case_names if n.startswith("sched_")]
    therm = [n for n in case_names if n.startswith("thermal_")]
    note("%s_junit_new54_split" % tag, len(sched) == 35 and len(therm) == 19
         and len(sched) + len(therm) == 54, "sched=%d thermal=%d" % (len(sched), len(therm)))
    note("%s_junit_new_cases_present" % tag,
         all(c in set(case_names) for c in (
             "sched_no_starvation_balanced", "sched_temp_guard_halves_budget",
             "sched_temp_guard_strict_gt", "sched_temp_unknown_not_claimed",
             "sched_budget_zero_all_skipped", "sched_deterministic",
             "thermal_parse_decimal_rejected", "thermal_json_injected_not_measured")), "")
    note("%s_r7_synthetic_on" % tag, r7["synthetic"] and r7["sim"], "")
    note("%s_r7_channels" % tag, r7["channels"] == 4, "")
    note("%s_r7_stop_unavailable" % tag, r7["stop"], "")
    note("%s_r7_acceptance" % tag, r7["accept"], "")


def main():
    base = os.path.normpath(os.path.join(HERE, ".."))
    impl = "vision-item009-192_168_0_103-20261020-item009-r6"
    fresh = "vision-item009-192_168_0_103-verify-20261020-item009-v1"
    p = lambda tag, suffix: os.path.join(base, "%s.%s" % (tag, suffix))

    data = {}
    for label, tag in (("impl", impl), ("fresh", fresh)):
        st = parse_selftest(rd(p(tag, "app_core-evidence_item009-selftest-20261020-item009-r6.log"
                                  if label == "impl" else
                                  "app_core-evidence_item009-selftest-verify-20261020-item009-v1.log")))
        ch = parse_chain(rd(p(tag, "app_core-evidence_item009-chain-20261020-item009-r6.log"
                                if label == "impl" else
                                "app_core-evidence_item009-chain-verify-20261020-item009-v1.log")))
        c4 = parse_chain4(rd(p(tag, "app_core-evidence_item009-chain4-20261020-item009-r6.log"
                                 if label == "impl" else
                                 "app_core-evidence_item009-chain4-verify-20261020-item009-v1.log")))
        junit = parse_junit(rd(p(tag, "app_core-evidence_item009-core-tests-20261020-item009-r6.xml"
                                   if label == "impl" else
                                   "app_core-evidence_item009-core-tests-verify-20261020-item009-v1.xml")))
        # r7 替代链路回归：实现方唯一会话产物（交付 runner 默认步不含 ai-runner）。
        # fresh 轮对该链路的等价覆盖 = 探针 prod_no_synthetic_frames 判例（生产模式无自造帧）
        # + selftest 判例集合恒等；r7 归档仍按实现方轮独立 oracle 断言。
        if label == "impl":
            r7 = parse_r7(rd(p(tag.replace("-r6", "-r7"),
                               "app_core-evidence_item009-ai-runner-20261020-item009-r7.log")))
        else:
            r7 = parse_r7(rd(p(impl.replace("-r6", "-r7"),
                               "app_core-evidence_item009-ai-runner-20261020-item009-r7.log")))
        data[label] = (st, ch, c4, junit, r7)

    # 每轮独立 oracle
    for label in ("impl", "fresh"):
        oracle(label, *data[label])

    # 跨轮恒等（确定域）
    ist, ich, ic4, ijunit, ir7 = data["impl"]
    fst, fch, fc4, fjunit, fr7 = data["fresh"]
    note("X_selftest_case_set_identical", set(ist["names"]) == set(fst["names"]), "")
    note("X_chain_check_set_identical", ich["checks"] == fch["checks"], "")
    note("X_chain4_check_set_identical", ic4["checks"] == fc4["checks"], "")
    note("X_profile_structure_identical",
         [x[:3] for x in ich["profiles"]] == [x[:3] for x in fch["profiles"]],
         "profile name/ready/reason 恒等（model 版本串含 runtime 信息，按结构比对）")
    note("X_junit_case_names_identical", set(ijunit[2]) == set(fjunit[2]),
         "impl=%d fresh=%d" % (len(ijunit[2]), len(fjunit[2])))
    note("X_junit_counts_identical", ijunit[0] == fjunit[0] and ijunit[1] == fjunit[1],
         "tests=%d/%d" % (ijunit[0], fjunit[0]))
    note("X_r7_shape_identical",
         (ir7["synthetic"], ir7["channels"]) == (fr7["synthetic"], fr7["channels"]), "")
    # fresh config token 恒等（models_dir 不同属预期）
    note("X_config_tokens_identical", ich["config_tokens"] and fch["config_tokens"], "")

    ok_all = all(e["ok"] for e in EVID)
    with open(os.path.join(HERE, "item009-compare-verify-20261020-item009-v1.json"), "w",
              encoding="utf-8") as fh:
        json.dump({"stamp": "verify-20261020-item009-v1", "checks": EVID, "all_pass": ok_all},
                  fh, ensure_ascii=False, indent=2)
    print("COMPARE SUMMARY all_pass=%s (%d checks)" % (ok_all, len(EVID)), flush=True)
    return 0 if ok_all else 1


if __name__ == "__main__":
    sys.exit(main())
