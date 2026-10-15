#!/usr/bin/env bash
# ITEM-012 software verification round driver (verifier-owned).
# Credentials are imported into this process environment and exported to children;
# the credential value itself is never printed. Default stamp: VERIFY-20261016-v1
# (override with VERIFY_STAMP).
set -u
V="${TRAIN_RK3588_SSH_CREDENTIAL:-}"
if [ -z "$V" ]; then
  echo "TRAIN_RK3588_SSH_CREDENTIAL not loaded into this process; refusing to guess" >&2
  exit 2
fi
export TRAIN_RK3588_SSH_CREDENTIAL="$V"
cd "E:/work/rail_issue/rk3588_handover" || exit 1
STAMP="${VERIFY_STAMP:-VERIFY-20261016-v1}"
OUT="docs/hmi/test_evidence/ITEM-012-registry"
mkdir -p "$OUT"
export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL="*"
python "docs/hmi/test_evidence/ITEM-012-registry/verify_registry_fixtures.py" \
  --host 192.168.0.103 --user admin \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL \
  --expected-fingerprint "SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU" \
  --cpp "docs/hmi/test_evidence/ITEM-012-registry/verify_registry_ext.cpp" \
  --impl-log "docs/hmi/test_evidence/ITEM-012-registry/build-TestRun-192_168_0_103-20261015-ITEM012-r2.core-evidence_core-tests-20261015-ITEM012-r2.log" \
  --outdir "$OUT" --stamp "$STAMP" \
  --connect-timeout 10 --timeout-sec 1200
rc=$?
echo "FIXTURES_RC=$rc"
exit $rc
