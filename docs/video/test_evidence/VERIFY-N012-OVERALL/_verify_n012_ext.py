#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""VERIFY-N012-OVERALL 交付整体轮扩展夹具驱动（VERIFY-20261021-n012-v1，复用 ITEM-017 vext 已验证夹具）。

在 --keep-remote 保留的板端 /tmp/railhmi_measure 树上：
  A. 独立参数轮（EXA）：--duration 3（与主轮 6/8s 不同的第三窗口）→ 产物在新鲜窗口下重判据
     （recorded 记录容量面/延迟包含性独立重算——EXA oracle 参数无关）；
  B. CLI 负例：未知参数 exit2 / --help exit0 + usage 行；
  C. 板端 g++ 直编验证自有 _verify_n012_vext.cpp（链接交付 librailhmi_core.a），
     LatencyHistogram 边界/JSON 结构独立判例；
  D. 稳定性对照（EXD）：同参两轮 acceptance 行键面恒等（自然波动域：吞吐数值/延迟分位/
     recovery 数值逐值漂移按物理意义豁免，键面与 result 域恒等独立断言）；
  E. 凭据泄漏扫描（EX-E）：目录全证据文件 SSH secret + front/side 相机凭据语义；
     admin 为登记用户名豁免（ITEM-002 先例口径）；
  F. 板端清理 /tmp/railhmi_measure + EXT 临时产物（本轮以 --keep-remote 保留，本驱动收尾）。
