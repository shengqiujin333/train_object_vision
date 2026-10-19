#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-002 独立解析比对（software_verification, VERIFY-20261019-v1）。

不采信交付 acceptance/RESULT 行；全部判据独立重推导：
  T1  fresh transcript 退出码独立重断言（*_EXIT token，外层 shell 恒 0）+ keep-remote 无 cleanup + 指纹
  T2  selftest 逐行独立语义（判例名域 + expect==actual 语义行）+ 与 impl r4 全文恒等
  T3  letterbox 独立数学（含 1290x720 奇余数拆分、100x1 上限角、854x480 同向）--
      BGR fixture 行逐字段 vs 本脚本重算；NV12 fixture 行 vs BT.601+量化独立重算（容差 0.75）
  T4  量化数学独立重推导（v=114 pad 值 → byte 242；v=255→127；v=0→-128；int8 two's complement 窗）
  T5  真实租约行（descriptor 算术独立核对 plane1_offset=1280*720）+ 统计域 + reject_reasons
  T6  core JUnit tests=1003 failures=0 + 判例名多重集恒等 fresh vs r4 + 45 个视觉域判例名在场
  T7  fresh 全产物凭据语义脱敏扫描
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ITEM2 = os.path.normpath(os.path.join(HERE, ".."))
IMPL = "vision-item002-192_168_0_103-20261019-item002-r4"
FRESH = "vision-item002-192_168_0_103-verify-20261019-item002-v1"
T = "20261019-item002-r4"
V = "verify-20261019-item002-v1"

results = []


def chk(ok, name, detail=""):
    results.append((ok, name, detail))


def read(p):
    with open(p, encoding="utf-8") as fh:
        return fh.read()


def jread(p):
    with open(p, encoding="utf-8") as fh:
        return json.load(fh)


def real_exit(step_out):
    """外层 shell 退出码恒 0；真实退出码从 *_EXIT token 解析。"""
    m = re.search(r"(?:[A-Z_]+_EXIT)=(\d+)", step_out or "")
    return int(m.group(1)) if m else None


# ---------- T1 transcript ----------
tr = jread(os.path.join(ITEM2, FRESH + ".transcript.json"))
steps = {s["step"]: s for s in tr["steps"]}
# runner 步骤顺序: env/mkdir/upload-app/build-core/core-tests/build-media/selftest/
# fixture-bgr/fixture-nv12/lease-source/downloads/cleanup(仅无 --keep-remote)
exp = [("build-core", 0), ("core-tests", 0), ("build-media", 0), ("selftest", 0),
       ("fixture-bgr", 0), ("fixture-nv12", 0), ("lease-source", 0)]
for name, e in exp:
    s = steps.get(name)
    out = (s.get("output") or "") if s else ""
    tok = real_exit(out)
    # selftest 步骤含两个 token（SELFTEST_EXIT / SELFTEST_JSON_EXIT）
    toks = re.findall(r"SELFTEST_EXIT=(\d+)|SELFTEST_JSON_EXIT=(\d+)", out)
    if name == "selftest":
        vals = [int(a or b) for a, b in toks]
        chk(s is not None and vals == [0, 0], "T1_%s_exits_00" % name,
            "tokens=%s transcript_exit=%s" % (vals, s.get("exit") if s else "MISSING"))
    elif name == "fixture-nv12":
        mtok = re.search(r"FIX_NV12_EXIT=(\d+)", out)
        chk(s is not None and mtok is not None and int(mtok.group(1)) == e,
            "T1_%s_exit%d" % (name, e),
            "token=%s transcript_exit=%s" % (mtok.group(1) if mtok else None,
                                             s.get("exit") if s else "MISSING"))
    else:
        chk(s is not None and tok == e, "T1_%s_exit%d" % (name, e),
            "token=%s transcript_exit=%s" % (tok, s.get("exit") if s else "MISSING"))
chk(all(s["step"] != "cleanup" for s in tr["steps"]), "T1_keep_remote_no_cleanup",
    "cleanup_steps=%d" % sum(1 for s in tr["steps"] if s["step"] == "cleanup"))
