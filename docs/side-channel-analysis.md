# Side-Channel and Constant-Time Posture — Delegated ML-KEM and ML-DSA

Sprint 3, Story 3.4. Prepared for CMVP lab pre-engagement.

## Summary

QudoSSL delegates ML-KEM and ML-DSA math to qudo-pqc-lib under `QUDO_PQC_DELEGATE`
([ADR-0005](adr/ADR-0005-crypto-layer-delegation.md)); SLH-DSA is not delegated
([ADR-0010](adr/ADR-0010-slh-dsa-stays-upstream.md)) and is out of scope here. A line-by-line source
review of the complete delegated ML-KEM decapsulation path — from
`openssl/crypto/ml_kem/ml_kem.c:2490` down through the qudo-pqc-lib wrapper into
mlkem-native's `mlk_kem_dec` — found **no branch, no memory index, and no
variable-latency operation controlled by secret material**. The Fujisaki–Okamoto
implicit-rejection comparison at
`qudo-pqc-lib/qudo-mlkem/mlkem-native/mlkem/src/kem.c:422` feeds only a
constant-time conditional move, the rejection key is computed unconditionally on
every call, and no plain `memcmp` exists anywhere in the ML-KEM C sources.
**That is the entire good news, and it is source review, not measurement.**
As of this document there is **zero measured constant-time evidence for either
delegated family** — no ctgrind run, no dudect run, no timing capture of any
kind, on any operational environment. Two ready-made ctgrind harnesses exist in
qudo-pqc-lib but are compiled by no build we ship or run, and no QudoSSL CI job
mentions valgrind, ctgrind, or constant-time testing at all. Separately, we
identified a genuine coverage defect in OpenSSL 3.5.7's own secret-poisoning
annotation at `openssl/crypto/ml_kem/ml_kem.c:2518`, which under-covers ML-KEM-768
and ML-KEM-1024 and leaves the implicit-rejection secret `z` untracked; because
that annotation compiles only under `OPENSSL_CONSTANT_TIME_VALIDATION` it has no
effect on any shipped binary, but it would silently weaken the first constant-time
validation run and must be widened before that run happens. It was deliberately
left unmodified this sprint.

## Scope

**In scope.** The delegated ML-KEM decapsulation path as deployed, plus the
instrumentation that any future constant-time measurement would depend on. The
delegated ML-DSA path is covered where it differs materially.

**Out of scope.** SLH-DSA is served by unmodified upstream OpenSSL
([ADR-0010](adr/ADR-0010-slh-dsa-stays-upstream.md)) and is not analysed here.
Power and electromagnetic channels are not addressed; this is a timing-channel
document only. `providers/implementations/` is untouched by design and contains
no delegated math.

**Evidence labels used throughout.** Every claim below carries exactly one:

| Label | Meaning |
|---|---|
| **REVIEWED** | Established by reading source at the cited `file:line`. No code was executed. |
| **MEASURED** | Established by executing something; the command is recorded. |
| **NOT DONE** | No evidence of any kind exists yet. |

**Review host.** macOS arm64 (Darwin 25.5.0). `which valgrind` → not found;
`uname -m` → `arm64`. Valgrind has no Apple Silicon support, so no ctgrind
evidence can be produced on this machine at all. No build was run for this
document; all findings come from reading and grepping the working tree, plus
`nm` over artifacts a previous build had already produced.

**Line-number baseline.** Every `file:line` below is as of commit `23c24ac`
(2026-07-23). This matters for `openssl/crypto/ml_kem/ml_kem.c`: commit
`033cab7` inserted a comment block and shifted everything after roughly line
2160 down by ten lines. [`upstream-defects.md`](upstream-defects.md) UD-4
describes the *same* three lines under the pre-`033cab7` numbering
(`:2508`/`:2509`/`:2539`); the source text is identical, only the offset
differs. That document is the tracking record for the defect analysed in §4
here.

---

## 1. The threat: why a KEM specifically

ML-KEM is a CCA-secure KEM built by applying the Fujisaki–Okamoto transform to a
CPA-secure public-key encryption scheme. FIPS 203 `ML-KEM.Decaps` does the
following: decrypt the ciphertext with the private key, re-derive the coins,
**re-encrypt**, and compare the re-encryption against the ciphertext that arrived.
If they match, return the real shared secret. If they do not match, return a
pseudorandom secret derived from the rejection value `z` — *implicit rejection*.
The decapsulator never signals failure.

Implicit rejection is what makes ML-KEM CCA-secure, and it is also precisely why
constant time is not optional here:

1. **The attacker controls the input and can send unlimited malformed
   ciphertexts.** In TLS, a decapsulating server will process any ciphertext an
   attacker offers. This is the classic chosen-ciphertext setting, and it is a
   *live, remote, repeatable* oracle — not a laboratory-only condition.
2. **The comparison result is a one-bit function of the long-term private key.**
   For a chosen malformed ciphertext, whether the re-encryption matches depends on
   the decrypted plaintext, which depends on the private key. If that bit is
   observable — through a taken branch, a timing difference, a cache line touched
   in one path and not the other — the attacker has an oracle.
3. **One bit per query is enough.** The FO transform's security proof assumes the
   rejection is *implicit*: indistinguishable from success. A timing side channel
   makes it explicit. Standard lattice key-recovery attacks (the Kyber/FrodoKEM
   line of plaintext-checking and decryption-failure attacks) recover the full
   private key from a few thousand to a few hundred thousand such queries. The
   consequence is not a leak of one session key; it is recovery of the long-term
   decapsulation key, and therefore of every session that key ever protected.
4. **The rejection path must be executed, not skipped.** A naive implementation
   computes the rejection key only when the comparison fails. That alone is a
   timing signal, independent of any branch on the secret bytes themselves.

The same reasoning does not apply symmetrically to a signature scheme. For
ML-DSA there is no attacker-chosen input processed against the private key in a
comparison-and-branch shape; the corresponding concern is rejection sampling in
signing, addressed separately in §7.

**This is why Story 3.4 exists, and why the ML-KEM decapsulation path is the
single most important thing in this document.**

---

## 2. What was inspected, and what was found

### 2.1 The reviewed path, end to end

The delegated decapsulation call chain, in order:

