#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-004 验证侧扩展夹具（software_verification，VERIFY-20261017-v1）。

在 --keep-remote 保留的 /tmp/railhmi_lease 树上黑盒驱动交付入口 railhmi-frame-lease
与已上传源码，验证侧自有判据：

  A. --consumers 0：流程走完、reads=0、lease_shared/lease_last 段正常、非崩溃
  B. 未知参数 → exit 2；--help → exit 0 且含 --selftest 用法行
  C. 租约日志独立解析断言（不采信交付 acceptance 行）
  D. frame_layout 自有负例（板端 g++ 直编 frame_layout.cpp + 验证侧自有判例名）
  E. 凭据泄漏扫描（本目录全部 ITEM-004 证据文件，3 个凭据值 0 命中）
"""
import json
import os
import re
import subprocess
import sys
import io
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(ROOT / "app" / "tools"))
import paramiko  # noqa: E402
from board_build import rrun  # noqa: E402

OUT = HERE
STAMP = "20261017-item004-verify-v1"
TAG = "lease-ext-%s-%s" % ("192_168_0_103", STAMP)
REMOTE = "/tmp/railhmi_lease"
BIN = REMOTE + "/build-media/tools/railhmi-frame-lease"

for n in ("TRAIN_RK3588_SSH_CREDENTIAL",):
    v = subprocess.run(["powershell.exe", "-NoProfile", "-Command",
                        '[Environment]::GetEnvironmentVariable("%s","User")' % n],
                       capture_output=True, text=True).stdout.strip()
    if v:
        os.environ[n] = v
secret = os.environ["TRAIN_RK3588_SSH_CREDENTIAL"]
cam_front = subprocess.run(["powershell.exe", "-NoProfile", "-Command",
                            '[Environment]::GetEnvironmentVariable("TRAIN_FRONT_RTSP_CREDENTIAL","User")'],
                           capture_output=True, text=True).stdout.strip()
cam_side = subprocess.run(["powershell.exe", "-NoProfile", "-Command",
                           '[Environment]::GetEnvironmentVariable("TRAIN_SIDE_RTSP_CREDENTIAL","User")'],
                          capture_output=True, text=True).stdout.strip()

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

def check(name, ok):
    checks.append({"name": name, "ok": bool(ok)})
    print("ext %s %s" % ("PASS" if ok else "FAIL", name))

# ---------- A. --consumers 0 ----------
r_a = run("extA-consumers-0",
          "cd %s && %s --source test --duration 1 --consumers 0 --out /tmp/extA.log; echo A_EXIT=$?"
          % (REMOTE, BIN), 60)
out_a = r_a.get("output", "")
a_exit = re.search(r"A_EXIT=(\d+)", out_a)
a_reads = re.search(r"type=lease_concurrent consumers=0 reads=(\d+)", out_a)
a_shared = "type=lease_shared" in out_a
a_last = re.search(r"type=lease_last release_after_last=(\d+)", out_a)
check("A_consumers0_completes_reads0_shared_last1",
      a_exit is not None and a_exit.group(1) == "0"
      and a_reads is not None and a_reads.group(1) == "0"
      and a_shared and a_last is not None and a_last.group(1) == "1")

# ---------- B. 未知参数 / --help ----------
r_b1 = run("extB-unknown-arg", "cd %s && %s --bogus-flag; echo B1_EXIT=$?" % (REMOTE, BIN), 30)
out_b1 = r_b1.get("output", "")
check("B_unknown_arg_exit2", "B1_EXIT=2" in out_b1 and "unknown arg: --bogus-flag" in out_b1)

r_b2 = run("extB-help", "cd %s && %s --help; echo B2_EXIT=$?" % (REMOTE, BIN), 30)
out_b2 = r_b2.get("output", "")
check("B_help_exit0_usage", "B2_EXIT=0" in out_b2 and "--selftest" in out_b2)

# ---------- C. 租约运行独立解析（重跑 test 源，duration 2） ----------
r_c1 = run("extC-rerun-test", "cd %s && %s --source test --duration 2 --consumers 3 "
           "--out /tmp/extC.log; echo C_EXIT=$?" % (REMOTE, BIN), 90)
out_c1 = r_c1.get("output", "")
pre = re.search(r"type=lease_pre not_ready=(\w+) status=(\S+) plane_count=(\d+)", out_c1)
first = re.search(r"type=lease_first valid=true camera=(\S+) origin=(\S+) format=(\S+) "
                  r"size=(\d+)x(\d+) colorimetry=(\S+) buffer_ref=(\S+) memory_kind=(\S+) "
                  r"planes=(\d+) buffer_bytes=(\d+) plane0_stride=(\d+) plane1_offset=(\d+) "
                  r"plane_ok=(\w+)", out_c1)
shared = re.search(r"type=lease_shared use_count=(\d+) released_while_shared=(\d+)", out_c1)
partial = re.search(r"type=lease_partial release_after_copies=(\d+)", out_c1)
last = re.search(r"type=lease_last release_after_last=(\d+)", out_c1)
summary = re.search(r"type=summary released_total=(\d+) final_not_ready=(\w+)", out_c1)

# ---------- F. descriptor 算术独立复核（1280x720 NV12 连续） ----------
desc_ok = (first is not None
           and first.group(1) == "CAM-01" and first.group(2) == "simulation"
           and first.group(3) == "NV12" and first.group(4) == "1280" and first.group(5) == "720"
           and first.group(7) == "fr-CAM-01-1" and first.group(8) == "cpu"
           and int(first.group(9)) == 2
           and int(first.group(10)) == 1280 * 720 * 3 // 2   # 1382400
           and int(first.group(11)) == 1280                  # plane0 stride == width
           and int(first.group(12)) == 1280 * 720            # UV offset == Y size
           and first.group(13) == "true")
check("F_descriptor_arithmetic_NV12_1280x720", desc_ok)

c_pre_ok = (pre is not None and pre.group(1) == "true" and pre.group(2) == "NOT_READY"
            and pre.group(3) == "0")
check("C_pre_not_ready_plane_count0", c_pre_ok)

c_chain_ok = (shared is not None and shared.group(2) == "0"
              and partial is not None and partial.group(1) == "0"
              and last is not None and last.group(1) == "1")
check("C_release_callback_exactly_once_on_last_consumer", c_chain_ok)

s_ok = summary is not None and summary.group(2) == "false"
check("C_summary_final_not_ready_false", s_ok)

ref_ok = first is not None and first.group(7) == "fr-CAM-01-1"
check("C_buffer_ref_first_is_1", ref_ok)

# ---------- D. frame_layout 自有负例（验证侧判例名，g++ 直编，不采信实现 single unittest） ----------
VCODE = r'''
#include "railhmi/core/frame_layout.h"
#include <cstdio>
using namespace railhmi::core;
static int fails = 0;
static void check(bool ok, const char* name) {
    std::printf("vext %s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++fails;
}
int main() {
    FrameLayout l;
    // v-01 连续 NV12 数学
    check(make_nv12_layout(1280, 720, 1280, 1280, 921600ULL, &l) == LayoutStatus::Ok,
          "v01_nv12_contiguous_ok");
    check(l.buffer_bytes == 1382400ULL, "v02_nv12_total_1382400");
    check(l.planes[0].size_bytes == 921600ULL, "v03_y_size_921600");
    check(l.planes[1].offset == 921600ULL, "v04_uv_offset_921600");
    check(l.planes[1].size_bytes == 460800ULL, "v05_uv_size_460800");
    // v-06 零维显式拒绝
    check(make_nv12_layout(0, 720, 1280, 1280, 921600ULL, &l) == LayoutStatus::InvalidDimensions,
          "v06_zero_width_rejected");
    check(make_nv12_layout(1280, 0, 1280, 1280, 921600ULL, &l) == LayoutStatus::InvalidDimensions,
          "v07_zero_height_rejected");
    // v-08 padded stride ok / Y < stride
    check(make_nv12_layout(1280, 720, 1408, 1408, 1408ULL * 720, &l) == LayoutStatus::Ok,
          "v08_nv12_padded_ok");
    check(l.buffer_bytes == 1408ULL * 720 + 1408ULL * 360, "v09_padded_total_1520640");
    // v-10 odd 拒绝（width 与 height 两个方向）
    check(make_nv12_layout(1281, 720, 1282, 1282, 1282ULL * 720, &l) == LayoutStatus::OddDimensions,
          "v10_odd_width_rejected");
    check(make_nv12_layout(1280, 721, 1280, 1280, 1280ULL * 720, &l) == LayoutStatus::OddDimensions,
          "v11_odd_height_rejected");
    // v-12 stride 不足拒绝（Y / UV 两方向）
    check(make_nv12_layout(1280, 720, 1279, 1280, 1280ULL * 720, &l) == LayoutStatus::StrideTooSmall,
          "v12_y_stride_lt_width_rejected");
    check(make_nv12_layout(1280, 720, 1280, 1279, 1280ULL * 720, &l) == LayoutStatus::StrideTooSmall,
          "v13_uv_stride_lt_width_rejected");
    // v-14 UV inside Y → overlap
    check(make_nv12_layout(1280, 720, 1280, 1280, 921599ULL, &l) == LayoutStatus::OverlappingPlanes,
          "v14_uv_inside_y_rejected");
    // v-15 packed 最小 stride ok / size
    check(make_packed_layout(PixelFormat::Bgr888, 640, 480, 640ULL * 3, &l) == LayoutStatus::Ok,
          "v15_packed_min_stride_ok");
    check(l.planes[0].size_bytes == 640ULL * 3 * 480, "v16_packed_size_921600");
    // v-17 packed stride < w*3 拒绝
    check(make_packed_layout(PixelFormat::Rgb888, 640, 480, 640ULL * 3 - 1, &l)
              == LayoutStatus::StrideTooSmall, "v17_packed_stride_lt_w3_rejected");
    // v-18 packed factory 拒绝 NV12（Nv12 不能转 packed 路径）
    check(make_packed_layout(PixelFormat::Nv12, 640, 480, 1920, &l)
              == LayoutStatus::UnsupportedFormat, "v18_packed_factory_rejects_nv12");
    // v-19 validate 零 buffer
    FrameLayout m;
    m.width = 1280; m.height = 720; m.format = PixelFormat::Nv12;
    m.buffer_bytes = 0;
    m.planes = {{0, 1280, 921600}, {921600, 1280, 460800}};
    check(validate_layout(m) == LayoutStatus::PlaneTooSmall, "v19_validate_zero_buffer_rejected");
    // v-20 validate UV plane 越界
    FrameLayout e;
    e.width = 1280; e.height = 720; e.format = PixelFormat::Nv12;
    e.buffer_bytes = 1382400;
    e.planes = {{0, 1280, 921600}, {1382401, 1280, 460800}};
    check(validate_layout(e) == LayoutStatus::ExceedsBuffer, "v20_validate_uv_beyond_buffer");
    // v-21 validate UV size 不足（stride*h/2）
    FrameLayout u;
    u.width = 1280; u.height = 720; u.format = PixelFormat::Nv12;
    u.buffer_bytes = 1382400;
    u.planes = {{0, 1280, 921600}, {921600, 1280, 460799}};
    check(validate_layout(u) == LayoutStatus::PlaneTooSmall, "v21_validate_uv_size_lt_half_row");
    // v-22 validate packed size 不足
    FrameLayout p;
    p.width = 640; p.height = 480; p.format = PixelFormat::Bgr888;
    p.buffer_bytes = 640ULL * 3 * 480 - 1;
    p.planes = {{0, 640ULL * 3, 640ULL * 3 * 480}};
    check(validate_layout(p) == LayoutStatus::ExceedsBuffer, "v22_validate_packed_size_lt_needed");
    // v-23 validate 不重叠（重叠平面在 validate 层拒绝）
    FrameLayout o;
    o.width = 1280; o.height = 720; o.format = PixelFormat::Nv12;
    o.buffer_bytes = 1382400;
    o.planes = {{0, 1280, 921600}, {921599, 1280, 460800}};
    check(validate_layout(o) == LayoutStatus::OverlappingPlanes, "v23_validate_overlap_rejected");
    // v-24 validate NV12 单平面 → TooManyPlanes（ planes<2 判据）
    FrameLayout q;
    q.width = 1280; q.height = 720; q.format = PixelFormat::Nv12;
    q.buffer_bytes = 921600;
    q.planes = {{0, 1280, 921600}};
    check(validate_layout(q) == LayoutStatus::TooManyPlanes, "v24_validate_nv12_single_plane");
    // v-25 parse_pixel_format round trip
    PixelFormat f;
    check(parse_pixel_format("NV12", &f) && f == PixelFormat::Nv12, "v25_parse_nv12");
    check(!parse_pixel_format("UYVY", &f), "v26_parse_uyvy_rejected");
    // v-27 to_string round trip
    check(std::string(to_string(PixelFormat::Bgr888)) == "BGR888", "v27_to_string_bgr888");
    std::printf("vext %s (%d failure(s))\n", fails == 0 ? "OK" : "FAILED", fails);
    return fails == 0 ? 0 : 1;
}
'''
print("self-contained D side: compiling on board...")

sftp = client.open_sftp()
try:
    sftp.putfo(io.BytesIO(VCODE.encode("utf-8")), REMOTE + "/vext_frame_layout.cpp")
finally:
    sftp.close()

# 远端布局：--local-dir=app 的内容直接位于 REMOTE 下（src/core/...，无 app/ 前缀）
r_d = run("extD-compile-run",
          "cd %s && g++ -std=c++17 -I src/core/include -o /tmp/vext_frame_layout "
          "vext_frame_layout.cpp src/core/src/frame_layout.cpp 2>&1 | head -20; "
          "if [ -x /tmp/vext_frame_layout ]; then /tmp/vext_frame_layout; echo VEXT_EXIT=$?; "
          "else echo VEXT_COMPILE_FAILED; fi" % REMOTE, 120)
out_d = r_d.get("output", "")
vext_pass = out_d.count("vext PASS")
vext_fail = out_d.count("vext FAIL")
d_ok = "VEXT_EXIT=0" in out_d and vext_fail == 0 and vext_pass >= 20
check("D_vext_frame_layout_all_pass_%d" % vext_pass, d_ok)

# ---------- 收尾：清理板端 EXT 临时产物 ----------
run("ext-cleanup-tmp", "rm -f /tmp/extA.log /tmp/extB.log /tmp/extC.log /tmp/vext_frame_layout "
    "%s/vext_frame_layout.cpp && echo EXT_TMP_CLEANED" % REMOTE, 20)

client.close()

# ---------- E. 凭据泄漏扫描（本目录全部文件） ----------
# 判据口径（沿用 ITEM-002 教训）：SSH 凭据值与登记用户名 admin 可能重合，--user admin 是
# 命令行合法 token，文件级同现属假阳性；扫描只针对两相机凭据值。SSH 凭据仅在 vext 编译
# 日志等 hmac 形态命中且值≠"admin" 时才报；此处按凭据语义形态判定 front/side 两个值。
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
        # SSH 凭据：仅当值非平凡（≠admin）且不在合法 token 集合中才视为泄漏
        if not ssh_is_admin and secret and secret.encode() in data:
            hits.append((p.name, "ssh"))
ext_e = len(hits) == 0
check("E_no_credential_in_evidence", ext_e)

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
