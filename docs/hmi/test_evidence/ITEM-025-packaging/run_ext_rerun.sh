#!/usr/bin/env bash
# ITEM-025 verification re-run driver v2: only the two v1-failed fixture steps
# (ext compile/run + negative control) plus a re-confirmation packaging scan,
# after a fresh TestRun rebuild. Verifier-owned.
set -u
BASE="E:/work/rail_issue/rk3588_handover"
EV="$BASE/docs/hmi/test_evidence/ITEM-025-packaging"
STAMP="${VERIFY_STAMP:-VERIFY-20261017-v2}"

cd "$BASE" || exit 1
V="$(powershell.exe -NoProfile -Command "[Environment]::GetEnvironmentVariable('TRAIN_RK3588_SSH_CREDENTIAL','User')" | tr -d '\r\n')"
if [ -z "$V" ]; then echo "credential empty" >&2; exit 2; fi
export TRAIN_RK3588_SSH_CREDENTIAL="$V"
export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL="*"

python "$EV/verify_packaging25_ext_rerun.py" \
  --host 192.168.0.103 --user admin \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL \
  --expected-fingerprint "SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU" \
  --outdir "$EV" --stamp "$STAMP" \
  --connect-timeout 10 --timeout-sec 1500
rc=$?
echo "RERUN_RC=$rc"
exit $rc
