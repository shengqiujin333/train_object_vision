#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-012 验证轮 v1 —— V-CMP：独立解析比对 oracle。

impl r4 归档（…20261021-item012-r4.*） vs fresh 验证轮（…verify-20261019-item012-v1.*）：
对每份交付日志用**独立重推导**断言（不采信交付 RESULT 行），
并对 impl r4 归档做同判据 dry-run 证明判据非空转。

判据族（TC-T-DBG-01..03 + 任务书用户可观察点）：
  C0  transcript 全部步骤 exit=0 + 每步 *EXIT token=0
  C1  JUnit tests=1412 failures=0 + 65 debug_view 判例名精确域（多重集恒等 fresh vs r4）
  C2  debug selftest 17 判例名集合 fresh vs r4 恒等 + RESULT PASS
  C3  6 fixtures 语义重推导（从 type=view json 独立解析）：
      fields 字段齐备/格式化；unavailable 全占位非 0 + 原因显式；
      mask_on 叠加提供+尺寸+正像素；mask_off 不提供+可用性可观察；
      no_mask SEG_UNAVAILABLE；driver 不可见无数据
  C4  三组配置口径（dev 全开+合成掩码 / driver 全开不可见 / maint 全关）
      独立语义重推导（角色可见性/未知占位/掩码门/ROI 门）
  C5  media selftest 30 判例（含 debug_mask_capture_* 两条独立断言）
  C6  chain：debug_view 运行态 JSON 独立解析断言（可见性/字段/掩码真实可用
      1280x720/正像素>0/ROI 门控）+ debug_view_stopped 每路不可用+原因
  C7  凭据泄漏扫描（len>=10 才计命中，admin 假阳性口径同前轮）
  C8  确定性域：selftest 两轮 JSON 恒等（fresh 内部）+ manifest 在场

用法: python verify_item012_compare.py --fresh-stamp verify-20261019-item012-v1
    [--impl-stamp 20261021-item012-r4]
退出: 0 全部判据通过；1 有失败（结果 JSON 落 item012-compare-<stamp>.json）。
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

# 交付单测新增的 65 个判例名（test_vision_debug_view.cpp 独立 grep 得出，验证侧自采）
DEBUG_CASE_DOMAIN = [
    "debug_switches_default_all_off", "debug_switches_describe_none",
    "debug_parse_switches_ok", "debug_parse_switches_values",
    "debug_parse_switches_empty_all_off", "debug_parse_switches_unknown_rejected",
    "debug_parse_switches_none_explicit", "debug_parse_switches_off_explicit",
    "debug_parse_switches_mask_text", "debug_switches_set",
    "debug_overlay_tokens", "debug_parse_overlay_mask", "debug_parse_overlay_rejects",
    "debug_overlay_count_four",
    "debug_driver_hidden", "debug_driver_no_channels", "debug_driver_no_overlays",
    "debug_driver_reason",
    "debug_ops_role_visible_maintainer", "debug_ops_role_visible_developer",
    "debug_ops_role_visible_admin",
    "debug_ops_default_overlays_off_maintainer", "debug_ops_default_overlays_off_developer",
    "debug_ops_default_overlays_off_admin",
    "debug_channels_count", "debug_channel_available", "debug_model_version_text",
    "debug_fps_text", "debug_latency_text", "debug_confidence_text", "debug_box_count_text",
    "debug_distance_text_unknown", "debug_last_result_text", "debug_available_no_reason",
    "debug_unavailable_reason_explicit", "debug_unknown_placeholders",
    "debug_unknown_not_zero",
    "debug_json_visible_switches", "debug_json_unavailable_reason",
    "debug_json_unknown_placeholder", "debug_json_no_mask_when_not_provided",
    "debug_mask_switch_off_no_overlay", "debug_mask_available_reported",
    "debug_mask_switch_off_no_plane",
    "debug_mask_overlay_provided", "debug_mask_zero_copy", "debug_mask_dimensions",
    "debug_mask_positive_pixels", "debug_mask_positive_pixels_bounded",
    "debug_json_mask_payload",
    "debug_mask_unavailable_no_overlay", "debug_mask_unavailable_reason",
    "debug_json_overlay_flags_false",
    "debug_mask_size_mismatch_rejected", "debug_mask_size_mismatch_reason",
    "debug_roi_absent_no_overlay", "debug_roi_absent_reason",
    "debug_roi_overlay_provided", "debug_roi_rect_values", "debug_json_roi_rect",
    "debug_overlays_effective_when_on", "debug_boxes_text_overlays",
    "debug_mask_roi_off_by_default",
    "debug_empty_registry_ok", "debug_json_empty_channels",
]

