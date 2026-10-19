#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-001 独立解析比对（不采信交付 acceptance 行）。

比对域：
  T1  fresh transcript 全步骤退出码独立重断言 + keep-remote 无 cleanup + 主机指纹
  T2  selftest 日志逐行解析（8 判例、期望==实际）+ 与 impl r3 全文恒等
  T3  真实 rknn_query JSON 结构独立断言（seg 13 输出/NHWC/唯一 proto/三尺度；det 3x255）
  T4  fresh vs impl r3 张量集合逐字段语义恒等（dir/index 键）
  T5  verify-seg/det READY 行 + 负例 MODEL_MISMATCH + 量化交叉核对
  T6  配置汇总计数/逐路语义/profile_gap 行/nodesc NOT_READY
  T7  core JUnit 958/0 + 判例名集合级恒等
  T8  fresh 全产物凭据语义脱敏扫描
"""
import json
import os
import re
import sys

BASE = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
IMPL = "vision-item001-192_168_0_103-20261018-item001-r3"
FRESH = "vision-item001-192_168_0_103-verify-20261018-item001-v1"
T = "20261018-item001-r3"
V = "verify-20261018-item001-v1"

results = []


def chk(ok, name, detail=""):
    results.append((ok, name, detail))


def read(p):
    with open(p, encoding="utf-8") as fh:
        return fh.read()


def jread(p):
    with open(p, encoding="utf-8") as fh:
        return json.load(fh)


# ---- T1 transcript ----
# 注意：runner 的 rrun 退出码是外层 shell（含 echo EXIT=$?）的退出码，恒 0；
# 真实探针退出码须从 output 的 *_EXIT token 解析（ITEM-002 同源教训）。
tr = jread(os.path.join(BASE, FRESH + ".transcript.json"))
steps = {s["step"]: s for s in tr["steps"]}
exp = [("build-core", 0), ("core-tests", 0), ("build-media", 0), ("selftest", 0),
       ("rknn-probe-seg", 0), ("rknn-probe-det", 0), ("verify-seg", 0), ("verify-det", 0),
       ("verify-negative", 1), ("config-no-desc", 0), ("config-with-desc", 0)]
for name, e in exp:
    s = steps.get(name)
    out = (s.get("output") or "") if s else ""
    mtok = re.search(r"(?:[A-Z_]+_EXIT|BUILD_EXIT|TEST_EXIT)=(\d+)", out)
    real_exit = int(mtok.group(1)) if mtok else None
    chk(s is not None and real_exit == e, "T1_%s_exit%d" % (name, e),
        "token=%s transcript_exit=%s" % (real_exit, s.get("exit") if s else "MISSING"))
chk(all(s["step"] != "cleanup" for s in tr["steps"]), "T1_keep_remote_no_cleanup")
fp = tr.get("host_key_fingerprint") or ""
chk(fp.startswith("SHA256:"), "T1_host_fingerprint_present", fp)

# ---- T2 selftest ----
st = read(os.path.join(BASE, "%s.app_core-evidence_item001-selftest-%s.log" % (FRESH, V)))
passes = re.findall(r"^\[PASS\] (.+) -> ready=(\w+) reason=(\S+) \(expect ready=(\w+) reason=(\S+)\)$",
                    st, re.M)
chk(len(passes) == 8, "T2_selftest_8_pass_lines", str(len(passes)))
# 组序: (case, actual_ready, actual_reason, expect_ready, expect_reason) → actual==expect
chk(all(a == c and b == d for _, a, b, c, d in passes), "T2_selftest_expectations_match_actual")
chk("RESULT: PASS passed=8 failed=0" in st, "T2_selftest_result_line")
st3 = read(os.path.join(BASE, "%s.app_core-evidence_item001-selftest-%s.log" % (IMPL, T)))
chk(st.strip() == st3.strip(), "T2_selftest_identical_to_impl_r3")

# ---- T3 real rknn_query JSON structure ----
seg = jread(os.path.join(BASE, "%s.app_core-evidence_item001-rknn-seg-%s.json" % (FRESH, V)))
det = jread(os.path.join(BASE, "%s.app_core-evidence_item001-rknn-det-%s.json" % (FRESH, V)))
chk(seg["load_status"] == "ok" and len(seg["inputs"]) == 1 and len(seg["outputs"]) == 13,
    "T3_seg_13_outputs")
inp = seg["inputs"][0]
chk(inp["dims"] == [1, 640, 640, 3] and inp["fmt"] == "nhwc" and inp["type"] == "int8"
    and inp["qnt"] == "affine" and inp["zp"] == -128, "T3_seg_input_nhwc_int8")
protos = [t for t in seg["outputs"] if t["dims"] == [1, 32, 160, 160]]
chk(len(protos) == 1, "T3_seg_exactly_one_proto")
branch = [t for t in seg["outputs"] if t["dims"] != [1, 32, 160, 160]]
scales = sorted(set((t["dims"][2], t["dims"][3]) for t in branch))
chk(scales == [(20, 20), (40, 40), (80, 80)], "T3_seg_three_scales", str(scales))
chk(det["load_status"] == "ok" and len(det["outputs"]) == 3
    and sorted(t["dims"][1] for t in det["outputs"]) == [255, 255, 255], "T3_det_3x255")

# ---- T4 tensor set identity vs impl r3 ----
def tensors(doc):
    m = {}
    for direction in ("inputs", "outputs"):
        for t in doc[direction]:
            key = (direction, t["index"])
            m[key] = (t["name"], t["n_dims"], tuple(t["dims"]), t["type"],
                      t["fmt"], t["qnt"], t["zp"], t["scale"])
    return m


seg3 = jread(os.path.join(BASE, "%s.app_core-evidence_item001-rknn-seg-%s.json" % (IMPL, T)))
det3 = jread(os.path.join(BASE, "%s.app_core-evidence_item001-rknn-det-%s.json" % (IMPL, T)))
chk(tensors(seg) == tensors(seg3), "T4_seg_tensors_identical_to_r3")
chk(tensors(det) == tensors(det3), "T4_det_tensors_identical_to_r3")

# ---- T5 verify logs ----
vs = read(os.path.join(BASE, "%s.app_core-evidence_item001-verify-seg-%s.log" % (FRESH, V)))
vd = read(os.path.join(BASE, "%s.app_core-evidence_item001-verify-det-%s.log" % (FRESH, V)))
vn = read(os.path.join(BASE, "%s.app_core-evidence_item001-verify-negative-%s.log" % (FRESH, V)))
chk("ready=1 reason=READY" in vs and "kind=yolov8_seg" in vs and "matched=10" in vs
    and "mismatched=0" in vs, "T5_verify_seg_ready_10_0")
chk("ready=1 reason=READY" in vd and "kind=yolov5_detect" in vd and "matched=8" in vd,
    "T5_verify_det_ready_8")
chk("ready=0 reason=MODEL_MISMATCH" in vn
    and "declared=yolov5_detect identified=yolov8_seg" in vn, "T5_negative_mismatch")
m = re.search(r"input\[0\]\.quant=int8/affine zp=(-?\d+) scale=([\d.]+)", vs)
chk(m is not None and int(m.group(1)) == inp["zp"]
    and abs(float(m.group(2)) - inp["scale"]) < 1e-6, "T5_verify_seg_matches_raw_query")

# ---- T6 config summary ----
cfg_log = read(os.path.join(BASE, "%s.app_core-evidence_item001-config-desc-%s.log" % (FRESH, V)))
cfg = jread(os.path.join(BASE, "%s.app_core-evidence_item001-config-desc-%s.json" % (FRESH, V)))
chk(cfg["declared_count"] == 4 and cfg["declared_failed"] == 0
    and cfg["unsupported_profiles"] == 3 and cfg["unverified"] == 0
    and cfg["profile_gap"] is True, "T6_config_counters")
front = [mm for mm in cfg["models"] if mm["profile"] == "front"]
chk(len(front) == 1 and front[0]["available"] is True and front[0]["reason"] == "READY"
    and front[0]["model_name"] == "yolov5s"
    and front[0]["model_version"] == "rknn-2.3.2/yolov5s", "T6_front_ready_with_version")
chk(all(mm["reason"] == "PROFILE_UNSUPPORTED" for mm in cfg["models"] if mm["profile"] != "front"),
    "T6_others_profile_unsupported")
chk("profile.front evidence=1 required=2 yolov8_seg=missing yolov5_detect=ok" in cfg_log,
    "T6_profile_gap_line")
nd = read(os.path.join(BASE, "%s.app_core-evidence_item001-config-nodesc-%s.log" % (FRESH, V)))
chk("profile=front model=yolov5s" in nd and "available=0 reason=NOT_READY" in nd
    and "file present; structure not verified" in nd, "T6_nodesc_not_ready")

# ---- T7 core JUnit ----
xml = read(os.path.join(BASE, "%s.app_core-evidence_item001-core-tests-%s.xml" % (FRESH, V)))
mt = re.search(r'<testsuites tests="(\d+)" failures="(\d+)"', xml)
chk(mt is not None and mt.group(1) == "958" and mt.group(2) == "0", "T7_junit_958_0",
    mt.group(0) if mt else "none")
xml3 = read(os.path.join(BASE, "%s.app_core-evidence_item001-core-tests-%s.xml" % (IMPL, T)))
# 重名判例按既有登记口径只做集合级比对（30 个重名用例→唯一名 928）；计数恒等单独断言
f_names = re.findall(r'testcase classname="[^"]*" name="([^"]+)"', xml)
r3_names = re.findall(r'testcase classname="[^"]*" name="([^"]+)"', xml3)
chk(len(f_names) == 958 and len(r3_names) == 958, "T7_junit_case_count_958",
    "fresh=%d r3=%d" % (len(f_names), len(r3_names)))
chk(sorted(f_names) == sorted(r3_names), "T7_junit_names_multiset_identical",
    "unique fresh=%d r3=%d" % (len(set(f_names)), len(set(r3_names))))
chk(len(set(f_names)) == 928, "T7_junit_unique_names_928", str(len(set(f_names))))

# ---- T8 credential-leak scan over fresh artifacts ----
leak = []
for fn in sorted(os.listdir(BASE)):
    if V not in fn:
        continue
    p = os.path.join(BASE, fn)
    if not os.path.isfile(p):
        continue
    body = read(p)
    if re.search(r"[a-z]+://[^/\s:]+:[^@\s]+@", body):
        leak.append((fn, "cred-url"))
    if "password=" in body or "TRAIN_RK3588_SSH_CREDENTIAL=" in body:
        leak.append((fn, "password-kv"))
chk(not leak, "T8_no_credential_leak", str(leak[:3]))

npass = sum(1 for ok, _, _ in results if ok)
for ok, name, detail in results:
    if not ok:
        print("COMPARE FAIL", name, detail)
print("COMPARE SUMMARY passed=%d/%d" % (npass, len(results)))
raise SystemExit(0 if npass == len(results) else 1)
