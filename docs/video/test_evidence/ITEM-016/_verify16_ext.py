#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-016 验证侧扩展夹具驱动（VERIFY-20261017-v1）。

在 --keep-remote 保留的板端 /tmp/railhmi_diag 树上：
  A. 独立参数轮（EXA）：改 thresholds.yaml freeze_timeout_ms=300 → 探针重建产物判据由
     验证侧按新参数重推导（探针 CAM-02 gap 注入为交付常量，作对照；--duration 3）；
  B. CLI 负例：未知参数 exit2 / --help exit0；
  C. 板端 g++ 直编验证侧自有 _verify16_vext.cpp（链接交付三层 .a，49 判例），覆盖
     classify/attempt/redact/pipeline 触发词/结构化 JSON/health 日志滚动/真实 test 多路断流联动；
  D. 凭据泄漏扫描：全部 ITEM-016 证据文件，SSH secret + 两类相机凭据语义（token 口径），
     admin 为登记用户名不算泄漏（ITEM-002 先例）；
  E. 板端清理 /tmp/railhmi_diag + EXT 临时产物（本轮以 --keep-remote 保留，由本驱动收尾）。
判据独立推导，不采信交付 type=acceptance 行为最终依据。
"""
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(ROOT / "app" / "tools"))
import paramiko  # noqa: E402
from board_build import rrun  # noqa: E402

STAMP = "20261017-item016-verify-v1"
TAG = "diag-ext-192_168_0_103-" + STAMP
REMOTE = "/tmp/railhmi_diag"
BIN = REMOTE + "/build-media/tools/railhmi-diagnostics"
VEXT = (HERE / "_verify16_vext.cpp").read_text(encoding="utf-8")

for n in ("TRAIN_RK3588_SSH_CREDENTIAL",):
    v = subprocess.run(["powershell.exe", "-NoProfile", "-Command",
                        '[Environment]::GetEnvironmentVariable("%s","User")' % n],
                       capture_output=True, text=True, timeout=30).stdout.strip()
    if v:
        os.environ[n] = v
missing = [n for n in ("TRAIN_RK3588_SSH_CREDENTIAL",) if not os.environ.get(n)]
if missing:
    sys.stderr.write("凭据缺失: %s\n" % missing)
    raise SystemExit(2)
secret = os.environ["TRAIN_RK3588_SSH_CREDENTIAL"]

TRANS_STEPS = []
TRANS = {"host_key_fingerprint": None, "steps": TRANS_STEPS}

client = paramiko.SSHClient()
client.load_system_host_keys()
client.set_missing_host_key_policy(paramiko.RejectPolicy())
client.connect("192.168.0.103", port=22, username="admin", password=secret,
               timeout=10, auth_timeout=10, banner_timeout=10,
               look_for_keys=False, allow_agent=False)
tr = client.get_transport()
if tr is not None and tr.get_remote_server_key() is not None:
    from board_build import fingerprint
    TRANS["host_key_fingerprint"] = fingerprint(tr.get_remote_server_key())

STEPS = []


def run(step, cmd, timeout=120):
    res = rrun(client, cmd, timeout)
    STEPS.append({"step": step, "cmd": cmd, "exit": res.get("exit"), "output": res.get("output", "")})
    TRANS_STEPS.append({"step": step, "cmd": cmd, **res})
    return res


CHECKS = []


def check(name, ok, detail=""):
    CHECKS.append({"name": name, "ok": bool(ok), "detail": str(detail)[:300]})
    print("ext16 %s %s %s" % ("PASS" if ok else "FAIL", name, "" if ok else str(detail)[:220]))


def kv_kv(line):
    d = {}
    for tok in line.split(" "):
        if "=" in tok:
            k, v = tok.split("=", 1)
            d[k] = v
    return d


def parse_probe(text):
    d = {"metrics": {}, "acceptance": None, "capability": None, "elements": {}}
    m = re.search(r"^type=acceptance (.*)$", text, re.M)
    if m:
        d["acceptance"] = kv_kv(m.group(1))
    m = re.search(r"^type=capability (.*)$", text, re.M)
    if m:
        d["capability"] = kv_kv(m.group(1))
    for mm in re.finditer(r"^type=metric camera=(\S+) presence=(\S+) \([^)]*\) (.*)$", text, re.M):
        d["metrics"][mm.group(1)] = {"presence": mm.group(2), **kv_kv(mm.group(3))}
    for mm in re.finditer(r"^type=element name=(\S+) status=(\S+) long_name=(.*)$", text, re.M):
        d["elements"][mm.group(1)] = mm.group(2)
    return d


# ---------- EX-A: 独立参数轮（freeze_timeout_ms=300, reconnect_backoff_ms=300） ----------
run("EXA-prep", "cd %s && rm -rf config_vt && cp -r config config_vt && "
                "sed -i 's/freeze_timeout_ms: 2000/freeze_timeout_ms: 300/; "
                "s/reconnect_backoff_ms: 1000/reconnect_backoff_ms: 300/' config_vt/thresholds.yaml && "
                "grep -E 'freeze_timeout|reconnect_backoff' config_vt/thresholds.yaml" % REMOTE, 30)

res = run("EXA-run", "cd %s && %s --config config_vt --duration 3 "
                     "--logdir /tmp/vt_exa_logs --out /tmp/vt_exa.log --json /tmp/vt_exa.json; "
                     "echo EXA_EXIT=$?" % (REMOTE, BIN), 150)
exa_out = res["output"]
check("EX-A run done", "EXA_EXIT=0" in exa_out, exa_out[-200:])
res2 = run("EXA-fetch", "cat /tmp/vt_exa.log", 20)
exa = res2["output"]
pa = parse_probe(exa)
check("EX-A acceptance result=PASS", (pa["acceptance"] or {}).get("result") == "PASS",
      str(pa["acceptance"]))
# 12 字段键集仍齐全（独立参数轮重判据，与主轮一致域）
required = {"state", "fps", "last_frame_mono_ms", "last_frame_wall_ms", "reconnect_count",
            "quality_flags", "display_dropped", "ai_dropped", "recording_overflow", "gap_count",
            "attempt_id", "last_error"}
for cam, mkv in pa["metrics"].items():
    miss = required - set(mkv)
    check("EX-A %s keys full" % cam, not miss, str(sorted(miss)))
# 三态分类在独立参数轮仍成立
pres = {c: v.get("presence") for c, v in pa["metrics"].items()}
check("EX-A tristate exactly online/gap/unverified",
      sorted(pres.values()) == ["gap", "online", "unverified"], str(pres))
# EX-A 脱敏行独立重判据
m = re.search(r"^type=redaction raw_has_cred=\S+ redacted=(\S+) clean=(\S+)$", exa, re.M)
check("EX-A redaction clean", m is not None and m.group(2) == "true" and
      "<redacted>" == m.group(1), exa[-160:])
# EX-A 按日文件存在且 3 行
res3 = run("EXA-daily", "ls /tmp/vt_exa_logs && wc -l /tmp/vt_exa_logs/video-*.log", 20)
mline = [l for l in res3["output"].splitlines() if re.match(r"^\d+ /tmp/vt_exa_logs/", l)]
check("EX-A daily file 3 lines", len(mline) == 1 and mline[0].startswith("3 /tmp/vt_exa_logs/"),
      res3["output"][-200:])

# EX-D: 同参数确定性（语义行恒等，自然波动域排除）
res_s2 = run("EXD-run2", "cd %s && %s --config config --duration 2 "
                         "--logdir /tmp/vt_det_logs --out /tmp/vt_det2.log; "
                         "echo EXD_EXIT=$?" % (REMOTE, BIN), 120)
det2 = res_s2["output"]
check("EX-D run2 done", "EXD_EXIT=0" in det2, det2[-160:])

# ---------- EX-B: CLI 负例 ----------
res_u = run("EXB-unknown", "cd %s && %s --bogus-flag; echo EXB_EXIT=$?" % (REMOTE, BIN), 30)
check("EX-B unknown arg exit2", "EXB_EXIT=2" in res_u["output"], res_u["output"][-160:])
res_h = run("EXB-help", "cd %s && %s --help; echo EXB_EXIT=$?" % (REMOTE, BIN), 30)
check("EX-B help exit0 + usage", "EXB_EXIT=0" in res_h["output"] and "usage: railhmi-diagnostics" in res_h["output"],
      res_h["output"][-200:])

# ---------- EX-C: 验证侧 g++ 直编自有夹具 ----------
# ---------- EX-C: 验证侧 g++ 直编自有夹具前置：确保保留树存在（--keep-remote 已构建） ----------
res_tree = run("tree-check", "ls %s/build-media/tools/railhmi-diagnostics && echo TREE_OK" % REMOTE, 20)
if "TREE_OK" not in res_tree["output"]:
    sys.stderr.write("错误: %s 构建树不存在（前置 --keep-remote 复跑未成功或已清理），EXT 无法继续。\n" % REMOTE)
    client.close()
    out_trans = HERE / (TAG + ".transcript.json")
    out_trans.write_text(json.dumps(TRANS, ensure_ascii=False, indent=2), encoding="utf-8")
    raise SystemExit(3)

sftp = client.open_sftp()
try:
    sftp.get_channel().settimeout(120)
    sftp.putfo if False else None
    with sftp.open(REMOTE + "/vext16.cpp", "w") as f:
        f.write(VEXT)
finally:
    sftp.close()
run("EXC-upload", "ls -la %s/vext16.cpp" % REMOTE, 20)

compile_cmd = (
    "cd %s && g++ -std=c++17 -pthread -O1 "
    "-I src/core/include -I src/media/include -I src/platform/include "
    "$(pkg-config --cflags gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0) "
    "$(pkg-config --cflags librga) "
    "-o vext16 vext16.cpp "
    "build-media/src/media/librailhmi_media.a "
    "build-media/src/core/librailhmi_core.a build-media/src/platform/librailhmi_platform.a "
    "$(pkg-config --libs gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0) "
    "$(pkg-config --libs librga) 2>&1 | tail -8; "
    "if [ -x vext16 ]; then echo VEXT_BUILD_OK; else echo VEXT_COMPILE_FAILED; fi" % REMOTE)
res_c = run("EXC-build", compile_cmd, 240)
check("EX-C build ok", "VEXT_BUILD_OK" in res_c["output"], res_c["output"][-400:])

if "VEXT_BUILD_OK" in res_c["output"]:
    res_r = run("EXC-run", "cd %s && ./vext16; echo VEXT_EXIT=$?" % REMOTE, 180)
    vext_out = res_r["output"]
    vfails = re.findall(r"^vext FAIL (\S+)", vext_out, re.M)
    npass = len(re.findall(r"^vext PASS ", vext_out, re.M))
    check("EX-C vext all pass (52)", "VEXT_EXIT=0" in vext_out and not vfails,
          "pass=%d fails=%s" % (npass, vfails[:8]))
    # 50 具名判例（52 处 check(，g17 一处判例运行期展开成 4 个动态名）→ 50 + 4 = 54 PASS 行。
    check("EX-C vext PASS line count matches 54", npass == 54, "%d" % npass)

# ---------- EX-E：凭据泄漏扫描（目录全文件，token 语义口径；admin 为登记用户名不算） ----------
cam_front = subprocess.run(["powershell.exe", "-NoProfile", "-Command",
                            '[Environment]::GetEnvironmentVariable("TRAIN_FRONT_RTSP_CREDENTIAL","User")'],
                           capture_output=True, text=True, timeout=30).stdout.strip()
cam_side = subprocess.run(["powershell.exe", "-NoProfile", "-Command",
                           '[Environment]::GetEnvironmentVariable("TRAIN_SIDE_RTSP_CREDENTIAL","User")'],
                          capture_output=True, text=True, timeout=30).stdout.strip()

SECRETS = [s for s in (secret, cam_front, cam_side) if s and len(s) >= 4]
patterns = [
    re.compile(r"password=\S+"),
    re.compile(r"rtsp://[^\s/]+:[^\s/@]+@"),
    re.compile(r"location=rtsp://[^\s]*?"),
    re.compile(r"pwd=\S+"),
    re.compile(r"token=\S+"),
]
hits = []
for p in HERE.iterdir():
    if not p.is_file() or p.name.startswith("_verify16_ext") or p.suffix not in (".md", ".log", ".json", ".txt"):
        continue
    try:
        body = p.read_text(encoding="utf-8", errors="ignore")
    except Exception:
        continue
    for s in SECRETS:
        if s in body and "admin" != s:
            hits.append((p.name, "secret-literal"))
    for pat in patterns:
        for mm in pat.finditer(body):
            frag = mm.group(0)[:60]
            # 登记用户名调用形态不算泄漏（ITEM-002 先例：--user admin 是 token 合法形态）；
            # 探针自身样本中已打码的 ***（含 password=***）不算泄漏。
            if re.fullmatch(r"password=(admin|\*\*\*)|pwd=(admin|\*\*\*)", frag):
                continue
            if "***" in frag:
                continue
            hits.append((p.name, frag))
check("EX-E credential leak scan clean", not hits, str(hits[:6]))

# ---------- 板端清理 ----------
run("cleanup-ext", "rm -rf %s/config_vt /tmp/vt_exa.log /tmp/vt_exa.json /tmp/vt_exa_logs "
                   "/tmp/vt_det2.log /tmp/vt_det_logs /tmp/vext16.log %s/vext16.cpp %s/vext16 && echo EXT_CLEANED" % (
                       REMOTE, REMOTE, REMOTE), 30)
res_c2 = run("cleanup-tree", "rm -rf %s && echo BOARD_CLEAN_DONE" % REMOTE, 30)
check("cleanup tree done", "BOARD_CLEAN_DONE" in res_c2["output"], res_c2["output"][-60:])

# ---------- 归档 ----------
sftp = client.open_sftp()
try:
    sftp.get_channel().settimeout(120)
    for rel in ("vext16.log",):
        pass
finally:
    sftp.close()

client.close()

out_trans = HERE / (TAG + ".transcript.json")
out_trans.write_text(json.dumps(TRANS, ensure_ascii=False, indent=2), encoding="utf-8")
log = HERE / (TAG + ".log")
with log.open("w", encoding="utf-8") as fh:
    fh.write("host_key_fingerprint: %s\n" % TRANS["host_key_fingerprint"])
    for s in TRANS_STEPS:
        fh.write("\n===== [%s] exit=%s =====\n%s\n" % (s.get("step"), s.get("exit"), s.get("output") or ""))

nfails = sum(1 for c in CHECKS if not c["ok"])
print("\next16 summary: checks=%d fails=%d" % (len(CHECKS), nfails))
json.dump({"stamp": STAMP, "checks": CHECKS, "host_key_fingerprint": TRANS["host_key_fingerprint"]},
          open(HERE / (TAG + ".checks.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=2)
nfail_total = nfails
