#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-014 验证侧独立解析比对（VERIFY-20261017-v1）。

实现方 r2 归档 vs 验证侧 fresh 复跑（verify-v1）core-evidence 逐字段对齐；
timing/generations/recovery_latency_ms 为自然时序波动域，按字段归一后比对。
另独立核对 selftest 判例、core-tests 尾行、JSON 汇总。
"""
import json
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
IMPL = "20261017-item014-r2"
VERI = "20261017-item014-verify-v1"

# 自然波动字段：值随运行时刻变化，比对存在性不比对数值
FLUCT = {"recovery_latency_ms"}


def load_core(tag):
    return open(HERE / ("recov-192_168_0_103-%s.core-evidence_recovery-%s.log" % (tag, tag)),
                encoding="utf-8", errors="replace").read()


def kv_line(line):
    kv = {}
    for tok in line.split(" "):
        if "=" in tok:
            k, v = tok.split("=", 1)
            kv[k] = v
    return kv


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


checks = []


def check(name, ok, detail=""):
    checks.append({"name": name, "ok": bool(ok), "detail": str(detail)[:300]})
    print("compare %s %s %s" % ("PASS" if ok else "FAIL", name, "" if ok else str(detail)[:200]))


impl_rows = parse_core(load_core(IMPL))
veri_rows = parse_core(load_core(VERI))
check("row count %d" % len(impl_rows), len(impl_rows) == len(veri_rows),
      "impl=%d veri=%d" % (len(impl_rows), len(veri_rows)))

for i, (a, b) in enumerate(zip(impl_rows, veri_rows)):
    if a.get("type") == "timing":
        # timing 行为调度时刻（tick 25ms 粒度内的自然抖动域）：只比键集，
        # 语义（退避/门控先后）已在 C2/C4 独立重推导。
        check("row%02d type=timing keyset" % i, set(a.keys()) == set(b.keys()),
              "%s vs %s" % (sorted(a), sorted(b)))
        continue
    ok, why = rows_equal_ignoring_fluct(a, b)
    check("row%02d type=%s" % (i, a.get("type", "?")), ok, why)

# 行类型序列逐行 type 相同（含 fluct 行本身也存在）
check("row types identical", [r.get("type") for r in impl_rows] == [r.get("type") for r in veri_rows])

# ---- 关键判据独立重推导（不采信 acceptance 行）----
veri = {r.get("type"): r for r in veri_rows}
rec1 = next(r for r in veri_rows if r.get("type") == "recovery" and r.get("cam") == "CAM-01")
rec2 = next(r for r in veri_rows if r.get("type") == "recovery" and r.get("cam") == "CAM-02")
rec3 = next(r for r in veri_rows if r.get("type") == "recovery" and r.get("cam") == "CAM-03")
gens = next(r for r in veri_rows if r.get("type") == "generations")
timing = next(r for r in veri_rows if r.get("type") == "timing")
home = next(r for r in veri_rows if r.get("type") == "home")
acc = next(r for r in veri_rows if r.get("type") == "acceptance")

base_backoff = int(veri["config"]["reconnect_backoff_ms"])
t_cut = int(timing["t_cut_ms"])
rebuild1 = int(timing["rebuild1_ms"])
rebuild3 = int(timing["rebuild3_ms"])
change = int(timing["change_ms"])

# 判据 1: 断流重建由真帧确认（generation 前进 + recovered_generation=2 + attempts=1）
check("C1 rebuild confirmed by new frame",
      rec1.get("recovered") == "true" and int(rec1.get("recovered_generation", "0")) >= 2
      and int(rec1.get("attempts", "0")) == 1 and rec1.get("kind") == "NETWORK",
      rec1)
# 判据 2: 退避遵守（rebuild1 - t_cut >= base_backoff - 50，与交付同容差）
check("C2 backoff respected >= %dms" % (base_backoff - 50),
      rebuild1 >= 0 and (rebuild1 - t_cut) >= base_backoff - 50,
      "t_cut=%d rebuild1=%d" % (t_cut, rebuild1))
# 判据 3: 鉴权不重试（stopped + attempts=0 + generation 不变 + 仍出帧——由单测/复跑确认 CAM-02 帧）
check("C3 auth no retry",
      rec2.get("stopped") == "true" and int(rec2.get("attempts", "1")) == 0
      and rec2.get("kind") == "AUTH_FAILED",
      rec2)
# 判据 4: 能力门控（change 前无 rebuild、change 后恰一次且真帧确认）
check("C4 capability gated",
      rebuild3 >= 0 and rebuild3 >= change
      and int(rec3.get("attempts", "0")) == 1 and rec3.get("recovered") == "true"
      and rec3.get("kind") == "UNSUPPORTED",
      "rebuild3=%d change=%d rec3=%s" % (rebuild3, change, rec3))
# 判据 5: 单路隔离（CAM-02 generation 不变；CAM-01/CAM-03 前进）
g2b, g2a = gens["cam02"].split("->")
check("C5 single channel isolation",
      g2b == g2a and gens["cam01"].split("->")[1] > gens["cam01"].split("->")[0]
      and gens["cam03"].split("->")[1] > gens["cam03"].split("->")[0],
      gens)
# 判据 6: ONVIF 默认关闭 + 启用未配置显式 UNCONFIGURED 且不动作
check("C6 onvif default off", rec1.get("onvif") == "disabled", rec1.get("onvif"))
check("C7 onvif unconfigured no tick",
      home.get("onvif") == "unavailable:UNCONFIGURED", home.get("onvif"))
# 判据 8: 交付 acceptance 行与独立判据一致
expect = {"rebuild_confirmed_by_new_frame": "true", "attempts_counted_once": "true",
          "backoff_respected": "true", "auth_no_retry": "true", "capability_gated": "true",
          "single_channel_isolation": "true", "onvif_default_off": "true",
          "onvif_unconfigured": "true", "result": "PASS"}
check("C8 acceptance row matches independent verdict", all(acc.get(k) == v for k, v in expect.items()), acc)

# ---- selftest 判例两轮一致 ----
def selftest_lines(tag):
    t = open(HERE / ("recov-192_168_0_103-%s.log" % tag), encoding="utf-8", errors="replace").read()
    m = re.search(r"===== \[selftest\] exit=(\S+) =====\n(.*?)(?=\n===== \[)", t, re.S)
    return m.group(1), m.group(2)

si_exit, si_body = selftest_lines(IMPL)
sv_exit, sv_body = selftest_lines(VERI)
check("selftest exit identical", si_exit == sv_exit == "0", "%s/%s" % (si_exit, sv_exit))
si_cases = sorted(re.findall(r"selftest (?:PASS|FAIL) (\S+)", si_body))
sv_cases = sorted(re.findall(r"selftest (?:PASS|FAIL) (\S+)", sv_body))
check("selftest 10 cases identical lists", si_cases == sv_cases and len(si_cases) == 10,
      "%d/%d %s" % (len(si_cases), len(sv_cases), si_cases))
check("selftest no FAIL lines", "FAIL" not in si_body and "FAIL" not in sv_body.replace("FAILED", ""))

# ---- core-tests 尾行一致（PASS 列表逐行，除 stamp/junit 计数） ----
def core_passes(tag):
    t = open(HERE / ("recov-192_168_0_103-%s.log" % tag), encoding="utf-8", errors="replace").read()
    m = re.search(r"===== \[core-tests\] exit=(\S+) =====\n(.*?)(?=\n===== \[)", t, re.S)
    return m.group(1), m.group(2)

ci_exit, ci_body = core_passes(IMPL)
cv_exit, cv_body = core_passes(VERI)
ci_pass = re.findall(r"^\[PASS\] (.*)$", ci_body, re.M)
cv_pass = re.findall(r"^\[PASS\] (.*)$", cv_body, re.M)
check("core-tests exit identical", ci_exit == cv_exit == "0")
check("core-tests PASS lists identical (%d lines)" % len(ci_pass), ci_pass == cv_pass)
check("core-tests 857/0 both",
      "== 857 passed, 0 failed ==" in ci_body and "== 857 passed, 0 failed ==" in cv_body)

# ---- JSON 汇总比对（recovery_latency/generation 为自然波动，只比结构域） ----
ji = json.load(open(HERE / ("recov-192_168_0_103-%s.core-evidence_recovery-%s.json" % (IMPL, IMPL)),
                    encoding="utf-8"))
jv = json.load(open(HERE / ("recov-192_168_0_103-%s.core-evidence_recovery-%s.json" % (VERI, VERI)),
                    encoding="utf-8"))
check("json keys identical", list(ji.keys()) == list(jv.keys()), str(list(jv.keys())))
check("json cam02 stopped attempts0", jv["cam02"]["stopped"] is True and jv["cam02"]["attempts"] == 0, str(jv["cam02"]))
check("json cam01 attempts1 recovered", jv["cam01"]["attempts"] == 1 and jv["cam01"]["recovered"] is True, str(jv["cam01"]))
check("json cam03 attempts1", jv["cam03"]["attempts"] == 1, str(jv["cam03"]))
check("json acceptance true", jv["acceptance"] is True, str(jv["acceptance"]))

out = {"impl_tag": IMPL, "verify_tag": VERI, "checks": checks,
       "summary": {"total": len(checks), "failed": sum(1 for c in checks if not c["ok"])}}
(HERE / "_verify_compare_result.json").write_text(json.dumps(out, ensure_ascii=False, indent=2),
                                                  encoding="utf-8")
print("compare summary: %d checks, %d failed" % (len(checks), sum(1 for c in checks if not c["ok"])))
raise SystemExit(0 if all(c["ok"] for c in checks) else 1)
