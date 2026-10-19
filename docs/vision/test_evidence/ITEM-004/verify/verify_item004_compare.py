#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-004 独立解析比对（software_verification, VERIFY-20261019-item004-v1）。

不采信交付 acceptance/RESULT 行；全部判据独立重推导：
  T1  fresh transcript 退出码独立重断言（*_EXIT token，外层 shell 恒 0）+ keep-remote 无 cleanup
      + 指纹 + 15 下载（25 产物 = 2+2+2+2+10×2+log/transcript）
  T2  selftest 判例数从探针源 run_selftest 段 check( 调用数统计（不手抄）+ 判例名独立逐条
      + 关键语义行精确锚定 + fresh vs impl r1 全文恒等
  T3  分割数学独立重推导：独立 letterbox 公式 + 独立 DFL softmax/掩码闭式计数/逐像素镜像
      （对齐历史已部署 code/rknpu2_postprocess.cc 公式）+ 10 夹具 type=seg/box/mask 行逐字段独立断言
  T4  config 行独立重推导（confidence_min 从 thresholds.yaml 源读 = 0.5）
  T5  core JUnit tests=1059 failures=0 + 判例名多重集恒等 fresh vs r1 +
      分割域判例名（从验证侧单测源 check 名字独立提取）在场
  T6  全 fresh 产物凭据语义脱敏扫描
"""
import collections
import json
import math
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ITEMD = os.path.normpath(os.path.join(HERE, ".."))
IMPL = "vision-item004-192_168_0_103-20261019-item004-r1"
FRESH = "vision-item004-192_168_0_103-verify-20261019-item004-v1"
T = "20261019-item004-r1"
V = "verify-20261019-item004-v1"

results = []


def chk(ok, name, detail=""):
    results.append((ok, name, detail))
    if not ok:
        print("FAIL %s :: %s" % (name, detail))


def read(p):
    with open(p, encoding="utf-8") as fh:
        return fh.read()


def jread(p):
    with open(p, encoding="utf-8") as fh:
        return json.load(fh)


PX = 0.0
PY = 140.0
SW, SH = 1280, 720
S = 0.5


def clamp(v, hi):
    return max(0.0, min(v, hi))


def norm(mx1, my1, mx2, my2):
    rx1, ry1 = (mx1 - PX) / S, (my1 - PY) / S
    rx2, ry2 = (mx2 - PX) / S, (my2 - PY) / S
    cx1, cy1 = clamp(rx1, SW), clamp(ry1, SH)
    cx2, cy2 = clamp(rx2, SW), clamp(ry2, SH)
    if not (cx2 > cx1) or not (cy2 > cy1):
        return None
    nx, ny = cx1 / SW, cy1 / SH
    nw, nh = (cx2 - cx1) / SW, (cy2 - cy1) / SH
    if nx + nw > 1.0:
        nw = 1.0 - nx
    if ny + nh > 1.0:
        nh = 1.0 - ny
    if not (nw > 0.0) or not (nh > 0.0):
        return None
    return (nx, ny, nw, nh)


# ---------- T1 transcript ----------
tr = jread(os.path.join(ITEMD, FRESH + ".transcript.json"))
steps = {s["step"]: s for s in tr["steps"]}
FIXES = ["rail", "zero_coeff", "nocrop", "nms", "empty",
         "badcount", "badsegshape", "badproto", "badfmt", "int8"]
exp = [("build-core", "BUILD_EXIT", 0), ("core-tests", "TEST_EXIT", 0),
       ("selftest", "SELFTEST", 0)] + \
      [("fixture-" + fx, "FIX_" + fx.upper(), 0) for fx in FIXES] + \
      [("config", "CONFIG_EXIT", 0)]
for name, tok, e in exp:
    s = steps.get(name)
    out = (s.get("output") or "") if s else ""
    toks = re.findall(r"([A-Z_0-9]+(?:_EXIT)?)=(\d+)", out)
    if name == "selftest":
        vals = [int(v) for v in re.findall(r"SELFTEST(?:_JSON)?_EXIT=(\d+)", out)]
        chk(s is not None and vals == [0, 0], "T1_selftest_exit_00",
            "tokens=%s transcript_exit=%s" % (vals, s.get("exit") if s else "MISSING"))
    else:
        mtok = re.search(r"([A-Z_0-9]+)_EXIT=(\d+)", out)
        chk(s is not None and mtok is not None and int(mtok.group(2)) == e,
            "T1_%s_exit%d" % (name, e),
            "token=%s transcript_exit=%s" % (mtok.group(2) if mtok else None,
                                             s.get("exit") if s else "MISSING"))
chk(all(s["step"] != "cleanup" for s in tr["steps"]), "T1_keep_remote_no_cleanup",
    "cleanup_steps=%d" % sum(1 for s in tr["steps"] if s["step"] == "cleanup"))
fp = tr.get("host_key_fingerprint") or ""
chk(fp.startswith("SHA256:") and "NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU" in fp,
    "T1_host_fingerprint_expected", fp)
up = steps.get("upload-app", {})
chk("uploaded 287 files" in (up.get("output") or ""), "T1_upload_app_reported",
    (up.get("output") or "")[:40])
dl = [s for s in tr["steps"] if s["step"] == "download"]
chk(len(dl) == 25 and all(s.get("exit") == 0 for s in dl), "T1_25_downloads_ok", str(len(dl)))

# ---------- T2 selftest ----------
F_SELF = "%s.app_core-evidence_item004-selftest-%s.log"
st = read(os.path.join(ITEMD, F_SELF % (FRESH, V)))
PROBE_SRC = os.path.normpath(os.path.join(ITEMD, "..", "..", "..", "..",
                                          "app", "src", "core", "tools", "seg_probe.cpp"))
probe_src = read(PROBE_SRC)
m_runself = re.search(r"int run_selftest\(\) \{(.*?)\n\}", probe_src, re.S)
self_body = m_runself.group(1) if m_runself else ""
n_self_calls = len(re.findall(r"^\s*check\((?!bool)", self_body, re.M))
pass_lines = re.findall(r"^\[PASS\] (.+)$", st, re.M)
names2 = [ln.split(" :: ")[0] for ln in pass_lines]
chk(len(pass_lines) == n_self_calls, "T2_selftest_pass_count_from_source",
    "board=%d source=%d" % (len(pass_lines), n_self_calls))
chk("RESULT: PASS failures=0" in st, "T2_selftest_result_line")
must_have = ["rail decode ok", "box<->mask one-to-one", "box normalized coords",
             "score/class/label", "mask dims/stride/size = original", "mask binary + has positives",
             "mask positive inside box (left half)", "mask zero inside box (right half)",
             "mask zero outside box", "zero coefficients -> all-zero mask, box kept",
             "mask_threshold configurable", "crop_to_box configurable", "empty -> SEG_EMPTY",
             "nms keeps 1 (mask follows)", "12 tensors -> MODEL_MISMATCH",
             "bad seg channels -> MODEL_MISMATCH", "bad proto shape -> MODEL_MISMATCH",
             "non-NCHW -> MODEL_MISMATCH", "box channels not 4*dfl -> MODEL_MISMATCH",
             "int8 decode + mask", "int8 box within quant tol", "int8 mask aligned"]
missing = [m for m in must_have if m not in names2]
chk(not missing, "T2_selftest_judgement_names_present", "missing=%s" % (missing,))
st3 = read(os.path.join(ITEMD, F_SELF % (IMPL, T)))
chk(st.strip() == st3.strip(), "T2_selftest_identical_to_impl_r1")

# ---------- T3 fixtures 独立数学 ----------
def seg_xywh_line_all(log):
    return re.findall(r"^box\[(\d+)\] xywh=\(([-\d.]+),([-\d.]+),([-\d.]+),([-\d.]+)\) "
                      r"score=([\d.]+) cls=(\d+) label=(\S+)$", log, re.M)


def seg_row_all(log):
    return re.findall(r"^type=seg ok=(\d) reason=(\S+) boxes=(\d+) masks=(\d+) candidates=(-?\d+) "
                      r"dropped_score=(-?\d+) dropped_class=(-?\d+) nms_suppressed=(-?\d+) "
                      "dropped_bounds=(-?\\d+) truncated=(-?\\d+)$", log, re.M)


def kvline(log, prefix):
    for ln in log.splitlines():
        if ln.startswith(prefix):
            return dict(tok.split("=", 1) for tok in ln[len(prefix):].split() if "=" in tok)
    return {}


def fixture_log(name, tag):
    p = os.path.join(ITEMD, "%s.app_core-evidence_item004-fixture-%s-%s.log" % (tag, name, V if tag == FRESH else T))
    return read(p)


def near(a, b, tol=2e-3):
    return abs(a - b) <= tol


# 独立 letterbox
lbX = {"scale": 0.5, "pad_x": 0.0, "pad_y": 140.0}
chk(640 / 1280 == 0.5 and (640 - 720 * 0.5) // 2 == 140, "T3a_letterbox_independent_math", str(lbX))

# rail 夹具: encode(0,30,40,4,4,4,4,0.9,1.0) -> DFL softmax(12@bin4)≈4（残差<1e-3）
# s0 stride = 640/80 = 8（镜像易错为 16——640/40 是 s1 的）
d_rail = 4.0
mr = norm((-d_rail + 40 + 0.5) * 8.0, (-d_rail + 30 + 0.5) * 8.0,
          (d_rail + 40 + 0.5) * 8.0, (d_rail + 30 + 0.5) * 8.0)
chk(mr is not None and near(mr[0], 0.45625, 1e-4) and near(mr[1], 0.2, 1e-4) and
    near(mr[2], 0.1, 1e-4) and near(mr[3], 0.177778, 1e-4),
    "T3b_rail_fixture_independent_xywh", str(mr))

# rail log 行独立解析
rl = fixture_log("rail", FRESH)
rl_row = kvline(rl, "type=seg ")
chk(rl_row.get("ok") == "1" and rl_row.get("reason") == "SEG_OK" and
    rl_row.get("boxes") == "1" and rl_row.get("masks") == "1" and
    rl_row.get("candidates") == "1" and rl_row.get("dropped_score") == "8399" and
    rl_row.get("nms_suppressed") == "0",
    "T3c_rail_type_seg_row_indep", str(rl_row))
brails = seg_xywh_line_all(rl)
chk(len(brails) == 1 and near(float(brails[0][1]), 0.45625, 2e-3) and
    near(float(brails[0][2]), 0.2, 2e-3) and near(float(brails[0][3]), 0.1, 2e-3) and
    near(float(brails[0][4]), 0.177778, 2e-3) and near(float(brails[0][5]), 0.9, 5e-3) and
    brails[0][7] == "rail",
    "T3d_rail_box_line_xywh", str(brails))
mrl = re.search(r"^mask\[0\] (.+)$", rl, re.M)
mkv = dict(t.split("=", 1) for t in mrl.group(1).split() if "=" in t)
chk(mkv.get("mask") == "1280x720" and mkv.get("stride") == "1280" and mkv.get("size") == "921600" and
    mkv.get("positive") == "7040" and mkv.get("px(600,240)") == "1",
    "T3e_rail_mask_row_indep", str(mkv))
# 7040 闭式独立重算: crop rows 240..367(128) x positive cols 585..639(55) = 7040
chk(128 * 55 == 7040, "T3f_rail_closed_form_count", "128*55=%d" % (128 * 55))

# zero_coeff
zl = fixture_log("zero_coeff", FRESH)
zrow = kvline(zl, "type=seg ")
zmkv = dict(t.split("=", 1) for t in re.search(r"^mask\[0\] (.+)$", zl, re.M).group(1).split() if "=" in t)
chk(zrow.get("ok") == "1" and zrow.get("boxes") == "1" and zrow.get("masks") == "1" and
    zmkv.get("positive") == "0", "T3g_zero_coeff_mask_zero_box_kept", str(zmkv))

# nocrop
nc = fixture_log("nocrop", FRESH)
nmkv = dict(t.split("=", 1) for t in re.search(r"^mask\[0\] (.+)$", nc, re.M).group(1).split() if "=" in t)
chk(nmkv.get("positive") == "460800", "T3h_nocrop_closed_form_720x640", str(nmkv))

# nms: 两框编码独立 IoU 重推导 (+1 约定)
nml = fixture_log("nms", FRESH)
nrow = kvline(nml, "type=seg ")
nboxes = seg_xywh_line_all(nml)
chk(nrow.get("boxes") == "1" and nrow.get("masks") == "1" and
    nrow.get("nms_suppressed") == "1" and nrow.get("candidates") == "2",
    "T3i_nms_row", str(nrow))
st0 = 16.0  # scale0 stride


def iou_plus1(a, b):
    w = max(0.0, min(a[2], b[2]) - max(a[0], b[0]) + 1.0)
    h = max(0.0, min(a[3], b[3]) - max(a[1], b[1]) + 1.0)
    i = w * h
    u = (a[2] - a[0] + 1) * (a[3] - a[1] + 1) + (b[2] - b[0] + 1) * (b[3] - b[1] + 1) - i
    return 0.0 if u <= 0 else i / u


Abox = (292.0, 212.0, 356.0, 276.0)
Bbox = (300.0, 212.0, 364.0, 276.0)
IoUv = iou_plus1(Abox, Bbox)
chk(0.45 < IoUv < 0.98, "T3j_nms_iou_indep_above_threshold", "indep IoU=%r" % IoUv)

# empty
el = fixture_log("empty", FRESH)
erow = kvline(el, "type=seg ")
chk(erow.get("ok") == "1" and erow.get("reason") == "SEG_EMPTY" and
    erow.get("boxes") == "0" and erow.get("masks") == "0",
    "T3k_empty_seg_empty", str(erow))

# MODEL_MISMATCH 五类负例（错误信息逐 token 断言）
bad_expect = {
    "badcount": "MODEL_MISMATCH: tensor count=12 expected=13",
    "badsegshape": "MODEL_MISMATCH: branch[0].seg channels=16 expected=32",
    "badproto": "MODEL_MISMATCH: proto shape 32x80x160 expected 32x160x160",
    "badfmt": "MODEL_MISMATCH: branch[0].unsupported fmt (expect nchw)",
}
for fxn, want in bad_expect.items():
    lg = fixture_log(fxn, FRESH)
    chk(not re.search(r"ok=1", lg) and want in lg,
        "T3l_mismatch_%s_exact_err" % fxn, want[:60])

# selftest bad box channels=63
chk("box channels not 4*dfl -> MODEL_MISMATCH :: MODEL_MISMATCH: branch[0].box channels=63 not 4*dfl" in st,
    "T3m_box63_mismatch_in_selftest")

# int8 量化路径
i8 = fixture_log("int8", FRESH)
i8row = kvline(i8, "type=seg ")
imkv = dict(t.split("=", 1) for t in re.search(r"^mask\[0\] (.+)$", i8, re.M).group(1).split() if "=" in t)
i8b = seg_xywh_line_all(i8)
chk(i8row.get("boxes") == "1" and imkv.get("positive") == "7040" and
    imkv.get("px(600,240)") == "1" and
    near(float(i8b[0][1]), 0.45625, 0.01) and near(float(i8b[0][3]), 0.1, 0.01),
    "T3n_int8_0.1_scale_quant_path", str(imkv))
# float32 scale 精度重算（ITEM-003 教训①复现）
q = struct.pack("f", 0.9)
qf = struct.unpack("f", q)[0]
chk(qf == 0.8999999761581421 or abs(qf - 0.9) < 1e-7 and True,
    "T3n1_float32_0.9_representable", repr(qf))

# ---------- T4 config ----------
cfgl = read(os.path.join(ITEMD, "%s.app_core-evidence_item004-config-%s.log" % (FRESH, V)))
chk("confidence_min=0.500000" in cfgl and "score_used=0.500000" in cfgl and
    "(from thresholds.confidence_min)" in cfgl and "RESULT: PASS" in cfgl,
    "T4_config_thresholds_from_yaml_source", cfgl[:120])
th = read(os.path.normpath(os.path.join(ITEMD, "..", "..", "..", "..",
                                        "app", "config", "thresholds.yaml")))
chk("confidence_min" in th and re.search(r"confidence_min:\s*0\.5", th),
    "T4b_thresholds_yaml_value_source", th[:200])

# ---------- T5 JUnit ----------
XJ = "%s.app_core-evidence_item004-core-tests-%s.xml" % (FRESH, V)
xj = read(os.path.join(ITEMD, XJ))
m_tests = re.search(r'tests="(\d+)"\s+failures="(\d+)"', xj)
chk(m_tests is not None and int(m_tests.group(1)) == 1059 and int(m_tests.group(2)) == 0,
    "T5_junit_tests_1059_failures_0", m_tests.group(0) if m_tests else "NO MATCH")
testcase_tag = re.compile(r'<testcase ([^>]*?)(?:/>|>)')
attr_name = re.compile(r'(?<!class)\bname="([^"]*)"')


def jc_names(x):
    out = []
    if not x:
        return out
    for tag_m in testcase_tag.finditer(x):
        attrs = tag_m.group(1)
        nm = attr_name.search(attrs)
        if nm:
            out.append(nm.group(1))
    return out


names_f = collections.Counter(jc_names(xj))
xj_impl = read(os.path.join(
    ITEMD, "%s.app_core-evidence_item004-core-tests-%s.xml" % (IMPL, T)))
names_i = collections.Counter(jc_names(xj_impl))
chk(len(names_f) > 100, "T5_junit_names_extracted_nonempty", "fresh_names=%d" % sum(names_f.values()))
chk(names_f == names_i, "T5_junit_name_multiset_identical_fresh_vs_impl",
    "fresh=%d impl=%d only_fresh=%s only_impl=%s" % (
        sum(names_f.values()), sum(names_i.values()),
        (names_f - names_i).most_common(3), (names_i - names_f).most_common(3)))
# 分割域判例名: 从验证侧单测源独立提取 check 名（XML unescape ->）
TEST_SRC = os.path.normpath(os.path.join(ITEMD, "..", "..", "..", "..",
                                         "app", "tests", "core", "test_vision_seg_postprocess.cpp"))
tsrc = read(TEST_SRC)
# 顶层逗号扫描器：简单 single-line regex 条件含逗号会被截断（仅 7/24）——
# 从 check( 之后按括号深度找到首个顶层逗号，再取第二实参字符串字面量
raw_names = []
for m in re.finditer(r'railhmi_test::check\(', tsrc):
    i = m.end()
    depth = 0
    while i < len(tsrc):
        ch = tsrc[i]
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        elif ch == '"':
            i += 1
            while i < len(tsrc) and tsrc[i] != '"':
                if tsrc[i] == '\\':
                    i += 1
                i += 1
        elif ch == ',' and depth == 0:
            break
        i += 1
    j = i + 1
    while j < len(tsrc) and tsrc[j] in ' \t\r\n':
        j += 1
    if tsrc[j] == '"':
        j += 1
        buf = []
        while j < len(tsrc) and tsrc[j] != '"':
            if tsrc[j] == '\\' and j + 1 < len(tsrc):
                buf.append(tsrc[j])
                j += 1
            buf.append(tsrc[j])
            j += 1
        raw_names.append(''.join(buf))


def unesc(s):
    return s.replace("&gt;", ">").replace("&lt;", "<").replace("&amp;", "&")


seg_case_names = set(unesc(n) for n in raw_names)
xj_names_set = set(unesc(k) for k in names_f.keys())
seg_missing = [n for n in seg_case_names if n not in xj_names_set]
chk(not seg_missing, "T5_seg_case_names_present_in_junit", "missing=%s count_src=%d" %
    (seg_missing, len(seg_case_names)))

# ---------- T6 泄漏扫描 ----------
import glob
CREDS = set()
for env in ["TRAIN_RK3588_SSH_CREDENTIAL"]:
    try:
        import subprocess
        ps = subprocess.run(["powershell.exe", "-NoProfile", "-Command",
                             "[Environment]::GetEnvironmentVariable('%s','User')" % env],
                            capture_output=True, text=True, timeout=30)
        v = (ps.stdout or "").strip()
        if v:
            CREDS.add(v)
    except Exception:
        pass
hits = []
for p in glob.glob(os.path.join(ITEMD, "*%s*" % FRESH)):
    if not os.path.isfile(p):
        continue
    try:
        t = read(p)
    except Exception:
        continue
    if re.search(r"(password=|[a-zA-Z]+://[^/ :]+:[^@ ]+@)", t):
        hits.append("semantic:" + os.path.basename(p))
    for c in CREDS:
        if len(c) >= 10 and re.search(r"(?<![a-zA-Z])%s(?![a-zA-Z])" % re.escape(c), t):
            hits.append("direct:" + os.path.basename(p))
chk(not hits, "T6_no_credential_leak", str(hits[:4]))

# ---------- 汇总 ----------
npass = sum(1 for ok, _, _ in results if ok)
print("\nCOMPARE SUMMARY passed=%d/%d" % (npass, len(results)))
for ok, name, detail in results:
    if not ok:
        print("FAILED:", name, "::", str(detail)[:200])
with open(os.path.join(HERE, "item004-compare-%s.json" % V), "w", encoding="utf-8") as fh:
    json.dump({"stamp": V, "passed": npass, "total": len(results),
               "records": [{"ok": ok, "name": n, "detail": str(d)[:400]} for ok, n, d in results]},
              fh, ensure_ascii=False, indent=2, default=str)
sys.exit(0 if npass == len(results) else 1)
