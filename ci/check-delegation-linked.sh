#!/bin/sh
# Assert the built artifacts really are QudoSSL -- i.e. that crypto-layer
# delegation (ADR-0005) is compiled in and linked, not merely configured.
#
# Why this exists: the Windows job passed for an entire sprint while building
# stock upstream OpenSSL. build.ps1 built the qudo archive, located it, and
# then never passed --with-qudo-pqc-archive to Configure, so QUDO_PQC_DELEGATE
# was undefined and OpenSSL's own reference ML-KEM/ML-DSA math was compiled
# instead. Every existing check still passed: the libraries existed, the
# archive existed, and the test suite went green -- because upstream OpenSSL
# passes upstream OpenSSL's tests.
#
# An artifact-exists check cannot catch that. Only a positive assertion that
# qudo's math is actually inside the shipped binaries can. Run this in every
# OE's verify step.
#
# Usage: ci/check-delegation-linked.sh [openssl-build-dir]
set -eu

OSSL_DIR="${1:-openssl}"
fail=0

note() { printf '  %s\n' "$1"; }
bad()  { printf '  FAIL: %s\n' "$1" >&2; fail=1; }

echo "==> checking crypto-layer delegation in ${OSSL_DIR}"

# 1. Configure must have defined the macro.
if [ -f "${OSSL_DIR}/configdata.pm" ]; then
    if grep -q 'QUDO_PQC_DELEGATE' "${OSSL_DIR}/configdata.pm"; then
        note "configdata.pm defines QUDO_PQC_DELEGATE"
    else
        bad "configdata.pm does NOT define QUDO_PQC_DELEGATE -- Configure was"
        bad "run without --with-qudo-pqc-archive/--with-qudo-pqc-include, so"
        bad "this build is upstream OpenSSL, not QudoSSL."
    fi
else
    bad "no configdata.pm under ${OSSL_DIR} -- tree not configured"
fi

# 2. qudo math entry points must be linked into the shipped artifacts. Check
#    every artifact that exists, so this works on .so, .dylib and .dll layouts.
found_any=0
# Use `nm` (ALL symbols), NOT `nm -g` (exported/global only). On Linux, OpenSSL
# links libcrypto.so / fips.so with a version script that LOCALIZES every
# non-public symbol, so the qudo math is present but invisible to `nm -g`; the
# static libcrypto.a still lists it. macOS does not localize, so `nm -g` worked
# there but is not portable. We therefore count over all symbols and require the
# delegated math to be present in AT LEAST ONE artifact (the static archive is
# the reliable cross-platform witness) rather than in every one -- a
# version-scripted .so legitimately shows zero under any exported-only view.
total=0
for lib in \
    "${OSSL_DIR}/libcrypto.so" "${OSSL_DIR}/libcrypto.dylib" \
    "${OSSL_DIR}/libcrypto.a" \
    "${OSSL_DIR}/providers/fips.so" "${OSSL_DIR}/providers/fips.dylib"
do
    [ -f "${lib}" ] || continue
    found_any=1
    n=$(nm "${lib}" 2>/dev/null | awk '{print $NF}' | sed 's/^_//' \
        | grep -cE '^QUDO_(KEM|MLDSA)_' || true)
    note "$(basename "${lib}"): ${n} qudo math symbols"
    total=$((total + n))
done

if [ "${found_any}" -eq 0 ]; then
    bad "no libcrypto/fips artifact found under ${OSSL_DIR} -- nothing verified."
elif [ "${total}" -eq 0 ]; then
    bad "no QUDO_KEM_/QUDO_MLDSA_ symbols in ANY built artifact -- the delegated"
    bad "  math is absent; this build is upstream OpenSSL, not QudoSSL."
fi

echo
if [ "${fail}" -eq 0 ]; then
    echo "DELEGATION CHECK PASSED -- artifacts contain the delegated PQC math."
    exit 0
fi

cat >&2 <<'EOF'

DELEGATION CHECK FAILED.

The build produced working OpenSSL binaries that do NOT use qudo-pqc-lib for
ML-KEM/ML-DSA. Such a build will pass the OpenSSL test suite and every
artifact-exists check while certifying the wrong product.

Confirm the Configure line passes:
    --with-qudo-pqc-archive=<path to libqudo-pqc.a / qudo-pqc.lib>
    --with-qudo-pqc-include=<qudo-pqc-lib>/include
    --with-qudo-pqc-include=<qudo-pqc-lib>/qudo-mlkem/include
    --with-qudo-pqc-include=<qudo-pqc-lib>/qudo-mldsa/include
    --with-qudo-pqc-include=<qudo-pqc-lib>/qudo-slhdsa/include

See build/Makefile (Unix) and build/build.ps1 (Windows).
EOF
exit 1
