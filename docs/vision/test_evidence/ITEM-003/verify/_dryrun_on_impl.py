"""Dry-run the ITEM-003 compare oracle against the impl-r3 archive (FRESH alias to IMPL).

Zero board cost: catches oracle construction errors before judgement rounds.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

src_path = os.path.join(HERE, "verify_item003_compare.py")
src = open(src_path, encoding="utf-8").read()
src = src.replace('FRESH = "vision-item003-192_168_0_103-verify-20261019-item003-v1"',
                  'FRESH = IMPL')
src = src.replace('V = "verify-20261019-item003-v1"', 'V = T')
os.environ.setdefault("TRAIN_RK3588_SSH_CREDENTIAL", "dryrun-no-secret")
os.environ.setdefault("TRAIN_FRONT_RTSP_CREDENTIAL", "dryrun-front")
os.environ.setdefault("TRAIN_SIDE_RTSP_CREDENTIAL", "dryrun-side")
code = compile(src, src_path, "exec")
g = {"__file__": src_path, "__name__": "__main__"}
exec(code, g)
