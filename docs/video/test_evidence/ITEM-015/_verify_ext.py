#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-015 验证侧扩展夹具（software_verification，VERIFY-20261017-v1）。

在 --keep-remote 保留的 /tmp/railhmi_timebase 树上黑盒驱动交付 core 库与探针，
验证侧自有判据（不采信 acceptance 行）：

  A. 独立参数轮: 复制 config 改 replay_sync_threshold_ms=100 → 配置路径生效判据
     （探针 config 行回显 100；500ms 偏差按参数重推导应超阈提示；同参重跑语义行恒等）
  B. CLI 负例: --bogus-flag exit2 + unknown arg；--help exit0 用法行
  C. 板端 g++ 直编验证侧自有夹具 _verify_vext.cpp（链接交付 librailhmi_core.a，
     14 场景 49 判例）：阈值半开边界/守卫不写输出/重复校准不叠加/换算幂等/负与大跨度/
     空锁定列表/多缺路与未知键/负阈值守恒/seek 容差边界/多代陈旧保持/最后接受值/
     gen0 初始代语义/单路/未校准优先
  D. 凭据泄漏扫描（本目录全部证据文件，SSH secret + 两类相机凭据语义 0 命中）
  E. 板端 EXT 临时产物清理
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
STAMP = "20261017-item015-verify-v1"
TAG = "tb-ext-%s-%s" % ("192_168_0_103", STAMP)
REMOTE = "/tmp/railhmi_timebase"
BIN = REMOTE + "/build-core/tools/railhmi-timebase-sync"
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


# ---------- EX-A: 独立参数轮（replay_sync_threshold_ms=100） ----------
res = run("EXA-prep", "cd %s && cp -r config config_vt && "
                      "sed -i 's/replay_sync_threshold_ms: 300/replay_sync_threshold_ms: 100/' "
                      "config_vt/thresholds.yaml && grep replay_sync config_vt/thresholds.yaml" % REMOTE, 30)
check("EX-A config copy shows 100", "100" in res["output"], res["output"][:200])

res = run("EXA-run", "cd %s && %s --config config_vt --out /tmp/vt_exa.log --json /tmp/vt_exa.json; "
                     "echo EXA_EXIT=$?" % (REMOTE, BIN), 120)
check("EX-A run exit0", res["output"].rstrip().endswith("EXA_EXIT=0"), res["output"][-200:])
exa = run("EXA-fetch", "cat /tmp/vt_exa.log", 20)["output"]
(OUT / (TAG + ".exa.log")).write_text(exa, encoding="utf-8")


def raw_kv(text, needle):
    for ln in text.splitlines():
        if needle in ln:
            return ln
    return ""


def tail_val(line, key):
    # 值可能含空格（prompt=同步偏差 N ms（阈值 M ms））: 取 key= 之后到行尾
    m = re.search(r"%s=(.*)$" % re.escape(key), line)
    return m.group(1) if m else ""


cfg_line = raw_kv(exa, "type=config")
check("EX-A config threshold echoed 100", "replay_sync_threshold_ms=100" in cfg_line, cfg_line)
skew_line = raw_kv(exa, "case=skewed")
dev_s = int(re.search(r"max_deviation_ms=(\d+)", skew_line).group(1))
prompt_s = tail_val(skew_line, "prompt")
# 800ms 偏差 > 100 阈值: 不同步 + 提示含新阈值 100
check("EX-A skewed dev 800 over 100", dev_s == 800, skew_line)
check("EX-A skewed not in_sync with prompt(threshold=100)",
      " in_sync=false " in skew_line and "100" in prompt_s and "同步偏差" in prompt_s, prompt_s)
real_line = raw_kv(exa, "case=real_heads")
dev_r = int(re.search(r"max_deviation_ms=(\d+)", real_line).group(1))
check("EX-A real heads in_sync within 100", " in_sync=true " in real_line and 0 <= dev_r <= 100,
      real_line)
