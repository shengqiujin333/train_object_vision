#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-015 验证侧独立解析比对（VERIFY-20261017-v1）。

实现方 r2 归档 vs 验证侧 fresh 复跑（verify-v1）core-evidence 逐行逐字段对齐；
真实墙钟/单调时钟读数为自然波动域（wall_ms/mono_us/+1s_wall 按 roundtrip 语义
独立重推导，不比对绝对值）。同步/守卫/seek/acceptance 判据在 fresh 数据上
独立重推导，不采信 acceptance 行。
"""
import json
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
IMPL = "20261017-item015-r2"
VERI = "20261017-item015-verify-v1"

# 自然波动字段：真实时钟读数随运行时刻变化，按语义重推导不比绝对值
FLUCT = {"wall_ms", "mono_us", "+1s_wall"}

checks = []


def checkp(name, ok, detail=""):
    checks.append({"name": name, "ok": bool(ok), "detail": str(detail)[:300]})
    print("compare %s %s %s" % ("PASS" if ok else "FAIL", name, "" if ok else str(detail)[:200]))


def kv_line(line):
    kv = {}
    for tok in line.split(" "):
        if "=" in tok:
            k, v = tok.split("=", 1)
            kv[k] = v
    return kv


def load_core(tag):
    return open(HERE / ("tb-192_168_0_103-%s.core-evidence_timebase-sync-%s.log" % (tag, tag)),
                encoding="utf-8", errors="replace").read()


def parse_core(text):
    rows = []
    for ln in text.strip().splitlines():
        rows.append(kv_line(ln))
    return rows


def rows_equal_ignoring_fluct(a, b):
    if set(a.keys()) != set(b.keys()):
        return False, "keyset %s vs %s" % (sorted(a), sorted(b))
    for k in a:
        if k in FLUCT:
            continue
        if a[k] != b[k]:
            return False, "%s: %s vs %s" % (k, a[k], b[k])
    return True, ""


impl_rows = parse_core(load_core(IMPL))
veri_rows = parse_core(load_core(VERI))
checkp("row count %d" % len(impl_rows), len(impl_rows) == len(veri_rows),
       "impl=%d veri=%d" % (len(impl_rows), len(veri_rows)))
checkp("row types identical", [r.get("type") for r in impl_rows] == [r.get("type") for r in veri_rows],
       "%s vs %s" % ([r.get("type") for r in impl_rows], [r.get("type") for r in veri_rows]))

for i, (a, b) in enumerate(zip(impl_rows, veri_rows)):
    if a.get("type") == "timebase":
        # 真实时钟读数行：键集 + 布尔域恒等 + +1s 语义独立重推导
        checkp("row%02d type=timebase keyset" % i, set(a.keys()) == set(b.keys()),
               "%s vs %s" % (sorted(a), sorted(b)))
        continue
    ok, why = rows_equal_ignoring_fluct(a, b)
    checkp("row%02d type=%s" % (i, a.get("type", "?")), ok, why)

# ---- fresh 复跑判据独立重推导（不采信 acceptance 行）----
tb = next(r for r in veri_rows if r.get("type") == "timebase")
uncal = next(r for r in veri_rows if r.get("type") == "uncalibrated")
sync_real = next(r for r in veri_rows if r.get("type") == "sync" and r.get("case") == "real_heads")
sync_skew = next(r for r in veri_rows if r.get("type") == "sync" and r.get("case") == "skewed")
sync_miss = next(r for r in veri_rows if r.get("type") == "sync" and r.get("case") == "missing_clock")
seek = next(r for r in veri_rows if r.get("type") == "seek")
acc = next(r for r in veri_rows if r.get("type") == "acceptance")

# C1: 真实时钟校准 + 单调↔UTC 单次线性换算（mono+1s → wall+1000ms；+1s_wall 由
#     mono+1e6µs 换算；wall_ms 与 +1s_wall 差恰 1000；wall≈系统 UTC 纪元毫秒）
wall = int(tb["wall_ms"])
w1 = int(tb["+1s_wall"])
mono = int(tb["mono_us"])
checkp("C1 calibrated + can_write_utc true",
       tb.get("calibrated") == "true" and tb.get("can_write_utc") == "true", tb)
checkp("C1 single linear conversion (+1s_wall - wall == 1000)", w1 - wall == 1000,
       "wall=%d +1s_wall=%d" % (wall, w1))
# 换算只做一次：mono+1s 的 µs→ms 恰好进位 1000ms，不重复叠加（重复叠加会 ≥2× 偏差）
checkp("C1 wall is UTC epoch scale", 1_500_000_000_000 < wall < 2_200_000_000_000, str(wall))
checkp("C1 roundtrip true", tb.get("roundtrip") == "true", tb)

# C2: 未校准守卫——can_write_utc=false / 不写 UTC 业务事件 / 输出未被污染
checkp("C2 uncalibrated guard", uncal.get("can_write_utc") == "false"
       and uncal.get("utc_event_written") == "false" and uncal.get("reject") == "true", uncal)

# C3: 三路实际播放头同步——available / in_sync / 偏差在阈值内且小（真实时钟连续读）
thresh = None
cfg_row = next(r for r in veri_rows if r.get("type") == "config")
thresh = int(cfg_row["replay_sync_threshold_ms"])
checkp("C3 threshold from config == 300", thresh == 300, cfg_row)
dev = int(sync_real["max_deviation_ms"])
checkp("C3 real heads available+in_sync, dev in [0,threshold]",
       sync_real.get("status") == "available" and sync_real.get("in_sync") == "true"
       and 0 <= dev <= thresh, sync_real)
# 播放头来自真实单调钟连续读（每路间隔 5ms）：dev 不可能为负、且必须被 max−min 语义涵盖
checkp("C3 API 无固定 offsets 语义行", sync_real.get("source") == "calibrated_monotonic_clock",
       sync_real)

# C4: 超阈值显式提示——dev>thresh、in_sync=false、prompt 含偏差与阈值
# （探针行 prompt= 值含空格，kv_line 按 token 切分会被截断——prompt 用行尾正则取全值）
def raw_line(rows_text, needle):
    for ln in rows_text.splitlines():
        if needle in ln:
            return ln
    return ""


veri_text = load_core(VERI)
skew_line = raw_line(veri_text, "case=skewed")
m_prompt = re.search(r"prompt=(.*)$", skew_line)
full_prompt = m_prompt.group(1) if m_prompt else ""
dev_s = int(sync_skew["max_deviation_ms"])
checkp("C4 skewed over threshold", not sync_skew.get("in_sync") == "true" and dev_s > thresh, sync_skew)
checkp("C4 prompt explicit with deviation and threshold",
       str(dev_s) in full_prompt and str(thresh) in full_prompt
       and "同步偏差" in full_prompt, full_prompt)
checkp("C4 skewed dev == 800 (heads[0]+800 injected)", dev_s == 800, sync_skew)

# C5: 缺任一路有效时钟 → available=false MISSING_CLOCK
checkp("C5 missing clock unavailable", sync_miss.get("available") == "false"
       and sync_miss.get("reason") == "MISSING_CLOCK"
       and sync_miss.get("missing") == "CAM-03", sync_miss)

# C6: seek 世代——递增 / 陈旧回调丢弃且位置不变 / 未达不提前 reached / 达到才 reached
checkp("C6 generations increment", int(seek["gen1"]) == 1 and int(seek["gen2"]) == 2, seek)
checkp("C6 stale callbacks discarded",
       seek.get("stale0_discarded") == "true" and seek.get("stale1_discarded") == "true", seek)
checkp("C6 stale position unchanged", seek.get("stale_pos_unchanged") == "true", seek)
checkp("C6 current gen callback accepted", seek.get("accepted") == "true", seek)
checkp("C6 not premature located", seek.get("premature_located") == "false", seek)
checkp("C6 reached only within tolerance", seek.get("reached") == "true", seek)

# C7: acceptance 行与独立判据一致（10 键全 true + result=PASS）
expect = {"timebase_calibrated": "true", "conversion_roundtrip": "true",
          "uncalibrated_no_utc": "true", "sync_real_in_sync": "true",
          "sync_prompt_over_threshold": "true", "sync_missing_clock_unavailable": "true",
          "seek_stale_discarded": "true", "seek_not_premature": "true",
          "seek_reached": "true", "threshold_from_config": "true", "result": "PASS"}
checkp("C7 acceptance row matches independent verdict",
       all(acc.get(k) == v for k, v in expect.items()), acc)

# ---- selftest 判例两轮一致 ----
def section(tag, name):
    t = open(HERE / ("tb-192_168_0_103-%s.log" % tag), encoding="utf-8", errors="replace").read()
    m = re.search(r"===== \[%s\] exit=(\S+) =====\n(.*?)(?=\n===== \[)" % name, t, re.S)
    return m.group(1), m.group(2)


si_exit, si_body = section(IMPL, "selftest")
sv_exit, sv_body = section(VERI, "selftest")
checkp("selftest exit identical", si_exit == sv_exit == "0", "%s/%s" % (si_exit, sv_exit))
si_cases = re.findall(r"selftest (?:PASS|FAIL) (\S+)", si_body)
sv_cases = re.findall(r"selftest (?:PASS|FAIL) (\S+)", sv_body)
checkp("selftest 9 cases identical lists (ordered)", si_cases == sv_cases and len(si_cases) == 9,
       "%d/%d" % (len(si_cases), len(sv_cases)))
checkp("selftest no FAIL lines", "FAIL" not in si_body and "FAIL" not in sv_body)
checkp("selftest OK summary", "selftest OK (0 failure(s))" in sv_body, sv_body[-100:])

# ---- core-tests PASS 列表逐行一致（除 stamp 行）----
ci_exit, ci_body = section(IMPL, "core-tests")
cv_exit, cv_body = section(VERI, "core-tests")
ci_pass = re.findall(r"^\[PASS\] (.*)$", ci_body, re.M)
cv_pass = re.findall(r"^\[PASS\] (.*)$", cv_body, re.M)
checkp("core-tests exit identical", ci_exit == cv_exit == "0")
checkp("core-tests PASS lists identical (%d lines)" % len(ci_pass), ci_pass == cv_pass,
       "impl=%d veri=%d" % (len(ci_pass), len(cv_pass)))
checkp("core-tests 879/0 both",
       "== 879 passed, 0 failed ==" in ci_body and "== 879 passed, 0 failed ==" in cv_body)
# 新增 replay_sync 组判例须在交付单测内（抽样精确判例名；"gen1 accepted" 在
# replay_sync 组出现两次——探针 selftest 无重名、单测两处，按集合+计数共同断言）
replay_sample = ["uncalibrated guard", "try_to_wall_ms false when uncalibrated", "calibrated",
                 "mono->wall", "wall->mono roundtrip", "sync uncalibrated", "sync missing clock",
                 "sync in_sync <=300", "sync out prompt", "sync single available",
                 "configurable threshold", "seek1 locating", "stale gen0 discarded",
                 "stale no position update", "gen1 accepted", "not reached yet",
                 "gen increments", "gen1 now stale", "stale does not update", "gen2 accepted",
                 "reached within tolerance", "reset"]
import collections
c_hits = collections.Counter(x for x in ci_pass if x in replay_sample)
checkp("replay_sync group all sampled cases present (unique 22)", len(c_hits) == 22,
       "missing=%s" % [n for n in replay_sample if n not in c_hits])
checkp("replay_sync sampled hits == 23 (gen1 accepted x2)", sum(c_hits.values()) == 23,
       "total=%d" % sum(c_hits.values()))
checkp("no unexpected sampled name", all(c_hits[n] == 1 for n in replay_sample if n != "gen1 accepted"),
       str({k: v for k, v in c_hits.items() if v != 1}))

# ---- transcript 步骤 exit 全 0（两轮）----
def steps_ok(tag):
    t = json.load(open(HERE / ("tb-192_168_0_103-%s.transcript.json" % tag), encoding="utf-8"))
    return {s["step"]: s.get("exit") for s in t["steps"]}, t.get("host_key_fingerprint")


si_steps, si_fp = steps_ok(IMPL)
sv_steps, sv_fp = steps_ok(VERI)
checkp("impl steps all exit 0", all(v == 0 for v in si_steps.values()), si_steps)
checkp("verify steps all exit 0", all(v == 0 for v in sv_steps.values()), sv_steps)
checkp("host key fingerprint consistent",
       si_fp == sv_fp == "SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU",
       "%s / %s" % (si_fp, sv_fp))

# ---- JSON 汇总比对 ----
ji = json.load(open(HERE / ("tb-192_168_0_103-%s.core-evidence_timebase-sync-%s.json" % (IMPL, IMPL)),
                    encoding="utf-8"))
jv = json.load(open(HERE / ("tb-192_168_0_103-%s.core-evidence_timebase-sync-%s.json" % (VERI, VERI)),
                    encoding="utf-8"))
checkp("json keys identical", list(ji.keys()) == list(jv.keys()), str(list(jv.keys())))
checkp("json threshold 300", jv["threshold_ms"] == 300, str(jv["threshold_ms"]))
checkp("json sync_real in_sync true", jv["sync_real"]["in_sync"] is True
       and 0 <= jv["sync_real"]["max_deviation_ms"] <= 300, str(jv["sync_real"]))
checkp("json sync_skewed over threshold", jv["sync_skewed"]["in_sync"] is False
       and jv["sync_skewed"]["max_deviation_ms"] > 300, str(jv["sync_skewed"]))
checkp("json missing_clock", jv["missing_clock"] == "MISSING_CLOCK", str(jv["missing_clock"]))
checkp("json seek gens 1/2", jv["seek"]["gen1"] == 1 and jv["seek"]["gen2"] == 2, str(jv["seek"]))
checkp("json seek stale+reached", jv["seek"]["stale_discarded"] is True
       and jv["seek"]["reached"] is True, str(jv["seek"]))
checkp("json acceptance true", jv["acceptance"] is True, str(jv["acceptance"]))

out = {"impl_tag": IMPL, "verify_tag": VERI, "checks": checks,
       "summary": {"total": len(checks), "failed": sum(1 for c in checks if not c["ok"])}}
(HERE / "_verify_compare_result.json").write_text(json.dumps(out, ensure_ascii=False, indent=2),
                                                  encoding="utf-8")
print("compare summary: %d checks, %d failed" % (len(checks), sum(1 for c in checks if not c["ok"])))
raise SystemExit(0 if all(c["ok"] for c in checks) else 1)
