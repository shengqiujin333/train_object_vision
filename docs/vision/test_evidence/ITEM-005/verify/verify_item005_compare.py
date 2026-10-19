#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-005 独立解析比对（software_verification, VERIFY-20261019-item005-v1）。

不采信交付 acceptance/RESULT 行；全部判据独立重推导：
  T1  fresh transcript 退出码独立重断言（*_EXIT token，外层 shell 恒 0——ITEM-002 教训①）
      + keep-remote 无 cleanup + 指纹 + 39 下载（core-tests xml + selftest 2 + config 2 + 17 fixtures×2 = 38 + 1 额外下载 = 39；transcript/log 由 runner 本地写）
  T2  selftest 判例数从探针源 run_selftest 段 check( 调用数统计（不手抄）+ 判例名独立逐条
      + fresh vs impl r4 全文恒等
  T3  17 夹具 type=two_stage 行独立逐字段断言（值全部由验证侧从夹具几何/缓存参数独立推导，
      含掩码像素采样预言 rail_mask 几何独立重算）+ det 行 xywh/zone 独立断言
  T4  config 行独立重推导（danger_zone 从 app/config/roi.yaml 源读 CAM-01 = x0.08 y0.82 w0.84 h0.16）
  T5  core JUnit tests=1084 failures=0 + 判例名多重集恒等 fresh vs r4 +
      两阶段域判例名（从验证侧单测源 railhmi_test::check 名字独立提取）在场
  T6  全 fresh 产物凭据语义脱敏扫描
"""
import collections
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ITEMD = os.path.normpath(os.path.join(HERE, ".."))
IMPL = "vision-item005-192_168_0_103-20261019-item005-r4"
FRESH = "vision-item005-192_168_0_103-verify-20261019-item005-v1"
T = "20261019-item005-r4"
V = "verify-20261019-item005-v1"

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


def kvline(log, prefix):
    for ln in log.splitlines():
        if ln.startswith(prefix):
            return dict(tok.split("=", 1) for tok in ln[len(prefix):].split() if "=" in tok)
    return {}


def fixture_log(name, tag):
    p = os.path.join(ITEMD, "%s.app_core-evidence_item005-fixture-%s-%s.log"
                     % (tag, name, V if tag == FRESH else T))
    return read(p)


# ---------- T1 transcript ----------
FIXES = ["center_in", "center_out", "no_person", "seg_unavailable", "mask_empty",
         "detect_unavailable", "frame_mismatch", "danger_zone_hit", "danger_zone_miss",
         "stale", "cache_disabled", "cache_hit", "cache_expired", "cache_gen_change",
         "bounds", "multi", "mask_misaligned"]
tr = jread(os.path.join(ITEMD, FRESH + ".transcript.json"))
steps = collections.OrderedDict()
for s in tr["steps"]:
    steps.setdefault(s["step"], s)
exp = [("build-core", "BUILD_EXIT", 0), ("core-tests", "TEST_EXIT", 0)] + \
      [("fixture-" + fx, "FIX_" + fx.upper(), 0) for fx in FIXES] + \
      [("config", "CONFIG_EXIT", 0)]
for name, tok, e in exp:
    s = steps.get(name)
    out = (s.get("output") or "") if s else ""
    mtok = re.search(r"([A-Z_0-9]+)_EXIT=(\d+)", out)
    chk(s is not None and mtok is not None and int(mtok.group(2)) == e,
        "T1_%s_exit%d" % (name, e),
        "token=%s transcript_exit=%s" % (mtok.group(2) if mtok else None,
                                         s.get("exit") if s else "MISSING"))
s = steps.get("selftest")
sout = (s.get("output") or "") if s else ""
vals = [int(v) for v in re.findall(r"SELFTEST(?:_JSON)?_EXIT=(\d+)", sout)]
chk(s is not None and vals == [0, 0], "T1_selftest_exit_00",
    "tokens=%s transcript_exit=%s" % (vals, s.get("exit") if s else "MISSING"))
chk(all(s["step"] != "cleanup" for s in tr["steps"]), "T1_keep_remote_no_cleanup",
    "cleanup_steps=%d" % sum(1 for s in tr["steps"] if s["step"] == "cleanup"))
fp = tr.get("host_key_fingerprint") or ""
chk(fp.startswith("SHA256:") and "NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU" in fp,
    "T1_host_fingerprint_expected", fp)
up = steps.get("upload-app", {})
chk(re.search(r"uploaded \d+ files", (up.get("output") or "")) is not None,
    "T1_upload_app_reported", (up.get("output") or "")[:40])
dl = [s for s in tr["steps"] if s["step"] == "download"]
# 产物 = core-tests xml + selftest log/json + config log/json + 17 fixtures×(log+json) = 38 + transcript/log 由 runner 本地写
chk(len(dl) == 39 and all(s.get("exit") == 0 for s in dl), "T1_39_downloads_ok", str(len(dl)))

# ---------- T2 selftest ----------
F_SELF = "%s.app_core-evidence_item005-selftest-%s.log"
st = read(os.path.join(ITEMD, F_SELF % (FRESH, V)))
PROBE_SRC = os.path.normpath(os.path.join(ITEMD, "..", "..", "..", "..",
                                          "app", "src", "core", "tools", "two_stage_probe.cpp"))
probe_src = read(PROBE_SRC)
m_runself = re.search(r"int run_selftest\(\) \{(.*?)\n\}", probe_src, re.S)
self_body = m_runself.group(1) if m_runself else ""
n_self_calls = len(re.findall(r"^\s*check\((?!bool)", self_body, re.M))
pass_lines = re.findall(r"^\[PASS\] (.+)$", st, re.M)
names2 = [ln.split(" :: ")[0] for ln in pass_lines]
chk(len(pass_lines) == n_self_calls, "T2_selftest_pass_count_from_source",
    "board=%d source=%d" % (len(pass_lines), n_self_calls))
chk("RESULT: PASS failures=0" in st, "T2_selftest_result_line")
must_have = ["same_frame identical", "center_in alarm", "center_in detection semantics",
             "center_out no alarm", "no person -> NO_PERSON",
             "seg unavailable -> UNAVAILABLE (no safe conclusion)",
             "empty mask -> UNAVAILABLE", "detect unavailable -> UNAVAILABLE",
             "seg_frame != frame -> FRAME_MISMATCH", "detect_frame != frame -> FRAME_MISMATCH",
             "danger_zone hit", "danger_zone miss (on rail but outside zone)",
             "duplicate seq rejected", "out-of-order seq rejected", "stale generation rejected",
             "cache disabled -> no reuse", "cache enabled same generation -> reuse",
             "cache expired -> unavailable", "cache generation change rejected",
             "multi person aggregation", "out-of-bounds center dropped",
             "misaligned mask -> UNAVAILABLE"]
missing = [m for m in must_have if m not in names2]
chk(not missing, "T2_selftest_judgement_names_present", "missing=%s" % (missing,))
st3 = read(os.path.join(ITEMD, F_SELF % (IMPL, T)))
chk(st.strip() == st3.strip(), "T2_selftest_identical_to_impl_r4")

# ---------- T3 fixtures 独立判定（值由验证侧几何/参数独立推导） ----------
# 夹具几何（探针源 rail_mask(1280,720, 0.0,0.6,1.0,1.0)）：
#   band = y∈[432,719] 全宽（py0=int(0.6*720)=432, py1=int(1.0*720)clamp=719）
# 掩码像素采样预言独立重算（与实现 mask_positive_at 同一归一化 floor 语义）：
IW, IH = 1280, 720


def mask_hit_pred(cx, cy, y0=0.6, y1=1.0, w=IW, h=IH):
    px = min(max(int(cx * w), 0), w - 1)
    py = min(max(int(cy * h), 0), h - 1)
    py0 = min(max(int(y0 * h), 0), h - 1)
    py1 = min(max(int(y1 * h), 0), h - 1)
    return py0 <= py <= py1  # 全宽带：任意 x 命中


def center(x, y, w_, h_):
    return x + w_ / 2.0, y + h_ / 2.0


# 逐夹具独立预言（persons 由探针源 fixture 段读取；期望值验证侧重推导）
# center_in: person(0.45,0.75,0.1,0.1) 中心(0.5,0.8) 在带内
ci = kvline(fixture_log("center_in", FRESH), "type=two_stage ")
cx, cy = center(0.45, 0.75, 0.1, 0.1)
chk(mask_hit_pred(cx, cy), "T3a_center_in_mask_pixel_pred", "(%r,%r)" % (cx, cy))
chk(ci.get("ok") == "1" and ci.get("available") == "1" and ci.get("reason") == "TWO_STAGE_OK"
    and ci.get("persons") == "1" and ci.get("mask_hit") == "1" and ci.get("alarms") == "1"
    and ci.get("dropped_no_mask") == "0" and ci.get("used_cache") == "0",
    "T3b_center_in_row", str(ci))
d0 = re.search(r"^det\[0\] xywh=\(([-\d.]+),([-\d.]+),([-\d.]+),([-\d.]+)\) score=([\d.]+) "
               r"mask_hit=(\d) zone=(\d) alarm=(\d)$", fixture_log("center_in", FRESH), re.M)
chk(d0 is not None and abs(float(d0.group(1)) - 0.45) < 1e-4 and abs(float(d0.group(2)) - 0.75) < 1e-4
    and d0.group(6) == "1" and d0.group(8) == "1",
    "T3c_center_in_det_line", d0.group(0) if d0 else "MISSING")

# center_out: person(0.45,0.15,0.1,0.1) 中心(0.5,0.2) 在带外
co = kvline(fixture_log("center_out", FRESH), "type=two_stage ")
cx2, cy2 = center(0.45, 0.15, 0.1, 0.1)
chk(not mask_hit_pred(cx2, cy2), "T3d_center_out_mask_pixel_pred", "(%r,%r)" % (cx2, cy2))
chk(co.get("ok") == "1" and co.get("available") == "1" and co.get("reason") == "TWO_STAGE_NO_ALARM"
    and co.get("mask_hit") == "0" and co.get("dropped_no_mask") == "1" and co.get("alarms") == "0",
    "T3e_center_out_row", str(co))

# no_person
np_ = kvline(fixture_log("no_person", FRESH), "type=two_stage ")
chk(np_.get("available") == "1" and np_.get("persons") == "0" and np_.get("alarms") == "0"
    and np_.get("reason") == "TWO_STAGE_NO_PERSON", "T3f_no_person_row", str(np_))

# 不可用类（reason=TWO_STAGE_UNAVAILABLE）：seg_unavailable/mask_empty/detect_unavailable/
# mask_misaligned；frame_mismatch 有独立 reason（T3i），不并入本组（预期先行自查②）
for fxn in ["seg_unavailable", "mask_empty", "detect_unavailable", "mask_misaligned"]:
    lg = fixture_log(fxn, FRESH)
    row = kvline(lg, "type=two_stage ")
    chk(row.get("ok") == "0" and row.get("available") == "0"
        and row.get("reason") == "TWO_STAGE_UNAVAILABLE"
        and row.get("alarms") == "0", "T3g_%s_unavailable_row" % fxn, str(row))

# frame_mismatch 行特有 reason
fm = kvline(fixture_log("frame_mismatch", FRESH), "type=two_stage ")
chk(fm.get("reason") == "TWO_STAGE_FRAME_MISMATCH", "T3i_frame_mismatch_reason", str(fm))

# danger_zone_hit: person(0.45,0.85,0.1,0.1) 中心(0.5,0.9)；zone x0.08 y0.82 w0.84 h0.16
dh = kvline(fixture_log("danger_zone_hit", FRESH), "type=two_stage ")
cx3, cy3 = center(0.45, 0.85, 0.1, 0.1)
in_zone = 0.08 <= cx3 <= 0.08 + 0.84 and 0.82 <= cy3 <= 0.82 + 0.16
chk(in_zone and mask_hit_pred(cx3, cy3), "T3j_dz_hit_zone_pred", "(%r,%r)" % (cx3, cy3))
chk(dh.get("mask_hit") == "1" and dh.get("alarms") == "1" and dh.get("dropped_zone") == "0"
    and dh.get("reason") == "TWO_STAGE_OK", "T3k_dz_hit_row", str(dh))
d0h = re.search(r"^det\[0\] .*mask_hit=1 zone=1 alarm=1$", fixture_log("danger_zone_hit", FRESH), re.M)
chk(d0h is not None, "T3l_dz_hit_det_line", d0h.group(0) if d0h else "MISSING")

# danger_zone_miss: person(0.45,0.65,0.1,0.1) 中心(0.5,0.7) 在轨道带内但 zone 外
dm = kvline(fixture_log("danger_zone_miss", FRESH), "type=two_stage ")
cx4, cy4 = center(0.45, 0.65, 0.1, 0.1)
in_zone2 = 0.08 <= cx4 <= 0.08 + 0.84 and 0.82 <= cy4 <= 0.82 + 0.16
chk(mask_hit_pred(cx4, cy4) and not in_zone2, "T3m_dz_miss_zone_pred", "(%r,%r)" % (cx4, cy4))
chk(dm.get("mask_hit") == "1" and dm.get("dropped_zone") == "1" and dm.get("alarms") == "0"
    and dm.get("reason") == "TWO_STAGE_NO_ALARM", "T3n_dz_miss_row", str(dm))
d0m = re.search(r"^det\[0\] .*mask_hit=1 zone=0 alarm=0$", fixture_log("danger_zone_miss", FRESH), re.M)
chk(d0m is not None, "T3o_dz_miss_det_line", d0m.group(0) if d0m else "MISSING")

# stale: 重复 seq（两帧同 seq=20）→ FrameGate DuplicateSeq
slg = fixture_log("stale", FRESH)
srow = kvline(slg, "type=two_stage ")
chk(srow.get("ok") == "0" and srow.get("available") == "0"
    and srow.get("reason") == "TWO_STAGE_STALE", "T3p_stale_row", str(srow))

# cache_disabled: 默认策略下 seg 不可用 → UNAVAILABLE used_cache=0
cd = kvline(fixture_log("cache_disabled", FRESH), "type=two_stage ")
chk(cd.get("available") == "0" and cd.get("used_cache") == "0"
    and cd.get("reason") == "TWO_STAGE_UNAVAILABLE", "T3q_cache_disabled_row", str(cd))

# cache_hit: 启用 max_age_ms=500，帧1 store（t=0），帧2 seg 不可用（t=100000us=100ms）
# → 同 generation 复用 used_cache=1 cache_age_ms=100 alarms=1（掩码来自缓存）
ch = kvline(fixture_log("cache_hit", FRESH), "type=two_stage ")
chk((100000 - 0) // 1000 == 100, "T3r_cache_age_indep_arith", "100ms")
chk(ch.get("available") == "1" and ch.get("used_cache") == "1" and ch.get("cache_age_ms") == "100"
    and ch.get("alarms") == "1" and ch.get("mask_hit") == "1"
    and ch.get("reason") == "TWO_STAGE_OK", "T3s_cache_hit_row", str(ch))

# cache_expired: t=900000us=900ms > 500ms → 不复用
ce = kvline(fixture_log("cache_expired", FRESH), "type=two_stage ")
chk((900000 - 0) // 1000 > 500, "T3t_cache_expired_indep_arith", "900ms>500ms")
chk(ce.get("available") == "0" and ce.get("used_cache") == "0"
    and ce.get("reason") == "TWO_STAGE_UNAVAILABLE", "T3u_cache_expired_row", str(ce))

# cache_gen_change: generation 2 请求（缓存 gen 1）→ 拒绝复用
cg = kvline(fixture_log("cache_gen_change", FRESH), "type=two_stage ")
chk(cg.get("available") == "0" and cg.get("used_cache") == "0"
    and cg.get("reason") == "TWO_STAGE_UNAVAILABLE", "T3v_cache_gen_change_row", str(cg))

# bounds: person(1.02,...) 中心 1.07>1 → dropped_bounds
bd = kvline(fixture_log("bounds", FRESH), "type=two_stage ")
cx5 = 1.02 + 0.1 / 2.0
chk(cx5 > 1.0, "T3w_bounds_center_pred", "%r>1" % cx5)
chk(bd.get("dropped_bounds") == "1" and bd.get("alarms") == "0" and bd.get("mask_hit") == "0"
    and bd.get("available") == "1", "T3x_bounds_row", str(bd))

# multi: 3 人（中心 y=0.8 命中 / y=0.9 命中 / y=0.2 未命中）→ mask_hit=2 dropped_no_mask=1 alarms=2
mlg = fixture_log("multi", FRESH)
mu = kvline(mlg, "type=two_stage ")
chk(mask_hit_pred(0.5, 0.8) and mask_hit_pred(0.5, 0.9) and not mask_hit_pred(0.5, 0.2),
    "T3y_multi_pixel_preds", "3 centers")
chk(mu.get("persons") == "3" and mu.get("mask_hit") == "2" and mu.get("dropped_no_mask") == "1"
    and mu.get("alarms") == "2" and mu.get("available") == "1", "T3z_multi_row", str(mu))
dets = re.findall(r"^det\[\d+\] xywh=\(([-\d.]+),([-\d.]+),([-\d.]+),([-\d.]+)\) score=([\d.]+) "
                  r"mask_hit=(\d) zone=(\d) alarm=(\d)$", mlg, re.M)
chk(len(dets) == 3 and [d[5] for d in dets] == ["1", "1", "0"] and
    [d[7] for d in dets] == ["1", "1", "0"], "T3z2_multi_det_lines", str(dets))

# 每个 fixture 的 RESULT 行（探针自身退出判据；判据独立断言已覆盖值域）
for fx in FIXES:
    lg = fixture_log(fx, FRESH)
    chk(lg.strip().endswith("RESULT: PASS"), "T3_result_pass_%s" % fx)

# ---------- T4 config ----------
cfgl = read(os.path.join(ITEMD, "%s.app_core-evidence_item005-config-%s.log" % (FRESH, V)))
chk("danger_zone=found x=0.080000 y=0.820000 w=0.840000 h=0.160000" in cfgl,
    "T4_config_danger_zone_from_roi_yaml", cfgl[:150])
roiy = read(os.path.normpath(os.path.join(ITEMD, "..", "..", "..", "..",
                                          "app", "config", "roi.yaml")))
mz = re.search(r"camera:\s*CAM-01.*?name:\s*danger_zone.*?x:\s*([\d.]+).*?y:\s*([\d.]+)"
               r".*?w:\s*([\d.]+).*?h:\s*([\d.]+)", roiy, re.S)
chk(mz is not None and mz.group(1) == "0.08" and mz.group(2) == "0.82" and
    mz.group(3) == "0.84" and mz.group(4) == "0.16", "T4b_roi_yaml_source_values",
    mz.group(0)[:80] if mz else "NO MATCH")
chk("danger_zone=1" in cfgl and "mask_hit=1" in cfgl and "RESULT: PASS" in cfgl,
    "T4c_config_run_zone_path", "")

# ---------- T5 JUnit ----------
XJ = "%s.app_core-evidence_item005-core-tests-%s.xml" % (FRESH, V)
xj = read(os.path.join(ITEMD, XJ))
m_tests = re.search(r'tests="(\d+)"\s+failures="(\d+)"', xj)
chk(m_tests is not None and int(m_tests.group(1)) == 1084 and int(m_tests.group(2)) == 0,
    "T5_junit_tests_1084_failures_0", m_tests.group(0) if m_tests else "NO MATCH")
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
    ITEMD, "%s.app_core-evidence_item005-core-tests-%s.xml" % (IMPL, T)))
names_i = collections.Counter(jc_names(xj_impl))
chk(len(names_f) > 100, "T5_junit_names_extracted_nonempty", "fresh_names=%d" % sum(names_f.values()))
chk(names_f == names_i, "T5_junit_name_multiset_identical_fresh_vs_impl",
    "fresh=%d impl=%d only_fresh=%s only_impl=%s" % (
        sum(names_f.values()), sum(names_i.values()),
        (names_f - names_i).most_common(3), (names_i - names_f).most_common(3)))
# 两阶段域判例名: 从验证侧单测源独立提取 check 名（XML unescape ->）
TEST_SRC = os.path.normpath(os.path.join(ITEMD, "..", "..", "..", "..",
                                         "app", "tests", "core", "test_vision_two_stage.cpp"))
tsrc = read(TEST_SRC)
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


ts_case_names = set(unesc(n) for n in raw_names)
xj_names_set = set(unesc(k) for k in names_f.keys())
ts_missing = [n for n in ts_case_names if n not in xj_names_set]
chk(not ts_missing, "T5_twostage_case_names_present_in_junit", "missing=%s count_src=%d" %
    (ts_missing, len(ts_case_names)))

# ---------- T6 泄漏扫描 ----------
import glob
CREDS = set()
for env in ["TRAIN_RK3588_SSH_CREDENTIAL"]:
    try:
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
with open(os.path.join(HERE, "item005-compare-%s.json" % V), "w", encoding="utf-8") as fh:
    json.dump({"stamp": V, "passed": npass, "total": len(results),
               "records": [{"ok": ok, "name": n, "detail": str(d)[:400]} for ok, n, d in results]},
              fh, ensure_ascii=False, indent=2, default=str)
sys.exit(0 if npass == len(results) else 1)
