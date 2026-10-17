#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-001 验证侧扩展夹具（software_verification 独立判据）。

在 --keep-remote 保留的 /tmp/railhmi_video 树上黑盒驱动交付入口
railhmi-video-capability：
  EXT-01 元素缺失注入：GST_PLUGIN_SYSTEM_PATH="" → 期望 exit 3 且 stderr
         显式列出缺失元素名；报告 readiness=blocked_element_missing:<el>。
  EXT-02 --camera CAM-99 → 期望 exit 2（请求的相机不在注册表）。
  EXT-03 --config 不存在目录 → 期望 exit 2（配置错误路径）。
  EXT-04 caps 确定性：--camera CAM-01 --probe-caps 两次，相机 caps 行
         （codec/format/size/fps/colorimetry）逐字段一致。
  EXT-05 --exists 假否独立复现：gst-inspect-1.0 --exists mppvideodec 退出码
         vs 直接 gst-inspect-1.0 mppvideodec（设计要求不依赖 --exists）。
  EXT-06 凭据泄漏扫描：全部证据文件中两个相机凭据值 0 命中。
"""
import hashlib
import io
import json
import os
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, "app/tools")
import paramiko  # noqa: E402
from board_build import fingerprint, rrun  # noqa: E402

OUT = Path(__file__).resolve().parent
STAMP = "20261017-item001-verify-v1"
TAG = "video-cap-ext-%s-%s" % ("192_168_0_103", STAMP)
REMOTE = "/tmp/railhmi_video"
BIN = REMOTE + "/build-media/tools/railhmi-video-capability"

for n in ['TRAIN_RK3588_SSH_CREDENTIAL', 'TRAIN_FRONT_RTSP_CREDENTIAL', 'TRAIN_SIDE_RTSP_CREDENTIAL']:
    v = subprocess.run(['powershell.exe', '-NoProfile', '-Command',
                        '[Environment]::GetEnvironmentVariable("%s","User")' % n],
                       capture_output=True, text=True).stdout.strip()
    os.environ[n] = v

secret = os.environ['TRAIN_RK3588_SSH_CREDENTIAL']
cam_front = os.environ['TRAIN_FRONT_RTSP_CREDENTIAL']
cam_side = os.environ['TRAIN_SIDE_RTSP_CREDENTIAL']

client = paramiko.SSHClient()
client.load_system_host_keys()
client.set_missing_host_key_policy(paramiko.RejectPolicy())
client.connect('192.168.0.103', port=22, username='admin', password=secret,
               timeout=10, auth_timeout=10, banner_timeout=10,
               look_for_keys=False, allow_agent=False)

steps = []

def run(step, cmd, timeout=60):
    res = rrun(client, cmd, timeout)
    steps.append({"step": step, **res})
    print("[%s] exit=%s" % (step, res.get("exit")))
    return res

# 相机凭据经 0600 临时文件注入板端进程环境（复用交付运行器机制）；用后即删。
def push_cam_env():
    lines = ["TRAIN_FRONT_RTSP_CREDENTIAL=%s" % ("'" + cam_front.replace("'", "'\\''") + "'"),
             "TRAIN_SIDE_RTSP_CREDENTIAL=%s" % ("'" + cam_side.replace("'", "'\\''") + "'")]
    sftp = client.open_sftp()
    try:
        sftp.putfo(io.BytesIO(("\n".join(lines) + "\n").encode("utf-8")), REMOTE + "/.cam_env.sh")
        sftp.chmod(REMOTE + "/.cam_env.sh", 0o600)
    finally:
        sftp.close()

CAM_ENV = ("set -a && . %s/.cam_env.sh && set +a" % REMOTE)

push_cam_env()

# EXT-01: 元素缺失注入（空系统插件路径 → 全部元素 absent → exit 3；凭据在场 → readiness 按缺元素报）
r1 = run("ext01-element-missing",
         "cd %s && %s && GST_PLUGIN_SYSTEM_PATH= %s --config ./config "
         "--out /tmp/ext01.log; echo EXT01_EXIT=$?" % (REMOTE, CAM_ENV, BIN), 120)
out1 = r1.get("output", "")
ext01_exit = "EXT01_EXIT=3" in out1
ext01_names = bool(re.search(r"required GStreamer elements missing:.*rtspsrc", out1))
ext01_readiness = "blocked_element_missing:" in out1
ext01_cred = "credential=present" in out1
print("EXT01 exit3=%s names=%s readiness=%s cred=%s" % (ext01_exit, ext01_names, ext01_readiness, ext01_cred))

# EXT-02: 非法相机 id（凭据在场与否不影响退出码路径）
r2 = run("ext02-bad-camera",
         "cd %s && %s && %s --config ./config --camera CAM-99; echo EXT02_EXIT=$?" % (REMOTE, CAM_ENV, BIN), 30)
ext02 = "EXT02_EXIT=2" in r2.get("output", "") and "not found" in r2.get("output", "")
print("EXT02 ok=%s" % ext02)

# EXT-03: 配置目录不存在
r3 = run("ext03-bad-config",
         "cd %s && %s --config ./config_missing_dir; echo EXT03_EXIT=$?" % (REMOTE, BIN), 30)
ext03 = "EXT03_EXIT=2" in r3.get("output", "")
print("EXT03 ok=%s" % ext03)

# EXT-04: caps 确定性（CAM-01 两次，凭据在场）
r4a = run("ext04-caps-run1",
          "cd %s && %s && %s --config ./config --camera CAM-01 --probe-caps --caps-timeout-ms 12000 "
          "--out /tmp/ext04a.log; echo EXT04A_EXIT=$?" % (REMOTE, CAM_ENV, BIN), 120)
r4b = run("ext04-caps-run2",
          "cd %s && %s && %s --config ./config --camera CAM-01 --probe-caps --caps-timeout-ms 12000 "
          "--out /tmp/ext04b.log; echo EXT04B_EXIT=$?" % (REMOTE, CAM_ENV, BIN), 120)
r4h = run("ext04-diff",
          "diff /tmp/ext04a.log /tmp/ext04b.log; echo EXT04_DIFF_EXIT=$?", 20)
out4a = r4a.get("output", "")
caps_line = re.search(r"caps status=measured codec=(\S+) format=(\S+) size=(\S+) fps=(\S+) colorimetry=(\S+)",
                      out4a)
caps_fields = caps_line.groups() if caps_line else None
ext04 = ("EXT04A_EXIT=0" in out4a and "EXT04B_EXIT=0" in r4b.get("output", "")
         and "EXT04_DIFF_EXIT=0" in r4h.get("output", "") and caps_fields is not None)
print("EXT04 deterministic=%s caps=%s" % (ext04, caps_fields))

# 收尾：删除板端凭据临时文件并确认
run("ext-camenv-cleanup", "rm -f %s/.cam_env.sh && ls %s/.cam_env.sh 2>&1; echo CLEANUP_DONE" % (REMOTE, REMOTE), 20)

# EXT-05: --exists 假否独立复现
r5 = run("ext05-exists-false-negative",
         "gst-inspect-1.0 --exists mppvideodec >/dev/null 2>&1; echo EXISTS_EXIT=$?; "
         "gst-inspect-1.0 mppvideodec 2>/dev/null | sed -n '1p'; echo DIRECT_DONE", 30)
out5 = r5.get("output", "")
exists_exit = re.search(r"EXISTS_EXIT=(\d+)", out5)
direct_ok = "Factory Details" in out5
ext05 = exists_exit is not None and exists_exit.group(1) != "0" and direct_ok
print("EXT05 exists_exit=%s direct_present=%s" % (exists_exit.group(1) if exists_exit else None, direct_ok))

client.close()

# EXT-06: 凭据泄漏扫描（本机全部 ITEM-001 证据文件）
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
ext06 = len(hits) == 0
print("EXT06 credential_hits=%s ok=%s" % (hits, ext06))

checks = [
    ("EXT01_element_missing_exit3_names_readiness", ext01_exit and ext01_names and ext01_readiness and ext01_cred),
    ("EXT02_bad_camera_exit2", ext02),
    ("EXT03_bad_config_exit2", ext03),
    ("EXT04_caps_deterministic", ext04),
    ("EXT05_exists_false_negative_reproduced", ext05),
    ("EXT06_no_credential_in_evidence", ext06),
]
fails = 0
for name, ok in checks:
    print("ext %s %s" % ("PASS" if ok else "FAIL", name))
    fails += 0 if ok else 1
print("ext OK (%d failure(s))" % fails if fails == 0 else "ext FAILED (%d failure(s))" % fails)

(OUT / (TAG + ".json")).write_text(json.dumps({
    "stamp": STAMP, "steps": steps,
    "caps_fields_run1": list(caps_fields) if caps_fields else None,
    "credential_hits": hits,
    "checks": [{"name": n, "ok": bool(o)} for n, o in checks],
    "failures": fails,
}, ensure_ascii=False, indent=2), encoding="utf-8")
sys.exit(0 if fails == 0 else 1)
