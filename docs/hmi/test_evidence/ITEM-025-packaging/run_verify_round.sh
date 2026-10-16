#!/usr/bin/env bash
# ITEM-025 software_verification round driver (verifier-owned).
# Subject: implementer round stamp 20261016-ITEM025-r1 (commit 6d1787b).
# Credentials are loaded from the Windows User env via PowerShell, exported
# only inside this process and its children; the credential value is never
# printed. Default stamp: VERIFY-20261017-v1.
set -u
BASE="E:/work/rail_issue/rk3588_handover"
EV="$BASE/docs/hmi/test_evidence/ITEM-025-packaging"
STAMP="${VERIFY_STAMP:-VERIFY-20261017-v1}"

cd "$BASE" || exit 1

echo "== offline self-check 0 (artifacts + oracle tables) =="
python "$EV/offline_selfcheck0.py" || { echo "OFFLINE0_RC=$?"; exit 1; }
echo "OFFLINE0_RC=0"

echo "== offline self-check 1 (local scan + delivered checker + neg dry-run) =="
python "$EV/offline_selfcheck1.py" || { echo "OFFLINE1_RC=$?"; exit 1; }
echo "OFFLINE1_RC=0"

V="$(powershell.exe -NoProfile -Command "[Environment]::GetEnvironmentVariable('TRAIN_RK3588_SSH_CREDENTIAL','User')" | tr -d '\r\n')"
if [ -z "$V" ]; then echo "credential empty" >&2; exit 2; fi
export TRAIN_RK3588_SSH_CREDENTIAL="$V"
export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL="*"

OUT="$EV"
python "$EV/verify_packaging25_fixtures.py" \
  --host 192.168.0.103 --user admin \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL \
  --expected-fingerprint "SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU" \
  --impl-testrun-log "$EV/build-TestRun-192_168_0_103-20261016-ITEM025-r1.core-evidence_core-tests-20261016-ITEM025-r1.log" \
  --impl-faultprobe-log "$EV/build-TestRun-192_168_0_103-20261016-ITEM025-r1.core-evidence_fault-probe-20261016-ITEM025-r1.log" \
  --impl-sim-log "$EV/build-TestRun-192_168_0_103-20261016-ITEM025-r1.core-evidence_sim-inject-20261016-ITEM025-r1.log" \
  --outdir "$OUT" --stamp "$STAMP" \
  --connect-timeout 10 --timeout-sec 1500
rc=$?
echo "FIXTURES_RC=$rc"
exit $rc
