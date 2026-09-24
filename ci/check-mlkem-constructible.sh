#!/usr/bin/env bash
#
# ML-KEM constructibility gate — Sprint 2 Story 2.9 / audit finding A1.
#
# ADR-0009 mandates consuming qudo-pqc-lib as a standard build with no call to
# qudo_pqc_init(). In that configuration QUDO_KEM_init() fails its RNG self-test
# and both constructors return NULL, so ML-KEM cannot be used at all — every
# seeded ML-KEM entry point takes a QUDO_KEM handle and there is no handle-free
# path.
#
# This gate proves the library is usable in the configuration QudoSSL actually
# ships. It deliberately never calls qudo_pqc_init(), unlike qudo-pqc-lib's own
# test suite, which bootstraps through it and therefore reports 80/80 passing
# while this failure is live (audit finding A8).
#
# EXPECTED TO FAIL until Story 2.9 lands upstream.
#
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARCHIVE="${REPO_ROOT}/qudo-pqc-lib/build/lib/libqudo-pqc.a"
WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

if [ ! -f "${ARCHIVE}" ]; then
    echo "error: ${ARCHIVE} not built. Run 'make -C build qudo-pqc' first." >&2
    exit 2
fi

cat > "${WORK}/probe.c" <<'EOF'
/* No qudo_pqc_init() — exactly how QudoSSL will consume the library. */
#include <stdio.h>
#include "mlkem_wrapper.h"

int main(void)
{
    int rc = QUDO_KEM_init();
    const QUDO_KEM *k = QUDO_KEM_new("ML-KEM-768");

    printf("  QUDO_KEM_init()            = %d\n", rc);
    printf("  QUDO_KEM_new(ML-KEM-768)   = %s\n", k ? "OK" : "NULL");

    return (rc == 0 && k != NULL) ? 0 : 1;
}
EOF

cc -I "${REPO_ROOT}/qudo-pqc-lib/include" \
   -I "${REPO_ROOT}/qudo-pqc-lib/qudo-mlkem/include" \
   -o "${WORK}/probe" "${WORK}/probe.c" "${ARCHIVE}" 2>"${WORK}/cc.log" || {
    echo "error: probe failed to link" >&2; sed 's/^/    /' "${WORK}/cc.log" >&2; exit 2; }

echo "==> constructing ML-KEM without qudo_pqc_init()"
if "${WORK}/probe"; then
    echo "ML-KEM CONSTRUCTIBILITY GATE PASSED"
    exit 0
fi

cat >&2 <<EOF

ML-KEM CONSTRUCTIBILITY GATE FAILED.

QUDO_KEM_init() runs an RNG self-test requiring qudo's CTR-DRBG to be seeded,
which happens only inside qudo_pqc_init() — the call ADR-0009 forbids. Both
constructors return NULL, and every seeded ML-KEM entry point requires a handle.

Consequence: once Story 2.0 links the archive, ossl_ml_kem_genkey would call
QUDO_KEM_new(), get NULL, and ML-KEM keygen/encap/decap would fail in both
providers — so OpenSSL's ML-KEM POST KAT fails and fips.so refuses to load.

Root cause: gated on QUDO_COMBINED_BUILD (qudo-pqc-lib/CMakeLists.txt:174,
unconditional), not on QUDO_FIPS_MODULE.

Fix belongs upstream. See docs/adr/ADR-0009 (Correction) and Sprint 2 Story 2.9.
EOF
exit 1