| Layer | Entry point | File:line |
|---|---|---|
| OpenSSL crypto core | `ossl_ml_kem_decap` | `openssl/crypto/ml_kem/ml_kem.c:2490` |
| OpenSSL delegation shim | `qudo_decap` | `openssl/crypto/ml_kem/ml_kem.c:2318` |
| Key serialisation | `ossl_ml_kem_encode_private_key` → `encode_prvkey` | `openssl/crypto/ml_kem/ml_kem.c:2050`, `:1575` |
| qudo-pqc-lib public API | `QUDO_KEM_decaps` | `qudo-pqc-lib/qudo-mlkem/src/mlkem_wrapper.c:1059` |
| qudo-pqc-lib dispatch | `qudo_mlkem_decaps_{512,768,1024}` | `qudo-pqc-lib/qudo-mlkem/src/mlkem_wrapper.c:596-669` |
| mlkem-native FO transform | `mlk_kem_dec` | `qudo-pqc-lib/qudo-mlkem/mlkem-native/mlkem/src/kem.c:376` |
| mlkem-native CPA decrypt | `mlk_indcpa_dec` | `qudo-pqc-lib/qudo-mlkem/mlkem-native/mlkem/src/indcpa.c:598` |
| mlkem-native CT primitives | `mlk_ct_memcmp`, `mlk_ct_cmov_zero` | `qudo-pqc-lib/qudo-mlkem/mlkem-native/mlkem/src/verify.h:326`, `:376` |

### 2.2 Findings

**REVIEWED — the OpenSSL-side shim has four branches, none on secret data.**
`qudo_decap` (`openssl/crypto/ml_kem/ml_kem.c:2318-2350`) branches on: handle
construction failure (`:2326`), allocation failure (`:2330`), private-key
encoding failure (`:2340`), and the qudo return status (`:2342`). The encoding
step (`:2340`) reaches `ossl_ml_kem_encode_private_key` at `:2050-2058`, which
branches only on `ossl_ml_kem_have_prvkey` and a length equality, then calls
`encode_prvkey` (`:1575-1586`) — a fixed-rank `vector_encode` (`:1579`),
`encode_pubkey` (`:1581`) and two `memcpy` (`:1583`, `:1585`), with no
data-dependent test. None of these branches is a function of the ciphertext or of
private-key *content*.

**REVIEWED — the qudo-pqc-lib wrapper is a pass-through, and its FIPS gates
compile out entirely in the shipped build.** `QUDO_KEM_decaps`
(`qudo-pqc-lib/qudo-mlkem/src/mlkem_wrapper.c:1059-1070`) is `QUDO_FIPS_GATE`,
`QUDO_FIPS_IND_CHECK`, four NULL checks, then `kem->decaps(...)`. Both gate macros
are defined as `((void)0)` unless `QUDO_FIPS_MODULE` is set
(`mlkem_wrapper.c:24` opens the `#ifdef`, `:57-59` are the no-op definitions), and
QudoSSL builds the library with `-DQUDO_PQC_MATH_ONLY=ON` and deliberately without
`QUDO_FIPS_MODULE` (`build/build_libqudo_pqc.sh:85`; see
[ADR-0009](adr/ADR-0009-qudo-pqc-standard-build.md)). The per-parameter-set
dispatchers `qudo_mlkem_decaps_{512,768,1024}` branch only on
`mlk_cpu_has_extension(...)`, i.e. on CPU features determined at init, never on
key or ciphertext bytes.

**REVIEWED — the FO comparison result never controls a branch and never reaches
the return value.** In `mlk_kem_dec`
(`qudo-pqc-lib/qudo-mlkem/mlkem-native/mlkem/src/kem.c:376-436`):

```c
422:  fail = mlk_ct_memcmp(ct, tmp, MLKEM_INDCCA_CIPHERTEXTBYTES);
423:
424:  /* Compute rejection key */
425:  mlk_memcpy(tmp, sk + MLKEM_INDCCA_SECRETKEYBYTES - MLKEM_SYMBYTES,
426:             MLKEM_SYMBYTES);
427:  mlk_memcpy(tmp + MLKEM_SYMBYTES, ct, MLKEM_INDCCA_CIPHERTEXTBYTES);
428:  mlk_hash_j(ss, tmp, MLKEM_SYMBYTES + MLKEM_INDCCA_CIPHERTEXTBYTES);
429:
430:  /* Copy true key to return buffer if fail is 0 */
431:  mlk_ct_cmov_zero(ss, kr, MLKEM_SYMBYTES, fail);
```

The variable `fail` appears at exactly two places in the function: it is assigned
at `:422` and consumed at `:431`. It is never tested, never returned, and never
used as an index. The rejection key is computed at `:425-428` **unconditionally on
every call**, so the failing and succeeding paths execute the same instruction
sequence. The function's `ret` (declared `int ret = 0` at `kem.c:381`) is set only
by allocation failure (`:391`), `mlk_kem_check_sk` (`:396`) and `mlk_indcpa_dec`
(`:402`) — none of which is ciphertext-dependent.

**REVIEWED — the constant-time primitives are hardened beyond the FIPS 203
reference implementation.** `mlk_ct_memcmp` (`verify.h:326-357`) accumulates the
XOR difference in `r`, and also maintains a second accumulator `s` whose only
purpose is documented in the source itself at `verify.h:345`: *"s is useless, but
prevents the loop from being aborted once r=0xff."* The return at `verify.h:356` is
`(mlk_value_barrier_u8(mlk_ct_cmask_nonzero_u8(r) ^ s) ^ s)` — a compiler barrier
around the mask conversion so the optimiser cannot reintroduce an early exit.
`mlk_ct_cmov_zero` (`verify.h:376-394`) is a fixed-length loop of per-byte
`mlk_ct_sel_uint8`. The barrier itself is a `volatile` object:
`verify.c:14 volatile uint64_t mlk_ct_opt_blocker_u64 = 0;`.

**MEASURED — the only comparison in the whole ML-KEM source tree is the
constant-time one.** `grep -rn "memcmp[[:space:]]*(" qudo-pqc-lib/qudo-mlkem/mlkem-native/mlkem/src/`,
filtered to exclude `mlk_ct_memcmp`, returns nothing (exit 1). Even the secret-key
hash check uses the constant-time comparator: `mlk_kem_check_sk` at `kem.c:105-108`,
with the source's own note at `:102-104` that a plain `memcmp` would otherwise be
the library's only one.

