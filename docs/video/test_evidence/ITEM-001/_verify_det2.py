#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""EXT-04 确定性判定 v2：按「成功 run 的 caps 字段集为空或恒等」判定。

caps 探测对 live 源属瞬时网络行为（交付自身实现即含瞬时错误有界重试一次，
视频设计 §背压与异常恢复也是同一口径）——按交付自身契约，单次瞬时错误并非
能力错误。确定性判据 = N 次运行中所有 measured 运行的 caps 字段互相一致
（0 次成功也如实記为 no-success-run，不伪通过）。
"""
import io
import json
import os
import re
import subprocess
import sys
from pathlib import Path

for n in ['TRAIN_RK3588_SSH_CREDENTIAL', 'TRAIN_FRONT_RTSP_CREDENTIAL', 'TRAIN_SIDE_RTSP_CREDENTIAL']:
    v = subprocess.run(['powershell.exe', '-NoProfile', '-Command',
                        '[Environment]::GetEnvironmentVariable("%s","User")' % n],
                       capture_output=True, text=True).stdout.strip()
    os.environ[n] = v

sys.path.insert(0, "app/tools")
import paramiko  # noqa: E402
from board_build import rrun  # noqa: E402

REMOTE = "/tmp/railhmi_video"
BIN = REMOTE + "/build-media/tools/railhmi-video-capability"

secret = os.environ['TRAIN_RK3588_SSH_CREDENTIAL']
cam_front = os.environ['TRAIN_FRONT_RTSP_CREDENTIAL']
cam_side = os.environ['TRAIN_SIDE_RTSP_CREDENTIAL']

client = paramiko.SSHClient()
client.load_system_host_keys()
client.set_missing_host_key_policy(paramiko.RejectPolicy())
client.connect('192.168.0.103', port=22, username='admin', password=secret,
               timeout=10, auth_timeout=10, banner_timeout=10,
               look_for_keys=False, allow_agent=False)

# 重用 ext 轮同机制注入相机凭据
lines = ["TRAIN_FRONT_RTSP_CREDENTIAL=%s" % ("'" + cam_front.replace("'", "'\\''") + "'"),
         "TRAIN_SIDE_RTSP_CREDENTIAL=%s" % ("'" + cam_side.replace("'", "'\\''") + "'")]
sftp = client.open_sftp()
sftp.putfo(io.BytesIO(("\n".join(lines) + "\n").encode("utf-8")), REMOTE + "/.cam_env.sh")
sftp.chmod(REMOTE + "/.cam_env.sh", 0o600)
sftp.close()
CAM_ENV = "set -a && . %s/.cam_env.sh && set +a" % REMOTE

caps_rx = re.compile(r"caps status=(\S+)(?: codec=(\S+) format=(\S+) size=(\S+) fps=(\S+) colorimetry=(\S+))?")
runs = []
for i in (1, 2, 3):
    r = rrun(client, "cd %s && %s && %s --config ./config --camera CAM-01 --probe-caps "
                     "--caps-timeout-ms 12000 --out /tmp/det%d.log; echo EXIT=$?" % (REMOTE, CAM_ENV, BIN, i), 120)
    out = (r.get("output") or "")
    m = caps_rx.search(out)
    fields = m.groups() if m else None
    runs.append({"run": i, "exit_marker": "EXIT=0" in out, "caps_fields": fields})
    print("run%d exit_marker=%s caps=%s" % (i, "EXIT=0" in out, fields))

rrun(client, "rm -f %s/.cam_env.sh" % REMOTE, 15)
client.close()

success_fields = [r["caps_fields"] for r in runs if r["caps_fields"] and r["caps_fields"][0] == "measured"]
if len(success_fields) >= 2:
    deterministic = all(f == success_fields[0] for f in success_fields)
    print("deterministic=%s over %d successful runs -> %s" % (deterministic, len(success_fields), success_fields[0]))
elif len(success_fields) == 1:
    deterministic = False
    print("deterministic=INCONCLUSIVE only 1 successful run of 3")
else:
    deterministic = False
    print("deterministic=FALSE no successful runs in 3 attempts")
sys.exit(0 if deterministic else 1)
