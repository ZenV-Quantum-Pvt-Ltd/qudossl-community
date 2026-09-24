# ADR-0009 — qudo-pqc-lib is consumed as a standard build, not a FIPS module

- **Status:** **Accepted, but BLOCKED — two claims below were disproved by execution on 2026-07-22.**
- **Date:** 2026-07-22 (post-freeze amendment)
- **Design ref:** amends §1.1, §12.3, §22.1, §23.2; supersedes ADR-0003 / D5
- **Depends on:** ADR-0005 (crypto-layer delegation)
- **Blocked by:** upstream qudo-pqc-lib math-only mode (see Correction below)

## CORRECTION (2026-07-22, PR #2 audit)

The build-mode decision stands: the algorithm objects are byte-identical between
standard and FIPS-module builds, so this choice does not perturb the math. What
does **not** hold is the inference that not defining `QUDO_FIPS_MODULE`
decouples qudo's FIPS machinery. It does not, and two claims in this ADR are
false as written:

**1. ML-KEM cannot be constructed in the mandated configuration.** Verified by
execution against a standard build (`-DBUILD_SHARED_LIBS=OFF`, no
`QUDO_FIPS_MODULE`):

```
QUDO_KEM_init()            = -5  (QUDO_KEM_ERROR_RNG)
QUDO_KEM_new("ML-KEM-768") = NULL
```

Causation proven with the same binary and one extra call:

```
qudo_pqc_init(NULL) = 1  ->  QUDO_KEM_init() = 0  ->  QUDO_KEM_new = OK
```

`QUDO_KEM_init()` runs an RNG self-test that requires qudo's CTR-DRBG to be
seeded, which happens only inside `qudo_pqc_init()` — the call this ADR forbids.
Every seeded ML-KEM entry point takes a `const QUDO_KEM *`, so there is no
handle-free path. The claim below that "seeded APIs exist for all three
families" is true but insufficient: for ML-KEM they exist and cannot be invoked.

**2. The link-time thesis is false.** Linking a program that references *only*
`QUDO_KEM_keypair_derand` and `QUDO_KEM_encaps_derand` still pulls in a full
parallel FIPS apparatus:

```
qudo_ctrdrbg (4 syms)   qudo_aes (4)      qudo_fips_hmac (4)
qudo_pqc_post (4)       qudo_fips_rand (7)  qudo_pqc_init   qudo_audit (3)
```

So `fips.so` would ship a second AES, a second CTR-DRBG, a second HMAC-SHA-256
and qudo's POST — exactly the "two equivalent approved functions" finding §6
exists to prevent.

**Root cause of both.** The entanglement is gated on `QUDO_COMBINED_BUILD`,
which `CMakeLists.txt:174` sets unconditionally — **not** on `QUDO_FIPS_MODULE`.
Turning `QUDO_FIPS_MODULE` off therefore achieves nothing for boundary purposes.

