#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-003 独立解析比对（software_verification, VERIFY-20261019-item003-v1）。

不采信交付 acceptance/RESULT 行；全部判据独立重推导：
  T1  fresh transcript 退出码独立重断言（*_EXIT token，外层 shell 恒 0）+ keep-remote 无 cleanup
      + 指纹 + 17 下载
  T2  selftest 判例数从探针源 run_selftest 段 check( 调用数统计（不手抄）+ 19 判例名独立逐条
      + 关键语义行精确锚定 + fresh vs impl r3 全文恒等
  T3  解码数学独立重推导：独立 letterbox 公式 + 独立 encode→decode 正反推 (对齐历史已部署
      code/yolov5_postprocess.cc 公式) + 六夹具 type=detect/box 行逐字段独立断言
  T4  config 行独立重推导（confidence_min 从 thresholds.yaml 源读 = 0.5、score_used、from 标注）
  T5  core JUnit tests=1035 failures=0 + 判例名多重集恒等 fresh vs r3 +
      21 个 detect 域判例名（从验证侧 test 源 check( 名字独立提取）在场
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
IMPL = "vision-item003-192_168_0_103-20261019-item003-r3"
FRESH = "vision-item003-192_168_0_103-verify-20261019-item003-v1"
T = "20261019-item003-r3"
V = "verify-20261019-item003-v1"

results = []


def chk(ok, name, detail=""):
    results.append((ok, name, detail))


def read(p):
    with open(p, encoding="utf-8") as fh:
        return fh.read()


def jread(p):
    with open(p, encoding="utf-8") as fh:
        return json.load(fh)


# ---------- T1 transcript ----------
tr = jread(os.path.join(ITEMD, FRESH + ".transcript.json"))
steps = {s["step"]: s for s in tr["steps"]}
exp = [("build-core", 0), ("core-tests", 0), ("selftest", 0),
       ("fixture-person", 0), ("fixture-nonperson", 0), ("fixture-nms", 0),
       ("fixture-empty", 0), ("fixture-padbox", 0), ("fixture-int8", 0), ("config", 0)]
for name, e in exp:
    s = steps.get(name)
    out = (s.get("output") or "") if s else ""
    toks = re.findall(r"([A-Z_0-9]+_EXIT)=(\d+)", out)
    if name == "selftest":
        vals = [int(v) for _, v in toks]
        chk(s is not None and vals == [0, 0], "T1_%s_exits_00" % name,
            "tokens=%s transcript_exit=%s" % (vals, s.get("exit") if s else "MISSING"))
    else:
        mtok = re.search(r"[A-Z_0-9]+_EXIT=(\d+)", out)
        chk(s is not None and mtok is not None and int(mtok.group(1)) == e,
            "T1_%s_exit%d" % (name, e),
            "token=%s transcript_exit=%s" % (mtok.group(1) if mtok else None,
                                             s.get("exit") if s else "MISSING"))
chk(all(s["step"] != "cleanup" for s in tr["steps"]), "T1_keep_remote_no_cleanup",
    "cleanup_steps=%d" % sum(1 for s in tr["steps"] if s["step"] == "cleanup"))
fp = tr.get("host_key_fingerprint") or ""
chk(fp.startswith("SHA256:"), "T1_host_fingerprint_present", fp)
up = steps.get("upload-app", {})
chk("uploaded 282 files" in (up.get("output") or ""), "T1_upload_app_reported",
    (up.get("output") or "")[:40])
dl = [s for s in tr["steps"] if s["step"] == "download"]
chk(len(dl) == 17 and all(s.get("exit") == 0 for s in dl), "T1_17_downloads_ok", str(len(dl)))

# ---------- T2 selftest ----------
F_SELF = "%s.app_core-evidence_item003-selftest-%s.log"
st = read(os.path.join(ITEMD, F_SELF % (FRESH, V)))
PROBE_SRC = os.path.normpath(os.path.join(ITEMD, "..", "..", "..", "..",
                                          "app", "src", "core", "tools", "detect_probe.cpp"))
probe_src = read(PROBE_SRC)
m_runself = re.search(r"int run_selftest\(\) \{(.*?)\n\}", probe_src, re.S)
self_body = m_runself.group(1) if m_runself else ""
n_self_calls = len(re.findall(r"^\s*check\((?!bool)", self_body, re.M))
pass_lines = re.findall(r"^\[PASS\] (.+)$", st, re.M)
names2 = [ln.split(" :: ")[0] for ln in pass_lines]
chk(len(pass_lines) == n_self_calls == 19, "T2_selftest_pass_count_from_source",
    "board=%d source=%d" % (len(pass_lines), n_self_calls))
chk("RESULT: PASS failures=0" in st, "T2_selftest_result_line")
must_have = ["person decode ok", "person boxes==1", "person normalized coords",
             "person score=obj*cls", "person class label", "person source cell/anchor",
             "person bounds invariant", "nonperson decode ok", "nonperson dropped",
             "empty image -> empty boxes ok", "nms decode ok", "nms keeps 1",
             "nms threshold from config changeable", "padbox decode ok",
             "pad-region box dropped", "score threshold from config filters",
             "bad tensor channels rejected", "int8 decode ok", "int8 dequant coords"]
missing = [m for m in must_have if m not in names2]
chk(not missing, "T2_selftest_19_judgements_names", "missing=%s count=%d" % (missing, len(names2)))
chk("person normalized coords :: box xywh=(0.31250,0.44444,0.10000,0.17778) score=0.8100" in st,
    "T2_person_normalized_coords_exact")
st3 = read(os.path.join(ITEMD, F_SELF % (IMPL, T)))
chk(st.strip() == st3.strip(), "T2_selftest_identical_to_impl_r3")

# ---------- T3 解码 + fixture 独立数学 ----------
def letterbox_indep(sw, sh, dw, dh):
    s = min(dw / sw, dh / sh)
    rw = int(round(sw * s))
    rh = int(round(sh * s))
    pw, ph = dw - rw, dh - rh
    return {"scale": s, "rw": rw, "rh": rh, "pad_x": pw // 2, "pad_y": ph // 2}


lb = letterbox_indep(1280, 720, 640, 640)
chk(lb == {"scale": 0.5, "rw": 640, "rh": 360, "pad_x": 0, "pad_y": 140},
    "T3a_letterbox_independent_math", str(lb))

ANCHORS = [10, 13, 16, 30, 33, 23, 30, 61, 62, 45, 59, 119, 116, 90, 156, 198, 373, 326]
LB = {"scale": 0.5, "pad_x": 0, "pad_y": 140}


def encode_decode_indep(mx, my, wpx, hpx, s, a, drop=True):
    """夹具编码公式的独立镜像 + YOLOv5 sigmoid 后解码 + 逆 letterbox 归一化。

    解码公式独立对照 code/yolov5_postprocess.cc process_fp32:
      bx=(v*2-0.5+j)*stride; bw=(v*2)^2*anchor_w
    逆 letterbox: rx=(mx-pad)/scale 后裁剪到原图。
    """
    H = 80 >> s
    stride = 640.0 / H
    uh, uv = mx / stride, my / stride
    j, i = int(math.floor(uh)), int(math.floor(uv))
    assert 0 <= i < H and 0 <= j < H
    v_cx = (uh - j + 0.5) / 2.0
    v_cy = (uv - i + 0.5) / 2.0
    aw, ah = ANCHORS[s * 6 + a * 2], ANCHORS[s * 6 + a * 2 + 1]
    v_bw = math.sqrt(wpx / aw) / 2.0
    v_bh = math.sqrt(hpx / ah) / 2.0
    bx = (v_cx * 2 - 0.5 + j) * stride
    by = (v_cy * 2 - 0.5 + i) * stride
    w2 = (v_bw * 2) ** 2 * aw
    h2 = (v_bh * 2) ** 2 * ah
    m_x1, m_y1, m_x2, m_y2 = bx - w2 / 2, by - h2 / 2, bx + w2 / 2, by + h2 / 2
    if not drop:
        return m_x1, m_y1, m_x2, m_y2
    ix1 = (m_x1 - LB["pad_x"]) / LB["scale"]
    iy1 = (m_y1 - LB["pad_y"]) / LB["scale"]
    ix2 = (m_x2 - LB["pad_x"]) / LB["scale"]
    iy2 = (m_y2 - LB["pad_y"]) / LB["scale"]
    rx1 = max(0.0, min(ix1, 1280.0))
    ry1 = max(0.0, min(iy1, 720.0))
    rx2 = max(0.0, min(ix2, 1280.0))
    ry2 = max(0.0, min(iy2, 720.0))
    return rx1 / 1280.0, ry1 / 720.0, (rx2 - rx1) / 1280.0, (ry2 - ry1) / 720.0


px, py, pw_, ph_ = encode_decode_indep(232.0, 332.0, 64.0, 64.0, 0, 2)
exp_box = (400.0 / 1280.0, 320.0 / 720.0, 128.0 / 1280.0, 128.0 / 720.0)
chk(all(abs(a - b) < 1e-4 for a, b in zip((px, py, pw_, ph_), exp_box)),
    "T3a_person_decode_independent_math",
    "%.6f,%.6f,%.6f,%.6f expect=%.6f,%.6f,%.6f,%.6f"
    % (px, py, pw_, ph_, exp_box[0], exp_box[1], exp_box[2], exp_box[3]))

def fixlog(fx_name):
    """交付 runner 产物文件名: fixture-<name>-<stamp>（stamp 在名后）。"""
    return read(os.path.join(ITEMD, "%s.app_core-evidence_item003-fixture-%s-%s.log"
                             % (FRESH, fx_name, V)))


DETECT_PAT = re.compile(
    r"type=detect ok=(\d) reason=(\S+) boxes=(\d+) candidates=(\d+) dropped_score=(\d+) "
    r"dropped_class=(\d+) nms_suppressed=(\d+) dropped_bounds=(\d+) truncated=(\d+)")
BOX_PAT = re.compile(
    r"box xywh=\(([\d.]+),([\d.]+),([\d.]+),([\d.]+)\) score=([\d.]+) cls=(\d+) label=(\S+) "
    r"scale=(\d+) anchor=(\d+) cell=(\d+),(\d+)")


def parse_detect(text):
    m = DETECT_PAT.search(text)
    if not m:
        return None
    return {"ok": int(m.group(1)), "reason": m.group(2), "boxes": int(m.group(3)),
            "candidates": int(m.group(4)), "dropped_score": int(m.group(5)),
            "dropped_class": int(m.group(6)), "suppressed": int(m.group(7)),
            "dropped_bounds": int(m.group(8)), "truncated": int(m.group(9))}


# 额外角点独立数学（真实网格单元锚定：scale0 cell(41,29) 中心=232/8=29 格 → cell_j=29）
chk(encode_decode_indep(232.0, 332.0, 64.0, 64.0, 0, 2) is not None, "T3a_selfencode_runs")
cell_j = int(math.floor(232.0 / 8.0))
cell_i = int(math.floor(332.0 / 8.0))
chk(cell_i == 41 and cell_j == 29, "T3a_cell_indices_independent", "i=%d j=%d" % (cell_i, cell_j))

# T3b: person 夹具逐字段
person_log = fixlog("person")
f = parse_detect(person_log)
okp = (f is not None and f["ok"] == 1 and f["reason"] == "DETECT_OK" and f["boxes"] == 1
       and f["candidates"] == 1 and f["suppressed"] == 0 and f["dropped_bounds"] == 0
       and f["truncated"] == 0)
chk(okp, "T3b_person_detect_row_independent", str(f))
mb = BOX_PAT.search(person_log)
chk(mb is not None, "T3b_person_box_row_present")
if mb:
    bx, by, bw_, bh_ = (float(mb.group(i)) for i in range(1, 5))
    score, cls, label = float(mb.group(5)), int(mb.group(6)), mb.group(7)
    sc, an, ci, cj = int(mb.group(8)), int(mb.group(9)), int(mb.group(10)), int(mb.group(11))
    chk(all(abs(a - b) < 1e-4 for a, b in zip((bx, by, bw_, bh_), exp_box)),
        "T3b_person_box_coords_vs_independent", "%.5f,%.5f,%.5f,%.5f" % (bx, by, bw_, bh_))
    chk(abs(score - 0.81) < 2e-3 and cls == 0 and label == "person",
        "T3b_person_box_score_cls_label", "score=%.4f cls=%d label=%s" % (score, cls, label))
    chk(sc == 0 and an == 2 and ci == cell_i and cj == cell_j,
        "T3b_person_box_source_fields", "scale=%d anchor=%d cell=%d,%d" % (sc, an, ci, cj))

# T3b: nms 夹具（0.45→1 框 suppressed=1；iou 0.99→2 的联动由 selftest 锚定 + 独立 IoU 数学）
def iou_plus1(ax1, ay1, ax2, ay2, bx1, by1, bx2, by2):
    w = max(0.0, min(ax2, bx2) - max(ax1, bx1) + 1.0)
    h = max(0.0, min(ay2, by2) - max(ay1, by1) + 1.0)
    inter = w * h
    u = (ax2 - ax1 + 1.0) * (ay2 - ay1 + 1.0) + (bx2 - bx1 + 1.0) * (by2 - by1 + 1.0) - inter
    return 0.0 if u <= 0 else inter / u


m1 = encode_decode_indep(232.0, 332.0, 64.0, 64.0, 0, 2, drop=False)
m2 = encode_decode_indep(240.0, 340.0, 64.0, 64.0, 0, 2, drop=False)
iou_np = iou_plus1(m1[0], m1[1], m1[2], m1[3], m2[0], m2[1], m2[2], m2[3])
chk(0.45 < iou_np < 0.98, "T3b_nms_pair_iou_in_expected_window", "iou=%.4f" % iou_np)
nms_log = fixlog("nms")
fn = parse_detect(nms_log)
chk(fn is not None and fn["ok"] == 1 and fn["boxes"] == 1 and fn["candidates"] == 2
    and fn["suppressed"] == 1, "T3b_nms_detect_row_independent", str(fn))
mb = BOX_PAT.search(nms_log)
chk(mb is not None and abs(float(mb.group(5)) - 0.81) < 2e-3,
    "T3b_nms_keeps_higher_score", mb.group(0)[:80] if mb else "no box")
chk("nms threshold from config changeable :: boxes=2" in st,
    "T3b_nms_099_keeps_2_via_config")

# T3b: nonperson（cls=2 car → dropped_class=1, boxes=0）
np_log = fixlog("nonperson")
fnp = parse_detect(np_log)
chk(fnp is not None and fnp["ok"] == 1 and fnp["reason"] == "DETECT_EMPTY" and fnp["boxes"] == 0
    and fnp["candidates"] == 1 and fnp["dropped_class"] == 1,
    "T3b_nonperson_detect_row_independent", str(fnp))

# T3b: empty（全零夹具 → DETECT_EMPTY no candidates）
em_log = fixlog("empty")
fe = parse_detect(em_log)
chk(fe is not None and fe["ok"] == 1 and fe["reason"] == "DETECT_EMPTY" and fe["boxes"] == 0
    and fe["candidates"] == 0, "T3b_empty_detect_row_independent", str(fe))

# T3b: padbox（my=60<pad_y=140 → dropped_bounds=1）
pb_log = fixlog("padbox")
fpb = parse_detect(pb_log)
chk(fpb is not None and fpb["ok"] == 1 and fpb["boxes"] == 0 and fpb["candidates"] == 1
    and fpb["dropped_bounds"] == 1, "T3b_padbox_detect_row_independent", str(fpb))

# T3b: int8 独立反量化重算（scale=1/255 zp=-128；夹具 q(v)=round(v/scale)+zp 截断 [-128,127]）
# 注意 scale 是 C++ float（Int8Fixture.scale 为 float 字段）：float32(1/255) < double(1/255)，
# 0.9/scale_float32 = 229.49999 → round 229 → byte 101 → dq=0.89804 → score=0.8065。
# double 口径会错误推出 byte 102/0.8135（本轮 dry-run 预演抓出的构造错）。
QS = struct.unpack("f", struct.pack("f", 1.0 / 255.0))[0]
QZP = -128


def q(v):
    qv = round(v / QS) + QZP
    return max(-128.0, min(127.0, qv))


def dq(b):
    """int8 two's complement: 夹具写入 byte = uint8(int8_t(qc))，故 byte>=128 表示负数。"""
    signed = b if b < 128 else b - 256
    return (signed - QZP) * QS


# 夹具编码值独立重算（对齐探针 Int8Fixture 语义：q(value))
j0, i0 = 29, 41
v_cx = (232.0 / 8.0 - j0 + 0.5) / 2.0
v_cy = (332.0 / 8.0 - i0 + 0.5) / 2.0
aw, ah = ANCHORS[2 * 2], ANCHORS[2 * 2 + 1]
v_bw = math.sqrt(64.0 / aw) / 2.0
v_bh = math.sqrt(64.0 / ah) / 2.0
# int8 反量化后再解码（独立全链路）
cx_q = dq(q(v_cx))
cy_q = dq(q(v_cy))
bw_q = dq(q(v_bw))
bh_q = dq(q(v_bh))
bx = (cx_q * 2 - 0.5 + j0) * 8.0
by = (cy_q * 2 - 0.5 + i0) * 8.0
w2 = (bw_q * 2) ** 2 * aw
h2 = (bh_q * 2) ** 2 * ah
mx1, my1, mx2, my2 = bx - w2 / 2, by - h2 / 2, bx + w2 / 2, by + h2 / 2
ix1 = (mx1 - LB["pad_x"]) / LB["scale"]
iy1 = (my1 - LB["pad_y"]) / LB["scale"]
ix2 = (mx2 - LB["pad_x"]) / LB["scale"]
iy2 = (my2 - LB["pad_y"]) / LB["scale"]
rx1 = max(0.0, min(ix1, 1280.0)) / 1280.0
ry1 = max(0.0, min(iy1, 720.0)) / 720.0
rx2 = max(0.0, min(ix2, 1280.0)) / 1280.0
ry2 = max(0.0, min(iy2, 720.0)) / 720.0
obj_q = dq(q(0.9))
cls_q = dq(q(0.9))
score_exp = obj_q * cls_q
it8_log = fixlog("int8")
fi8 = parse_detect(it8_log)
chk(fi8 is not None and fi8["ok"] == 1 and fi8["boxes"] == 1 and fi8["candidates"] == 1,
    "T3b_int8_detect_row_independent", str(fi8))
mb = BOX_PAT.search(it8_log)
chk(mb is not None, "T3b_int8_box_row_present")
if mb:
    bx_, by_, bw__, bh__ = (float(mb.group(i)) for i in range(1, 5))
    sc_ = float(mb.group(5))
    chk(abs(bx_ - rx1) < 5e-3 and abs(by_ - ry1) < 5e-3 and abs(bw__ - (rx2 - rx1)) < 5e-3
        and abs(bh__ - (ry2 - ry1)) < 5e-3,
        "T3b_int8_coords_vs_independent_dequant",
        "have=%.5f,%.5f,%.5f,%.5f exp=%.5f,%.5f,%.5f,%.5f"
        % (bx_, by_, bw__, bh__, rx1, ry1, rx2 - rx1, ry2 - ry1))
    chk(abs(sc_ - score_exp) < 5e-3, "T3b_int8_score_vs_independent",
        "have=%.4f exp=%.4f" % (sc_, score_exp))

# ---------- T4 config 独立重推导 ----------
cfg_log = read(os.path.join(ITEMD, FRESH + ".app_core-evidence_item003-config-%s.log" % V))
ty_path = os.path.normpath(os.path.join(ITEMD, "..", "..", "..", "..", "app", "config",
                                        "thresholds.yaml"))
ty = read(ty_path)
mty = re.search(r"confidence_min:\s*([\d.]+)", ty)
conf_src = float(mty.group(1)) if mty else None
chk(conf_src == 0.5, "T4_thresholds_yaml_confidence_min", str(conf_src))
m = re.search(r"type=config dir=(\S+) confidence_min=([\d.]+) score_used=([\d.]+) nms_iou=([\d.]+) "
              r"\(([^)]+)\)", cfg_log)
chk(m is not None, "T4_config_row_present")
if m:
    chk(m.group(2) == "0.500000" and m.group(3) == "0.500000" and m.group(4) == "0.450000"
        and m.group(5) == "from thresholds.confidence_min",
        "T4_config_row_semantics", m.group(0)[:110])
m = re.search(r"type=detect ok=1 reason=DETECT_OK boxes=1", cfg_log)
chk(m is not None, "T4_config_person_box_kept_at_0.5")
chk("box xywh=(0.31250,0.44444,0.10000,0.17778) score=0.8100 cls=0 label=person" in cfg_log,
    "T4_config_box_exact_coords")

# ---------- T5 core JUnit ----------
XML_PAT = "%s.app_core-evidence_item003-core-tests-%s.xml"
xf = read(os.path.join(ITEMD, XML_PAT % (FRESH, V)))
m = re.search(r'<testsuites tests="(\d+)" failures="(\d+)"', xf)
chk(m is not None and m.group(1) == "1035" and m.group(2) == "0", "T5_junit_1035_0",
    m.group(0) if m else "no match")
names_f = re.findall(r'<testcase classname="[^"]*" name="([^"]+)"', xf)
xf3 = read(os.path.join(ITEMD, XML_PAT % (IMPL, T)))
names_3 = re.findall(r'<testcase classname="[^"]*" name="([^"]+)"', xf3)
cf = collections.Counter(names_f)
c3 = collections.Counter(names_3)
chk(cf == c3, "T5_junit_name_multiset_identical_to_r3",
    "fresh=%d impl=%d" % (len(names_f), len(names_3)))
# detect 域判例名从 test 源独立提取（验证侧边界：非手抄）
TEST_SRC = os.path.normpath(os.path.join(ITEMD, "..", "..", "..", "..", "app", "tests", "core",
                                         "test_vision_detect_postprocess.cpp"))
tsrc = read(TEST_SRC)

def extract_check_names(src_text):
    """顶级逗号扫描器：check( 内逐字符跟踪括号深度，深度==1 处的第一个逗号后的
    第一个字符串字面量即判例名（跳过嵌套调用的内部逗号）。"""
    names = []
    for m in re.finditer(r"check\(", src_text):
        i = m.end()
        depth = 1
        while i < len(src_text) and depth > 0:
            c = src_text[i]
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
            elif c == "," and depth == 1:
                mm = re.match(r'\s*"([^"]+)"', src_text[i + 1:i + 81])
                if mm:
                    names.append(mm.group(1))
                break
            i += 1
    return names


det_names = set(extract_check_names(tsrc))
chk(len(det_names) >= 20, "T5_test_source_judgements_extracted", str(len(det_names)))
# JUnit XML 转义: 判例名含 -> 被写成 -&gt;（ITEM-020 教训④），先 unescape 再比对
def unesc(n):
    return (n.replace("&gt;", ">").replace("&lt;", "<")
             .replace("&quot;", '"').replace("&apos;", "'").replace("&amp;", "&"))


names_f_un = {unesc(n) for n in names_f}
names_3_un = {unesc(n) for n in names_3}
missing_d = [n for n in det_names if n not in names_f_un]
chk(not missing_d, "T5_all_detect_judgements_on_board", "missing=%s" % missing_d)
# 代表性独立在场
for probe in ("int8 x within quant tol", "boxes sorted by score desc", "inverse clamps to frame",
              "iou identical"):
    chk(probe in set(names_f), "T5_has[%s]" % probe)

# ---------- T6 凭据语义脱敏扫描 ----------
ssh_sec = os.environ.get("TRAIN_RK3588_SSH_CREDENTIAL", "")
cam_front = os.environ.get("TRAIN_FRONT_RTSP_CREDENTIAL", "")
cam_side = os.environ.get("TRAIN_SIDE_RTSP_CREDENTIAL", "")
pat = re.compile(r"[a-z]+://[^/\s:]+:[^@\s]+@|password=\S+")
hits = []
files_scanned = 0
for fnm in os.listdir(ITEMD):
    if not fnm.startswith(FRESH):
        continue
    pth = os.path.join(ITEMD, fnm)
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
        if frag.startswith("password=admin") or frag == "admin":
            continue
        hits.append((fnm, frag[:40]))
chk(len(hits) == 0, "T6_no_credential_leak_fresh_products",
    "files=%d hits=%s" % (files_scanned, hits[:5]))

# ---------- 汇总 ----------
n_fail = sum(1 for ok, _, _ in results if not ok)
with open(os.path.join(ITEMD, "item003-compare-%s.json" % V), "w", encoding="utf-8") as fh:
    json.dump({"checks": [{"name": n, "ok": ok, "detail": d} for ok, n, d in results],
               "passed": len(results) - n_fail, "failed": n_fail}, fh, ensure_ascii=False, indent=2)
for ok, n, d in results:
    print("%s %s %s" % ("PASS" if ok else "FAIL", n, d))
print("COMPARE SUMMARY passed=%d/%d" % (len(results) - n_fail, len(results)))
sys.exit(0 if n_fail == 0 else 1)
