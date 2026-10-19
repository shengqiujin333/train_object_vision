"""Offline dry-run: load verify_item009_compare_v2 as module, run impl+r7+neg oracle only."""
import importlib.util
import io
import json
import os
import sys
from contextlib import redirect_stdout

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location(
    "cmp", os.path.join(HERE, "verify_item009_compare_v2.py"))
cmp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cmp)

base = os.path.normpath(os.path.join(HERE, ".."))
impl = "vision-item009-192_168_0_103-20261021-item009-r8"
p = lambda tag, suffix: os.path.join(base, "%s.%s" % (tag, suffix))

st = cmp.parse_selftest(cmp.rd(p(impl, "app_core-evidence_item009-selftest-20261021-item009-r8.log")))
ch = cmp.parse_chain(cmp.rd(p(impl, "app_core-evidence_item009-chain-20261021-item009-r8.log")))
c4 = cmp.parse_chain4(cmp.rd(p(impl, "app_core-evidence_item009-chain4-20261021-item009-r8.log")))
junit = cmp.parse_junit(cmp.rd(p(impl, "app_core-evidence_item009-core-tests-20261021-item009-r8.xml")))
ng = cmp.parse_neg(cmp.rd(p(impl, "app_core-evidence_item009-neg-missing-20261021-item009-r8.log")))
r7 = cmp.parse_r7(cmp.rd(os.path.join(
    base, "vision-item009-192_168_0_103-20261020-item009-r7"
          ".app_core-evidence_item009-ai-runner-20261020-item009-r7.log")))

n_before = len(cmp.EVID)
cmp.oracle("impl", st, ch, c4, junit, r7)
n_oracle = len(cmp.EVID) - n_before
cmp.neg_oracle("impl", ng)
n_neg = len(cmp.EVID) - n_before - n_oracle

fails = [e for e in cmp.EVID if not e["ok"]]
for f in fails:
    print("FAIL:", f["step"], "|", f["detail"][:200])
print("oracle=%d neg=%d total=%d fails=%d" % (n_oracle, n_neg, len(cmp.EVID), len(fails)))
sys.exit(1 if fails else 0)