**REVIEWED — `mlk_kem_check_sk` does branch, but on public data, and the source
says so and proves it by declassifying.** `kem.c:76-115` recomputes `H(pk)` and
compares it against the copy embedded in `sk`, then branches on the result at
`kem.c:396`. The comment at `kem.c:88-91` states that the hashed and compared
regions are public. It backs this with explicit declassification of exactly those
regions before the hash: `kem.c:95` declassifies the public key inside `sk`, and
`kem.c:97-98` declassifies the `pkhash` field. This is called out here rather than
buried, because it is the one genuine branch-on-buffer-contents in the decap path
and a reviewer will find it.

**REVIEWED — no secret-indexed table lookup and no runtime assertion on secret
coefficients survives a release build.** `mlk_indcpa_dec`
(`qudo-pqc-lib/qudo-mlkem/mlkem-native/mlkem/src/indcpa.c:598`) contains exactly
one branch, an allocation NULL check, and is otherwise straight-line
unpack/NTT/basemul/inverse-NTT/subtract/reduce/to-message. `mlk_poly_tomsg`
(`compress.c:712-732`) is a fixed double loop over `mlk_scalar_compress_d1`, which
(`compress.h:50-67`) is pure multiply-add-shift with no branch and no memory
access. `mlk_assert_bound` expands to `do{}while(0)` unless `MLKEM_DEBUG` or `CBMC`
is defined (`debug.h:94-103`), so no assertion on secret coefficients exists in the
shipped build.

**INFERRED, not measured — the upstream formal-verification claim.** The delegated
backend is `pq-code-package/mlkem-native`, whose README (`qudo-pqc-lib/qudo-mlkem/mlkem-native/README.md:20-21`)
states that all C code in `mlkem/src/*` and `mlkem/src/fips202/*` is proved
memory-safe and type-safe with CBMC, and that all AArch64 and x86_64 assembly is
proved functionally correct, memory-safe, and of secret-independent timing using
HOL-Light. mldsa-native carries an equivalent statement
(`qudo-pqc-lib/qudo-mldsa/mldsa-native/README.md:73-79`). **We have not run those
proofs and we do not hold their artifacts.** This is an inherited upstream claim
and should be presented to the lab as such, not as QudoSSL evidence.

### 2.3 What was *not* inspected

Stated explicitly, because the scope of a negative finding is the whole of its
meaning:

- **The AVX2 and NEON assembly backends were not read.** Runtime dispatch
  (`mlkem_wrapper.c:596-669`) selects `mlkem_avx2*_dec`, `mlkem_neon*_dec` or
  `mlkem_ref*_dec` by CPU feature. Only the reference C path was reviewed. The
  constant-time property of the assembly rests entirely on the inherited
  HOL-Light claim above.
- **Keccak/FIPS 202 was not reviewed**, although `mlk_hash_g`, `mlk_hash_j` and
  `mlk_hash_h` are on the decap path.
- **No compiler output was examined.** Source-level constant-time discipline can be
  undone by an optimiser. mlkem-native mitigates this with value barriers, but we
  have not disassembled a shipped object to confirm the mitigations held under our
  flags (`-O3`, per `openssl/Makefile:4075`).
- **The delegated ML-DSA path received a lighter review than ML-KEM** and is
  reported separately in §7.
- **Nothing about power or EM channels.**

---

## 3. The gap: we have no measured constant-time evidence

**This section is the honest bottom line of the document. Sections 2 and 6 must
not be read as substitutes for it.**

**NOT DONE — there is no measured constant-time evidence for delegated ML-KEM.**
No ctgrind run, no dudect run, no timing capture, on any operational environment,
at any point in Sprints 1–3.

**NOT DONE — there is no measured constant-time evidence for delegated ML-DSA.**
Same position.

**MEASURED — the absence itself.** `grep -rniE "valgrind|ctgrind|constant.time|dudect"`
over `.github/` and `ci/` at the repo root returns **no matches** (exit 1). The
`ci/` directory contains four gate scripts — `check-boundary-symbols.sh`,
`check-delegation-linked.sh`, `check-mlkem-constructible.sh`,
`check-seeded-entrypoints.sh` — and none of them is timing-related.

**MEASURED — constant-time tests are excluded from qudo-pqc-lib's own CI by
construction.** `qudo-pqc-lib/.github/workflows/ci-standalone.yml:72-73` runs
`ctest ... -E "constant_time"`, with the reason given in the comment at
`:67-69`: dudect-style statistical tests need quiet dedicated hardware and shared
runners produce false positives. The reasoning is sound for *statistical* timing
tests; it does not apply to ctgrind, which is deterministic taint tracking (see
§6).

**MEASURED — measurement is impossible on the current development machine.**
`which valgrind` → not found; the host is arm64 macOS. Valgrind does not support
Apple Silicon. Every ctgrind result must therefore come from a Linux runner.

Source review establishes that the code *looks* constant-time. It cannot establish
that the compiler emitted constant-time code, that the assembly backends behave,
or that no path was missed. **A CMVP lab should treat §2 as a design review under
IG D.E and nothing more.** The measured half of the argument does not exist yet.
The plan to create it is §6.

---

## 4. Defect: the ML-KEM secret-poisoning annotation under-covers 768 and 1024

This is the same defect tracked as UD-4 in
[`upstream-defects.md`](upstream-defects.md), which cites it under the
pre-`033cab7` line numbering. Nothing here supersedes that record; this section
adds the side-channel consequence.

### 4.1 What the code says

`openssl/crypto/ml_kem/ml_kem.c:2513-2520`, inside `ossl_ml_kem_decap`:

```c
2513: #if defined(OPENSSL_CONSTANT_TIME_VALIDATION)
2514:     /*
2515:      * Data derived from |s| and |z| defaults secret, and to avoid side-channel
2516:      * leaks should not influence control flow.
2517:      */
2518:     classify_bytes = 2 * sizeof(scalar) + ML_KEM_RANDOM_BYTES;
2519:     CONSTTIME_SECRET(key->s, classify_bytes);
2520: #endif
```

`CONSTTIME_SECRET` is `VALGRIND_MAKE_MEM_UNDEFINED` (`ml_kem.c:154`); its inverse
`CONSTTIME_DECLASSIFY` is `VALGRIND_MAKE_MEM_DEFINED` (`ml_kem.c:161`). Both are
empty macros when `OPENSSL_CONSTANT_TIME_VALIDATION` is not defined
(`ml_kem.c:163-168`). This is the standard ctgrind idiom: mark the secret as
"uninitialised" so valgrind's memcheck reports any branch or memory index derived
from it.

