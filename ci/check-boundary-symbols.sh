#!/usr/bin/env bash
#
# Boundary gate — QudoSSL must use OpenSSL's FIPS infrastructure ONLY.
#
# ARCHITECTURAL RULE
#   qudo-pqc-lib supplies PQC algorithm MATH and nothing else. Every piece of
#   FIPS machinery inside the cryptographic boundary is OpenSSL's:
#
#     POST / CAST / PCT ....... OpenSSL providers/fips/self_test*.c
#     Integrity HMAC .......... OpenSSL fipsmodule.cnf flow
#     Approved DRBG ........... OpenSSL providers/implementations/rands
#     AES / SHA / HMAC ........ OpenSSL crypto/{aes,sha,hmac}
#     State machine ........... OpenSSL, the single authority
#     Approval indicator ...... OpenSSL fipsindicator.c
#
#   qudo-pqc-lib ships its own parallel implementations of all of the above so
#   it can stand alone. NONE of them may enter the certified module: a second
#   AES or CTR-DRBG inside one boundary is the "two equivalent approved
#   functions" finding that design §6 exists to prevent.
#
# HOW THIS GATE WORKS
#   The forbidden set is derived from the archive itself — every symbol exported
#   by a qudo FIPS-infrastructure object — so it stays correct as qudo-pqc-lib
#   evolves rather than drifting from a hand-maintained list. A probe is linked
#   that references ONLY caller-deterministic math entry points, and the gate
#   fails if any forbidden symbol comes with it.
#
# CURRENT STATUS: FAILS. See docs/adr/ADR-0009 (Correction) and Sprint 2 Story
# 2.9. Root cause is object-level link granularity — the deterministic entry
# points share mlkem_wrapper.c.o with QUDO_KEM_init, so referencing any of them
# drags in the RNG chain. Fix is upstream in qudo-pqc-lib.
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

# Symbols implementing qudo's own FIPS infrastructure: its DRBG, its HMAC and
# integrity-HMAC machinery, its POST/PCT/state machine and its security-level
# controls. OpenSSL supplies every one of these services for QudoSSL; none of
# them may be reachable from the math.
#
# The set is derived from SYMBOL names across every object in the archive, NOT
# from object filenames. Under QUDO_PQC_MATH_ONLY the FIPS sources are not
# compiled into separately-named objects, so the previous filename regex
# matched 0 of 84 objects, produced an empty forbidden set, and made the
# comm(1) below vacuous — the gate passed unconditionally and could not fail.
FIPS_SYM_RE='^qudo_(fips_|pqc_(post|pct|integrity|indicator|embedded|init|audit|state|ctrdrbg|rand|hmac|get_module_boundary|get_integrity|get_security_level|get_min_security_level|set_min_security_level|check_security_level))'

echo "==> deriving forbidden symbol set from ${ARCHIVE##*/}"
mkdir -p "${WORK}/ar" && (cd "${WORK}/ar" && ar x "${ARCHIVE}")

obj_total=$(ls "${WORK}/ar" | wc -l | tr -d ' ')
echo "  objects in archive: ${obj_total}"

: > "${WORK}/allsyms.txt"
for o in "${WORK}/ar"/*.o; do
    [ -e "${o}" ] || continue
    nm -gU "${o}" 2>/dev/null | awk '{print $NF}' | sed 's/^_//' >> "${WORK}/allsyms.txt"
done
sort -u -o "${WORK}/allsyms.txt" "${WORK}/allsyms.txt"
sym_total=$(wc -l < "${WORK}/allsyms.txt" | tr -d ' ')
echo "  defined symbols: ${sym_total}"

# Fail closed (1): objects present but no symbols extracted means nm or the
# parse broke, not that the boundary is clean.
if [ "${obj_total}" -gt 0 ] && [ "${sym_total}" -eq 0 ]; then
    echo "error: ${obj_total} objects but 0 symbols extracted — nm/parse failure." >&2
    exit 2
fi

grep -E "${FIPS_SYM_RE}" "${WORK}/allsyms.txt" > "${WORK}/forbidden.txt" || true
forbid_count=$(wc -l < "${WORK}/forbidden.txt" | tr -d ' ')
echo "  forbidden symbols: ${forbid_count}"

# Fail closed (2): qudo-pqc-lib ships these utilities in the archive even under
# MATH_ONLY, so an empty forbidden set means FIPS_SYM_RE no longer matches
# reality and this gate has silently lost its teeth.
if [ "${forbid_count}" -eq 0 ]; then
    echo "error: forbidden set is empty — FIPS_SYM_RE matches no symbol in the" >&2
    echo "       archive, so this gate can never fail. Update the regex." >&2
    exit 2
fi

# Probe referencing ONLY caller-deterministic math. Every input is supplied by
# the caller, so none of these needs an RNG, a self-test or a state machine.
#
# QUDO_KEM_keypair_derand is deliberately NOT used: despite the name its seed
# argument is an OUTPUT, so it draws randomness internally.
cat > "${WORK}/probe.c" <<'EOF'
#include "mlkem_wrapper.h"
void *refs[] = {
    (void *)QUDO_KEM_keypair_from_seed,   /* const seed[]      IN */
    (void *)QUDO_KEM_encaps_derand,       /* const rand[32]    IN */
    (void *)QUDO_KEM_decaps,              /* deterministic        */
};
int main(void) { return refs[0] == 0; }
EOF