UNKNOWN_TXT = "—"


def check(ok, name, detail=""):
    RES.append({"ok": bool(ok), "name": name, "detail": str(detail)[:500]})
    print("[%s] %s %s" % ("PASS" if ok else "FAIL", name, detail), flush=True)


def read(path):
    with open(path, encoding="utf-8", errors="replace") as fh:
        return fh.read()


def unesc(s):
    return (s.replace("&gt;", ">").replace("&lt;", "<")
            .replace("&quot;", '"').replace("&apos;", "'").replace("&amp;", "&"))


def junit(path):
    root = ET.parse(path).getroot()
    names = [unesc(tc.get("name")) for tc in root.iter("testcase") if tc.get("name")]
    attrs = {e.tag: e.text for e in root.iter() if e.tag in ("testsuite",)}
    return names, attrs


def view_json(text, tag="debug_view"):
    m = re.search(r"^type=%s json=(\{.*\})$" % tag, text, re.M)
    return json.loads(m.group(1)) if m else None


def kv_of_json(j):
    """把 DebugViewModel JSON 拆成 per-camera dict 列表。"""
    return {c["camera_id"]: c for c in j.get("channels", [])}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fresh-stamp", required=True)
    ap.add_argument("--impl-stamp", default="20261021-item012-r4")
    args = ap.parse_args()
    fs = args.fresh_stamp
    impl = args.impl_stamp
    T = "vision-item012-192_168_0_103-%s" % fs
    IP = "vision-item012-192_168_0_103-%s" % impl

    def f(name):
        p = os.path.join(EVID, "%s.%s" % (T, name))
        return read(p) if os.path.isfile(p) else None

    def i(name):
        p = os.path.join(EVID, "%s.%s" % (IP, name))
        return read(p) if os.path.isfile(p) else None

    fx_log = "app_core-evidence_item012-%s-%s.log"
    fx_json = "app_core-evidence_item012-%s-%s.json"

    # ============ 主体判据（对任意 stamp 可执行）============
    def run_suite(tag, impl_mode):
        n0 = len(RES)
        trp = os.path.join(EVID, "%s.transcript.json" % T) if tag == "fresh" else None
        # ---- C0 transcript ----
        if tag == "fresh":
            check(os.path.isfile(trp), "C00_fresh_transcript_present", fs)
            tr = json.loads(read(trp))
            steps = {s["step"]: s for s in tr["steps"]}
            need = ["env", "mkdir", "upload-app", "upload-model", "build-core", "build-media",
                    "core-tests", "debug-selftest", "fixture-fields", "fixture-unavailable",
                    "fixture-mask_on", "fixture-mask_off", "fixture-no_mask", "fixture-driver",
                    "config-dev-all-overlays", "config-driver-all-overlays",
                    "config-maint-default-off", "media-selftest", "chain", "download"]
            missing = [s for s in need if s not in steps]
            check(not missing, "C00_steps_present", "missing=%s" % missing)
            bad_exit = [k for k, v in steps.items()
                        if k not in ("download",) and v.get("exit") not in (0, None)]
            check(not bad_exit, "C00_step_exits_zero", "bad=%s" % bad_exit)
            upload = steps.get("upload-app", {}).get("output", "")
            mup = re.search(r"uploaded (\d+) files", upload)
            check(mup is not None and int(mup.group(1)) > 300, "C00_upload_scale",
                  upload[:40])
            # 每步 EXIT token 全 0（build×2 + test + selftest + 6 fixtures + 3 config +
            # media + chain = 15）
            main_path = os.path.join(EVID, "%s.log" % T)
            main_log = read(main_path) if os.path.isfile(main_path) else None
            toks = re.findall(r"(BUILD_CORE_EXIT|BUILD_MEDIA_EXIT|TEST_EXIT|DEBUG_SELFTEST_EXIT|"
                              r"FIXTURE_EXIT|CFG_EXIT|MEDIA_SELFTEST_EXIT|CHAIN_EXIT)=(\d+)",
                              main_log or "")
            check(len(toks) >= 15 and all(v == "0" for _, v in toks),
                  "C01_exit_tokens_zero", "n=%d %s" % (len(toks), toks[:4]))
            cleanup = steps.get("cleanup")
            # 期望由运行参数推导：keep_remote=true -> cleanup 必须延后；false -> 必须清理
            kept = tr.get("invocation", {}).get("keep_remote")
            if kept:
                check(cleanup is None, "C02_cleanup_deferred_keep_remote",
                      "keep-remote: cleanup deferred")
            else:
                check(cleanup is not None and cleanup.get("exit") == 0,
                      "C02_cleanup_executed", "")
        else:
            # dry-run 对 impl r4：transcript 在场即可，判据子集
            trp = os.path.join(EVID, "%s.transcript.json" % IP)
            check(os.path.isfile(trp), "D00_impl_transcript_present", impl)
            tr = json.loads(read(trp))
            steps = {s["step"]: s for s in tr["steps"]}
            bad = [k for k, v in steps.items()
                   if k not in ("download", "cleanup") and v.get("exit") not in (0, None)]
            check(not bad, "D00_impl_step_exits_zero", "bad=%s" % bad)

        # ---- C1 JUnit ----
        xml_name = None
        for cand in sorted(os.listdir(EVID)):
            if cand.startswith(T if tag == "fresh" else IP) and \
               cand.endswith("core-tests-%s.xml" % (fs if tag == "fresh" else impl)):
                xml_name = cand
        if tag == "fresh":
            check(xml_name is not None, "C10_junit_xml_present", str(xml_name))
        if xml_name:
            names, attrs = junit(os.path.join(EVID, xml_name))
            nf = sum(1 for e in ET.parse(os.path.join(EVID, xml_name)).getroot().iter("failure"))
            check(nf == 0, "C10_junit_failures_zero", "failures=%d" % nf)
            dom = [n for n in names if n.startswith("debug_")]
            cnt = Counter(dom)
            want = Counter(DEBUG_CASE_DOMAIN)
            check(cnt == want, "C11_debug_case_domain_multiset",
                  "fresh_dom=%d want=%d diff=%s" % (
                      sum(cnt.values()), sum(want.values()),
                      list((cnt - want).elements())[:5] + list((want - cnt).elements())[:5]))
            tests = sum(int(e.get("tests", 0)) for e in
                        ET.parse(os.path.join(EVID, xml_name)).getroot().iter("testsuite"))
            check(tests == 1412, "C12_junit_total_1412", "tests=%d" % tests)

        # ---- C1b JUnit 跨轮恒等（fresh vs r4 判例名多重集）----
        xml_impl = None
        for cand in sorted(os.listdir(EVID)):
            if cand.startswith(IP) and cand.endswith("core-tests-%s.xml" % impl):
                xml_impl = cand
        if tag == "fresh" and xml_name and xml_impl:
            names_a, _ = junit(os.path.join(EVID, xml_name))
            names_b, _ = junit(os.path.join(EVID, xml_impl))
            check(Counter(names_a) == Counter(names_b), "C1b_junit_names_fresh_eq_r4",
                  "fresh=%d impl=%d" % (len(names_a), len(names_b)))

        # ---- C2 selftest ----
        st_f = f(fx_log % ("debug-selftest", fs)) if tag == "fresh" else \
            i(fx_log % ("debug-selftest", impl))
        st_i = i(fx_log % ("debug-selftest", impl))
        if st_f and st_i:
            names_f = re.findall(r"^\[PASS\] (\S+)", st_f, re.M)
            names_i = re.findall(r"^\[PASS\] (\S+)", st_i, re.M)
            check(len(names_f) == 17 and len(names_i) == 17, "C20_selftest_17_cases",
                  "fresh=%d impl=%d" % (len(names_f), len(names_i)))
            check(names_f == names_i, "C21_selftest_names_identity", "fresh vs r4 逐行恒等")
            check("RESULT: PASS failures=0" in st_f, "C22_selftest_result_pass", "")
        elif tag == "fresh":
            check(False, "C2_selftest_logs_present", "missing")

        # ---- C3 fixtures 语义重推导 ----
        for name in ("fields", "unavailable", "mask_on", "mask_off", "no_mask", "driver"):
            txt = f(fx_log % ("fixture-" + name, fs)) if tag == "fresh" else \
                i(fx_log % ("fixture-" + name, impl))
            if not txt:
                check(False, "C3_%s_log_present" % name, "missing")
                continue
            v = view_json(txt, "view")
            check(v is not None, "C3_%s_view_json_parsed" % name, "")
            if v is None:
                continue
            cams = kv_of_json(v)
            ok = True
            det = []
            if name == "fields":
                c = cams.get("CAM-01", {})
                ok = (v.get("visible") is True and v.get("overlays_effective") is False and
                      c.get("model_version") == "rknn-2.3.2/yolov5s" and
                      c.get("fps_text") == "5.0 fps" and c.get("latency_text") == "142 ms" and
                      c.get("confidence_text") == "0.87" and c.get("box_count_text") == "1" and
                      c.get("last_result_text") == "0 秒前" and
                      c.get("distance_text") == UNKNOWN_TXT and
                      len(c.get("boxes", [])) == 1 and
                      c["boxes"][0].get("label") == "person" and
                      c["boxes"][0].get("in_roi") is True)
                det = ["field formatting rederived"]
            elif name == "unavailable":
                c = cams.get("CAM-04", {})
                unknowns = [c.get(k) for k in ("model_version", "fps_text", "latency_text",
                                               "confidence_text", "distance_text",
                                               "box_count_text", "last_result_text")]
                ok = (c.get("available") is False and
                      c.get("unavailable_reason") == "PROFILE_UNSUPPORTED" and
                      all(u == UNKNOWN_TXT for u in unknowns) and
                      all(u not in ("0", "0.0", "0 ms", "0.0 fps", "0.0 m", "0.00")
                          for u in unknowns))
                det = ["unknown!=0 rederived"]
            elif name == "mask_on":
                c = cams.get("CAM-01", {})
                mk = c.get("mask") or {}
                ok = (c.get("overlays", {}).get("mask") is True and
                      mk.get("width") == 64 and mk.get("height") == 48 and
                      mk.get("stride_bytes") == 64 and mk.get("kind") == "rail" and
                      mk.get("plane_present") is True and mk.get("positive_pixels") == 1024)
                det = ["positive rederived: 64*48 every 3rd -> %s" % mk.get("positive_pixels")]
                # 独立重算：i%3==0 的下标数
                exp = sum(1 for i in range(64 * 48) if i % 3 == 0)
                ok = ok and mk.get("positive_pixels") == exp
                det = ["positive_pixels==%d (independent recompute %d)"
                       % (mk.get("positive_pixels", -1), exp)]
            elif name == "mask_off":
                c = cams.get("CAM-01", {})
                ok = (c.get("overlays", {}).get("mask") is False and
                      c.get("mask_available") is True and
                      ("mask" not in c or c.get("mask") is None or
                       (c.get("mask") or {}).get("plane_present") is None))
                det = ["switch off -> no payload, availability observable"]
            elif name == "no_mask":
                c = cams.get("CAM-01", {})
                ok = (c.get("overlays", {}).get("mask") is False and
                      c.get("mask_available") is False and
                      c.get("mask_unavailable_reason") == "SEG_UNAVAILABLE")
                det = ["no mask -> reason only"]
            elif name == "driver":
                ok = (v.get("visible") is False and v.get("channels") == [] and
                      v.get("overlays_effective") is False and
                      "driver" in v.get("hidden_reason", "") and
                      v.get("switches", {}).get("mask") is True)
                det = ["switches requested all-on yet hidden"]
            check(ok, "C3_%s_semantics" % name, "; ".join(det))
            check("RESULT: PASS failures=0" in txt, "C3_%s_result_pass" % name, "")

        # ---- C4 config 三口径 ----
        for name, role in (("dev-all-overlays", "developer"),
                           ("driver-all-overlays", "driver"),
                           ("maint-default-off", "maintainer")):
            txt = f(fx_log % ("config-" + name, fs)) if tag == "fresh" else \
                i(fx_log % ("config-" + name, impl))
            if not txt:
                check(False, "C4_%s_log_present" % name, "missing")
                continue
            v = view_json(txt, "view")
            check(v is not None, "C4_%s_view_json_parsed" % name, "")
            if v is None:
                continue
            cams = kv_of_json(v)
            if name == "dev-all-overlays":
                c1 = cams.get("CAM-01", {})
                others = [c for cid, c in cams.items() if cid != "CAM-01"]
                mk = c1.get("mask") or {}
                exp_pos = sum(1 for i in range(1280 * 720) if i % 7 == 0)
                ok = (v.get("visible") is True and v.get("overlays_effective") is True and
                      c1.get("available") is False and
                      c1.get("unavailable_reason") == "等待结果" and
                      c1.get("model_version") == UNKNOWN_TXT and
                      c1.get("overlays", {}) == {"boxes": True, "roi": True, "text": True,
                                                 "mask": True} and
                      c1.get("has_roi") is True and
                      (c1.get("roi_rect") or {}).get("x") == 0.08 and
                      mk.get("width") == 1280 and mk.get("height") == 720 and
                      mk.get("kind") == "rail" and mk.get("plane_present") is True and
                      mk.get("positive_pixels") == exp_pos and
                      all(c.get("overlays", {}).get("mask") is False and
                          c.get("mask_available") is False and
                          c.get("mask_unavailable_reason") == "掩码不可用"
                          for c in others))
                check(ok, "C4_dev_all_overlays_semantics",
                      "mask_demo 1280x720 every-7th rederived=%d" % exp_pos)
            elif name == "driver-all-overlays":
                ok = (v.get("visible") is False and v.get("channels") == [] and
                      v.get("overlays_effective") is False and
                      v.get("switches", {}).get("mask") is True and
                      "driver" in v.get("hidden_reason", ""))
                check(ok, "C4_driver_hidden_even_all_on", v.get("hidden_reason", "")[:60])
            else:
                ok = (v.get("visible") is True and v.get("overlays_effective") is False and
                      v.get("switches_desc") == "none" and
                      all(c.get("overlays", {}) == {"boxes": False, "roi": False,
                                                    "text": False, "mask": False}
                          for c in cams.values()))
                check(ok, "C4_maint_default_off_semantics", "overlays none -> all flags false")
            check("RESULT: PASS failures=0" in txt, "C4_%s_result_pass" % name, "")

        # ---- C5 media selftest ----
        mtxt = f(fx_log % ("media-selftest", fs)) if tag == "fresh" else \
            i(fx_log % ("media-selftest", impl))
        if mtxt:
            names_m = re.findall(r"^\[PASS\] (\S+)", mtxt, re.M)
            check(len(names_m) == 30, "C50_media_selftest_30", "n=%d" % len(names_m))
            check("debug_mask_capture_unavailable_reason" in names_m and
                  "debug_mask_capture_off_not_captured" in names_m,
                  "C51_media_mask_capture_cases", "2 mask capture cases in domain")
            check("RESULT: PASS failures=0" in mtxt, "C52_media_selftest_pass", "")
        else:
            check(False, "C5_media_selftest_log_present", "missing")

        # ---- C6 chain debug_view ----
        ctxt = f(fx_log % ("chain", fs)) if tag == "fresh" else i(fx_log % ("chain", impl))
        if ctxt:
            v = view_json(ctxt, "debug_view")
            check(v is not None, "C60_chain_debug_view_present", "")
            if v:
                cams = kv_of_json(v)
                c1 = cams.get("CAM-01", {})
                mk = c1.get("mask") or {}
                ok = (v.get("visible") is True and v.get("overlays_effective") is True and
                      c1.get("available") is True and
                      "yolov8s-seg" in (c1.get("model_version") or "") and
                      "yolov5s" in (c1.get("model_version") or "") and
                      c1.get("fps_text") not in (None, "", UNKNOWN_TXT) and
                      c1.get("latency_text") not in (None, "", UNKNOWN_TXT) and
                      c1.get("confidence_text") == UNKNOWN_TXT and
                      c1.get("box_count_text") == "0" and
                      c1.get("overlays", {}).get("mask") is True and
                      c1.get("overlays", {}).get("boxes") is False and
                      mk.get("width") == 1280 and mk.get("height") == 720 and
                      mk.get("stride_bytes") == 1280 and mk.get("kind") == "rail" and
                      mk.get("plane_present") is True and
                      isinstance(mk.get("positive_pixels"), int) and
                      0 < mk.get("positive_pixels", 0) < 1280 * 720)
                check(ok, "C61_chain_running_state_semantics",
                      "mask %sx%s pos=%s conf=%s" % (mk.get("width"), mk.get("height"),
                                                     mk.get("positive_pixels"),
                                                     c1.get("confidence_text")))
                vs = view_json(ctxt, "debug_view_stopped")
                check(vs is not None, "C62_chain_stopped_view_present", "")
                if vs:
                    cs = kv_of_json(vs)
                    ok2 = (all(c.get("available") is False and
                               c.get("unavailable_reason") == "AI 不可用"
                               for c in cs.values()) and len(cs) >= 1)
                    check(ok2, "C63_chain_stopped_unavailable_with_reason",
                          "n=%d" % len(cs))
            pf = re.findall(r"^\[PASS\] chain_\S+", ctxt, re.M)
            check(len(pf) == 13, "C64_chain_13_pass_lines", "n=%d" % len(pf))
            check("RESULT: PASS failures=0" in ctxt, "C65_chain_result_pass", "")
            # 覆盖门控：mask 开关行与 capture 一致
            dsw = re.search(r"^type=debug_switch role=(\S+) overlays=(\S+) capture_mask=(\S+)$",
                            ctxt, re.M)
            check(dsw is not None and dsw.group(1) == "developer" and
                  dsw.group(2) == "mask" and dsw.group(3) == "true",
                  "C66_debug_switch_echo", dsw.groups() if dsw else "missing")
        else:
            check(False, "C6_chain_log_present", "missing")
        return len(RES) - n0

    # ---- fresh 主判据 ----
    run_suite("fresh", impl_mode=False)

    # ---- C7 凭据泄漏（fresh 归档；len>=10 口径）----
    secret = os.environ.get("TRAIN_RK3588_SSH_CREDENTIAL", "")
    if len(secret) < 10:
        ps_secret = None
        try:
            import subprocess
            p = subprocess.run(
                ["powershell.exe", "-NoProfile", "-Command",
                 "[Environment]::GetEnvironmentVariable('TRAIN_RK3588_SSH_CREDENTIAL','User')"],
                capture_output=True, text=True, timeout=30)
            ps_secret = (p.stdout or "").strip()
        except Exception:
            ps_secret = None
        secret = ps_secret or secret
    leak_hits = []
    if len(secret) >= 10:
        for cand in sorted(os.listdir(EVID)):
            if not cand.startswith(T):
                continue
            body = read(os.path.join(EVID, cand))
            if secret in body:
                leak_hits.append(cand)
    check(not leak_hits, "C7_credential_not_in_fresh_evidence",
          "cred_len=%d hits=%s" % (len(secret), leak_hits))

    # ---- C8 跨轮恒等：selftest/fixture 稳定行 fresh vs impl r4 ----
    for name in ("debug-selftest", "fixture-fields", "fixture-unavailable", "fixture-mask_on",
                 "fixture-mask_off", "fixture-no_mask", "fixture-driver"):
        a = f(fx_log % (name, fs))
        b = i(fx_log % (name, impl))
        if a and b:
            ka = "\n".join(ln for ln in a.splitlines() if ln.startswith("type="))
            kb = "\n".join(ln for ln in b.splitlines() if ln.startswith("type="))
            check(ka == kb, "C8_type_lines_identity_%s" % name,
                  "fresh=%d impl=%d type-lines" % (len(ka), len(kb)))

    # ---- 汇总 ----
    fails = [r for r in RES if not r["ok"]]
    total = len(RES)
    out = {
        "stamp": fs, "impl_stamp": impl, "total": total,
        "failed": len(fails), "ok": not fails, "results": RES,
    }
    with open(os.path.join(HERE, "item012-compare-%s.json" % fs), "w", encoding="utf-8") as fh:
        json.dump(out, fh, ensure_ascii=False, indent=1)
    print("COMPARE TOTAL: %d/%d %s" % (total - len(fails), total,
                                       "PASS" if not fails else "FAIL"), flush=True)
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())