### 4.2 The arithmetic

- `scalar` is `uint16_t c[ML_KEM_DEGREE]` (`ml_kem.c:94-97`) and `ML_KEM_DEGREE`
  is 256 (`openssl/include/crypto/ml_kem.h:19`), so `sizeof(scalar)` = **512 bytes**.
- `ML_KEM_RANDOM_BYTES` is 32 (`openssl/include/crypto/ml_kem.h:47`).
- Therefore `classify_bytes` = 2 × 512 + 32 = **1056 bytes**, a constant,
  independent of parameter set.
- The key layout is fixed at `ml_kem.c:1878`:
  `key->z = (uint8_t *)(rank + (key->s = key->m + rank * rank));`
  — i.e. the private vector `s` occupies `rank` scalars and the implicit-rejection
  secret `z` begins immediately after it. `rank` is 2, 3, 4 for ML-KEM-512, -768,
  -1024 (`openssl/include/crypto/ml_kem.h:95,104,113`).

Poisoning starts at `key->s` and runs for 1056 bytes:

| Parameter set | rank | `s` size | `s` + `z` (intended) | Poisoned | **Unpoisoned** | Coverage |
|---|---|---|---|---|---|---|
| ML-KEM-512 | 2 | 1024 B | 1056 B | 1056 B | 0 B | 100% |
| ML-KEM-768 | 3 | 1536 B | 1568 B | 1056 B | 480 B of `s` **+ all 32 B of `z`** | 67.3% |
| ML-KEM-1024 | 4 | 2048 B | 2080 B | 1056 B | 992 B of `s` **+ all 32 B of `z`** | 50.8% |

The constant is correct for — and only for — ML-KEM-512. For ML-KEM-768 and
ML-KEM-1024 the poisoned window ends inside `s` and never reaches `z` at all.

One residual applies even to ML-KEM-512, and the table above does not capture it.
The `z` allocation is 64 bytes, not 32: it carries the keygen seed half `d`
immediately after `z` (`ml_kem.c:1871-1875` documents the layout; `key->d =
key->z + ML_KEM_RANDOM_BYTES` at `:1733`, `:2092`, `:2258`). The 1056-byte window
therefore stops at the end of `z` and never covers `d`, at any rank. `d` is not
read on the decapsulation path, and the upstream comment scopes the intent to
`s` and `z`, so this is a lesser point than the rank bug — but a rank-aware fix
does not address it either, and it should not be described as 100% coverage
without that qualification.

### 4.3 Why `z` specifically is the wrong thing to leave untracked

`z` is the FO implicit-rejection secret. It is the input to the rejection-key
computation at `kem.c:425-428`, reached via the last 32 bytes of the serialised
private key that `encode_prvkey` writes at `ml_kem.c:1585`. A branch on `z` in the
rejection path is *exactly* the failure mode Story 3.4 exists to rule out — and for
two of the three parameter sets, the annotation as written would not detect it. A
constant-time validation run would come back green for ML-KEM-768 and ML-KEM-1024
while having tested substantially less than it appeared to.

### 4.4 The same file already contains the correct idiom

About 110 lines earlier, `ossl_ml_kem_genkey` declassifies with the rank-aware
expression:

```c
2406:     CONSTTIME_DECLASSIFY(key->s, vinfo->rank * sizeof(scalar));
2407:     CONSTTIME_DECLASSIFY(key->z, 2 * ML_KEM_RANDOM_BYTES);
```

So the fix is `vinfo->rank * sizeof(scalar) + ML_KEM_RANDOM_BYTES`. Inside
`ossl_ml_kem_decap` the local `vinfo` is declared at `ml_kem.c:2494` and assigned
from `key->vinfo` at `:2504`, both before the poisoning site at `:2518`, so no new
dereference is needed. Note that `:2407` also shows upstream treating the `z`
allocation as 64 bytes on the declassify side, which is the asymmetry described
at the end of §4.2.

### 4.5 This does **not** affect any shipped build

Stated plainly because it is the first question a lab will ask:

**MEASURED — `OPENSSL_CONSTANT_TIME_VALIDATION` is not defined in any build we
produce.** `grep -c OPENSSL_CONSTANT_TIME_VALIDATION openssl/configdata.pm` → **0**.
Grepping `openssl/Configure` and `openssl/INSTALL.md` finds no Configure option that
enables it; it would have to be injected through `CPPFLAGS` by hand. Nothing under
`build/`, `.github/` or `ci/` defines it either (`grep -rn
OPENSSL_CONSTANT_TIME_VALIDATION build/ .github/ ci/` → exit 1). With the macro
undefined, `CONSTTIME_SECRET` and `CONSTTIME_DECLASSIFY` expand to nothing
(`ml_kem.c:163-168`), `classify_bytes` is not even declared (`ml_kem.c:2497-2499`),
and no instruction is emitted.

**The defect is in the *coverage of a future constant-time validation run*, not in
the cryptography, not in the FIPS module, and not in any binary a customer or a lab
will receive.**

### 4.6 Provenance and disposition

**MEASURED — this is upstream OpenSSL 3.5.7 code, not a QudoSSL edit.**
`git blame -L 2518,2519 openssl/crypto/ml_kem/ml_kem.c` attributes both lines to
commit `4ca9ad0`, identified by `git show --stat` as *"Squashed 'openssl/' content
from commit 8cf17aa"* — the OpenSSL 3.5.7 subtree import (see
[subtree-pins.md](subtree-pins.md)). The line is unchanged from upstream.

