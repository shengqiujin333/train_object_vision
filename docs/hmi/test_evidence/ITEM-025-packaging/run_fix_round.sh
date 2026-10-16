#!/usr/bin/env bash
# ITEM-025 fix-round board verification driver (verifier-owned), stamp v4.
# Runs the deliverable TestRun entry + ext fixture + deployment scan +
# negative controls on 192.168.0.103 with finite timeouts.
set -u
BASE="E:/work/rail_issue/rk3588_handover"
EV="$BASE/docs/hmi/test_evidence/ITEM-025-packaging"
STAMP="${VERIFY_STAMP:-VERIFY-20261017-v4}"

cd "$BASE" || exit 1
V="$(powershell.exe -NoProfile -Command "[Environment]::GetEnvironmentVariable('TRAIN_RK3588_SSH_CREDENTIAL','User')" | tr -d '\r\n')"
if [ -z "$V" ]; then echo "credential empty" >&2; exit 2; fi
export TRAIN_RK3588_SSH_CREDENTIAL="$V"
export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL="*"

python "$EV/verify_packaging25_fixround.py" \
  --host 192.168.0.103 --user admin \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL \
  --expected-fingerprint "SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU" \
  --outdir "$EV" --stamp "$STAMP" \
  --connect-timeout 10 --timeout-sec 1800
rc=$?
echo "VERIFY_V4_RC=$rc"
# driver rc1 = some assertion failed; rc2 = connect/credential env problem
exit $rc
