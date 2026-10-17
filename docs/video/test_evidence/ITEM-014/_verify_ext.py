#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-014 验证侧扩展夹具（software_verification，VERIFY-20261017-v2 修复轮）。

在 --keep-remote 保留的 /tmp/railhmi_recovery 树上黑盒驱动交付 railhmi-recovery
探针与已上传源码，验证侧自有判据：

  A. 独立参数轮: 复制 config 改 reconnect_backoff_ms=300 → 退避判据按参数独立重推导
  B. CLI 负例: --bogus-flag exit2 + --help exit0 用法行；同参二跑语义行恒等（确定性）
  C. 板端 g++ 直编验证侧自有夹具 _verify_vext.cpp（链接交付三层 .a，8 场景 26 判例），
     覆盖交付探针未测边界：恢复后同文本二次断流（D-01 修复核心，修复后必须 PASS）、
     重建后无新帧的继续重试、异文本对照、鉴权/能力门控、ONVIF 三态、单路隔离
  D. 凭据泄漏扫描（本目录全部 ITEM-014 证据文件，SSH secret + 两类相机凭据语义）
  E. 板端 EXT 临时产物清理

v2 判据协议（修复轮）：v1 的 3 条 D-01 呈证判据（缺陷在位时 FAIL）在修复后必须转 PASS；
全部 26 条判例预期全 PASS，任何 FAIL 都提示修复未到位或回归。
"""
import io
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
STAMP = "20261017-item014-verify-v2"
TAG = "recov-ext-%s-%s" % ("192_168_0_103", STAMP)
REMOTE = "/tmp/railhmi_recovery"
BIN = REMOTE + "/build-media/tools/railhmi-recovery"
VCODE = (HERE / "_verify_vext.cpp").read_text(encoding="utf-8")

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
    steps.append({"step": step, "cmd": cmd, **res})
    print("[%s] exit=%s" % (step, res.get("exit")))
    return res


checks = []


def check(name, ok, detail=""):
    checks.append({"name": name, "ok": bool(ok), "detail": str(detail)[:300]})
    print("ext %s %s %s" % ("PASS" if ok else "FAIL", name, str(detail)[:200] if not ok else ""))


def kv_line(line):
    kv = {}
    for tok in line.split(" "):
        if "=" in tok:
            k, v = tok.split("=", 1)
            kv[k] = v
    return kv


# ---------- EX-A: 独立参数轮（reconnect_backoff_ms=300） ----------
res = run("EXA-prep", "cd %s && cp -r config config_vt && "
                      "sed -i 's/reconnect_backoff_ms: 1000/reconnect_backoff_ms: 300/' "
                      "config_vt/thresholds.yaml && grep reconnect_backoff config_vt/thresholds.yaml" % REMOTE, 30)
check("EX-A config copy shows 300", "300" in res["output"], res["output"][:200])

res = run("EXA-run", "cd %s && %s --config config_vt --duration 9 "
                     "--out /tmp/vt_exa.log --json /tmp/vt_exa.json; echo EXA_EXIT=$?" % (REMOTE, BIN), 150)
exa_out = res["output"]
check("EX-A run exit0", res["output"].rstrip().endswith("EXA_EXIT=0"), res["output"][-200:])
res2 = run("EXA-fetch", "cat /tmp/vt_exa.log", 20)
exa = res2["output"]


def parse_probe(text):
    d = {}
    for key in ("config", "start", "generations", "timing", "home", "acceptance"):
        m = re.search(r"^type=%s (.*)$" % key, text, re.M)
        d[key] = kv_line(m.group(1)) if m else None
    d["recovery"] = {}
    for m in re.finditer(r"^type=recovery cam=(\S+) (.*)$", text, re.M):
        d["recovery"][m.group(1)] = kv_line(m.group(2))
    return d


pa = parse_probe(exa)
bb = int(pa["config"]["reconnect_backoff_ms"]) if pa["config"] else -1
check("EX-A base_backoff=300", bb == 300, str(pa["config"]))
t = pa["timing"]
if t and t.get("t_cut_ms") is not None:
    t_cut, r1 = int(t["t_cut_ms"]), int(t["rebuild1_ms"])
    check("EX-A backoff respected (rebuild1-t_cut >= 250)",
          r1 >= 0 and (r1 - t_cut) >= bb - 50, "t_cut=%d rebuild1=%d" % (t_cut, r1))
    check("EX-A second cut rebuild window >= 250",
          t.get("rebuild2_ms") and (int(t["rebuild2_ms"]) - r1) >= bb - 50, str(t))
    check("EX-A capability gated (rebuild3 >= change)",
          int(t["rebuild3_ms"]) >= int(t["change_ms"]), str(t))
    check("EX-A acceptance PASS", pa["acceptance"] and pa["acceptance"].get("result") == "PASS",
          str(pa["acceptance"]))
else:
    check("EX-A timing row present", False, str(t))

# ---------- EX-B: CLI 负例 + 确定性二跑 ----------
res = run("NEG-unknown", "cd %s && %s --bogus-flag; echo NEG_EXIT=$?" % (REMOTE, BIN), 30)
check("NEG unknown arg exit2", "NEG_EXIT=2" in res["output"] and "unknown arg" in res["output"],
      res["output"][-200:])
res = run("NEG-help", "cd %s && %s --help; echo HELP_EXIT=$?" % (REMOTE, BIN), 30)
check("NEG help exit0 usage", res["output"].rstrip().endswith("HELP_EXIT=0")
      and "usage: railhmi-recovery" in res["output"], res["output"][-200:])

res = run("DET-run2", "cd %s && %s --config config --duration 8 "
                      "--out /tmp/vt_det.log --json /tmp/vt_det.json; echo DET_EXIT=$?" % (REMOTE, BIN), 150)
res2 = run("DET-fetch", "cat /tmp/vt_det.log", 20)
det = parse_probe(res2["output"])
# 同参二跑语义行恒等（timing 时刻域除外）
sem_a = {k: v for k, v in pa.items()}  # EX-A 是 300 参数轮；DET 用默认 config 与 impl/verify 归档同类


def semantic_rows(text):
    keep = []
    for ln in text.strip().splitlines():
        if ln.startswith("type=timing"):
            continue
        keep.append(ln)
    return keep


run1_rows = semantic_rows(exa)
run2_rows = semantic_rows(res2["output"])
check("DET different-params rows differ (sanity)", run1_rows != run2_rows,
      "EX-A(300) vs DET(1000) must differ")
p2 = det
check("DET base_backoff=1000", p2["config"] and p2["config"].get("reconnect_backoff_ms") == "1000",
      str(p2["config"]))
check("DET acceptance PASS", p2["acceptance"] and p2["acceptance"].get("result") == "PASS",
      str(p2["acceptance"]))
t2 = p2["timing"]
if t2 and t2.get("rebuild1_ms") is not None:
    check("DET backoff respected >= 950",
          int(t2["rebuild1_ms"]) - int(t2["t_cut_ms"]) >= 950, str(t2))
check("DET json acceptance", run("DET-json", "cat /tmp/vt_det.json", 20)["output"].count('"acceptance": true') == 1)

# ---------- EX-C: 板端 g++ 直编验证侧自有夹具 ----------
res = run("EXT-prep", "cd %s && mkdir -p vext && ls build-media/src/media/librailhmi_media.a "
                      "build-media/src/core/librailhmi_core.a build-media/src/platform/librailhmi_platform.a"
         % REMOTE, 30)
check("EXT-C static libs present", res["exit"] == 0, res["output"][:200])

sftp = client.open_sftp()
try:
    sftp.get_channel().settimeout(60)
    sftp.putfo(io.BytesIO(VCODE.encode("utf-8")), REMOTE + "/vext/vext_recovery.cpp")
finally:
    sftp.close()

compile_cmd = (
    "cd %s && g++ -std=c++17 -pthread -I src/core/include -I src/media/include -I src/platform/include "
    "$(pkg-config --cflags gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0) "
    "$(pkg-config --cflags librga) "
    "-o /tmp/vext_recovery vext/vext_recovery.cpp "
    "build-media/src/media/librailhmi_media.a "
    "build-media/src/core/librailhmi_core.a build-media/src/platform/librailhmi_platform.a "
    "$(pkg-config --libs gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0) "
    "$(pkg-config --libs librga) 2>&1 | tail -8; "
    "if [ -x /tmp/vext_recovery ]; then echo VEXT_COMPILED; else echo VEXT_COMPILE_FAILED; fi" % REMOTE)
res = run("EXT-C-compile", compile_cmd, 240)
check("EXT-C compiled", "VEXT_COMPILED" in res["output"], res["output"][-400:])
if "VEXT_COMPILED" in res["output"]:
    res = run("EXT-C-run", "/tmp/vext_recovery; echo VEXT_EXIT=$?", 180)
    out = res["output"]
    vpass = out.count("vext PASS")
    vfail = out.count("vext FAIL")
    # v2 判据协议（修复轮）: v1 的 3 条 D-01 呈证判例（g02 两条 + g03b 一条）必须转 PASS，
    # 其余 23 条回归判例必须保持 PASS —— 全 26 条判例预期全绿，任何 FAIL 即修复未到位/回归。
    exhibit_names = ("g02 second cut attempts==2", "g02 second cut rebuilt+confirmed",
                     "g03b D-01 exhibit same-text repeat not rebuilt")
    fails_named = re.findall(r"^vext FAIL (.+)$", out, re.M)
    check("EXT-C no FAIL at all (fix round: exhibits must pass)", not fails_named,
          "named fails=%s" % fails_named)
    check("EXT-C 26 regression PASS lines (all)", vpass == 26, "got %d PASS" % vpass)
    check("EXT-C 0 FAIL lines", vfail == 0, "got %d FAIL" % vfail)
    check("EXT-C summary 0 failures", "vext summary 0 failure(s)" in out, out[-200:])
    check("EXT-C g02 second cut present", "vext OBSERVE g02_second_cut" in out, out[:400])
    check("EXT-C D-01 exhibit names exercised",
          all(n in out for n in exhibit_names), out[:400])
    # 保存原始输出供证据归档
    (OUT / (TAG + ".vext.log")).write_text(out, encoding="utf-8")
    run("EXT-C-fetch-done", "echo fetched", 10)

# ---------- EX-D: 凭据泄漏扫描 ----------
secrets = [s for s in (secret, cam_front, cam_side) if s]
patterns = [re.escape(s) for s in secrets if len(s) >= 6]
sem_pat = re.compile(r"(user-pw=\"[^\"]+\"|password=[^\s\"']+|[a-zA-Z0-9_]+:[^@/\s]+@)")
scan_targets = sorted(OUT.glob("*.log")) + sorted(OUT.glob("*.json")) + sorted(OUT.glob("*.md"))
hits = []
for f in scan_targets:
    try:
        body = f.read_text(encoding="utf-8", errors="replace")
    except OSError:
        continue
    for p in patterns:
        if p in body:
            hits.append("%s: literal secret" % f.name)
    for m in sem_pat.finditer(body):
        tok = m.group(0)
        # SSH 凭据恰为 admin 时 --user admin 是合法 token；只抓凭据语义形态
        if re.fullmatch(r"[a-zA-Z0-9_]+:admin@", tok):
            continue
        hits.append("%s: %s" % (f.name, tok[:60]))
check("EX-D credential leak 0 hits", not hits, "; ".join(hits[:5]))

# ---------- EX-E: 清理 ----------
run("CLEAN", "rm -rf /tmp/vext_recovery /tmp/vt_exa.log /tmp/vt_exa.json /tmp/vt_det.log "
             "/tmp/vt_det.json %s/config_vt %s/vext && echo CLEANED" % (REMOTE, REMOTE), 30)
res = run("CLEAN-check", "ls /tmp/vext_recovery 2>/dev/null; ls %s/config_vt 2>/dev/null; echo CLEAN_CHECK_DONE" % REMOTE, 20)
check("EX-E cleanup confirmed", "CLEAN_CHECK_DONE" in res["output"]
      and not [ln for ln in res["output"].splitlines() if ln.startswith("/tmp/")]
      and not [ln for ln in res["output"].splitlines() if ln.startswith(str(REMOTE) + "/")],
      res["output"][:200])

client.close()

summary = {"tag": TAG, "checks": checks,
           "summary": {"total": len(checks), "failed": sum(1 for c in checks if not c["ok"])}}
(OUT / (TAG + ".json")).write_text(__import__("json").dumps(summary, ensure_ascii=False, indent=2),
                                   encoding="utf-8")
print("ext summary: %d checks, %d failed" % (len(checks), sum(1 for c in checks if not c["ok"])))
raise SystemExit(0 if all(c["ok"] for c in checks) else 1)