**Disposition: deliberately left unmodified in Sprint 3.** The rationale is that we
do not yet run a constant-time validation build, so changing the line now would
alter upstream source under `openssl/` for no measurable effect, against the
project's standing instruction to minimise changes to upstream OpenSSL. It is
recorded here, and in [Open items](#open-items), as a **hard prerequisite**: the
annotation must be widened *before* the first constant-time run, or that run's
ML-KEM-768 and ML-KEM-1024 results must not be presented as evidence. It should
also be reported upstream.

---

## 5. Which annotations actually fire on the deployed path

This section exists because it determines whether a future ctgrind run measures
anything at all. It is a REVIEWED finding with a material consequence.

Both sides of the boundary use the *same* valgrind client request, so taint
propagates across the delegation boundary without any glue:

- OpenSSL: `ml_kem.c:154` — `#define CONSTTIME_SECRET(ptr, len) VALGRIND_MAKE_MEM_UNDEFINED(ptr, len)`
- mlkem-native: `qudo-pqc-lib/qudo-mlkem/mlkem-native/mlkem/src/sys.h:208-211` —
  `MLK_CT_TESTING_SECRET` is the same request, guarded by `MLK_CONFIG_CT_TESTING_ENABLED`
- mldsa-native: `qudo-pqc-lib/qudo-mldsa/mldsa-native/mldsa/src/sys.h:215-218` — likewise,
  guarded by `MLD_CONFIG_CT_TESTING_ENABLED`

**REVIEWED — OpenSSL poisons all three delegated ML-KEM operations.**
`CONSTTIME_SECRET` appears at `ml_kem.c:2386` (keygen seed), `:2437` (encap
entropy) and `:2519` (decap private key), with matching declassification at
`:2397`, `:2406-2407`, `:2464-2466` and `:2549-2550`. (Two further declassify
calls at `:1706` and `:1717` sit in the non-delegated `genkey` body and are not
reached under `QUDO_PQC_DELEGATE`.)

**REVIEWED — mlkem-native's *own* poisoning never fires on the deployed path.**
`MLK_CT_TESTING_SECRET` appears in only two places in the ML-KEM sources:
`kem.c:274` (inside `mlk_kem_keypair`) and `kem.c:360` (inside `mlk_kem_enc`).
Both live in the **randomized** API, behind `#if !defined(MLK_CONFIG_NO_RANDOMIZED_API)`
(`kem.c:250-284` and `:336-370`). QudoSSL calls only the **derandomized** entry
points — `QUDO_KEM_keypair_from_seed` (`ml_kem.c:2237`), `QUDO_KEM_encaps_derand`
(`ml_kem.c:2302`) and `QUDO_KEM_decaps` (`ml_kem.c:2342`). A boundary gate,
[`ci/check-seeded-entrypoints.sh:25`](../ci/check-seeded-entrypoints.sh), asserts
this: its `FORBIDDEN` list contains `QUDO_KEM_keypair` and `QUDO_KEM_encaps`, and
its `ALLOWED` list at `:27` names the derandomized forms.

Two limits on that gate, stated so it is not read as stronger than it is. It
matches source text only — it never executes anything, so it evidences what the
code calls, not what runs. And it has no precondition that its scan resolved any
file: `grep -rl` over a path that no longer exists emits nothing, the loop body
at `:32-42` never executes, `hits` stays 0 and the script exits 0 at `:45-48`. A
rename or relocation of `openssl/crypto/ml_kem` would turn it green rather than
red. It is correct today — it does scan the real sources — but it is a
fail-open gate and should be hardened before it is cited as a control.

**Consequence.** In an EVP-path constant-time run for ML-KEM, the *only* source of
taint is OpenSSL's `CONSTTIME_SECRET`. mlkem-native contributes none: a recursive
grep for `MLK_CT_TESTING_SECRET` over `mlkem/src/` — including the `native/`
backend headers — returns only `kem.c:274` and `:360`. That makes the
under-coverage at `ml_kem.c:2518` (§4) not a nice-to-have but the **single point of
failure** for ML-KEM-768 and ML-KEM-1024 evidence.

**MEASURED — for ML-DSA there is no taint source at the key or seed level on the
EVP path.** `grep -rn "CONSTTIME_SECRET\|OPENSSL_CONSTANT_TIME_VALIDATION\|VALGRIND"`
over `openssl/crypto/ml_dsa/` returns **nothing** (exit 1) — upstream added no
poisoning to ML-DSA. mldsa-native's three seed/`rnd`-level `MLD_CT_TESTING_SECRET`
sites (`sign.c:416`, `:984`, `:1036`) are all inside randomized wrappers guarded by
`#if !defined(MLD_CONFIG_NO_RANDOMIZED_API)` (`sign.c:395-424`, `:948-1008`,
`:1010-1047`), while QudoSSL calls `QUDO_MLDSA_keypair_internal`
(`openssl/crypto/ml_dsa/ml_dsa_key.c:525`), `QUDO_MLDSA_sign_internal`
(`openssl/crypto/ml_dsa/ml_dsa_sign.c:103`) and `QUDO_MLDSA_verify_internal`
(`ml_dsa_sign.c:131`).

**Correction to a tempting overstatement.** It is *not* true that an EVP-path
ML-DSA run would be entirely taint-free. Unlike ML-KEM, mldsa-native's native
backends do poison, outside the randomized API: `MLD_CT_TESTING_SECRET` marks the
output of `mld_rej_uniform_eta{2,4}_native` at
`mldsa-native/mldsa/src/native/aarch64/meta.h:93`, `:121` and
`mldsa-native/mldsa/src/native/x86_64/meta.h:110`, `:139`, each paired with a
`MLD_CT_TESTING_DECLASSIFY` of the input buffer. Those sites are reached from the
derandomized path QudoSSL calls, whenever a native backend is selected. So a run
would produce *some* taint — but it would begin deep inside the `eta` sampler
rather than at the private key, and it would cover nothing on a pure C build. The
honest statement is therefore: **an EVP-path ML-DSA ctgrind run today would give
coverage far weaker than it appears, starting from sampler outputs rather than
from key material, and on a non-native build it would be vacuous.** Key-level
poisoning annotations would have to be added first. This is not a defect in
anything; it is a prerequisite that must be budgeted. Note also the comment at
`aarch64/meta.h:83-89`, which states that constant-time testing *cannot* cover the
assembly's behaviour on accepted coefficients and that manual verification is
required — an inherited caveat, not something we have discharged.

---

## 6. The plan to obtain real evidence

### 6.1 The harnesses already exist and are never compiled

**REVIEWED — two ready-made ctgrind harnesses are present in the tree.**

| Harness | File | Lines | CMake option | ctest name |
|---|---|---|---|---|
| ML-KEM | `qudo-pqc-lib/qudo-mlkem/tests/test_ct_valgrind.c` | 66 | `MLKEM_CT_VALGRIND` (`qudo-mlkem/CMakeLists.txt:40`, default `OFF`) | `constant_time_valgrind` (`qudo-mlkem/tests/CMakeLists.txt:53-66`, test registered at `:58`) |
| ML-DSA | `qudo-pqc-lib/qudo-mldsa/tests/test_ct_valgrind.c` | 60 | `MLDSA_CT_VALGRIND` (`qudo-mldsa/CMakeLists.txt:43`, default `OFF`) | `constant_time_valgrind` (`qudo-mldsa/tests/CMakeLists.txt:83-95`, test registered at `:88`) |

Note the option names differ between the two subprojects — `MLKEM_CT_VALGRIND` and
`MLDSA_CT_VALGRIND`. Setting one does not enable the other.

When the option is ON, the subproject adds `-DMLK_CONFIG_CT_TESTING_ENABLED`
(`qudo-mlkem/CMakeLists.txt:52`) or `-DMLD_CONFIG_CT_TESTING_ENABLED`
(`qudo-mldsa/CMakeLists.txt:222`), which is what activates the
`MLK_/MLD_CT_TESTING_SECRET` macros in `sys.h`. The registered test invokes:

```
valgrind --error-exitcode=1 --exit-on-first-error=yes --track-origins=yes -q <binary>
```

`--error-exitcode=1` is what makes this a gate rather than a report.

**The ML-KEM harness exercises the implicit-rejection path explicitly.**
`qudo-mlkem/tests/test_ct_valgrind.c:27-35` runs `QUDO_KEM_keypair`,
`QUDO_KEM_encaps`, `QUDO_KEM_decaps`, then flips `ct[0] ^= 0x01` (`:34`) and
decapsulates the corrupted ciphertext (`:35`) — forcing the FO rejection branch.
`main` (`:53-66`) iterates the three parameter sets listed at `:55` —
ML-KEM-512, ML-KEM-768, ML-KEM-1024. This is exactly what §1 says must be tested.

**Two scope caveats, both important.**

1. **The standalone harness uses the *randomized* API** (`QUDO_KEM_keypair` at
   `:27`, `QUDO_KEM_encaps` at `:28`), which is not what QudoSSL calls (§5). It
   still produces valid taint — `mlk_kem_keypair` poisons its coins at `kem.c:274`
   and the taint propagates into `sk` — but it measures the library's own entry
   points, not the deployed ones. It is complementary to an EVP-path run, not a
   substitute.
2. **The ML-DSA harness covers keygen and sign only.**
   `qudo-mldsa/tests/test_ct_valgrind.c:29-31` calls `QUDO_MLDSA_keypair` and
   `QUDO_MLDSA_sign`; `main` (`:47-59`) iterates the three parameter sets listed
   at `:49` — ML-DSA-44/65/87. There is **no verify coverage** and no explicit
   exercise of a signing-rejection edge case. It also uses the randomized API, so
   like the ML-KEM harness it does not exercise the entry points QudoSSL calls.

**MEASURED — neither harness has ever been compiled by anything we run.** Both
CMake options default to `OFF`; `build/build_libqudo_pqc.sh:82-86` does not set
either; `qudo-pqc-lib/.github/workflows/ci-standalone.yml:71-73` excludes tests
matching `constant_time` regardless.

### 6.2 The two runs to perform

**Run A — library-level ctgrind (cheap, do first).**
On an x86_64 Linux runner with valgrind installed, configure each subproject with
its CT option on and run the registered test:

```
cmake -S qudo-pqc-lib/qudo-mlkem -B <out> -DMLKEM_CT_VALGRIND=ON
cmake --build <out>
ctest --test-dir <out> -R constant_time_valgrind --output-on-failure
```

and the equivalent with `-DMLDSA_CT_VALGRIND=ON` for `qudo-mldsa`. Capture stdout
verbatim as evidence. Cost is a CMake flag and a runner.

**Run B — EVP-path ctgrind (the run that actually matters).**
Evidence for the code *as deployed* must be taken through the public EVP path under
the FIPS provider, not against `libqudo-pqc.a` in isolation. Because
`CONSTTIME_SECRET` and `MLK_CT_TESTING_SECRET` are the same valgrind client request
(§5), OpenSSL's poisoning at `ml_kem.c:2519` already reaches
`QUDO_KEM_decaps` — no bridging code is needed. **This propagation is INFERRED
from the two macro definitions, not observed; it has never been run.** Sequence:

1. **Fix `ml_kem.c:2518` first** (§4). Without it, the ML-KEM-768 and ML-KEM-1024
   results are not worth capturing.
2. Build qudo-pqc-lib with `-DQUDO_PQC_MATH_ONLY=ON -DMLKEM_CT_VALGRIND=ON` so
   mlkem-native's *declassify* annotations (`kem.c:95-98`, `:231`,
   `indcpa.c:478`, `:554`) are live — without them, legitimate public-data branches
   inside `mlk_kem_check_sk` would produce false positives.
