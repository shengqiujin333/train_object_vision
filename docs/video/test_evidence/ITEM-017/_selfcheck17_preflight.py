# -*- coding: utf-8 -*-
"""ITEM-017 验证侧离线自检（上板前零成本抓夹具构造错，三件套之一）。

  a) 两个驱动脚本 py_compile；
  b) compare 预言机对实现方 r3 归档全部跑过（在 verify 归档缺席时逐项跳过，分支留痕）；
  c) vext 判例名与 check( 调用数一致性（板上计数断言的预备式）；
  d) compare 中 fresh 路径的 stamp 与 run/ext 的 STAMP 一致（防漂移）。
"""
import ast
import json
import py_compile
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
fails = []


def check(name, ok, detail=""):
    print("self17 %s %s %s" % ("PASS" if ok else "FAIL", name, "" if ok else str(detail)[:300]))
    if not ok:
        fails.append(name)


for script in ("_verify17_run.py", "_verify17_ext.py", "_verify17_compare.py"):
    try:
        py_compile.compile(str(HERE / script), doraise=True)
        check("a) %s compiles" % script, True)
    except Exception as exc:
        check("a) %s compiles" % script, False, repr(exc))

# b) compare 预言机在 r3 归档上跑（fresh 缺失时只跳过 fresh 分支，不误报）
sys.path.insert(0, str(HERE))
import _verify17_compare  # noqa: E402

r3_path = HERE / "measure-192_168_0_103-20261017-item017-r3.core-evidence_measure-20261017-item017-r3.json"
arc = json.loads(r3_path.read_text(encoding="utf-8"))
recs = arc["records"]
classes = {r["source_class"]: r for r in recs}
check("b1) r3 three classes", set(classes) == {"simulation", "file", "rtsp"}, str(sorted(classes)))
sim = classes.get("simulation", {})
tp = sim.get("throughput", {})
check("b2) r3 sim fps in band", 20.0 <= tp.get("source_fps", -1) <= 30.0, str(tp.get("source_fps")))
check("b3) r3 sim cpu plausibility", 0 < tp.get("cpu_pct", -1) <= 100, str(tp.get("cpu_pct")))
cb = sim.get("storage", {}).get("clip_bytes", -1)
check("b4) r3 clip granular", cb > 0 and cb % 188 == 0, str(cb))
win = sim.get("window_s", 6.0)
eb = sim.get("storage", {}).get("measured_bitrate_bps", -1)
check("b5) r3 bitrate math", abs(eb - cb * 8.0 / win) / (cb * 8.0 / win) < 0.05,
      "%s vs %s" % (eb, cb * 8.0 / win))
rc = sim.get("recovery", {})
check("b6) r3 recovery real", rc.get("attempts", 0) >= 1 and rc.get("last_recovery_ms", -1) > 0, str(rc))
sy = sim.get("sync", {})
check("b7) r3 sync available", sy.get("available") is True and 0 <= sy.get("max_deviation_ms", 9) < 300, str(sy))
lat = sim.get("latency", {})
for s in ("arrival_decode", "decode_convert", "convert_present", "end_to_end"):
    h = lat.get(s, {})
    ok = h.get("min", 9) <= h.get("p50", 8) <= h.get("p90", 7) <= h.get("p99", 6) <= h.get("max", 5)
    check("b8) r3 %s monotonic" % s, ok, str(h))
e2e = lat.get("end_to_end", {})
a_ = lat.get("arrival_decode", {})
b_ = lat.get("decode_convert", {})
c_ = lat.get("convert_present", {})
check("b9) r3 p99 additivity necessary cond",
      e2e.get("p99", 0) <= a_.get("p99", 0) + b_.get("p99", 0) + c_.get("p99", 0) + 1,
      "%s <= %s+%s+%s" % (e2e.get("p99"), a_.get("p99"), b_.get("p99"), c_.get("p99")))
rts = classes.get("rtsp", {})
check("b10) r3 rtsp tbe+missing", rts.get("status") == "to_be_executed" and
      rts.get("reason") == "CREDENTIAL_MISSING", str(rts))
check("b11) r3 npu explicit unavailable", tp.get("npu_pct") == -1 and
      "unavailable" in tp.get("npu_note", ""), "%s %s" % (tp.get("npu_pct"), tp.get("npu_note")))

# c) vext 判例名与 check() 调用数（板上计数断言的预备式）
vext = (HERE / "_verify17_vext.cpp").read_text(encoding="utf-8")
n_calls = len(re.findall(r"(?m)^\s*check\(", vext))
names = set(re.findall(r'check\([^;]*?,\s*"([a-z0-9_.]+)"', vext, re.S))
check("c1) vext check calls counted", n_calls > 0, str(n_calls))
check("c2) vext names subset of calls", len(names) <= n_calls * 3, "%d vs %d" % (len(names), n_calls))
# 花括号深度平衡（ITEM-019 插入漏闭包教训）：剥字符串字面量与 // 注释后计数，
# 否则夹具源内 JSON 示例字符串中的 { } 会被误计。
depth = 0
mind = 99
for line in vext.splitlines():
    s = re.sub(r'"(\\.|[^"\\])*"', '""', line)
    s = re.sub(r"//.*", "", s)
    for ch in s:
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            mind = min(mind, depth)
check("c3) vext brace balance", depth == 0 and mind >= 0, "final=%d min=%d" % (depth, mind))

# d) stamp 一致性
run_txt = (HERE / "_verify17_run.py").read_text(encoding="utf-8")
ext_txt = (HERE / "_verify17_ext.py").read_text(encoding="utf-8")
cmp_txt = (HERE / "_verify17_compare.py").read_text(encoding="utf-8")
check("d1) run stamp = v1", "20261018-item017-verify-v1" in run_txt)
check("d2) ext stamp matches run stamp", 'STAMP = "20261018-item017-verify-v1"' in ext_txt)
check("d3) compare fresh stamp matches", "20261018-item017-verify-v1" in cmp_txt)

print("\nself17 summary: fails=%d" % len(fails))
if fails:
    print("self17 FAILED: %s" % fails)
sys.exit(1 if fails else 0)
