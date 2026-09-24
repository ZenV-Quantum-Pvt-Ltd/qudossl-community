#!/usr/bin/env bash
#
# Stage 1 of the QudoSSL build: compile qudo-pqc-lib as a STANDARD static
# archive. qudo-pqc-lib supplies the PQC algorithm math and nothing else.
#
# Under crypto-layer delegation (ADR-0005 / D2), OpenSSL owns the entire FIPS
# apparatus -- POST KATs, CAST, PCT, integrity HMAC, state machine and the
# approved DRBG. Building qudo-pqc-lib as a FIPS module would link a second,
# redundant copy of that machinery toward the cert boundary. See ADR-0009.
#
# Consequences for callers, which are load-bearing:
#
#   * -DQUDO_PQC_MATH_ONLY=ON. This is what actually keeps qudo-pqc's FIPS
#     services out of the boundary: its POST, PCT, integrity check, CTR-DRBG,
#     AES and HMAC are not compiled at all. Merely omitting QUDO_FIPS_MODULE is
#     NOT sufficient -- the entanglement is gated on QUDO_COMBINED_BUILD, which
#     was unconditional. See ADR-0009's Correction section.
#
#   * NO -DQUDO_FIPS_MODULE, and none of the boundary-dedupe flags: all of them
#     hard-require it upstream, and math-only supersedes them. Epic 2 is moot.
#
#   * -DBUILD_SHARED_LIBS=OFF. Static linkage is mandatory (design 12.3 RULE):
#     the integrity HMAC must cover the algorithm code in one .text region.
#
#   * The delegation layer in crypto/ml_kem, crypto/ml_dsa and crypto/slh_dsa
#     MUST call the SEEDED entry points and supply randomness drawn from
#     OpenSSL's approved DRBG. Names verified with nm -g:
#         QUDO_KEM_keypair_from_seed        (const seed IN)
#         QUDO_KEM_encaps_derand            (const randomness[32] IN)
#         QUDO_KEM_decaps                   (deterministic)
#         mldsa_ref{44,65,87}_keypair_internal  (+ _neon / _avx2 variants)
#     (SLH-DSA is not delegated -- see ADR-0010.)
#     NOTE: QUDO_KEM_keypair_derand is NOT in that list -- despite the name its
#     seed argument is an OUTPUT, so it draws randomness internally.
#     Calling the plain keypair() variants would take randomness from
#     qudo-pqc's own platform entropy instead of the approved DRBG, which is a
#     cert finding. This is the single most important rule in Epic 3.
#
# Verified 2026-07-22: the algorithm objects (poly.o, polyvec.o, sign.o,
# slh_dsa.o) are byte-identical between the standard and FIPS-module builds, so
# this choice does not perturb the math.
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_DIR="${REPO_ROOT}/qudo-pqc-lib"
BUILD_DIR="${QUDO_PQC_BUILD_DIR:-${SRC_DIR}/build}"
BUILD_TYPE="${QUDO_PQC_BUILD_TYPE:-Release}"
JOBS="${JOBS:-$( (command -v nproc >/dev/null && nproc) || sysctl -n hw.ncpu || echo 4 )}"

if [ ! -f "${SRC_DIR}/CMakeLists.txt" ]; then
    echo "error: ${SRC_DIR} is not populated." >&2
    echo "       The qudo-pqc-lib subtree is missing from this checkout." >&2
    exit 1
fi

# Fail closed on a stale subtree. QUDO_PQC_MATH_ONLY is what keeps qudo-pqc's
# own FIPS services out of the boundary; CMake ignores unknown -D options
# silently, so without this check an old subtree would build in standard mode
# and quietly link a second AES, CTR-DRBG, HMAC and POST into fips.so.
if ! grep -q 'QUDO_PQC_MATH_ONLY' "${SRC_DIR}/CMakeLists.txt"; then
    cat >&2 <<'MSG'
error: the qudo-pqc-lib subtree does not support QUDO_PQC_MATH_ONLY.

QudoSSL requires the math-only build mode so that OpenSSL remains the sole
provider of every FIPS service inside the boundary. Without it, qudo-pqc's own
POST, PCT, integrity check, CTR-DRBG, AES and HMAC are linked into fips.so
alongside OpenSSL's.

Bump the subtree once ZenVInnovations/qudo-pqc-lib#9 has merged and been
tagged, then update docs/subtree-pins.md:

    git subtree pull --prefix=qudo-pqc-lib qudo-pqc-upstream <tag> --squash
MSG
    exit 1
fi

echo "==> Stage 1: qudo-pqc-lib -> libqudo-pqc.a"
echo "    source : ${SRC_DIR}"
echo "    build  : ${BUILD_DIR}"

cmake -S "${SRC_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DBUILD_SHARED_LIBS=OFF \
    -DQUDO_PQC_MATH_ONLY=ON \
    -DQUDO_PQC_BUILD_TESTS="${QUDO_PQC_BUILD_TESTS:-OFF}" \
    "$@"

# Build ONLY the qudo-pqc target -- the static libqudo-pqc.a that QudoSSL links.
# Do NOT build the standalone qudo-mlkem/qudo-mldsa/qudo-slhdsa sub-libraries:
# they recompile the wrapper sources without the static-link linkage macros, so
# on MSVC they fail with C2491 (dllimport definition). QudoSSL never consumes
# them; building them is wasted work that only breaks the Windows build.
cmake --build "${BUILD_DIR}" --parallel "${JOBS}" --target qudo-pqc

ARCHIVE="${BUILD_DIR}/lib/libqudo-pqc.a"
if [ ! -f "${ARCHIVE}" ]; then
    echo "error: expected archive not produced: ${ARCHIVE}" >&2
    find "${BUILD_DIR}" -name 'libqudo-pqc*' -print >&2 || true
    exit 1
fi

echo "==> Stage 1 complete: ${ARCHIVE}"