3. Configure a **separate** OpenSSL build tree with `-DOPENSSL_CONSTANT_TIME_VALIDATION`
   injected through `CPPFLAGS`. `Configure` exposes no option for it
   (grep of `openssl/Configure` and `openssl/INSTALL.md` finds none), so it must be
   passed by hand. Do not reuse the shared tree.
4. Run the ML-KEM decapsulation recipes of `openssl/test/recipes/30-test_evp.t`
   under `valgrind --error-exitcode=1`.

**Operational environment.** The measurement target is **x86_64 Linux**. arm64
macOS cannot host it — valgrind has no Apple Silicon support (MEASURED: `which
valgrind` → not found on the arm64 review host) — and macOS arm64 is therefore not
a viable OE for this class of evidence. `linux-x86_64` and `linux-aarch64` runners
already exist in CI (`.github/workflows/ci.yml:23-33`), so an
`ubuntu-24.04` runner is available immediately. **Both** Linux architectures should
be run: dispatch is by CPU feature (`mlkem_wrapper.c:596-669`), so x86_64 exercises
the AVX2 backend and only aarch64 exercises the NEON backend that also ships on
Apple Silicon.

**Where the macOS arm64 OE is left.** If macOS arm64 is a claimed OE, its
constant-time argument rests on (a) the source review in §2, (b) mlkem-native's
inherited HOL-Light claim for AArch64 assembly, and (c) by-proxy measurement on
Linux/aarch64 exercising the same NEON backend. **There will be no direct
measurement on that OE.** The lab must be told this in those terms.

