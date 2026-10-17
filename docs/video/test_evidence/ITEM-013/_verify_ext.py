#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-013 验证侧扩展夹具（software_verification，VERIFY-20261017-v1）。

在 --keep-remote 保留的 /tmp/railhmi_prov 树上黑盒驱动交付入口 railhmi-media-provider
与已上传源码，验证侧自有判据：

  A. 独立参数轮（pre=2 post=3 warmup=6 extra=3，run_s=8）：querycases 语义独立重推导
  B. CLI 负例: --bogus-flag exit2 + unknown arg 行；--help exit0 含用法行
  C. 板端 g++ 直编验证侧自有夹具 _verify_vext.cpp（链接交付 recording_provider.cpp，
     仅依赖 core recording_provider + persistence_model.h，41 判例）
  D. 凭据泄漏扫描（本目录全部 ITEM-013 证据文件，2 个相机凭据 + SSH secret）
  E. 板端 EXT 临时产物清理
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
STAMP = "20261017-item013-verify-v1"
TAG = "prov-ext-%s-%s" % ("192_168_0_103", STAMP)
REMOTE = "/tmp/railhmi_prov"
BIN = REMOTE + "/build-media/tools/railhmi-media-provider"

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


def parse_provider_run(text):
    d = {"clip": None, "query": {}, "play": [], "export": None, "nvr": None,
         "sync": None, "acc": None}
    m = re.search(r"^type=clip (.*)$", text, re.M)
    if m:
        d["clip"] = kv_line(m.group(1))
    for case in ("complete", "empty", "partial", "gaps", "expired"):
        m = re.search(r"^type=query case=%s (.*)$" % case, text, re.M)
        if m:
            d["query"][case] = kv_line("case=%s %s" % (case, m.group(1)))
    for m in re.finditer(r"^type=play (.*)$", text, re.M):
        d["play"].append(kv_line(m.group(1)))
    m = re.search(r"^type=export (.*)$", text, re.M)
    if m:
        d["export"] = kv_line(m.group(1))
    m = re.search(r"^type=nvr (.*)$", text, re.M)
    if m:
        d["nvr"] = kv_line(m.group(1))
    m = re.search(r"^type=sync (.*)$", text, re.M)
    if m:
        d["sync"] = kv_line(m.group(1))
    m = re.search(r"^type=acceptance (.*)$", text, re.M)
    if m:
        d["acc"] = kv_line(m.group(1))
    return d


def independent_semantics(tag, d, pre, post, warmup, extra, problems):
    """独立重推导（参数无关）——ITEM-012 教训: 可用性/覆盖先按容差重推导，
    不硬性要求 complete；判据在「空 ok≠可播放 / partial 三态 / 实际解码 position /
    NVR//thumbnail/export/sync」语义域，可用性标签按窗口与容差验证存在一致性。"""
    c = d["clip"]
    if not c or int(c.get("size_bytes", "0")) <= 0:
        problems.append("%s clip row: %s" % (tag, c))
        return
    # clip 行 actual=[a,b]（kv 值含方括号）；直接拆时间对
    m_amp = re.search(r"actual=\[(-?\d+),(-?\d+)\]", "actual=" + str(c.get("actual", "")))
    if not m_amp:
        problems.append("%s clip actual window missing: %s" % (tag, c))
        return
    t0, t1 = int(m_amp.group(1)), int(m_amp.group(2))
    net = t1 - t0
    window = (pre + post) * 1000
    if c.get("availability") == "complete":
        if net < window - 1000:
            problems.append("%s clip marked complete but net %d < window %d-1000" %
                            (tag, net, window))
    else:
        # incomplete/unavailable 允许在关键帧粒度短缺；净时长须仍 >0 且 ≤ 窗口+1000
        if not (0 < net <= window + 1000):
            problems.append("%s clip non-complete net %d outside (0, window+1000]" % (tag, net))
        # 交付在短缺时的验收不得假报 PASS（acceptance 可以由 query/partial 语义另证）
    ac = d["acc"]
    if not ac or ac.get("result") != "PASS":
        problems.append("%s acceptance result: %s" % (tag, ac))
        return
    ok_plays = [x for x in d["play"] if x.get("status") == "ok"]
    if len(ok_plays) < 5:
        problems.append("%s play rows <5: %s" % (tag, len(d["play"])))
        return
    # play 判据（参数独立）: reached=true / frames>0 / actual-requested ≤ 2000ms
    for p in d["play"]:
        req_ms = int(p.get("requested", "0"))
        act_ms = int(p.get("actual", "0"))
        dev = abs(act_ms - req_ms)
        if p.get("reached") != "true" or int(p.get("frames", "0")) <= 0 or dev > 2000:
            problems.append("%s play %s: dev=%d reached=%s frames=%s" %
                            (tag, p.get("id"), dev, p.get("reached"), p.get("frames")))
    for p, key in zip(d["play"], ("play-400", "play-500", "play-600", "play-2500", "play-4500")):
        if p.get("id") != key:
            problems.append("%s play id order: %s" % (tag, p.get("id")))
    # actual 落到 [t0, t1+1000) 盘界内（解码位置不得逃出片段媒体窗）
    for p in d["play"]:
        act_ms = int(p.get("actual", "0"))
        if not (t0 - 1000 <= act_ms <= t1 + 1000):
            problems.append("%s play %s actual %d outside media window" %
                            (tag, p.get("id"), act_ms))

    nvr = d["nvr"]
    if not (nvr.get("query", "").startswith("unavailable/UNCONFIGURED") and
            nvr.get("play", "").startswith("unavailable/UNCONFIGURED") and
            nvr.get("export", "").startswith("unavailable/UNCONFIGURED") and
            nvr.get("segments") == "0"):
        problems.append("%s nvr row: %s" % (tag, nvr))
    sy = d["sync"]
    if not (int(sy.get("close_max_deviation", "1")) <= 300 and
            sy.get("close_in_sync") == "true" and
            int(sy.get("spread_max_deviation", "0")) > 300 and
            sy.get("spread_in_sync") == "false"):
        problems.append("%s sync row: %s" % (tag, sy))
    ex = d["export"]
    if not (ex.get("status") == "ok" and ex.get("complete") == "true" and
            int(ex.get("size_bytes", "0")) > 0):
        problems.append("%s export row: %s" % (tag, ex))
    gq = d["query"].get("gaps")
    if not gq or gq.get("status") != "ok" or int(gq.get("gaps", "0")) < 1 or not gq.get("reason"):
        problems.append("%s gaps row: %s" % (tag, gq))
    exp = d["query"].get("expired")
    if not exp or exp.get("status") != "error" or exp.get("reason") != "TIMEOUT":
        problems.append("%s expired row: %s" % (tag, exp))


