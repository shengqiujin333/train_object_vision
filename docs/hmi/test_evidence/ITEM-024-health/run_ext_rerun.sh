#!/usr/bin/env bash
# ITEM-024 ext-fixture re-run driver (v2 expectations), verifier-owned.
set -u
V="$(powershell.exe -NoProfile -Command "[Environment]::GetEnvironmentVariable('TRAIN_RK3588_SSH_CREDENTIAL','User')" | tr -d '\r\n')"
if [ -z "$V" ]; then echo "credential empty" >&2; exit 2; fi
export TRAIN_RK3588_SSH_CREDENTIAL="$V"
cd "E:/work/rail_issue/rk3588_handover" || exit 1
export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL="*"
python "docs/hmi/test_evidence/ITEM-024-health/verify_health24_ext_rerun.py" \
  --expected-fingerprint "SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU" \
  --cpp "docs/hmi/test_evidence/ITEM-024-health/verify_health24_ext.cpp" \
  --outdir "docs/hmi/test_evidence/ITEM-024-health" \
  --stamp "VERIFY-20261016-v2"
rc=$?
echo "RERUN_RC=$rc"
exit $rc