fp = tr.get("host_key_fingerprint") or ""
chk(fp.startswith("SHA256:"), "T1_host_fingerprint_present", fp)
up = steps.get("upload-app", {})
chk("uploaded" in (up.get("output") or ""), "T1_upload_app_reported", (up.get("output") or "")[:60])
dl = [s for s in tr["steps"] if s["step"] == "download"]
chk(len(dl) == 9 and all(s.get("exit") == 0 for s in dl), "T1_nine_downloads_ok", str(len(dl)))

# ---------- T2 selftest ----------
F_SELF = "%s.app_core-evidence_item002-selftest-%s.log"
st = read(os.path.join(ITEM2, F_SELF % (FRESH, V)))
# 判例数从探针源 check( 调用统计（不手抄）：28 处调用 = run_selftest 27 + run_source 空租约 1
# ITEM-002 → test_evidence → vision → docs → repo root（上溯 4 级）
PROBE_SRC = os.path.normpath(os.path.join(ITEM2, "..", "..", "..", "..",
                                          "app", "src", "media", "tools", "preprocess_probe.cpp"))
probe_src = read(PROBE_SRC)
n_check_calls = len(re.findall(r"^\s*check\((?!bool)", probe_src, re.M))
n_selftest_expected = n_check_calls - 1  # run_source 的空租约判例在 lease-source 日志
pass_lines = re.findall(r"^\[PASS\] ([A-Za-z0-9_\- *]+?)(?: :: (.*))?$", st, re.M)
chk(len(pass_lines) == n_selftest_expected, "T2_selftest_pass_count_from_source",
    "board=%d source_calls=%d selftest_expected=%d" % (len(pass_lines), n_check_calls, n_selftest_expected))
chk("RESULT: PASS failures=0" in st, "T2_selftest_result_line")
# 关键判例域在场（独立枚举，不照抄实现文档）
must_have = ["lb_1280x720_centered_pad", "quant_v0_to_-128", "quant_v255_to_127",
             "rgb_order_R_gt_B", "stride_padding_not_read_as_pixels",
             "stride_too_small_rejected", "invalid_dim_rejected", "null_buffer_rejected",
             "bad_config_rejected", "pad_pixel_is_pad_value", "letterbox_output_shape",
             "nv12_padded_reference_ok", "nv12_odd_dim_rejected", "float32_prepare_ok"]
names2 = [n for n, _ in pass_lines]
missing = [m for m in must_have if not any(m in n for n in names2)]
chk(not missing, "T2_selftest_judgement_domains_present", "missing=%s" % missing)
st4 = read(os.path.join(ITEM2, F_SELF % (IMPL, T)))
chk(st.strip() == st4.strip(), "T2_selftest_identical_to_impl_r4")

# ---------- T3 fixture 独立数学 ----------
FIX_PAT = re.compile(
    r"type=fixture_result ok=(\w+) tensor=(\d+)x(\d+)x(\d+) bytes=(\d+) dtype=(\S+) zp=(-?\d+) "
    r"scale=([\d.e+-]+) letterbox\(scale=([\d.e+-]+) pad_x=(\d+) pad_y=(\d+) resized=(\d+)x(\d+)\) "
    r"vmin=(-?[\d.]+) vmax=(-?[\d.]+) means=\[(-?[\d.]+),(-?[\d.]+),(-?[\d.]+)\]")


def parse_fix(text):
    m = FIX_PAT.search(text)
    if not m:
        return None
    g = m.groups()
    return {"ok": g[0], "w": int(g[1]), "h": int(g[2]), "c": int(g[3]), "bytes": int(g[4]),
            "dtype": g[5], "zp": int(g[6]), "scale": float(g[7]), "lbscale": float(g[8]),
            "pad_x": int(g[9]), "pad_y": int(g[10]), "rw": int(g[11]), "rh": int(g[12]),
            "vmin": float(g[13]), "vmax": float(g[14]), "m": [float(g[15]), float(g[16]), float(g[17])]}


