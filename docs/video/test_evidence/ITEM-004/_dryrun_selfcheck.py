#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""离线自查 EXT 夹具正则（对已归档日志验证 A/C/F 形式），不连板。"""
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
log = (HERE / "lease-192_168_0_103-20261017-item004-verify-v1.log").read_text(
    encoding="utf-8", errors="replace")

# ---- C 用的正则应能解析 verify-v1 归档 ----
pre = re.search(r"type=lease_pre not_ready=(\w+) status=(\S+) plane_count=(\d+)", log)
first = re.search(r"type=lease_first valid=true camera=(\S+) origin=(\S+) format=(\S+) "
                  r"size=(\d+)x(\d+) colorimetry=(\S+) buffer_ref=(\S+) memory_kind=(\S+) "
                  r"planes=(\d+) buffer_bytes=(\d+) plane0_stride=(\d+) plane1_offset=(\d+) "
                  r"plane_ok=(\w+)", log)
shared = re.search(r"type=lease_shared use_count=(\d+) released_while_shared=(\d+)", log)
partial = re.search(r"type=lease_partial release_after_copies=(\d+)", log)
last = re.search(r"type=lease_last release_after_last=(\d+)", log)
summary = re.search(r"type=summary released_total=(\d+) final_not_ready=(\w+)", log)
conc = re.search(r"type=lease_concurrent consumers=(\d+) reads=(\d+)", log)

ok = []
ok.append(("pre", pre is not None and pre.groups() == ("true", "NOT_READY", "0")))
ok.append(("first", first is not None and first.group(10) == "1382400"))
ok.append(("shared", shared is not None))
ok.append(("partial", partial is not None))
ok.append(("last", last is not None and last.group(1) == "1"))
ok.append(("summary", summary is not None and summary.group(2) == "false"))
ok.append(("concurrent", conc is not None))
for name, r in ok:
    print("dryrun %s %s" % ("OK" if r else "MISS", name))
    if name == "first" and first:
        print("  groups:", first.groups())

# ---- A 情形: consumers=0 行形式（对真行做假想匹配） ----
line3 = "type=lease_concurrent consumers=3 reads=4405 max_use_count=3 published=77 rejected=0"
line0 = "type=lease_concurrent consumers=0 reads=0 max_use_count=0 published=1 rejected=0"
m0 = re.search(r"type=lease_concurrent consumers=0 reads=(\d+)", line0)
m3 = re.search(r"type=lease_concurrent consumers=0 reads=(\d+)", line3)
print("dryrun A regex vs consumers=0 line:", m0.group(0) if m0 else None)
print("dryrun A regex vs consumers=3 line (expect None):", m3)

# ---- published=1 时 released_total 语义（consumers=0 / duration 1）----
# test 源 1280x720 probe pump(50) 起码发一帧；A 段结束后进入 held 段。
# duration=1 主循环约 1s pump，published >= 1；released_total = published - 1 (store 持最新帧)。
# A 断言不绑 released_total（自然波动），只绑 structural。

# ---- B: --help 输出含 "--selftest" 字面量？ probe 源码用法行确实含 --selftest ----
print("dryrun done")
