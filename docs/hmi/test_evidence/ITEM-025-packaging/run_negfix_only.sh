#!/usr/bin/env bash
# ITEM-025 fix-round negative-control-only board step (verifier-owned).
# Complements VERIFY-20261017-v4: v4 passed everything except the negative
# control, which failed due to a verifier fixture bug (re-makedirs) fixed
# and host-pretested. This driver uploads only the checker-required file set.
set -u
BASE="E:/work/rail_issue/rk3588_handover"
EV="$BASE/docs/hmi/test_evidence/ITEM-025-packaging"
STAMP="${NEGFIX_STAMP:-VERIFY-20261017-v4b}"

cd "$BASE" || exit 1
V="$(powershell.exe -NoProfile -Command "[Environment]::GetEnvironmentVariable('TRAIN_RK3588_SSH_CREDENTIAL','User')" | tr -d '\r\n')"
if [ -z "$V" ]; then echo "credential empty" >&2; exit 2; fi
export TRAIN_RK3588_SSH_CREDENTIAL="$V"
export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL="*"

python "$EV/verify_packaging25_negfix_only.py" \
  --host 192.168.0.103 --user admin \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL \
  --expected-fingerprint "SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU" \
  --outdir "$EV" --stamp "$STAMP" \
  --connect-timeout 10
rc=$?
echo "NEGFIX_RC=$rc"
exit $rc
