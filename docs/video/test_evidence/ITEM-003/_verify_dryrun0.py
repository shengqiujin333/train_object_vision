#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""offline dry-run: 本地用 impl r2 证据跑 ext 解析器与判据构造（零板端成本）。

software_verification 惯例离线自检（ITEM-020..024 同款）：预推断言域，防夹具期望错
变成假 FAIL / 假 PASS。验完即归档为 _verify_dryrun0.py。
"""
import importlib.util
import re
from datetime import datetime, timezone

HERE = "."
spec = importlib.util.spec_from_file_location("ve", "_verify_ext.py")
ve = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ve)

raw = open("identity-192_168_0_103-20261017-item003-r2.core-evidence_identity-20261017-item003-r2.log",
           encoding="utf-8").read()
samples, summary, rebuild, gate, inj = ve.parse_streams(raw)
pre = [s for s in samples if s["gen"] == 1]
post = [s for s in samples if s["gen"] == 2]
sid = re.search(r"type=injection .*?session_id=(\S+)", raw).group(1)
sid_reb = re.search(r"type=rebuild .*?session_id=(\S+)", rebuild)
print("parsed samples=%d pre=%d post=%d sid=%s" % (len(samples), len(pre), len(post), sid))
print("rebuild_seen=%s gate_stale=%s sid_match=%s seq_reset=%s" % (
    "generation=2" in rebuild, "stale_old_generation=stale_generation" in gate,
    sid_reb and sid_reb.group(1) == sid, bool(post) and post[0]["seq"] == 1))

dt0 = datetime.fromtimestamp(samples[0]["wall_ms"] / 1000, timezone.utc)
dtl = datetime.fromtimestamp(samples[-1]["wall_ms"] / 1000, timezone.utc)
pts_ok = all(0 <= s["pts_ns"] < (1 << 33) for s in samples)
pts_reset = post[0]["pts_ns"] < pre[-1]["pts_ns"]
print("utc0=%s utcl=%s monotonic_wall=%s pts_ok=%s pts_reset=%s" % (
    dt0, dtl, samples[0]["wall_ms"] < samples[-1]["wall_ms"], pts_ok, pts_reset))

required = ["final_state=live", "session_id=", "generation=2", "frames_emitted=",
            "gate_admitted=", "gate_rejected="]
print("summary_keys_missing=%s" % [k for k in required if k not in summary])

# compare 范式
spec2 = importlib.util.spec_from_file_location("vc", "_verify_compare.py")
vc = importlib.util.module_from_spec(spec2)
spec2.loader.exec_module(vc)
a = [vc.norm(x) for x in raw.splitlines()]
print("compare norm sample: %r" % a[2])

# v1 round EXT-8 fixed criterion dry-run on impl r2 data
from datetime import datetime, timezone
samples8, *_ = ve.parse_streams(raw)
pre8 = [s for s in samples8 if s["gen"] == 1]
post8 = [s for s in samples8 if s["gen"] == 2]
dt = datetime.fromtimestamp(samples8[0]["wall_ms"] / 1000, timezone.utc)
dt2 = datetime.fromtimestamp(samples8[-1]["wall_ms"] / 1000, timezone.utc)
date_ok = (samples8[0]["wall_ms"] < samples8[-1]["wall_ms"] and
           dt.year == 2026 and dt.month == 10 and dt2.year == 2026 and dt.date() == dt2.date())
pts_ok = all(0 <= s["pts_ns"] < (1 << 33) for s in samples8)
pts_reset = bool(len(pre8) >= 2 and len(post8) >= 2 and post8[0]["pts_ns"] < pre8[-1]["pts_ns"])
print("EXT8-fixed: date_ok=%s pts_ok=%s pts_reset=%s pre=%d post=%d pre_last=%s post0=%s"
      % (date_ok, pts_ok, pts_reset, len(pre8), len(post8), pre8[-1]["pts_ns"], post8[0]["pts_ns"]))