**Resolution — `QUDO_PQC_MATH_ONLY`.** Reverting to `QUDO_FIPS_MODULE=ON` would
be strictly worse. Instead qudo-pqc-lib gains an **additive** build mode
(ZenVInnovations/qudo-pqc-lib#9) in which its own FIPS services are not
compiled at all: `qudo_pqc_{init,post,pct,integrity,indicator,audit,embedded_hmac}.c`,
`src/fips/*` and the FIPS boundary markers are excluded, and
`QUDO_COMBINED_BUILD` is left undefined so `*_rand.c` emits no reference to
`qudo_fips_rand_*`. That severs the chain at the source rather than relying on
linker garbage collection.

QudoSSL therefore builds with:

```sh
cmake -DBUILD_SHARED_LIBS=OFF -DQUDO_PQC_MATH_ONLY=ON
```

"Standard build" in this ADR's title means *not a FIPS module*. It does **not**
mean qudo-pqc-lib's default configuration — that still defines
`QUDO_COMBINED_BUILD` and is what produced both defects above.

Verified on 2026-07-22:

| Mode | Result |
|---|---|
| `QUDO_FIPS_MODULE=ON` (standalone cert path) | 397 exported symbols, **identical to pristine** |
| default standard, static | 317 objects, **identical to pristine** |
| `QUDO_PQC_MATH_ONLY=ON` | **zero** FIPS-infra objects; 1076 KB → 925 KB |

```
QUDO_KEM_init() = 0    QUDO_KEM_new("ML-KEM-768") = OK
keypair_from_seed + encaps_derand + decaps  ->  shared secrets MATCH
```

Both defects close, and no standalone certification path is affected — the
FIPS-mode symbol table diff against pristine `main` is empty.

**Enforcement.** `ci/check-mlkem-constructible.sh` and
`ci/check-boundary-symbols.sh` gate this in CI. Both fail against the current
subtree pin and pass against #9. `build/build_libqudo_pqc.sh` refuses to build
if the subtree predates the option, because CMake ignores unknown `-D` flags
silently and would otherwise fall back to standard mode unnoticed.

## Context

The design builds qudo-pqc-lib in FIPS-module mode with three boundary-dedupe
flags, in four separate places (§1.1, §12.3, §22.1 `QUDO_CMAKE_FLAGS`, §23.2):

```
-DQUDO_FIPS_MODULE=ON
-DQUDO_PQC_USE_HOST_DRBG=ON      -DQUDO_PQC_USE_HOST_HASHES=ON
-DQUDO_PQC_USE_HOST_AES=ON       -DQUDO_PQC_USE_HOST_HMAC=ON
-DQUDO_PQC_DISABLE_EMBEDDED_HMAC=ON
```

That framing predates ADR-0005. Under crypto-layer delegation, OpenSSL's FIPS
infrastructure runs unchanged and — because the crypto core now delegates —
every one of its self-tests exercises Qudo's math for free:

| Mechanism | Owner | How Qudo's math is covered |
|---|---|---|
| POST KATs | OpenSSL `self_test_kats.c` | feeds `ossl_ml_kem_*` / `ossl_ml_dsa_*`, which delegate |
| CAST | OpenSSL deferred `ST_ID_*` | same entry points |
| PCT on keygen | OpenSSL keymgmt | calls the delegated keygen |
| Integrity | OpenSSL HMAC over `fips.so` | Qudo's math is linked in, so covered |
| State machine | OpenSSL, single authority | — |
| Approved DRBG | OpenSSL | seeds passed into Qudo (see below) |

So qudo-pqc-lib needs to supply exactly one thing: **the algorithm math.**

## Decision

Build qudo-pqc-lib as a **standard** static archive:

```sh
cmake -S qudo-pqc-lib -B qudo-pqc-lib/build \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF
```

No `QUDO_FIPS_MODULE`, and therefore none of the boundary-dedupe flags — all
three hard-require it (`CMakeLists.txt:46-50` raises `FATAL_ERROR` otherwise).

## The rule this creates

**The delegation layer must call the seeded entry points**, supplying
randomness drawn from OpenSSL's approved DRBG. Names below verified with
`nm -g` against the built archive on 2026-07-22 — two earlier entries in this
list did not exist and have been corrected:

- `QUDO_KEM_keypair_derand`, `QUDO_KEM_keypair_from_seed`
- `QUDO_KEM_encaps_derand`  *(was written `QUDO_KEM_enc_derand` — no such symbol)*
- `mldsa_ref{44,65,87}_keypair_internal` and the `_neon` / `_avx2` dispatch
  variants  *(was written `mldsa44|65|87_keypair_internal` — no such symbol)*
- `QUDO_SLHDSA_keypair_internal`

Calling the plain `keypair()` variants would draw randomness from qudo-pqc's own
platform entropy rather than the approved DRBG — a cert finding. This is the
most important implementation rule in Epic 3, and it is what makes
`USE_HOST_DRBG` unnecessary: randomness enters as an argument, not through a
registered callback.

## Evidence

Verified 2026-07-22 against the pinned subtree:

- **The math is unaffected.** `poly.o`, `polyvec.o`, `sign.o` and `slh_dsa.o`
  are byte-identical between the standard and FIPS-module builds.
- **Seeded APIs exist** for all three families at the public wrapper level.
- **The design's §22.3 claim is false.** It states that
  `qudo_fips_aes.c`, `qudo_fips_hmac.c`, `qudo_fips_ctrdrbg.c`,
  `qudo_fips_embedded_hmac.c` and `qudo_fips_integrity.c` are compiled out so
  "the .o files don't exist in the archive". They do exist — with real code
  (`qudo_pqc_post.o` alone is 31 KB / 7 exported symbols) — and they exist in
  **both** modes. `QUDO_FIPS_MODULE` gates `#ifdef`s inside those files, not
  whether they compile. Standard mode is only ~31 KB smaller.

## Consequences

- **Epic 2 is moot.** ADR-0003 / D5 reduced Epic 2 to Story 2.1 (host DRBG) and
  Story 2.5 (disable embedded HMAC). Neither is needed now: randomness arrives
  as a seed argument, and the embedded HMAC is never invoked. Epic 2's remaining
  value is the standalone-consumer configuration, which is qudo-pqc-lib's own
  concern, not QudoSSL's.
- **Epics 4 and 5 shrink further**, on top of the ADR-0005 reduction. The
  self-test bridging stories (4.2 event bridge, 4.3 per-algorithm `ST_ID_*`,
  4.4 deferred CAST → qudo runners) and the KAT migration stories (5.1–5.3)
  have no subject. Story 5.4 (KAT-corruption negative test) survives and gets
  cheaper. The effort roll-up needs recomputing across Epics 2, 4 and 5.
- **What lands inside the boundary is decided at link time, not build time.**
  Because the scaffolding objects exist in the archive in either mode, the
  linker only pulls the members that resolve undefined symbols. If the
  delegation layer references only the math and the seeded entry points, the
  POST/PCT/integrity/indicator objects are never pulled into `fips.so`.
  **Verify this empirically at Epic 6** with `nm -g` over the built module —
  do not rely on the design's claim. A lab will ask what `qudo_pqc_post` is
  doing inside a module whose POST is OpenSSL's.
- §6.1's dedupe table lists `qudo_fips_ctrdrbg.c` and `qudo_fips_rand.c` as
  "removed from boundary". That is achieved here by never referencing them,
  not by the build flags the design attributes it to.
