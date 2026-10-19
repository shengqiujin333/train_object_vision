#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-011 板前 dry-run：V-CMP oracle 对 impl r3 归档预演（证明判据非空转）。

impl r3 无 --keep-remote（cleanup=cleaned 在场），文件清单与其自身一致；
C8_manifest_covers_verify_report 引用验证轮产物（VERIFY-ITEM-011.md），
对 impl 归档必然缺席——dry-run 排除该条后判据应全绿。
"""
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
stamp = sys.argv[1] if len(sys.argv) > 1 else "20261021-item011-r3"
out = os.path.join(HERE, "item011-compare-%s.json" % stamp)
rc = subprocess.call([sys.executable, os.path.join(HERE, "verify_item011_compare.py"),
                      "--fresh-stamp", stamp, "--impl-stamp", stamp])
d = json.load(open(out, encoding="utf-8"))
relevant = [r for r in d["checks"] if r["name"] != "C8_manifest_covers_verify_report"]
bad = [r["name"] for r in relevant if not r["ok"]]
print("DRYRUN relevant=%d failed=%d %s" % (len(relevant), len(bad), bad or "ALL_GREEN"))
sys.exit(0 if not bad else 1)
