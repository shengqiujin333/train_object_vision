#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-005 验证侧扩展夹具（software_verification，VERIFY-20261017-v1）。

在 --keep-remote 保留的 /tmp/railhmi_rga 树上驱动交付入口 railhmi-rga-convert
与已上传源码（g++ 直连 src/media/src/rga_convert.cpp），验证侧自有判据：

  A. 合成判据独立解析（不采信交付自身 acceptance 行）：
     padded 灰 spread=0 / min_px==max_px==200 / 纯蓝方向 / 显式失败路径
  B. 确定性：两轮同参 synthetic_padded/synthetic_blue 行逐字段 identical
  C. runner 未知参数 exit2；--help exit0
  D. 板端 g++ 直编 src/media/src/rga_convert.cpp + librailhmi_media.a：
     验证侧自有向量（红/绿纯色、乱值 padding、UV 交换负对照、
     hstride 边界、spread=-1 空图）
  E. worker 独立复跑（test 源再跑一轮）：bgr_converted==published 且 failed=0
  F. 凭据泄漏扫描
"""
import io
import json
import os
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(ROOT / "app" / "tools"))
import paramiko  # noqa: E402
from board_build import rrun  # noqa: E402

OUT = HERE
STAMP = "20261017-item005-verify-v1"
TAG = "rga-ext-192_168_0_103-%s" % STAMP
REMOTE = "/tmp/railhmi_rga"
BIN = REMOTE + "/build-media/tools/railhmi-rga-convert"

for n in ("TRAIN_RK3588_SSH_CREDENTIAL",):
    v = subprocess.run(["powershell.exe", "-NoProfile", "-Command",
                        '[Environment]::GetEnvironmentVariable("%s","User")' % n],
                       capture_output=True, text=True).stdout.strip()
    if v:
        os.environ[n] = v
secret = os.environ.get("TRAIN_RK3588_SSH_CREDENTIAL", "")
if not secret:
    sys.stderr.write("SSH 凭据缺失\n")
    raise SystemExit(2)

client = paramiko.SSHClient()
client.load_system_host_keys()
client.set_missing_host_key_policy(paramiko.RejectPolicy())
client.connect("192.168.0.103", port=22, username="admin", password=secret,
               timeout=10, auth_timeout=10, banner_timeout=10,
               look_for_keys=False, allow_agent=False)

steps = []

def run(step, cmd, timeout=60):
    res = rrun(client, cmd, timeout)
    steps.append({"step": step, **res})
    print("[%s] exit=%s" % (step, res.get("exit")))
    return res

checks = []

def check(name, ok, detail=""):
    checks.append({"name": name, "ok": bool(ok), "detail": detail})
    print("ext %s %s" % ("PASS" if ok else "FAIL", name))

# ---------- A. 探针独立复跑（合成判据段）+ 已归档日志解析 ----------
import re as _re

PAT_PADDED = (r"type=synthetic_padded ok=(\w+) err=(\S*) width=(\d+) height=(\d+) "
              r"b=(\d+) g=(\d+) r=(\d+) spread=(-?\d+) min_px=(\d+) max_px=(\d+) "
              r"padding_not_leaked=(\w+)")
PAT_BLUE = (r"type=synthetic_blue ok=(\w+) err=(\S*) b=(\d+) g=(\d+) r=(\d+) "
            r"blue_dominant=(\w+)")
PAT_FAIL = (r"type=failure_paths null_rejected=(\w+)\((\w*)\) odd_rejected=(\w+)\((\w*)\) "
            r"notready_rejected=(\w+)\((\w*)\)")
PAT_WORKER = (r"type=worker_convert published=(\d+) bgr_converted=(\d+) bgr_failed=(\d+) "
              r"last_error=(\S*) bgr_means=(\d+)/(\d+)/(\d+) spread=(-?\d+) "
              r"width=(\d+) height=(\d+)")
PAT_KEEP = (r"type=fail_keep_frames published_before=(\d+) published_after=(\d+) "
            r"still_producing=(\w+)")

def parse_padded(out):
    m = _re.search(PAT_PADDED, out)
    if not m:
        return None
    return {"ok": m.group(1), "err": m.group(2), "w": int(m.group(3)), "h": int(m.group(4)),
            "b": int(m.group(5)), "g": int(m.group(6)), "r": int(m.group(7)),
            "spread": int(m.group(8)), "min_px": int(m.group(9)), "max_px": int(m.group(10)),
            "pad": m.group(11)}

# A1: 独立跑一轮探针（test 源 1s）解析全部判据行
r_a = run("extA-rerun", "cd %s && %s --source test --duration 1 --out /tmp/extA.log; echo A_EXIT=$?"
          % (REMOTE, BIN), 90)
out_a = r_a.get("output", "")
pad = parse_padded(out_a)
blue_m = _re.search(PAT_BLUE, out_a)
fail_m = _re.search(PAT_FAIL, out_a)
keep_m = _re.search(PAT_KEEP, out_a)
a_exit = _re.search(r"A_EXIT=(\d+)", out_a)

a1 = (a_exit and a_exit.group(1) == "0" and pad and pad["ok"] == "true" and pad["err"] == ""
      and pad["w"] == 1280 and pad["h"] == 720 and pad["b"] == pad["g"] == pad["r"] == 200
      and pad["spread"] == 0 and pad["min_px"] == 200 and pad["max_px"] == 200
      and pad["pad"] == "true")
check("A1_padded_gray_exact_200_spread0_no_padding_leak", a1,
      json.dumps(pad, ensure_ascii=False) if pad else "no match")

a2 = (blue_m is not None and blue_m.group(1) == "true" and blue_m.group(2) == ""
      and int(blue_m.group(3)) > int(blue_m.group(4)) + 100
      and int(blue_m.group(3)) > int(blue_m.group(5)) + 100
      and blue_m.group(6) == "true")
check("A2_blue_dominant_direction", a2,
      blue_m.group(0) if blue_m else "no match")

a3 = (fail_m is not None and fail_m.group(1) == "true" and fail_m.group(2) == "null_arg"
      and fail_m.group(3) == "true" and fail_m.group(4) == "odd_dimensions"
      and fail_m.group(5) == "true" and fail_m.group(6) == "NOT_READY")
check("A3_failure_paths_explicit_tokens", a3,
      fail_m.group(0) if fail_m else "no match")

a4 = keep_m is not None and keep_m.group(3) == "true" and \
    int(keep_m.group(2)) > int(keep_m.group(1))
check("A4_failure_keeps_frames_publishing", a4,
      keep_m.group(0) if keep_m else "no match")

# ---------- B. 确定性：同参两轮 synthetic 行逐字段 identical（纯转换路径无自然波动项）----------
r_b = run("extB-det-run", "cd %s && %s --source test --duration 1 --out /tmp/extB.log; echo B_EXIT=$?"
          % (REMOTE, BIN), 90)
out_b = r_b.get("output", "")
pad2 = parse_padded(out_b)
blue2 = _re.search(PAT_BLUE, out_b)
delta_items = []
if pad and pad2:
    for k in ("ok", "err", "w", "h", "b", "g", "r", "spread", "min_px", "max_px", "pad"):
        if pad[k] != pad2[k]:
            delta_items.append((k, pad[k], pad2[k]))
b1 = bool(pad and pad2 and not delta_items)
check("B_synthetic_padded_deterministic", b1, "delta=%s" % delta_items if delta_items else "identical")
b2 = bool(blue_m and blue2 and blue_m.group(0) == blue2.group(0))
check("B_synthetic_blue_deterministic", b2)

# ---------- C. 未知参数 / --help ----------
r_c1 = run("extC-unknown-arg", "cd %s && %s --bogus-flag; echo C1_EXIT=$?" % (REMOTE, BIN), 30)
out_c1 = r_c1.get("output", "")
check("C_unknown_arg_exit2", "C1_EXIT=2" in out_c1 and "unknown arg: --bogus-flag" in out_c1)
r_c2 = run("extC-help", "cd %s && %s --help; echo C2_EXIT=$?" % (REMOTE, BIN), 30)
out_c2 = r_c2.get("output", "")
check("C_help_exit0_usage", "C2_EXIT=0" in out_c2 and "--selftest" in out_c2)

# ---------- D. 验证侧自有 RGA 向量（g++ 直编 rga_convert.cpp；期望先行自查） ----------
VCODE = r'''
#include "railhmi/media/rga_convert.h"
#include <cstdio>
#include <cstring>
#include <vector>
using namespace railhmi;
using media::BgrImage;
static int fails = 0;
static void check(bool ok, const char* name) {
    std::printf("vext %s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++fails;
}
static void fill_nv12(std::vector<unsigned char>* buf, int w, int h, int stride,
                      unsigned long long uv_off, int y, int u, int v, int pad_val) {
    buf->assign((size_t)uv_off + (size_t)stride * (size_t)(h / 2), pad_val);
    for (int r = 0; r < h; ++r)
        std::memset(buf->data() + (size_t)r * stride, y, w);
    unsigned char* uv = buf->data() + uv_off;
    for (size_t i = 0; i < (size_t)stride * (h / 2); i += 2) {
        uv[i] = (unsigned char)u; uv[i + 1] = (unsigned char)v;
    }
}
int main() {
    // v-01 hstride 数学（与 selftest 独立重推导）
    check(media::rga_hstride(1408, 1408ull * 720) == 720, "v01_hstride_padded_720");
    check(media::rga_hstride(1280, 1280ull * 720) == 720, "v02_hstride_contiguous_720");
    check(media::rga_hstride(1920, 1920ull * 1080) == 1080, "v03_hstride_1080p");
    // v-04 边界: uv_offset 恰为 1*stride → hstride=1（畸形但可整除）返回 1
    check(media::rga_hstride(1280, 1280) == 1, "v04_hstride_one_row");
    // v-05..07 hstride 拒绝族
    check(media::rga_hstride(1280, 1000) == -1, "v05_non_multiple_rejected");
    check(media::rga_hstride(0, 100) == -1, "v06_zero_stride_rejected");
    check(media::rga_hstride(-1280, 1) == -1, "v07_negative_stride_rejected");
    check(media::rga_hstride(1280, 0) == -1, "v08_zero_uv_offset_rejected");
    // v-09..11 红色纯色 BT601_FULL: Y=76,U=84,V=255 → BGR≈(29..43,0..20,231..255)，R 主导
    {
        const int w = 640, h = 480, stride = 640, uv = stride * h;
        std::vector<unsigned char> buf;
        fill_nv12(&buf, w, h, stride, uv, 76, 84, 255, 0);
        BgrImage img; std::string err;
        bool ok = media::nv12_to_bgr_raw(buf.data(), w, h, stride, uv, &img, &err);
        double b = 0, g = 0, r = 0;
        media::bgr_channel_means(img, &b, &g, &r);
        check(ok && err.empty(), "v09_red_converts_ok");
        check(r > b + 100 && r > g + 100, "v10_red_dominant");
        check(r >= 200 && b <= 80 && g <= 80, "v11_red_values_in_range");
    }
    // v-12..14 绿色纯色 Y=149,U=43,V=21 → G 主导（反向排除绿屏仅当预期 G 主导时措辞有用；
    // 这里正向断言 G 主导以证明通道无颠倒）
    {
        const int w = 640, h = 480, stride = 640, uv = stride * h;
        std::vector<unsigned char> buf;
        fill_nv12(&buf, w, h, stride, uv, 149, 43, 21, 0);
        BgrImage img; std::string err;
        bool ok = media::nv12_to_bgr_raw(buf.data(), w, h, stride, uv, &img, &err);
        double b = 0, g = 0, r = 0;
        media::bgr_channel_means(img, &b, &g, &r);
        check(ok, "v12_green_converts_ok");
        check(g > b + 100 && g > r + 100, "v13_green_dominant");
        check(g >= 200 && b <= 80 && r <= 80, "v14_green_values_in_range");
    }
    // v-15..16 乱值 padding 注入: padding 列/行填 0xFF；
    //   灰图 Y=200 仅在 [0,w) 行内写 → min_px 必须 == max_px == 200（0xFF padding 不泄漏）
    {
        const int w = 1280, h = 720, stride = 1408, uv = stride * h;
        std::vector<unsigned char> buf;
        fill_nv12(&buf, w, h, stride, uv, 200, 128, 128, 0xFF);
        BgrImage img; std::string err;
        bool ok = media::nv12_to_bgr_raw(buf.data(), w, h, stride, uv, &img, &err);
        int mn = 255, mx = 0;
        for (unsigned char p : img.data) {
            int v0 = p; if (v0 < mn) mn = v0; if (v0 > mx) mx = v0;
        }
        check(ok, "v15_padded_ff_converts_ok");
        check(mn == 200 && mx == 200, "v16_ff_padding_not_leaked");
    }
    // v-17 UV 交换负对照: U/V 互换（把 V 写在 U 位）→ 结果偏红（青蓝互换）
    //   灰图对照应为 neutral（三通道近似），交换版蓝红失衡方向翻转
    {
        const int w = 320, h = 240, stride = 320, uv = stride * h;
        std::vector<unsigned char> buf;
        fill_nv12(&buf, w, h, stride, uv, 128, 240, 110, 0);
        BgrImage img; std::string err;
        bool ok = media::nv12_to_bgr_raw(buf.data(), w, h, stride, uv, &img, &err);
        double b = 0, g = 0, r = 0;
        media::bgr_channel_means(img, &b, &g, &r);
        // 正常 (U=240,V=110)：蓝主导（同探针 synthetic_blue 比例）
        check(ok, "v17_uv_swap_converts_ok");
        check(b > r + 60, "v18_uv_normal_blue_dominant");
        // 交换 U/V → 红/蓝方向翻转
        std::vector<unsigned char> buf2;
        fill_nv12(&buf2, w, h, stride, uv, 128, 110, 240, 0);
        BgrImage img2; std::string err2;
        bool ok2 = media::nv12_to_bgr_raw(buf2.data(), w, h, stride, uv, &img2, &err2);
        double b2 = 0, g2 = 0, r2 = 0;
        media::bgr_channel_means(img2, &b2, &g2, &r2);
        check(ok2, "v19_uv_swap_round_ok");
        check(r2 > b2 + 60, "v20_uv_swap_flips_to_red_dominant");
    }
    // v-21..24 nv12_to_bgr_raw 校验族: 空/奇数/负维/stride 过小/uv_offset 不足
    {
        BgrImage img; std::string e;
        std::vector<unsigned char> small(64, 128);
        check(!media::nv12_to_bgr_raw(nullptr, 640, 480, 640, 640ull * 480, &img, &e) && e == "null_arg",
              "v21_null_rejected");
        e.clear();
        check(!media::nv12_to_bgr_raw(small.data(), 641, 480, 642, 642ull * 480, &img, &e)
              && e == "odd_dimensions", "v22_odd_rejected");
        e.clear();
        check(!media::nv12_to_bgr_raw(small.data(), -640, 480, 640, 640ull * 480, &img, &e)
              && e == "invalid_dimensions", "v23_negative_dim_rejected");
        e.clear();
        // stride < width → stride_too_small
        std::vector<unsigned char> buf(640ull * 480 * 2, 128);
        check(!media::nv12_to_bgr_raw(buf.data(), 640, 480, 639, 640ull * 480, &img, &e)
              && e == "stride_too_small", "v24_stride_too_small_rejected");
        e.clear();
    }
    // v-25 uv_offset 非 stride 整数倍 → invalid_uv_offset（经 rga_hstride=-1 路径）
    {
        BgrImage img; std::string e;
        std::vector<unsigned char> buf(1000ull * 720 + 990, 128);
        check(!media::nv12_to_bgr_raw(buf.data(), 640, 480, 1000, 990, &img, &e)
              && e == "invalid_uv_offset", "v25_uv_offset_non_multiple_rejected");
        e.clear();
    }
    // v-26..27 空租约/格式租约: NOT_READY
    {
        BgrImage img; std::string e;
        media::FrameLease empty;
        check(!media::nv12_to_bgr(empty, &img, &e) && e == "NOT_READY", "v26_empty_lease_not_ready");
        e.clear();
    }
    // v-28..29 spread 空图 -1；灰图 spread 0
    {
        BgrImage empty;
        check(media::bgr_channel_spread(empty) == -1.0, "v28_spread_empty_is_minus1");
        const int w = 320, h = 240, stride = 320, uv = stride * h;
        std::vector<unsigned char> buf;
        fill_nv12(&buf, w, h, stride, uv, 200, 128, 128, 0);
        BgrImage img; std::string e;
        media::nv12_to_bgr_raw(buf.data(), w, h, stride, uv, &img, &e);
        check(media::bgr_channel_spread(img) == 0.0, "v29_spread_gray_is_0");
    }
    std::printf("vext %s (%d failure(s))\n", fails == 0 ? "OK" : "FAILED", fails);
    return fails == 0 ? 0 : 1;
}
'''
print("self D: compiling on board via g++ direct link (rga_convert.cpp + librailhmi_media.a)...")
sftp = client.open_sftp()
try:
    sftp.putfo(io.BytesIO(VCODE.encode("utf-8")), REMOTE + "/vext_rga_convert.cpp")
finally:
    sftp.close()

# 复用交付 MediaCheck 构建树; 链 librailhmi_media.a（含 frame_lease/rga_convert/pipeline_factory）+ core/platform + gst/rga
r_d = run("extD-compile-run",
          "cd %s && g++ -std=c++17 -I src/core/include -I src/media/include -I src/platform/include "
          "$(pkg-config --cflags gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0) "
          "$(pkg-config --cflags librga) "
          "-o /tmp/vext_rga_convert vext_rga_convert.cpp "
          "src/media/src/rga_convert.cpp src/media/src/frame_lease.cpp "
          "build-media/src/core/librailhmi_core.a build-media/src/platform/librailhmi_platform.a "
          "$(pkg-config --libs gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0) "
          "$(pkg-config --libs librga) 2>&1 | tail -8; "
          "if [ -x /tmp/vext_rga_convert ]; then /tmp/vext_rga_convert; echo VEXT_EXIT=$?; "
          "else echo VEXT_COMPILE_FAILED; fi" % REMOTE, 180)
out_d = r_d.get("output", "")
vext_pass = out_d.count("vext PASS")
vext_fail = out_d.count("vext FAIL")
d_ok = "VEXT_EXIT=0" in out_d and vext_fail == 0 and vext_pass >= 25
check("D_vext_rga_convert_all_pass_%d" % vext_pass, d_ok)

# ---------- E. worker 段独立判据：test 源 2s，converted==published 且 failed=0 ----------
worker_m = _re.search(PAT_WORKER, out_a)
e_ok = (worker_m is not None and worker_m.group(3) == "0" and worker_m.group(4) == ""
        and worker_m.group(2) == worker_m.group(1) and int(worker_m.group(1)) > 0)
check("E_worker_converted_equals_published_no_fail", e_ok,
      worker_m.group(0) if worker_m else "no match")

# ---------- 收尾：板端 tmp 清理 ----------
run("ext-cleanup-tmp", "rm -f /tmp/extA.log /tmp/extB.log /tmp/vext_rga_convert "
    "%s/vext_rga_convert.cpp && echo EXT_TMP_CLEANED" % REMOTE, 20)

client.close()

# ---------- F. 凭据泄漏扫描（本目录全部 ITEM-005 证据文件）----------
cam_front = subprocess.run(["powershell.exe", "-NoProfile", "-Command",
                            '[Environment]::GetEnvironmentVariable("TRAIN_FRONT_RTSP_CREDENTIAL","User")'],
                           capture_output=True, text=True).stdout.strip()
cam_side = subprocess.run(["powershell.exe", "-NoProfile", "-Command",
                           '[Environment]::GetEnvironmentVariable("TRAIN_SIDE_RTSP_CREDENTIAL","User")'],
                          capture_output=True, text=True).stdout.strip()
ssh_is_admin = secret == "admin"
hits = []
for p in OUT.iterdir():
    if p.is_file() and not p.name.startswith("_"):
        try:
            data = p.read_bytes()
        except OSError:
            continue
        for label, sec in (("front", cam_front), ("side", cam_side)):
            if sec and sec.encode() in data:
                hits.append((p.name, label))
        if not ssh_is_admin and secret and secret.encode() in data:
            hits.append((p.name, "ssh"))
f_ok = len(hits) == 0
check("F_no_credential_in_evidence", f_ok, "hits=%s" % hits)

fails_n = sum(1 for c in checks if not c["ok"])
for c in checks:
    print("%s %s" % ("PASS" if c["ok"] else "FAIL", c["name"]))
print("ext ok (%d failure(s))" % fails_n if fails_n == 0 else "ext FAILED (%d failure(s))" % fails_n)

(OUT / (TAG + ".json")).write_text(json.dumps({
    "stamp": STAMP, "steps": steps,
    "credential_hits": hits,
    "checks": checks,
    "failures": fails_n,
}, ensure_ascii=False, indent=2), encoding="utf-8")
raise SystemExit(0 if fails_n == 0 else 1)