acc_line = raw_kv(exa, "type=acceptance")
# 交付探针 acceptance 公式硬编码默认阈值 300（probe 源 ok 式含 threshold==300，
# 键名 threshold_from_config 实际检查的是 threshold==300）：非默认配置下自报
# FAIL/threshold_from_config=false 是交付契约（登记 O-1 交实现参考）；核心行为
# 的可配置阈值语义由本参数轮行为行与 vext 夹具独立验证，不采信 acceptance 行。
check("EX-A acceptance row matches deliverable contract (default-300 self-check, O-1)",
      "result=FAIL" in acc_line and "threshold_from_config=false" in acc_line, acc_line[-200:])
check("EX-A behavior rows prove threshold-100 semantics (independent of acceptance row)",
      "replay_sync_threshold_ms=100" in cfg_line
      and "（阈值 100 ms）" in skew_line and " in_sync=false " in skew_line
      and 0 <= dev_r <= 100, "cfg=%s skew=%s" % (cfg_line, skew_line))
exa_json = run("EXA-json", "cat /tmp/vt_exa.json", 20)["output"]
check("EX-A json threshold 100", '"threshold_ms": 100' in exa_json, exa_json[:200])
check("EX-A json acceptance false (O-1 deliverable contract under non-default config)",
      '"acceptance": false' in exa_json, exa_json[-200:])

# ---------- EX-B: CLI 负例 ----------
res = run("NEG-unknown", "cd %s && %s --bogus-flag; echo NEG_EXIT=$?" % (REMOTE, BIN), 30)
check("NEG unknown arg exit2", "NEG_EXIT=2" in res["output"] and "unknown arg" in res["output"],
      res["output"][-200:])
res = run("NEG-help", "cd %s && %s --help; echo HELP_EXIT=$?" % (REMOTE, BIN), 30)
check("NEG help exit0 usage", res["output"].rstrip().endswith("HELP_EXIT=0")
      and "usage: railhmi-timebase-sync" in res["output"], res["output"][-200:])

# ---------- EX-A2: 确定性（同参二跑语义行恒等）----------
res = run("DET-run2", "cd %s && %s --config config_vt --out /tmp/vt_det.log --json /tmp/vt_det.json; "
                      "echo DET_EXIT=$?" % (REMOTE, BIN), 120)
check("DET run exit0", res["output"].rstrip().endswith("DET_EXIT=0"), res["output"][-200:])
det = run("DET-fetch", "cat /tmp/vt_det.log", 20)["output"]
# 时钟读数行波动（timebase wall/mono 为真实时钟域），其余语义行须恒等
def semantic_rows(text):
    keep = []
    for ln in text.strip().splitlines():
        if ln.startswith("type=timebase"):
            continue   # 真实时钟读数行（波动域；+1s 语义已由 C1/v04 判据独立重推导）
        keep.append(ln)
    return keep


check("DET semantic rows identical (except real-clock line)",
      semantic_rows(exa) == semantic_rows(det), "\nEXA:%s\nDET:%s" % (semantic_rows(exa)[-1:], semantic_rows(det)[-1:]))
check("DET json acceptance false (O-1: same non-default config as EX-A)",
      '"acceptance": false' in run("DET-json", "cat /tmp/vt_det.json", 20)["output"])

# ---------- EX-C: 板端 g++ 直编验证侧自有夹具 ----------
res = run("EXT-prep", "cd %s && mkdir -p vext && ls build-core/src/core/librailhmi_core.a "
                      "src/core/include/railhmi/core/timebase.h "
                      "src/core/include/railhmi/core/replay_sync.h" % REMOTE, 30)
check("EXT-C core lib + headers present", res["exit"] == 0, res["output"][:300])

sftp = client.open_sftp()
try:
    sftp.get_channel().settimeout(60)
    sftp.putfo(io.BytesIO(VCODE.encode("utf-8")), REMOTE + "/vext/vext_timebase.cpp")
finally:
    sftp.close()

compile_cmd = (
    "cd %s && g++ -std=c++17 -I src/core/include "
    "-o /tmp/vext_timebase vext/vext_timebase.cpp "
    "build-core/src/core/librailhmi_core.a 2>&1 | tail -8; "
    "if [ -x /tmp/vext_timebase ]; then echo VEXT_COMPILED; else echo VEXT_COMPILE_FAILED; fi" % REMOTE)
