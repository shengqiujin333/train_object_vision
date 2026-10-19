#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-009 视觉推理验证轮 v1 —— 阶段 1：预言机离线预演。

对实现方 r6 终轮归档（selftest/chain/chain4 + JUnit）与 r7 替代链路回归，
把 V-CMP 的全部解析与语义重推导在板前零成本跑通；任何 FAIL 都是验证侧
oracle 构造错（板前修正），不是产品缺陷证据。

口径：
  * 时序域（steps/infer_calls/join/latency/fps/温度读数/轮数/队列深度/计数）
    随采样波动，只做结构与方向断言；
  * 确定域（判例名集合、profile 结构、reason token、语义算术、身份字段形状）
    做精确恒等/独立重推导断言。
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


def main():
    base = os.path.normpath(os.path.join(HERE, ".."))
    tag = "vision-item009-192_168_0_103-20261020-item009-r6"
    st_log = rd(os.path.join(base, tag + ".app_core-evidence_item009-selftest-20261020-item009-r6.log"))
    ch_log = rd(os.path.join(base, tag + ".app_core-evidence_item009-chain-20261020-item009-r6.log"))
    c4_log = rd(os.path.join(base, tag + ".app_core-evidence_item009-chain4-20261020-item009-r6.log"))
    junit = rd(os.path.join(base, tag + ".app_core-evidence_item009-core-tests-20261020-item009-r6.xml"))
    r7_log = rd(os.path.join(base, tag.replace("-r6", "-r7")
                            + ".app_core-evidence_item009-ai-runner-20261020-item009-r7.log"))

    # ---- S1 selftest 判例名集合（19 判例，域对齐） ----
    names = re.findall(r"^\[PASS\] (\S+)", st_log, re.M)
    exp = {
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
    note("S1_selftest_case_set", set(names) == exp and len(names) == 19,
         "n=%d missing=%s extra=%s" % (len(names), sorted(exp - set(names))[:3],
                                       sorted(set(names) - exp)[:3]))
    note("S1_selftest_result_pass", "RESULT: PASS failures=0" in st_log.splitlines()[-1], "")

    # ---- S2 时序域（采样波动，只做方向） ----
    note("S2_submit_max_below_50ms",
         int(re.search(r"submit_not_blocked_by_infer :: max=(\d+)us", st_log).group(1)) < 50000, "")
    note("S2_snapshot_p95_below_20ms",
         int(re.search(r"snapshot_not_blocked_by_infer :: p95=(\d+)us", st_log).group(1)) < 20000, "")
    note("S2_request_stop_immediate_token",
         re.search(r"request_stop_immediate :: (\d+)us", st_log).group(1) == "0", "")
    note("S2_join_below_400ms",
         int(re.search(r"stop_join_bounded_by_one_infer :: (\d+)us", st_log).group(1)) < 400000, "")
    note("S2_prod_no_synthetic",
         "steps=40 infer_calls=0" in
         re.search(r"prod_no_synthetic_frames :: (.+)$", st_log, re.M).group(1), "")
    note("S2_slow_infer_ran", "infer_calls=5" in
         re.search(r"slow_infer_did_run :: (.+)$", st_log, re.M).group(1), "")
    note("S2_blocking_line_shape",
         re.search(r"^type=selftest blocking submit_p95_us=\d+ snapshot_p95_us=\d+ "
                   r"request_stop_us=\d+ join_us=\d+$", st_log, re.M) is not None, "")
    m = re.search(r"^type=thermal state=(\S+) celsius=([\d.]+) source=(\S+) reason=(\S+) zones=(\d+)$",
                  st_log, re.M)
    note("S2_thermal_line_shape", m is not None and m.group(1) == "MEASURED"
         and 0.0 < float(m.group(2)) < 120.0 and m.group(3).startswith("thermal_zone")
         and m.group(4) == "THERMAL_OK" and int(m.group(5)) >= 1, m.group(0) if m else "absent")
    note("S2_drop_oldest_shape",
         re.search(r"bounded_drop_oldest :: pushed=5 depth=\d+ dropped=3$", st_log, re.M) is not None
         and "popped=5,6 last_pushed=6" in st_log, "")
    note("S2_fairness_shape",
         re.search(r"budget_fairness_balanced :: each lane 2 times$", st_log, re.M) is not None
         and "budget 4->2" in st_log and "INJECTED" in st_log, "")

    # ---- C1 chain 单路：语义重推导 ----
    cfg = dict(re.findall(r"^type=config .*?(\w+)=([\w./()-]+)", ch_log, re.M))
    # 独立重推导（app.yaml/thresholds.yaml 源值）：interval=5 depth=2 budget=4(config) limit=85 conf=0.5 confirm=3
    note("C1_config_tokens",
         "interval=5" in ch_log and "depth=2" in ch_log and "npu_budget=4(config)" in ch_log
         and "temp_limit_c=85.000000" in ch_log and "confirm_frames=3" in ch_log
         and "confidence_min=0.500000" in ch_log, "config line independently re-derived")
    lane = re.search(r"^type=lane camera=CAM-01 (.+)$", ch_log, re.M)
    lane_kv = {}
    for tok in lane.group(1).split(" "):
        if "=" in tok:
            k, v = tok.split("=", 1)
            lane_kv.setdefault(k, v)  # 首个 = 后整段为值；带空格的 detail 值由专门 regex 提取
    m_detail = re.search(r"last_unavailable_detail=(.+)$", lane.group(1))
    if m_detail:
        lane_kv["last_unavailable_detail"] = m_detail.group(1).strip()
    submitted = int(re.search(r"^type=summary submitted=(\d+) consumed=(\d+) published=(\d+) "
                              r"unavailable=(\d+)", ch_log, re.M).group(1))
    consumed = int(re.search(r"^type=summary submitted=(\d+) consumed=(\d+)", ch_log, re.M).group(2))
    note("C1_lane_fields_present",
         all(k in lane_kv for k in ("consumed", "inferred", "unavailable", "published", "dropped",
                                    "skipped_interval", "queue_depth", "last_infer_us", "latency_ms",
                                    "fps", "events", "env_ready", "env_unavailable", "env_queue_dropped")),
         str(sorted(set(lane_kv) - {"ready", "reason", "model", "camera", "profile"})[:5]))
    note("C1_interval_defreq_math",
         consumed * 5 >= submitted - 6 and consumed * 5 <= submitted + 6,
         "submitted=%d consumed=%d (interval=5: consumed*5 ~= submitted)" % (submitted, consumed))
    note("C1_skipped_plus_consumed",
         int(lane_kv["skipped_interval"]) + consumed == 300,
         "skipped_interval=%s consumed=%d" % (lane_kv["skipped_interval"], consumed))
    note("C1_unavailable_reason_family",
         lane_kv["unavailable_by_reason"].startswith("TWO_STAGE_UNAVAILABLE:")
         and int(lane_kv["unavailable_by_reason"].split(":")[1]) == int(lane_kv["unavailable"])
         and lane_kv["last_unavailable_reason"] == "TWO_STAGE_UNAVAILABLE"
         and "rail mask empty" in lane_kv["last_unavailable_detail"],
         lane_kv["unavailable_by_reason"])
    note("C1_infer_cost_plausible",
         20000 <= int(lane_kv["last_infer_us"]) <= 600000,
         "last_infer_us=%s (test pattern det+seg, 不外推)" % lane_kv["last_infer_us"])
    note("C1_latency_ordering",
         int(lane_kv["latency_ms"]) >= int(lane_kv["last_infer_us"]) // 1000,
         "latency=%s >= infer_ms=%d (latency 含排队)" % (lane_kv["latency_ms"],
                                                         int(lane_kv["last_infer_us"]) // 1000))
    note("C1_fps_matches_defreq",
         4.0 <= float(lane_kv["fps"]) <= 6.0, "fps=%s (25/5)" % lane_kv["fps"])
    note("C1_env_counters",
         lane_kv["env_ready"] == lane_kv["published"]
         and int(lane_kv["env_unavailable"]) == int(lane_kv["unavailable"]),
         "env_ready=%s env_unavailable=%s" % (lane_kv["env_ready"], lane_kv["env_unavailable"]))

    # envelope 身份/形状（可用与不可用两类）
    envs = re.findall(r"^type=envelope camera=(\S+) json=(\{.*)$", ch_log, re.M)
    note("C1_envelope_lines", len(envs) == 1, "n=%d" % len(envs))
    if envs:
        j = json.loads(envs[0][1])
        note("C1_envelope_identity",
             j["camera_id"] == "CAM-01" and j["profile"] == "front" and j["generation"] == 1
             and isinstance(j["seq"], int) and j["latency_ms"] >= 0
             and j["availability"] in ("ready", "unavailable")
             and j["model_version"] == "rknn-2.3.2/yolov5s+rknn-2.3.2/yolov8s-seg",
             "seq=%s avail=%s" % (j["seq"], j["availability"]))
        note("C1_envelope_mask_shape",
             len(j["masks"]) == 1 and j["masks"][0]["box_index"] == -1
             and j["masks"][0]["kind"] == "rail" and j["masks"][0]["width"] == 1280
             and j["masks"][0]["height"] == 720 and j["masks"][0]["plane_present"] is True,
             "masks kind=rail box_index=-1 (front 框/掩码分离)")

    # chain 判据行（9 项）与守护一致性
    chain_checks = re.findall(r"^\[PASS\] (chain_\w+)", ch_log, re.M)
    note("C1_chain_check_set", len(chain_checks) == 9 and len(set(chain_checks)) == 9,
         str(chain_checks))
    summ = re.search(r"^type=summary .*thermal=(\{.*\})$", ch_log, re.M)
    tj = json.loads(summ.group(1))
    note("C1_summary_thermal_measured",
         tj["valid"] is True and tj["measured"] is True and tj["injected"] is False
         and 0.0 < tj["celsius"] < 120.0 and tj["source"].startswith("thermal_zone"),
         json.dumps(tj)[:120])
    note("C1_guard_consistency",
         tj["celsius"] <= 85.0 and "guard=false" in ch_log and "thermal_guard_rounds=0" in ch_log,
         "实测 %.1f<=85 -> guard=false 一致" % tj["celsius"])

    # ---- C2 chain4：预算与无饥饿重推导 ----
    c4_lanes = re.findall(r"^type=lane camera=(CAM-\d+) (.+)$", c4_log, re.M)
    note("C2_four_lanes", len(c4_lanes) == 4, str([c for c, _ in c4_lanes]))
    cons = {}
    for cam, body in c4_lanes:
        kv = {}
        for tok in body.split(" "):
            if "=" in tok:
                k, v = tok.split("=", 1)
                kv.setdefault(k, v)
        md = re.search(r"last_unavailable_detail=(.+)$", body)
        if md:
            kv["last_unavailable_detail"] = md.group(1).strip()
        cons[cam] = kv
    c4_summ = re.search(r"^type=summary submitted=(\d+) consumed=(\d+) published=(\d+) "
                        r"unavailable=(\d+)", c4_log, re.M)
    c4_con = int(c4_summ.group(2))
    c4_pub = int(c4_summ.group(3))
    c4_un = int(c4_summ.group(4))
    note("C2_cli_budget_override", "npu_budget=1(cli-override)" in c4_log, "")
    note("C2_budget_effective_1",
         re.search(r"^type=summary .* budget=1 thermal_guard_rounds=\d+", c4_log, re.M) is not None,
         "summary budget=1 (cli-override 生效)")
    note("C2_consumed_sum_matches", sum(int(v["consumed"]) for v in cons.values()) == c4_con,
         "lanes=%s summary=%d" % ({k: int(v["consumed"]) for k, v in cons.items()}, c4_con))
    note("C2_published_sum_matches", sum(int(v["published"]) for v in cons.values()) == c4_pub, "")
    note("C2_unavailable_sum_matches", sum(int(v["unavailable"]) for v in cons.values()) == c4_un,
         "unavailable=%d" % c4_un)
    # 无饥饿：budget=1 下 4 路各 consumed>0 且均衡（每路 ≈ 总消耗/4，±6 采样窗）
    note("C2_no_starvation_all_lanes",
         all(int(v["consumed"]) > 0 for v in cons.values()),
         str({k: int(v["consumed"]) for k, v in cons.items()}))
    base_c = c4_con / 4.0
    note("C2_no_starvation_balanced",
         all(abs(int(v["consumed"]) - base_c) <= 6 for v in cons.values()),
         "base=%.1f" % base_c)
    # 不可用路 PROFILE_UNSUPPORTED（3 路），front TWO_STAGE
    note("C2_profiles_unsupported_family",
         all(cons[c]["last_unavailable_reason"] == "PROFILE_UNSUPPORTED"
             for c in ("CAM-04", "CAM-05", "CAM-06")),
         "3 non-front lanes PROFILE_UNSUPPORTED")
    note("C2_unsupported_fast_return",
         all(int(cons[c]["last_infer_us"]) <= 100 for c in ("CAM-04", "CAM-05", "CAM-06")),
         str({c: cons[c]["last_infer_us"] for c in ("CAM-04", "CAM-05", "CAM-06")}))
    note("C2_front_real_infer", int(cons["CAM-01"]["last_infer_us"]) >= 20000,
         cons["CAM-01"]["last_infer_us"])
    note("C2_dropped_bounded",
         all(int(v["dropped"]) >= 0 and int(v["queue_depth"]) <= 2 for v in cons.values()),
         "queue depth<=2 (config)")
    c4_envs = re.findall(r"^type=envelope camera=(\S+) json=(\{.*)$", c4_log, re.M)
    note("C2_envelope_count", len(c4_envs) == 4, "n=%d" % len(c4_envs))
    unsup = [json.loads(b) for c, b in c4_envs if c != "CAM-01"]
    note("C2_unavailable_envelope_shape",
         all(u["availability"] == "unavailable" and u["reason"] == "PROFILE_UNSUPPORTED"
             and u["box_count"] == 0 and u["boxes"] == [] and u["masks"] == []
             and u["model_version"] == "" for u in unsup),
         "不可用 + 空框 + 空掩码（与 ready 可区分）")
    fr = [json.loads(b) for c, b in c4_envs if c == "CAM-01"][0]
    note("C2_front_envelope_ready_or_unavail",
         fr["availability"] in ("ready", "unavailable") and fr["camera_id"] == "CAM-01", "")

    # ---- J1 JUnit：1215 判例（1161 + 54 新增），sched_* 域 ----
    tests = int(re.search(r'tests="(\d+)"', junit).group(1))
    fails = int(re.search(r'failures="(\d+)"', junit).group(1))
    case_names = re.findall(r'<testcase [^>]*\sname="([^"]+)"', junit)
    note("J1_junit_1215_0", tests == 1215 and fails == 0,
         "tests=%d failures=%d" % (tests, fails))
    note("J1_case_names_count", len(case_names) == 1215, "extracted=%d" % len(case_names))
    sched = [n for n in case_names if n.startswith("sched_")]
    therm = [n for n in case_names if n.startswith("thermal_")]
    note("J1_new54_split_35_19", len(sched) == 35 and len(therm) == 19
         and len(sched) + len(therm) == 54,
         "sched=%d thermal=%d sum=%d (本增量 54 = 温度 19 + 调度 35)" % (len(sched), len(therm),
                                                                        len(sched) + len(therm)))
    note("J1_new_cases_present",
         all(c in set(case_names) for c in (
             "sched_no_starvation_balanced", "sched_temp_guard_halves_budget",
             "sched_temp_guard_strict_gt", "sched_temp_unknown_not_claimed",
             "sched_budget_zero_all_skipped", "sched_deterministic",
             "thermal_parse_decimal_rejected", "thermal_json_injected_not_measured")), "")

    # ---- R7 替代链路回归（synthetic_tick=on 显式标注；4/4；停后不可用） ----
    note("R7_synthetic_tick_on", "synthetic_tick=on" in r7_log, "")
    note("R7_sim_detector_labeled", "sim(substitute,not-rknn)" in r7_log, "")
    note("R7_four_channels", len(re.findall(r"^type=channel ", r7_log, re.M)) == 4, "")
    note("R7_stop_unavailable", "stopped=AI 不可用" in r7_log, "")
    note("R7_acceptance_pass", "result=PASS" in r7_log and "channels_ran=4/4" in r7_log, "")

    ok_all = all(e["ok"] for e in EVID)
    with open(os.path.join(HERE, "dryrun_on_impl.json"), "w", encoding="utf-8") as fh:
        json.dump({"stage": "dryrun-on-impl", "checks": EVID, "all_pass": ok_all},
                  fh, ensure_ascii=False, indent=2)
    print("DRYRUN SUMMARY all_pass=%s (%d checks)" % (ok_all, len(EVID)), flush=True)
    return 0 if ok_all else 1


if __name__ == "__main__":
    sys.exit(main())
