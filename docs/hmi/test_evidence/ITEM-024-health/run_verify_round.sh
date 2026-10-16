#!/usr/bin/env bash
# ITEM-024 verification round driver (verifier-owned).
# Credentials are loaded from the Windows User env via PowerShell, exported
# only inside this process and its children; the credential value is never
# printed. Default stamp: VERIFY-20261016-v1.
set -u
V="$(powershell.exe -NoProfile -Command "[Environment]::GetEnvironmentVariable('TRAIN_RK3588_SSH_CREDENTIAL','User')" | tr -d '\r\n')"
if [ -z "$V" ]; then echo "credential empty" >&2; exit 2; fi
export TRAIN_RK3588_SSH_CREDENTIAL="$V"
cd "E:/work/rail_issue/rk3588_handover" || exit 1
STAMP="${VERIFY_STAMP:-VERIFY-20261016-v1}"
OUT="docs/hmi/test_evidence/ITEM-024-health"
mkdir -p "$OUT"
export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL="*"
python "docs/hmi/test_evidence/ITEM-024-health/verify_health24_fixtures.py" \
  --host 192.168.0.103 --user admin \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL \
  --expected-fingerprint "SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU" \
  --cpp "docs/hmi/test_evidence/ITEM-024-health/verify_health24_ext.cpp" \
  --impl-testrun-log "docs/hmi/test_evidence/ITEM-024-health/build-TestRun-192_168_0_103-20261016-ITEM024-r2.core-evidence_core-tests-20261016-ITEM024-r2.log" \
  --impl-healthprobe-log "docs/hmi/test_evidence/ITEM-024-health/build-TestRun-192_168_0_103-20261016-ITEM024-r2.core-evidence_health-probe-20261016-ITEM024-r2.log" \
  --outdir "$OUT" --stamp "$STAMP" \
  --connect-timeout 10 --timeout-sec 1500
rc=$?
echo "FIXTURES_RC=$rc"
exit $rc