# ---------- A. 独立参数轮 ----------
pre, post, warmup, extra = 2, 3, 6, 3
r_a = run("extA-param-run",
          "cd %s && %s --config config --pre-seconds %d --post-seconds %d --warmup %d --extra %d "
          "--outdir /tmp/extA --out /tmp/extA/prov.log --json /tmp/extA/prov.json; "
          "echo A_EXIT=$?" % (REMOTE, BIN, pre, post, warmup, extra), 120)
out_a = r_a.get("output", "")
a_exit = re.search(r"A_EXIT=(\d+)", out_a)
d_a = parse_provider_run(out_a)
problems = []
independent_semantics("EX-A", d_a, pre, post, warmup, extra, problems)
a_ok = (a_exit is not None and a_exit.group(1) == "0" and not problems)
check("A_independent_param_run", a_ok, "; ".join(problems)[:200] if problems else "")

# config echo: buffer_s = pre+post+2 由 warmup/post/extra 单独参数面驱动运行时长
# （EX-A 与 ITEM-012 教训：覆盖按 clip actual output 独立重新推导 = 参数无关，另行断言窗口数学。）
clip = d_a["clip"]
if clip:
    m_cl = re.search(r"actual=\[(-?\d+),(-?\d+)\]", "actual=" + str(clip.get("actual", "")))
    t0, t1 = (int(m_cl.group(1)), int(m_cl.group(2))) if m_cl else (0, 0)
    check("A_clip_duration_within_tolerance",
          abs(t1 - t0 - (pre + post) * 1000) <= 1100,
          "t1-t0=%d expect %d±1100 (keyframe粒度短缺按交付不变式核验)" % (t1 - t0, (pre + post) * 1000))
    if t1 - t0 < (pre + post) * 1000 - 1000:
        # 短缺属交付显式标注语义（partial/empty 查询语义不依赖 availability 标签）；
        # 验证侧独立观察并记录，不影响业务判据（登记观察项）。
        print("NOTE EX-A clip shortage: availability=%s actual=[%d,%d] net=%d window=%d" %
              (clip.get("availability"), t0, t1, t1 - t0, (pre + post) * 1000))
    # partial query: [t1-1000, t1+10000) → covered ≈ 1000（不依赖 availability）
    pq = d_a["query"].get("partial")
    if pq:
        cm = int(pq.get("covered_ms", "0"))
        # ① 若片段在 t1 侧短缺: covered 仍 = actual_t1 - (t1-1000)（≤1000）
        # ② 交付部分覆盖语义: status=partial（covered < 窗）
        check("A_partial_status_partial", pq.get("status") == "partial", str(pq))
        check("A_partial_covered_bounded",
              0 < cm <= 2000, "covered_ms=%s" % cm)
    # empty query: [t1+10000, t1+20000) -> ok 且 segments=0（不冒充可播）
    eq = d_a["query"].get("empty")
    if eq:
        check("A_empty_ok_zero_segments",
              eq.get("status") == "ok" and eq.get("segments") == "0" and
              eq.get("coverage_complete") == "false", str(eq))
    # complete query: 窗=片段自身 [t0,t1) → covered=net 且 coverage_complete=true
    # （标注 availability 与 classify 相互独立：classify 按片段实际区间算覆盖）
    cq = d_a["query"].get("complete")
    if cq:
        cm = int(cq.get("covered_ms", "0"))
        check("A_complete_covered_matches_net",
              cq.get("status") == "ok" and cq.get("coverage_complete") == "true" and
              abs(cm - (t1 - t0)) <= 100,
              "covered=%s net=%d" % (cm, t1 - t0))
    # gaps: reason token 非空
    gq = d_a["query"].get("gaps")
    if gq:
        check("A_gaps_reason_token",
              gq.get("status") == "ok" and int(gq.get("gaps", "0")) >= 1 and
              bool(gq.get("reason")), str(gq))