判据独立推导，不采信交付 type=acceptance 行为最终依据。
"""
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
from board_build import rrun, fingerprint  # noqa: E402

STAMP = "20261021-n012-overall-v1"
TAG = "measure-ext-192_168_0_103-" + STAMP
REMOTE = "/tmp/railhmi_measure"
BIN = REMOTE + "/build-media/tools/railhmi-measure"
VEXT = (HERE / "_verify_n012_vext.cpp").read_text(encoding="utf-8")

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
    print("ext17 %s %s %s" % ("PASS" if ok else "FAIL", name, "" if ok else str(detail)[:220]))


def kv_line(line):
    kv = {}
    for tok in line.split(" "):
        if "=" in tok:
            k, v = tok.split("=", 1)
            kv[k] = v
    return kv


def parse_measure(text):
    d = {"records": {}, "acceptance": None}
    for mm in re.finditer(r'^type=measure source=(\S+) (\{.*)$', text, re.M):
        try:
            d["records"][mm.group(1)] = json.loads(mm.group(2))
        except Exception as exc:
            d["records"][mm.group(1)] = {"_parse_error": repr(exc)}
    m = re.search(r"^type=acceptance (.*)$", text, re.M)
    if m:
        d["acceptance"] = kv_line(m.group(1))
    return d


def record_semantics(tag, recs, expect_window):
    """EXA/EXD 记录独立重判据（参数无关判据：三态/键面/物理域/恢复/同步）。
    expect_window: 记录声明的期望 window_s（用于 EXA 比对注入参数）。
    """
    sim = recs.get("simulation", {})
    fil = recs.get("file", {})
    rts = recs.get("rtsp", {})
    check("%s three records parsed" % tag, None not in (recs.get("simulation"),
                                                        recs.get("file"), recs.get("rtsp")),
          "records=%s" % sorted(recs))
    check("%s sim recorded" % tag, sim.get("status") == "recorded", str(sim.get("status")))
    check("%s file recorded" % tag, fil.get("status") == "recorded", str(fil.get("status")))
    check("%s rtsp to_be_executed+CREDS" % tag,
          rts.get("status") == "to_be_executed" and rts.get("reason") == "CREDENTIAL_MISSING",
          str(rts.get("status")))
    if expect_window is not None:
        check("%s sim window_s matches injected" % tag,
              abs(sim.get("window_s", -1) - expect_window) < 0.35,
              "%s vs %s" % (sim.get("window_s"), expect_window))
    tp = sim.get("throughput", {})
    check("%s sim source_fps band" % tag, 20.0 <= tp.get("source_fps", -1) <= 30.0,
          str(tp.get("source_fps")))
    check("%s sim display_fps >0" % tag, tp.get("display_fps", -1) > 0, str(tp.get("display_fps")))
    check("%s sim cpu in 0..100" % tag, 0 < tp.get("cpu_pct", -1) <= 100, str(tp.get("cpu_pct")))
    st = sim.get("storage", {})
    cb = st.get("clip_bytes", -1)
    check("%s sim clip 188-granularity positive" % tag, cb > 0 and cb % 188 == 0, str(cb))
    win = sim.get("window_s", 0.0) or 0.0
    if win > 0 and cb > 0:
        expect_br = cb * 8.0 / win
        got = st.get("measured_bitrate_bps", -1)
        check("%s sim bitrate arithmetic" % tag, abs(got - expect_br) / expect_br < 0.05,
              "%s vs %s" % (got, expect_br))
    sy = sim.get("sync", {})
    check("%s sim sync available" % tag, sy.get("available") is True, str(sy))
    check("%s sim dev < 300" % tag, 0 <= sy.get("max_deviation_ms", 9999) < 300, str(sy))
    rc = sim.get("recovery", {})
    check("%s sim recovery real" % tag, rc.get("attempts", 0) >= 1 and rc.get("last_recovery_ms", -1) > 0,
          str(rc))
    lat = sim.get("latency", {}).get("end_to_end", {})
    e = lat.get("n", 0)
    check("%s sim e2e samples" % tag, e > 0, str(lat))
    if e > 0:
        check("%s sim e2e monotonic" % tag,
              lat["min"] <= lat["p50"] <= lat["p90"] <= lat["p99"] <= lat["max"], str(lat))


# ---------- EX-A: 独立参数轮（--duration 3，第三窗口） ----------
run("EXA-run", "cd %s && %s --config config --duration 3 "
               "--outdir /tmp/vexa_measure --out /tmp/vexa.log --json /tmp/vexa.json; "
               "echo EXA_EXIT=$?" % (REMOTE, BIN), 150)
exa_out = STEPS[-1]["output"]
check("EX-A run done", "EXA_EXIT=0" in exa_out, exa_out[-200:])
res2 = run("EXA-fetch", "cat /tmp/vexa.log && echo ---JSON--- && cat /tmp/vexa.json", 20)
exa = res2["output"]
pa = parse_measure(exa)
record_semantics("EX-A", pa["records"], expect_window=3.0)
check("EX-A acceptance result=PASS", (pa["acceptance"] or {}).get("result") == "PASS",
      str(pa["acceptance"]))
# EXA 实测片段也必须是真 MPEG-TS（独立下载校验 188/0x47 粒度）
sftp = client.open_sftp()
try:
    sftp.get_channel().settimeout(60)
    import tempfile
    tmp_local = HERE / "_tmp_exa_clip_n012.ts"
    sftp.get("/tmp/vexa_measure/MEASURE_measure-clip.ts", str(tmp_local))
finally:
    sftp.close()
body = tmp_local.read_bytes()
segs = sum(1 for i in range(0, len(body) - 187, 188) if body[i] == 0x47)
total = (len(body) - 187) // 188 + 1
check("EX-A clip real MPEG-TS sync", total > 0 and segs == total,
      "segs=%d total=%d size=%d" % (segs, total, len(body)))
check("EX-A clip matches JSON clip_bytes",
      len(body) == pa["records"].get("simulation", {}).get("storage", {}).get("clip_bytes", -1),
      "%d vs %s" % (len(body), pa["records"].get("simulation", {}).get("storage", {})))
tmp_local.unlink()

# ---------- EXD: 同参稳定性对照（EXD run duration 3 第二轮） ----------
res_s2 = run("EXD-run2", "cd %s && %s --config config --duration 3 "
                         "--outdir /tmp/vexd_measure --out /tmp/vexd2.log; "
                         "echo EXD_EXIT=$?" % (REMOTE, BIN), 150)
det2 = res_s2["output"]
check("EX-D run2 done", "EXD_EXIT=0" in det2, det2[-160:])
pd = parse_measure(det2)
akeys_exa = set((pa["acceptance"] or {}).keys())
akeys_exd = set((pd["acceptance"] or {}).keys())
check("EX-D acceptance keyset identical", akeys_exa == akeys_exd,
      "exa=%s exd=%s" % (sorted(akeys_exa), sorted(akeys_exd)))
for k in akeys_exa:
    if k != "result":
        continue
check("EX-D result PASS both", (pa["acceptance"] or {}).get("result") == "PASS" and
      (pd["acceptance"] or {}).get("result") == "PASS", str(pd["acceptance"]))
# 结构性 token 域恒等（状态无关面）：三类 source/status/reason 序列逐字恒等
struct_exa = [(src, r.get("status"), r.get("reason")) for src, r in sorted(pa["records"].items())]
struct_exd = [(src, r.get("status"), r.get("reason")) for src, r in sorted(pd["records"].items())]
check("EX-D structural tokens stable", struct_exa == struct_exd,
      "%s vs %s" % (struct_exa, struct_exd))

# ---------- EX-B: CLI 负例 ----------
res_u = run("EXB-unknown", "cd %s && %s --bogus-flag; echo EXB_EXIT=$?" % (REMOTE, BIN), 30)
check("EX-B unknown arg exit2", "EXB_EXIT=2" in res_u["output"], res_u["output"][-160:])
res_h = run("EXB-help", "cd %s && %s --help; echo EXB_EXIT=$?" % (REMOTE, BIN), 30)
check("EX-B help exit0 + usage", "EXB_EXIT=0" in res_h["output"] and "usage: railhmi-measure" in res_h["output"],
      res_h["output"][-200:])

# ---------- EX-C: 验证侧 g++ 直编自有夹具 ----------
res_tree = run("tree-check", "ls %s/build-media/tools/railhmi-measure && echo TREE_OK" % REMOTE, 20)
if "TREE_OK" not in res_tree["output"]:
    sys.stderr.write("错误: %s 构建树不存在（前置 --keep-remote 复跑未成功或已清理），EXT 无法继续。\n" % REMOTE)
    client.close()
    out_trans = HERE / (TAG + ".transcript.json")
    out_trans.write_text(json.dumps(TRANS, ensure_ascii=False, indent=2), encoding="utf-8")
    raise SystemExit(3)

sftp = client.open_sftp()
try:
    sftp.get_channel().settimeout(120)
    with sftp.open(REMOTE + "/vext_n012.cpp", "w") as f:
        f.write(VEXT)
finally:
    sftp.close()
run("EXC-upload", "ls -la %s/vext_n012.cpp" % REMOTE, 20)

compile_cmd = (
    "cd %s && g++ -std=c++17 -pthread -O1 "
    "-I src/core/include -I src/media/include -I src/platform/include "
    "$(pkg-config --cflags gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0) "
    "$(pkg-config --cflags librga) "
    "-o vext_n012 vext_n012.cpp "
    "build-media/src/media/librailhmi_media.a "
    "build-media/src/core/librailhmi_core.a build-media/src/platform/librailhmi_platform.a "
    "$(pkg-config --libs gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0) "
    "$(pkg-config --libs librga) 2>&1 | tail -8; "
    "if [ -x vext_n012 ]; then echo VEXT_BUILD_OK; else echo VEXT_COMPILE_FAILED; fi" % REMOTE)
res_c = run("EXC-build", compile_cmd, 240)
check("EX-C build ok", "VEXT_BUILD_OK" in res_c["output"], res_c["output"][-400:])

if "VEXT_BUILD_OK" in res_c["output"]:
    res_r = run("EXC-run", "cd %s && ./vext_n012; echo VEXT_EXIT=$?" % REMOTE, 120)
    vext_out = res_r["output"]
    vfails = re.findall(r"^v17 FAIL (\S+)", vext_out, re.M)
    npass = len(re.findall(r"^v17 PASS ", vext_out, re.M))
    check("EX-C vext all pass", "VEXT_EXIT=0" in vext_out and not vfails,
          "pass=%d fails=%s" % (npass, vfails[:8]))
    # 判例数 = 夹具源 check( 调用数（不手抄）
    n_calls = len(re.findall(r"(?m)^\s*check\(", VEXT))
    check("EX-C vext PASS line count == check() calls", npass == n_calls,
          "%d vs %d" % (npass, n_calls))

# ---------- EX-E: 凭据泄漏扫描（目录全证据产物文件） ----------
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
    re.compile(r"pwd=\S+"),
    re.compile(r"token=\S+"),
]
hits = []
for p in HERE.iterdir():
    if not p.is_file() or p.name.startswith("_verify_n012_ext") or p.suffix not in (".md", ".log", ".json", ".txt"):
        continue
    try:
        body = p.read_text(encoding="utf-8", errors="ignore")
    except Exception:
        continue
    for s in SECRETS:
        if s in body and s != "admin":
            hits.append((p.name, "secret-literal"))
    for pat in patterns:
        for mm in pat.finditer(body):
            frag = mm.group(0)[:60]
            if re.fullmatch(r"password=(admin|\*\*\*)|pwd=(admin|\*\*\*)", frag):
                continue
            if "***" in frag:
                continue
            hits.append((p.name, frag))
check("EX-E credential leak scan clean", not hits, str(hits[:6]))
check("EX-E secrets nondegenerate", len(SECRETS) >= 1 and all(len(s) >= 4 for s in SECRETS),
      "loaded=%d" % len(SECRETS))

# ---------- 板端清理 ----------
run("cleanup-ext", "rm -rf /tmp/vexa_measure /tmp/vexa.log /tmp/vexa.json /tmp/vexd_measure "
                   "/tmp/vexd2.log %s/vext_n012.cpp %s/vext_n012 && echo EXT_CLEANED" % (REMOTE, REMOTE), 30)
res_c2 = run("cleanup-tree", "rm -rf %s && echo BOARD_CLEAN_DONE" % REMOTE, 30)
check("cleanup tree done", "BOARD_CLEAN_DONE" in res_c2["output"], res_c2["output"][-60:])

client.close()

out_trans = HERE / (TAG + ".transcript.json")
out_trans.write_text(json.dumps(TRANS, ensure_ascii=False, indent=2), encoding="utf-8")
log = HERE / (TAG + ".log")
with log.open("w", encoding="utf-8") as fh:
    fh.write("host_key_fingerprint: %s\n" % TRANS["host_key_fingerprint"])
    for s in TRANS_STEPS:
        fh.write("\n===== [%s] exit=%s =====\n%s\n" % (s.get("step"), s.get("exit"), s.get("output") or ""))

nfails = sum(1 for c in CHECKS if not c["ok"])
print("\next17 summary: checks=%d fails=%d" % (len(CHECKS), nfails))
json.dump({"stamp": STAMP, "checks": CHECKS, "host_key_fingerprint": TRANS["host_key_fingerprint"]},
          open(HERE / (TAG + ".checks.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=2)