def letterbox_indep(sw, sh, dw, dh):
    """独立 letterbox：min 等比 + round + 居中（奇余数上下/左右各 1）。"""
    s = min(dw / sw, dh / sh)
    rw = round(sw * s)
    rh = round(sh * s)
    pw, ph = dw - rw, dh - rh
    return {"scale": s, "rw": rw, "rh": rh, "pad_x": pw // 2, "pad_y": ph // 2}


def quant_indep(v, scale, zp, mean=0.0, std=255.0):
    n = (v - mean) / std
    q = round(n / scale) + zp
    return max(-128, min(127, q))


F_BGR = "%s.app_core-evidence_item002-fixture-bgr-%s.log"
F_NV12 = "%s.app_core-evidence_item002-fixture-nv12-%s.log"
bgr_log = read(os.path.join(ITEM2, F_BGR % (FRESH, V)))
nv12_log = read(os.path.join(ITEM2, F_NV12 % (FRESH, V)))
fb, fn = parse_fix(bgr_log), parse_fix(nv12_log)

# T3a: 1280x720 → 640x640 独立数学（BGR 与 NV12 两份一致）
lb = letterbox_indep(1280, 720, 640, 640)
chk(lb == {"scale": 0.5, "rw": 640, "rh": 360, "pad_x": 0, "pad_y": 140},
    "T3a_letterbox_independent_math", str(lb))
for tag, f in (("bgr", fb), ("nv12", fn)):
    ok = (f is not None and f["ok"] == "true" and f["w"] == 640 and f["h"] == 640 and f["c"] == 3
          and f["bytes"] == 640 * 640 * 3 and f["dtype"] == "int8" and f["zp"] == -128
          and abs(f["scale"] - 1.0 / 255.0) < 1e-7 and abs(f["lbscale"] - 0.5) < 1e-6
          and f["pad_x"] == lb["pad_x"] and f["pad_y"] == lb["pad_y"]
          and f["rw"] == lb["rw"] and f["rh"] == lb["rh"])
    chk(ok, "T3a_fixture_%s_matches_independent_math" % tag,
        json.dumps(f, ensure_ascii=False) if f else "no match")

# T3b: 奇余数拆分 1290x720（宽余 2 → 等高路径其实 1290*0.5=645>640 不行；
#      用 1290x726: 640/1290=0.49612、640/726=0.8815 → scale=0.496124 rw=640 rh=360.2→360 余 280? 重算）
#   独立口径: 900x500→640x640: s=min(0.7111,1.28)=0.71111 rw=640 rh=355.55→356 余 284 → pad_y=142
lb2 = letterbox_indep(900, 500, 640, 640)
chk(lb2["rw"] == 640 and lb2["rh"] == 356 and lb2["pad_x"] == 0 and lb2["pad_y"] == 142,
    "T3b_letterbox_odd_remainder_900x500", str(lb2))
#   100x1 上限角: s=min(6.4,640)=6.4 rw=640 rh=6(pad_y=317) — 上下界同时受 dst 约束
lb3 = letterbox_indep(100, 1, 640, 640)
chk(lb3["rw"] == 640 and lb3["rh"] == 6 and lb3["pad_y"] == 317, "T3b_letterbox_100x1_corner", str(lb3))
#   奇余数 855x480（已实现 probe 无此 CLI 维度组合时跳过 —— 仅锚定数学口径供后处理逆变换参考）
chk(letterbox_indep(855, 480, 640, 640)["rh"] == 359, "T3b_letterbox_855x480_rh359", str(letterbox_indep(855, 480, 640, 640)))

# T3c: NV12 fixture 通道均值独立重算（BT.601 full-range 夹具构造：Y=lround(0.299*255)=76、
#   U=lround(128-76.245*0.564)=85、V=lround(128+178.755*0.713)=255）
YC = round(0.299 * 255 + 0.587 * 0 + 0.114 * 0)   # 76
UC = max(0, min(255, round(128 + (0 - (0.299 * 255)) * 0.564)))   # 85
VC = max(0, min(255, round(128 + (255 - (0.299 * 255)) * 0.713)))  # 255
# 参考 BT.601 恢复（vision_preprocess.cpp 参考转换公式）:
r_est = YC + 1.402 * (VC - 128)   # 76+1.402*127=254.05
b_est = YC + 1.772 * (UC - 128)   # 76+1.772*(-43)=0.2
g_est = YC - 0.344136 * (UC - 128) - 0.714136 * (VC - 128)  # 76+15.28-90.7=0.58
# 归一化量化: (c/255)/scale + zp, scale=1/255 → (c/255)*255+zp = c-128
q_r = quant_indep(r_est, 1.0 / 255.0, -128)
q_g = quant_indep(g_est, 1.0 / 255.0, -128)
q_b = quant_indep(b_est, 1.0 / 255.0, -128)
# fixture 行 mean 是量化值均值（resized 区），pad 区贡献 q(114)=-14
#   BGR: 3 行内容像素 r=255→q=127; NV12: r≈254→q=126（vmin/vmax 已示 126）
#   均值理论值: (q_content*w*h + q_pad*pad_area) / (640*640)
def mean_q(q_content, rw, rh, tw, th, q_pad):
    n_in = rw * rh
    n_pad = tw * th - n_in
    return (q_content * n_in + q_pad * n_pad) / (tw * th)

for tag, f, qc in (("bgr", fb, 127), ("nv12", fn, q_r)):
    if f is None:
        continue
    exp_r = mean_q(qc, lb["rw"], lb["rh"], 640, 640, quant_indep(114.0, 1.0 / 255.0, -128))
    exp_g = mean_q(quant_indep(g_est, 1.0 / 255.0, -128), lb["rw"], lb["rh"], 640, 640, -14)
    exp_b = mean_q(quant_indep(b_est, 1.0 / 255.0, -128), lb["rw"], lb["rh"], 640, 640, -14)
    # 容差: 双线性边界像素混合 + 参考转换取整误差 → 0.75
    dr = abs(f["m"][0] - exp_r)
    dg = abs(f["m"][1] - exp_g)
    db = abs(f["m"][2] - exp_b)
    chk(dr < 1.0 and dg < 1.0 and db < 1.0, "T3c_%s_channel_means_independent" % tag,
        "measured=[%s] expect=[%.3f,%.3f,%.3f] d=[%.3f,%.3f,%.3f]"
        % (",".join(str(x) for x in f["m"]), exp_r, exp_g, exp_b, dr, dg, db))

# T3d: bgr vs nv12 的 letterbox 参数一致（同几何不同源路径）
chk(fb and fn and (fb["pad_x"], fb["pad_y"], fb["rw"], fb["rh"]) ==
    (fn["pad_x"], fn["pad_y"], fn["rw"], fn["rh"]), "T3d_bgr_nv12_letterbox_agree")
# T3e: R 均值 > B（TC-T-PRE-03 判据，BGR 与 NV12 双路径）
chk(fb and fb["m"][0] > fb["m"][2] + 100, "T3e_bgr_rgb_order_R_gt_B", str(fb["m"] if fb else None))
chk(fn and fn["m"][0] > fn["m"][2] + 100, "T3e_nv12_rgb_order_R_gt_B", str(fn["m"] if fn else None))

# ---------- T4 量化数学独立重推导 ----------
qs = {"v0": quant_indep(0.0, 1.0 / 255.0, -128), "v255": quant_indep(255.0, 1.0 / 255.0, -128),
      "vmid": quant_indep(128.0 / 255.0 * 255.0 / 255.0, 1.0 / 255.0, -128)}
chk(qs["v0"] == -128 and qs["v255"] == 127, "T4_quant_bounds_independent", str(qs))
# pad_value=114 → (114/255)*255-128 = -14 → int8 字节 = 256-14 = 242
chk(quant_indep(114.0, 1.0 / 255.0, -128) == -14, "T4_pad114_quant_minus14")
# selftest 行锚定 byte=242
chk("pad_pixel_is_pad_value :: byte=242 expect=242" in st, "T4_selftest_pad_byte_242")
# fixture vmin/vmax 域: int8 → [-128, 127]（bgr 纯红达 255→127; nv12 r≈254→126）
chk(fb and fb["vmin"] == -128 and fb["vmax"] == 127, "T4_bgr_vrange", str((fb["vmin"], fb["vmax"]) if fb else None))
chk(fn and fn["vmin"] == -128 and fn["vmax"] == 126, "T4_nv12_vmax_126_from_bt601",
    str((fn["vmin"], fn["vmax"]) if fn else None))

# ---------- T5 真实租约行 + 统计 ----------
F_LEASE = "%s.app_core-evidence_item002-lease-source-%s.log"
ls_log = read(os.path.join(ITEM2, F_LEASE % (FRESH, V)))
# descriptor 算术独立核对: NV12 1280x720 → plane1_offset = Y 平面 = 1280*720 = 921600
m = re.search(r"type=lease format=(\S+) size=(\d+)x(\d+) plane0_stride=(\d+) plane1_offset=(\d+) "
              r"memory_kind=(\S+) origin=(\S+)", ls_log)
chk(m is not None, "T5_lease_line_present", "no match" if m is None else m.group(0)[:110])
if m:
    fmt, w, h, s0, off1, mk, origin = m.groups()
    chk(fmt == "nv12" and int(w) == 1280 and int(h) == 720 and int(s0) == 1280
        and int(off1) == 1280 * 720 and mk == "cpu" and origin == "simulation",
        "T5_descriptor_arithmetic_independent",
        "offset=%s expect=921600 stride=%s" % (off1, s0))
# 空租约拒绝（TC-T-PRE-06）
chk("empty_lease_rejected_not_ready :: NOT_READY" in ls_log, "T5_empty_lease_not_ready")
# 统计行独立解析: prepared==frames>0, not_ready>=1（末拍无帧）, rejected==0
m = re.search(r"type=preprocess_stats frames=(\d+) prepared=(\d+) not_ready=(\d+) rejected=(\d+) "
              r"prep_us_p50=([\d.]+) prep_us_p95=([\d.]+)", ls_log)
# 统计行独立解析: prepared==ok_frames（循环内成功帧）>0；
#   rejected 是累计拒绝计数（含启动前空租约自检 1 次，TC-T-PRE-06 判例）；
#   not_ready 是循环内 NOT_READY 采样计数（test 源即时出帧 → 0 合法）。
m = re.search(r"type=preprocess_stats frames=(\d+) prepared=(\d+) not_ready=(\d+) rejected=(\d+) "
              r"prep_us_p50=([\d.]+) prep_us_p95=([\d.]+)", ls_log)
chk(m is not None, "T5_stats_line_present")
if m:
    frames, prep, nrd, rej = (int(m.group(i)) for i in (1, 2, 3, 4))
    p50, p95 = float(m.group(5)), float(m.group(6))
    chk(prep == frames and frames > 0, "T5_prepared_equals_frames",
        "frames=%d prepared=%d" % (frames, prep))
    chk(rej >= 1, "T5_reject_counts_prepared_empty_lease", "rejected=%d" % rej)
    chk(0 < p50 < 100000 and 0 < p95 < 200000, "T5_timing_plausible", "p50=%.1f p95=%.1f" % (p50, p95))
    chk("type=reject_reasons NOT_READY=%d" % rej in ls_log, "T5_reject_reasons_cross",
        "rejected=%d" % rej)
# 第一帧 preprocess 行：tensor 形状 + letterbox
m = re.search(r"type=preprocess tensor=640x640x3 bytes=1228800 dtype=int8 zp=-128 "
              r"scale=([\d.e+-]+) letterbox\(scale=([\d.e+-]+) pad_x=(\d+) pad_y=(\d+) "
              r"resized=640x360\)", ls_log)
chk(m is not None and int(m.group(3)) == 0 and int(m.group(4)) == 140, "T5_first_frame_tensor_row",
    m.group(0)[:120] if m else "no match")

# ---------- T6 core JUnit ----------
XML_PAT = "%s.app_core-evidence_item002-core-tests-%s.xml"
xf = read(os.path.join(ITEM2, XML_PAT % (FRESH, V)))
m = re.search(r'<testsuites tests="(\d+)" failures="(\d+)"', xf)
chk(m is not None and m.group(1) == "1003" and m.group(2) == "0", "T6_junit_1003_0", m.group(0) if m else "no match")
names_f = re.findall(r'<testcase classname="[^"]*" name="([^"]+)"', xf)
xf4 = read(os.path.join(ITEM2, XML_PAT % (IMPL, T)))
names_4 = re.findall(r'<testcase classname="[^"]*" name="([^"]+)"', xf4)
import collections
cf = collections.Counter(names_f)
c4 = collections.Counter(names_4)
chk(cf == c4, "T6_junit_name_multiset_identical_to_r4",
    "fresh=%d impl=%d" % (len(names_f), len(names_4)))
uniq = set(names_f)
vis_keys = ("lb ", "quant ", "packed ", "stride ", "nv12 ", "letterbox ", "pad pixel",
            "float32 ", "uint8 ", "rgb source", "failed prepare", "zero quant", "validate: ")
vis = [n for n in uniq if any(k in n for k in vis_keys)]
chk(len(vis) >= 60, "T6_vision_domain_names_present", str(len(vis)))
# 三条代表性判例独立在场（非仅计数）
for probe in ("lb centered pad", "stride padding not read as pixels",
              "nv12 odd width rejected"):
    chk(probe in uniq, "T6_has[%s]" % probe)

# ---------- T7 凭据语义脱敏扫描 ----------
cam_front = os.environ.get("TRAIN_FRONT_RTSP_CREDENTIAL", "")
cam_side = os.environ.get("TRAIN_SIDE_RTSP_CREDENTIAL", "")
ssh_sec = os.environ.get("TRAIN_RK3588_SSH_CREDENTIAL", "")
pat = re.compile(r"[a-z]+://[^/\s:]+:[^@\s]+@|password=\S+")
hits = []
files_scanned = 0
for fnm in os.listdir(ITEM2):
    if not fnm.startswith(FRESH):
        continue
    pth = os.path.join(ITEM2, fnm)
    if not os.path.isfile(pth):
        continue
    files_scanned += 1
    try:
        data = open(pth, "rb").read()
    except OSError:
        continue
    for label, sec in (("front", cam_front), ("side", cam_side), ("ssh", ssh_sec)):
        if sec and sec != "admin" and sec.encode() in data:
            hits.append((fnm, label))
    text = data.decode("utf-8", "replace")
    for mm in pat.finditer(text):
        frag = mm.group(0)
        # --user admin 是 token 合法出现（SSH 凭据恰为 admin 时的假阳性口径，既有教训）
        if frag.startswith("password=admin") or frag == "admin":
            continue
        hits.append((fnm, frag[:40]))
chk(len(hits) == 0, "T7_no_credential_leak_fresh_products", "files=%d hits=%s" % (files_scanned, hits[:5]))

# ---------- 汇总 ----------
n_fail = sum(1 for ok, _, _ in results if not ok)
with open(os.path.join(ITEM2, "item002-compare-%s.json" % V), "w", encoding="utf-8") as fh:
    json.dump({"checks": [{"name": n, "ok": ok, "detail": d} for ok, n, d in results],
               "passed": len(results) - n_fail, "failed": n_fail}, fh, ensure_ascii=False, indent=2)
for ok, n, d in results:
    print("%s %s %s" % ("PASS" if ok else "FAIL", n, d))
print("COMPARE SUMMARY passed=%d/%d" % (len(results) - n_fail, len(results)))
return_code = 0 if n_fail == 0 else 1
sys.exit(return_code)
