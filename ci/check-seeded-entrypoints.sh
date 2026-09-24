#!/usr/bin/env bash
#
# Seeded-entry-point gate — Sprint 2 Story 2.4.
#
# Every PQC operation must draw its randomness from OpenSSL's approved DRBG and
# pass it into qudo-pqc as an argument. qudo-pqc-lib also exposes non-seeded
# variants that generate randomness themselves, from platform entropy rather
# than the approved DRBG. Calling one inside the FIPS boundary would be a
# certification finding, and it would be invisible at runtime -- the code would
# work perfectly and produce keys from the wrong entropy source.
#
# This gate greps the delegation layer for those forbidden calls.
#
# Note QUDO_KEM_keypair_derand is forbidden despite its name: its seed argument
# is an OUTPUT, so it draws randomness internally. The seeded equivalent is
# QUDO_KEM_keypair_from_seed.
#
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# SLH-DSA is not delegated (ADR-0010), so it is not scanned.
SRC="${REPO_ROOT}/openssl/crypto/ml_kem ${REPO_ROOT}/openssl/crypto/ml_dsa"

# Non-seeded entry points: these obtain randomness themselves.
FORBIDDEN='QUDO_KEM_keypair|QUDO_KEM_keypair_derand|QUDO_KEM_encaps|QUDO_MLDSA_keypair|QUDO_MLDSA_sign|QUDO_SLHDSA_keypair|QUDO_SLHDSA_sign'
# ...except these seeded/deterministic forms, which are exactly what we want.
ALLOWED='QUDO_KEM_keypair_from_seed|QUDO_KEM_encaps_derand|QUDO_KEM_decaps|QUDO_MLDSA_keypair_internal|QUDO_MLDSA_sign_internal|QUDO_MLDSA_verify_internal|QUDO_SLHDSA_keypair_internal|QUDO_SLHDSA_sign_internal|QUDO_SLHDSA_verify_internal'

echo "==> scanning the delegation layer for non-seeded qudo entry points"

hits=0
for f in $(grep -rl 'QUDO_' ${SRC} --include='*.c' 2>/dev/null); do
    while IFS= read -r line; do
        # Strip anything matching an allowed name, then see if a forbidden
        # name still remains on the line.
        stripped=$(printf '%s' "${line}" | sed -E "s/(${ALLOWED})//g")
        # Match a forbidden base name plus any suffix, so self-randomizing
        # siblings (QUDO_MLDSA_sign_extmu, _concat, _with_context, _pre_hash,
        # QUDO_KEM_keypair_generate, ...) are caught, not just the bare name.
        if printf '%s' "${stripped}" | grep -qE "(${FORBIDDEN})[A-Za-z0-9_]*[[:space:]]*\("; then
            printf '  FAIL %s: %s\n' "${f#${REPO_ROOT}/}" "$(printf '%s' "${line}" | sed 's/^ *//')"
            hits=$((hits + 1))
        fi
    done < <(grep -nE "(${FORBIDDEN})[A-Za-z0-9_]*[[:space:]]*\(" "${f}" 2>/dev/null)
done

echo
if [ "${hits}" -eq 0 ]; then
    echo "SEEDED-ENTRYPOINT GATE PASSED — all delegated calls take caller-supplied randomness."
    exit 0
fi

cat >&2 <<EOF
SEEDED-ENTRYPOINT GATE FAILED — ${hits} non-seeded call site(s).

A non-seeded qudo entry point draws randomness from qudo-pqc's platform
entropy, not from OpenSSL's approved DRBG. Inside the FIPS boundary all key
and signature randomness must come from the approved DRBG.

Use instead:
  QUDO_KEM_keypair_from_seed / QUDO_KEM_encaps_derand / QUDO_KEM_decaps
  QUDO_MLDSA_keypair_internal / QUDO_MLDSA_sign_internal / QUDO_MLDSA_verify_internal
  QUDO_SLHDSA_keypair_internal / QUDO_SLHDSA_sign_internal / QUDO_SLHDSA_verify_internal

See docs/adr/ADR-0009 and build/build_libqudo_pqc.sh.
EOF
exit 1