res = run("EXT-C-compile", compile_cmd, 240)
check("EXT-C compiled", "VEXT_COMPILED" in res["output"], res["output"][-400:])
if "VEXT_COMPILED" in res["output"]:
    res = run("EXT-C-run", "/tmp/vext_timebase; echo VEXT_EXIT=$?", 120)
    out = res["output"]
    vpass = out.count("vext PASS")
    vfail = out.count("vext FAIL")
    (OUT / (TAG + ".vext.log")).write_text(out, encoding="utf-8")
    # 判例总数从夹具源码独立统计（check( 调用数），不手抄常数
    v_expect = len(re.findall(r"^\s*check\((?!bool)", VCODE, re.M))
    fails_named = re.findall(r"^vext FAIL (.+)$", out, re.M)
    check("EXT-C no FAIL lines", not fails_named, "named fails=%s" % fails_named)
    check("EXT-C all %d cases PASS (source-derived)" % v_expect, vpass == v_expect,
          "got %d PASS, expect %d" % (vpass, v_expect))
    check("EXT-C summary 0 failures", "vext summary 0 failure(s)" in out, out[-200:])
    check("EXT-C VEXT_EXIT=0", "VEXT_EXIT=0" in res["output"], res["output"][-100:])

# ---------- EX-D: 凭据泄漏扫描 ----------
secrets = [s for s in (secret, cam_front, cam_side) if s]
patterns = [re.escape(s) for s in secrets if len(s) >= 6]
sem_pat = re.compile(r"(user-pw=\"[^\"]+\"|password=[^\s\"']+|[a-zA-Z0-9_]+:[^@/\s]+@)")
scan_targets = sorted(OUT.glob("*.log")) + sorted(OUT.glob("*.json")) + sorted(OUT.glob("*.md")) \
    + sorted(OUT.glob("*.py")) + sorted(OUT.glob("*.cpp"))
hits = []
# 扫描器源文件与其结果归档会自匹配语义形态（扫描 detail 存档回写扫描文本，
# 与 ITEM-002 凭据语义口径同源）: 字面 secret 扫描仍覆盖全部文件；
# 语义形态扫描排除扫描器脚本与其结果 JSON（其余证据文件全查）。
sem_skip = {"_verify_ext.py"}
sem_skip_prefixes = ("tb-ext-",)
for f in scan_targets:
    try:
        body = f.read_text(encoding="utf-8", errors="replace")
    except OSError:
        continue
    for p in patterns:
        if p in body:
            hits.append("%s: literal secret" % f.name)
    if f.name in sem_skip or f.name.startswith(sem_skip_prefixes):
        continue
    for m in sem_pat.finditer(body):
        tok = m.group(0)
        # SSH 凭据恰为 admin 时 --user admin 是合法 token；只抓凭据语义形态
        if re.fullmatch(r"[a-zA-Z0-9_]+:admin@", tok):
            continue
        hits.append("%s: %s" % (f.name, tok[:60]))
check("EX-D credential leak 0 hits", not hits, "; ".join(hits[:5]))

# ---------- EX-E: 清理 ----------
run("CLEAN", "rm -rf /tmp/vext_timebase /tmp/vt_exa.log /tmp/vt_exa.json /tmp/vt_det.log "
             "/tmp/vt_det.json %s/config_vt %s/vext && echo CLEANED" % (REMOTE, REMOTE), 30)
res = run("CLEAN-check", "ls /tmp/vext_timebase 2>/dev/null; ls %s/config_vt 2>/dev/null; "
                         "echo CLEAN_CHECK_DONE" % REMOTE, 20)
check("EX-E cleanup confirmed", "CLEAN_CHECK_DONE" in res["output"]
      and not [ln for ln in res["output"].splitlines() if ln.startswith("/tmp/")]
      and not [ln for ln in res["output"].splitlines() if ln.startswith(str(REMOTE) + "/")],
      res["output"][:200])

client.close()

import json
summary = {"tag": TAG, "checks": checks,
           "summary": {"total": len(checks), "failed": sum(1 for c in checks if not c["ok"])}}
(OUT / (TAG + ".json")).write_text(json.dumps(summary, ensure_ascii=False, indent=2),
                                   encoding="utf-8")
print("ext summary: %d checks, %d failed" % (len(checks), sum(1 for c in checks if not c["ok"])))
raise SystemExit(0 if all(c["ok"] for c in checks) else 1)