**ctgrind versus dudect.** ctgrind (valgrind taint tracking) is *deterministic*: it
reports a specific branch or index on tainted data, at a specific instruction. It
does not need quiet hardware and can run on a shared CI runner — the
`ci-standalone.yml:67-69` rationale for excluding `constant_time` tests applies to
dudect's statistical measurement, not to ctgrind. dudect answers a different
question (is there a measurable timing difference on this microarchitecture) and
needs dedicated hardware. Which of the two the accrediting lab will accept as IG
D.E evidence is an open question (see [Open items](#open-items)); ctgrind is by far
the cheaper of the two here because the annotations are already in place on both
sides of the boundary.

### 6.3 Sequencing

| # | Step | Blocks |
|---|---|---|
| 1 | Widen `ml_kem.c:2518` to `vinfo->rank * sizeof(scalar) + ML_KEM_RANDOM_BYTES` | Steps 3, 4 for 768/1024 |
| 2 | Run A: library-level ctgrind, ML-KEM and ML-DSA, x86_64 Linux | — |
| 3 | Run B: EVP-path ctgrind for ML-KEM, x86_64 Linux | Step 1 |
| 4 | Repeat Run B on Linux/aarch64 for the NEON backend | Step 3 |
| 5 | Add ML-DSA poisoning annotations, then an EVP-path ML-DSA run | Nothing; not yet designed |
| 6 | Wire a ctgrind job into CI as a non-shared-runner-sensitive workflow | Steps 2–4 |

---

## 7. ML-DSA: the rejection-sampling question needs a written position

Story 3.4 also names "ML-DSA sign — rejection sampling". This is flagged here as an
**unresolved position, not a finding**, because it is not a measurement problem.

ML-DSA signing (FIPS 204) loops, sampling a commitment and restarting when the
result falls outside the required bounds. The number of iterations is variable and
is not independent of the private key. The standard position in the literature is
that this leak is benign — the rejection condition depends on the per-signature
commitment rather than on directly recoverable key bits, and the iteration count is
public in the hedged construction — but "the standard position in the literature"
is not a claim QudoSSL has written down and defended. A ctgrind run will not settle
it either way: taint tracking will either flag the loop condition (if the
commitment is tainted) or not (if it is declassified), and which of those happens is
a property of where the annotations are placed, not of the algorithm's security.

**This needs a written argument before lab engagement, not a measurement.** It is
listed under [Open items](#open-items).

---

## 8. Reconciliation with `qudo-pqc-lib/docs/security/SIDE_CHANNEL_ANALYSIS.md`

A vendor side-channel document already exists at
[`../qudo-pqc-lib/docs/security/SIDE_CHANNEL_ANALYSIS.md`](../qudo-pqc-lib/docs/security/SIDE_CHANNEL_ANALYSIS.md).
It predates delegation and describes qudo-pqc-lib as a standalone FIPS module. **It
is not a QudoSSL document and several of its claims do not carry over.** Where the
two conflict, this document governs for the QudoSSL boundary.

### 8.1 What it claims

- **§1** correctly self-describes as *"an informal screening and design review, not
  an accredited side-channel laboratory assessment."*
- **§2** describes the methodology as design review plus dudect-style leakage
  detection using Welch's t-statistic against a 4.5 threshold.
- **Table 4 (§3)** lists five constant-time design measures.
- **Table 5 (§4)** reports measured t-statistics, captioned *"dudect-style timing
  screening results (captured execution)."*
- **§5** limits coverage to *"a single platform (Apple M4 / macOS)"* and notes the
  lattice kernels' constant-time properties are *"inherited and assumed"* from the
  vendored `*-native` upstreams.
- **§6** recommends *"Extend the dudect screening to ML-KEM operations."*

### 8.2 Row-by-row reconciliation of Table 5

| Row | Reported | Status for QudoSSL |
|---|---|---|
| ML-DSA sign, 10,000 measurements, t = 0.688 | PASS | **Harness does not exist in the tree.** Cannot be reproduced. See below. |
| ML-DSA verify, 10,000 measurements, t = 0.935 | PASS | **Harness does not exist in the tree.** Cannot be reproduced. |
| SLH-DSA-SHA2-128f verify, 1,000, t = 1.28 | PASS | Reproducible, but **out of scope** — SLH-DSA is not delegated ([ADR-0010](adr/ADR-0010-slh-dsa-stays-upstream.md)). |
| SLH-DSA-SHAKE-128f verify, 1,000, t = 0.54 | PASS | Reproducible, but **out of scope** for the same reason. |
| *(ML-KEM — absent)* | — | The delegated family this document is chiefly about **has no row at all**, and §6 of that document concedes it. |

**MEASURED — the ML-DSA rows have no surviving harness.** An exhaustive
`grep -rilE "welford|t-statistic|t_statistic|dudect"` over `qudo-pqc-lib/`
(excluding build directories) matches only:
`qudo-slhdsa/tests/test_constant_time.c`, `docs/security/SIDE_CHANNEL_ANALYSIS.md`,
`docs/design/ARCHITECTURE.md`, `docs/security/THREAT_MODEL.md`,
`qudo-mlkem/examples/README.md` and `.github/workflows/ci-standalone.yml`. The only
dudect *code* is `qudo-slhdsa/tests/test_constant_time.c`, whose `main()`
(`:142-154`) runs exactly two cases at `:149-150` —
`test_verify_constant_time("SLH-DSA-SHA2-128f")` and
`test_verify_constant_time("SLH-DSA-SHAKE-128f")`. **There is no ML-DSA dudect
harness and no ML-KEM dudect harness anywhere in the repository.**

**Flagged rows.** The two ML-DSA rows of Table 5 must be treated as **unreproducible
as of this date**. Either the harness that produced them is restored, or the rows
are struck and ML-DSA is marked unmeasured. Carrying an unreproducible measurement
into a certification package is a worse position than an honest "not yet measured",
and a lab reviewing the Operational Testing Report will ask for the harness.

### 8.3 Table 4 rows that do not carry over to the QudoSSL boundary

Three of Table 4's five design measures describe code, or a rationale for code,
that does **not** carry into the QudoSSL boundary, because we build qudo-pqc-lib in
math-only mode ([ADR-0009](adr/ADR-0009-qudo-pqc-standard-build.md);
`build/build_libqudo_pqc.sh:85`):

| Table 4 row | Status in QudoSSL |
|---|---|
| "Constant-time AES default — `qudo_fips_aes_ct.c`" | **Not compiled.** `qudo-pqc-lib/CMakeLists.txt:321-329` drops `src/fips/qudo_fips_aes.c`, `qudo_fips_aes_ct.c`, `qudo_fips_ctrdrbg.c`, `qudo_fips_hmac.c` and `qudo_fips_rand.c`, and `:332-333` drops `qudo_fips_aes_ni.c`, when `QUDO_PQC_MATH_ONLY` is set. OpenSSL supplies AES. |
| "Constant-time comparison — `qudo_memcmp_ct`, used for ALL KAT/PCT/integrity comparisons" | **Rationale does not carry; the function does.** Its KAT/PCT/integrity callers (`src/qudo_pqc_post.c`, `src/qudo_pqc_pct.c`, `src/qudo_pqc_integrity.c`) are excluded by `CMakeLists.txt:302-312` under math-only — OpenSSL supplies POST, PCT and integrity. But the function itself lives in `src/qudo_pqc_platform.c:231`, which is always compiled (`CMakeLists.txt:293`). **MEASURED:** `nm -g qudo-pqc-lib/build/lib/libqudo-pqc.a` shows `T _qudo_memcmp_ct`; `nm -g openssl/libcrypto.dylib \| grep -c memcmp_ct` → **0**, so it is present in the archive but not pulled into the shipped library. |
| "No secret-dependent table lookups (AES)" | **Not applicable** — the AES sources are not compiled. |
| "ML-KEM implicit rejection — pseudo-random shared secret, no secret-dependent error branch" | **Applies and is confirmed by review** — see §2.2 and `kem.c:422-431`. Not measured. |
| "CPU dispatch is data-independent" | **Applies and is confirmed by review** — `mlkem_wrapper.c:596-669` dispatches on `mlk_cpu_has_extension` only. Not measured. |

### 8.4 Where the two documents agree

Both state that the lattice kernels' constant-time properties are inherited from
the vendored `*-native` upstreams rather than independently established here
(`SIDE_CHANNEL_ANALYSIS.md:46`; §2.2 above), and both state that the work is an
informal design review rather than an accredited assessment. On those two points
the documents are consistent and neither overstates.

---

## Open items

Ordered by what blocks what.

1. **Widen the ML-KEM decap poisoning at `openssl/crypto/ml_kem/ml_kem.c:2518`
   before the first constant-time run.** Change
   `2 * sizeof(scalar) + ML_KEM_RANDOM_BYTES` to
   `vinfo->rank * sizeof(scalar) + ML_KEM_RANDOM_BYTES`, matching the idiom
   already used at `:2406`. Test-build-only; cannot affect shipped code.
   Deliberately not done in Sprint 3. **Hard prerequisite for any ML-KEM-768 or
   ML-KEM-1024 constant-time claim.** Tracked as UD-4 in
   [`upstream-defects.md`](upstream-defects.md), including the upstream report —
   this is an OpenSSL 3.5.7 defect, not ours.
2. **Produce Run A** (library-level ctgrind, ML-KEM and ML-DSA, x86_64 Linux) — the
   cheapest real evidence available; harnesses already written, cost is two CMake
   flags and a runner.
3. **Produce Run B** (EVP-path ctgrind for ML-KEM decapsulation under the FIPS
   provider, x86_64 Linux), then repeat on Linux/aarch64 to cover the NEON backend.
   Blocked on item 1.
4. **Design ML-DSA key-level poisoning annotations.** No key- or seed-level taint
   source exists on the EVP path today (§5). The only poisoning that would fire is
   mldsa-native's native-backend rejection-sampling output
   (`native/{aarch64,x86_64}/meta.h`), which starts coverage inside the sampler
   rather than at key material and is absent altogether from a pure C build. This
   is unscoped work, not a flag flip.
5. **Write the ML-DSA rejection-sampling position** (§7). A written argument, not a
   measurement. Needed before lab engagement.
6. **Reconcile `qudo-pqc-lib/docs/security/SIDE_CHANNEL_ANALYSIS.md` Table 5.**
   Restore the harness behind the ML-DSA sign/verify rows or strike them; add an
   explicit "ML-KEM: not measured" statement; annotate the SLH-DSA rows as out of
   scope for the delegated boundary.
7. **Correct `SIDE_CHANNEL_ANALYSIS.md` Table 4** for math-only mode: three of its
   five rows describe code excluded from the QudoSSL boundary (§8.3).
8. **Wire a ctgrind job into CI.** ctgrind is deterministic and does not need quiet
   hardware, so the blanket `-E "constant_time"` exclusion at
   `qudo-pqc-lib/.github/workflows/ci-standalone.yml:73` need not apply to it.
   Nothing in `.github/` or `ci/` currently references valgrind or constant-time
   testing.
9. **Settle whether the accrediting lab accepts ctgrind (deterministic taint
   tracking) as IG D.E evidence, or requires dudect-style statistical
   measurement.** This changes the cost of everything above by roughly an order of
   magnitude and should be asked at pre-engagement.
10. **Confirm the OE list for constant-time evidence.** If macOS arm64 is a claimed
    OE, its constant-time argument will rest on source review plus mlkem-native's
    inherited HOL-Light claim plus by-proxy Linux/aarch64 measurement, with **no
    direct measurement on that OE**. The lab must be told this explicitly.
11. **Review the assembly backends and FIPS 202/Keccak**, or formally accept the
    upstream HOL-Light and CBMC claims as inherited evidence and record that
    acceptance. Neither has been done.
12. **Examine compiler output for at least one shipped object** to confirm the
    source-level constant-time constructions survived `-O3`
    (`openssl/Makefile:4075`). Not attempted.
13. **Harden `ci/check-seeded-entrypoints.sh` against its fail-open** (§5). It
    reports PASS with `hits=0` if its scan paths ever stop resolving, because the
    loop at `:32-42` simply never executes. Add a precondition that at least one
    source file was scanned before it may pass. This gate is the only control
    standing behind the "QudoSSL calls only derandomized entry points" premise
    that §5's taint analysis depends on.

---

### Related documents

- [ADR-0005 — crypto-layer delegation](adr/ADR-0005-crypto-layer-delegation.md)
- [ADR-0009 — qudo-pqc math-only build](adr/ADR-0009-qudo-pqc-standard-build.md)
- [ADR-0010 — SLH-DSA stays upstream](adr/ADR-0010-slh-dsa-stays-upstream.md)
- [Design errata](design-errata.md)
- [Subtree pins](subtree-pins.md)
- [Reproducibility](reproducibility.md)
- [qudo-pqc-lib vendor side-channel analysis](../qudo-pqc-lib/docs/security/SIDE_CHANNEL_ANALYSIS.md)