echo "==> linking probe against deterministic math entry points only"
if ! cc -I "${REPO_ROOT}/qudo-pqc-lib/include" \
        -I "${REPO_ROOT}/qudo-pqc-lib/qudo-mlkem/include" \
        -o "${WORK}/probe" "${WORK}/probe.c" "${ARCHIVE}" 2>"${WORK}/cc.log"; then
    echo "error: probe failed to link" >&2
    sed 's/^/    /' "${WORK}/cc.log" >&2
    exit 2
fi

nm "${WORK}/probe" 2>/dev/null | awk '{print $NF}' | sed 's/^_//' | sort -u > "${WORK}/linked.txt"
comm -12 "${WORK}/forbidden.txt" "${WORK}/linked.txt" > "${WORK}/violations.txt"
count=$(wc -l < "${WORK}/violations.txt" | tr -d ' ')

# Second, independent assertion. The probe is synthetic and pulls in only three
# ML-KEM entry points; the shipped module links ML-KEM *and* ML-DSA and much
# else besides, so it can drag in objects the probe never touches. When the
# module has been built, scan it directly — it is the artifact CMVP certifies.
for m in "${REPO_ROOT}/openssl/providers/fips.dylib" \
         "${REPO_ROOT}/openssl/providers/fips.so"; do
    [ -f "${m}" ] || continue
    echo "==> scanning shipped module ${m##*/}"
    # Plain nm, not -g: the module is linked with a version script that localizes
    # every non-exported symbol, so -g lists only OSSL_provider_init and the scan
    # is vacuous. Plain nm sees the local (t) symbols the linker actually pulled.
    nm "${m}" 2>/dev/null | awk '{print $NF}' | sed 's/^_//' | sort -u > "${WORK}/mod.txt"
    comm -12 "${WORK}/forbidden.txt" "${WORK}/mod.txt" > "${WORK}/mod_violations.txt"
    mod_count=$(wc -l < "${WORK}/mod_violations.txt" | tr -d ' ')
    echo "  qudo symbols linked in:  $(grep -cE '^(qudo_|QUDO_)' "${WORK}/mod.txt" || true)"
    echo "  FIPS-infra violations:   ${mod_count}"
    if [ "${mod_count}" -gt 0 ]; then
        cat "${WORK}/mod_violations.txt" >> "${WORK}/violations.txt"
        count=$((count + mod_count))
    fi
    # Direct check for a non-approved OS entropy source inside the boundary.
    # qudo's platform RNG (getentropy / BCryptGenRandom / arc4random) must not be
    # reachable from fips.so; OpenSSL owns the approved DRBG. This is an undefined
    # reference, so scan with -u.
    ent=$(nm -u "${m}" 2>/dev/null | grep -acE '\b(getentropy|BCryptGenRandom|arc4random)\b')
    echo "  OS entropy refs:         ${ent}"
    if [ "${ent}" -gt 0 ]; then
        echo "os-entropy-in-boundary" >> "${WORK}/violations.txt"
        count=$((count + ent))
    fi
done

echo
if [ "${count}" -eq 0 ]; then
    echo "BOUNDARY GATE PASSED — no qudo FIPS infrastructure reachable from the math."
    exit 0
fi

echo "  qudo FIPS-infra symbols linked in (${count}):"
sed 's/^/    /' "${WORK}/violations.txt"
cat >&2 <<EOF

BOUNDARY GATE FAILED — ${count} qudo FIPS-infrastructure symbol(s) reached the link.

QudoSSL must use OpenSSL's FIPS infrastructure ONLY. qudo-pqc-lib supplies PQC
algorithm math; POST, CAST, PCT, integrity, the approved DRBG, AES, SHA, HMAC,
the state machine and the approval indicator are all OpenSSL's. Linking qudo's
parallel copies into fips.so puts two implementations of the same approved
functions inside one boundary — the finding design §6 exists to prevent.

Root cause: object-level link granularity. The deterministic entry points share
mlkem_wrapper.c.o with QUDO_KEM_init, which references the RNG chain, so a
static link pulls the whole object and everything it needs.

Fix belongs upstream in ZenVInnovations/qudo-pqc-lib — build the math with
-ffunction-sections/-fdata-sections so consumers can --gc-sections, split the
wrapper so the deterministic entry points do not share a translation unit with
the FIPS machinery, or compile the FIPS infrastructure out entirely in the
math-only configuration.

See docs/adr/ADR-0009 (Correction), docs/design-errata.md §3, Sprint 2 Story 2.9.
EOF
exit 1