else:
    check("A_clip_duration_within_tolerance", False, "no clip")

# ---------- B. CLI 负例 ----------
r_b1 = run("extB-unknown-arg", "cd %s && %s --bogus-flag; echo B1_EXIT=$?" % (REMOTE, BIN), 30)
out_b1 = r_b1.get("output", "")
check("B_unknown_arg_exit2", "B1_EXIT=2" in out_b1 and "unknown arg: --bogus-flag" in out_b1)
r_b2 = run("extB-help", "cd %s && %s --help; echo B2_EXIT=$?" % (REMOTE, BIN), 30)
out_b2 = r_b2.get("output", "")
check("B_help_exit0_usage", "B2_EXIT=0" in out_b2 and "--selftest" in out_b2)

# ---------- C. vext 夹具编译运行 ----------
VCODE = HERE.joinpath("_verify_vext.cpp").read_text(encoding="utf-8")
print("vext: uploading C++ fixture (%d lines)" % VCODE.count("\n"))

sftp = client.open_sftp()
try:
    sftp.putfo(io.BytesIO(VCODE.encode("utf-8")), REMOTE + "/vext_recording_provider.cpp")
finally:
    sftp.close()

# 远端布局: upload_tree /tmp/railhmi_prov，app 内容平铺（src/core/...）
r_c = run("extC-vext-compile-run",
          "cd %s && g++ -std=c++17 -I src/core/include -o /tmp/vext_provider "
          "vext_recording_provider.cpp src/core/src/recording_provider.cpp 2>&1 | head -20; "
          "if [ -x /tmp/vext_provider ]; then /tmp/vext_provider; echo VEXT_EXIT=$?; "
          "else echo VEXT_COMPILE_FAILED; fi" % REMOTE, 120)
out_c = r_c.get("output", "")
vext_pass = out_c.count("vext PASS")
vext_fail = out_c.count("vext FAIL")
c_ok = "VEXT_EXIT=0" in out_c and vext_fail == 0 and vext_pass >= 41
check("C_vext_provider_all_pass_%d" % vext_pass, c_ok,
      out_c[-400:] if not c_ok else "%d/%d" % (vext_pass, vext_pass + vext_fail))

# ---------- 收尾：清理板端 EXT 产物 ----------
run("ext-cleanup", "rm -rf /tmp/extA /tmp/vext_provider %s/vext_recording_provider.cpp && "
    "echo EXT_CLEANED" % REMOTE, 20)
# 二次确认（ITEM-019 教训: rm 后确认不存在 exit=1 ls）
r_ls = run("ext-cleanup-verify", "ls /tmp/extA /tmp/vext_provider 2>&1; echo LS_EXIT=$?", 20)
check("CLEAN_board_ext_products_absent", "No such file or directory" in r_ls.get("output", ""))

client.close()

# ---------- D. 凭据泄漏扫描 ----------
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
check("D_no_credential_in_evidence", len(hits) == 0, str(hits))

fails_n = sum(1 for c in checks if not c["ok"])
for c in checks:
    print("%s %s %s" % ("PASS" if c["ok"] else "FAIL", c["name"], c.get("detail", "")))
print("ext %s" % ("ok (%d check(s))" % len(checks) if fails_n == 0
                  else "FAILED (%d failure(s))" % fails_n))

(OUT / (TAG + ".json")).write_text(json.dumps({
    "stamp": STAMP, "steps": steps, "credential_hits": hits,
    "checks": checks, "failures": fails_n,
}, ensure_ascii=False, indent=2), encoding="utf-8")
raise SystemExit(0 if fails_n == 0 else 1)
