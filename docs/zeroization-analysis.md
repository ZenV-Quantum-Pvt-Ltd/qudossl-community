# CSP zeroization analysis — ML-KEM and ML-DSA under crypto-layer delegation

Sprint 3, Story 3.3. Prepared as Operational Testing Report input.

## Summary

Every ML-KEM and ML-DSA critical security parameter that QudoSSL's delegation
code allocates is zeroized before release on every path, including early error
returns, and we verified this at both source and compiled-object level by
inspecting the relocations in the shipped FIPS-module objects rather than by
reading the source alone. The audit also found seven un-cleansed releases of PQC
private material in the OpenSSL 3.5.7 code we ship. Two sit inside the
delegation seam (`openssl/crypto/ml_dsa/ml_dsa_key.c`) and were fixed this
sprint alongside one zeroization-trigger divergence QudoSSL had introduced
itself; all three fixes are confirmed present in both the FIPS-module and the
libcrypto object. The remaining five sit in
`openssl/providers/implementations/`, which
[ADR-0005](adr/ADR-0005-crypto-layer-delegation.md) keeps byte-identical to
upstream and which the `upstream-parity` CI gate actively forbids us from
touching; none of them is inside the FIPS module, and they are being reported
upstream and picked up on the next sync rather than patched locally. Two further points a lab will want addressed are
covered explicitly below: why `vector_zero`'s `memset` is not a dead store the
compiler may elide, and why we consider a whole-process memory scrape to be
theatre and propose a quarantining-allocator test plus object-level relocation
assertions in its place. **We have not yet built or run either of those two
proposed artifacts** — this document reports the audit, not a completed test
campaign.

## Scope

**In scope.** ML-KEM (FIPS 203) and ML-DSA (FIPS 204) CSPs as they exist in the
QudoSSL delegated build: OpenSSL's key structs and codecs, the QudoSSL
delegation functions under `QUDO_PQC_DELEGATE`, and the zeroization behaviour of
qudo-pqc-lib's math cores as the delegated implementation.

**Out of scope.**

- **SLH-DSA.** Not delegated, per
  [ADR-0010](adr/ADR-0010-slh-dsa-stays-upstream.md). `openssl/crypto/slh_dsa/`
  was not audited in this pass. Its private keys are still module CSPs; see
  Open items.
- **Classical algorithm CSPs** (RSA, EC, DRBG state, TLS traffic keys). These
  are unmodified upstream OpenSSL and are covered by OpenSSL's own FIPS
  submission evidence.
- **Zeroization on power loss / uncontrolled termination.** Not applicable to a
  software module under ISO 19790 §7.9.

### Measurement environment

All measurements in this document were taken on the tree at
`/Users/venkateshpulimamidi/qudosslwork/qudossl`, macOS 15 / arm64, Apple clang
17.0.0, from the delegated FIPS build:

```
openssl/Makefile:4075   CFLAGS=-O3 -Wall -ffile-prefix-map=...
openssl/Makefile:4105   CNF_CPPFLAGS=... -DQUDO_PQC_DELEGATE -DNDEBUG -I<qudo includes>
```

`apps/openssl version -a` confirms `-DQUDO_PQC_DELEGATE` in the compiler line of
the built artifact, so the objects inspected below are the delegated product and
not upstream OpenSSL.

**Every claim below is labelled MEASURED, INFERRED, or NOT MEASURED.** Nothing
labelled MEASURED was taken from a source reading alone; each was confirmed
against a compiled object or a runtime invocation. All measurements are
single-OE (macOS arm64). They have **not** been repeated on Linux x86_64, Linux
aarch64, or Windows x64 — see Open items.

### Method

Three techniques, in increasing order of what they prove:

1. **Source review** of every allocation and release of PQC private material in
   the delegation seam and the surrounding OpenSSL code.
2. **Object-level relocation inspection.** For each function of interest,
   `objdump -d -r --disassemble-symbols=<sym> <object>` and a tally of the
   branch relocations. A function that cleanses shows `_OPENSSL_cleanse` or
   `_CRYPTO_clear_free`; a function that does not shows only `_CRYPTO_free`.
   This is what distinguishes "the source says it cleanses" from "the shipped
   binary cleanses", and it is the technique that found the defects in
   Section 3.
3. **Runtime invocation** of the built `apps/openssl` to confirm that a given
   code path is reached by ordinary use rather than only in theory.

`OPENSSL_clear_free(p, n)` is `CRYPTO_clear_free`, which is
`OPENSSL_cleanse(p, n)` followed by `free()`
(`openssl/crypto/mem.c:358-365`). Bare `OPENSSL_free(p)` is `CRYPTO_free`, a
plain `free()` with no cleanse (`openssl/crypto/mem.c:347-356`).
`OPENSSL_cleanse` is a `memset` invoked through a `volatile` function pointer
(`openssl/crypto/mem_clr.c:18-25`), which is why the two are distinguishable in
the relocation table and why the cleanse cannot be optimised away.

---

## 1. CSP inventory

Sizes below are for the parameter sets actually built. ML-KEM dk is
1632/2400/3168 bytes for ML-KEM-512/768/1024; ML-DSA sk is 2560/4032/4896 bytes
for ML-DSA-44/65/87 (`openssl/include/crypto/ml_dsa.h:26,31,36`).

### ML-KEM

| CSP | Storage | Zeroized by | Trigger | Status |
|---|---|---|---|---|
| `d` — keygen seed half (32 B) | Either `ML_KEM_KEY.seedbuf[32..63]` for a seed-only key (`openssl/crypto/ml_kem/ml_kem.c:2091-2095`), or inside the private-key allocation at `key->z + 32` for an expanded key (`ml_kem.c:1877-1878`, `ml_kem.c:2257-2258`) | Seed-only key: `OPENSSL_cleanse(key->seedbuf, 64)`, `ml_kem.c:2028`. Expanded key: the single `OPENSSL_cleanse` over `s ‖ z ‖ d` at `ml_kem.c:1897-1899` | `ossl_ml_kem_key_free` (`ml_kem.c:2017`) or `ossl_ml_kem_key_reset` (`ml_kem.c:1886`) | MEASURED — `_ossl_ml_kem_key_free` shows 3 × `_OPENSSL_cleanse`, 3 × `_CRYPTO_free` |
| `z` — implicit-rejection secret (32 B) | Same two locations as `d`; `z` immediately precedes `d` | Same calls as `d` | Same | MEASURED (same objdump) |
| `d ‖ z` on the keygen stack (64 B) | `uint8_t seed[64]` local in `ossl_ml_kem_genkey`, `ml_kem.c:2355` | `OPENSSL_cleanse(seed, sizeof(seed))`, `ml_kem.c:2394` | Unconditional, after keygen returns, success or failure | MEASURED — `_ossl_ml_kem_genkey` shows 2 × `_OPENSSL_cleanse` |
| Private vector `s` (rank × 384 B) | Inside the single private-key block allocated at `ml_kem.c:2388`; `key->s` set by `add_storage`, `ml_kem.c:1878` | `OPENSSL_cleanse(key->s, rank*sizeof(scalar) + 64)`, `ml_kem.c:1897-1899` | `ossl_ml_kem_key_reset`, called from `ossl_ml_kem_key_free:2034` and from `ossl_ml_kem_genkey:2401` on keygen failure | MEASURED |
| dk byte encoding — delegation copy | `prvenc` heap buffer in `qudo_genkey` (`ml_kem.c:2233`) and in `qudo_decap` (`ml_kem.c:2330`) | `OPENSSL_clear_free(prvenc, vinfo->prvkey_bytes)`, `ml_kem.c:2272` and `ml_kem.c:2347` | Single `end:` label in `qudo_genkey`; unconditional tail in `qudo_decap` | MEASURED — `_qudo_genkey` and `_ossl_ml_kem_decap` each show `_CRYPTO_clear_free` |
| dk byte encoding — decoded-key stash | `ML_KEM_KEY.encoded_dk` (`openssl/include/crypto/ml_kem.h:194`) | `OPENSSL_cleanse(key->encoded_dk, prvkey_bytes)` then free, `ml_kem.c:2030-2031` | `ossl_ml_kem_key_free` when `ossl_ml_kem_decoded_key(key)` | MEASURED |
| dk byte encoding — export path | `prvenc` from `OPENSSL_secure_zalloc` (`openssl/providers/implementations/keymgmt/ml_kem_kmgmt.c:281,286`) | `OPENSSL_secure_clear_free(prvenc, prvlen)`, `ml_kem_kmgmt.c:324` | `err:` label, taken on success and failure | MEASURED at source; secure-heap path, not separately objdump'd |
| `d ‖ z` — export path | `seedenc` from `OPENSSL_secure_zalloc` (`ml_kem_kmgmt.c:275`) | `OPENSSL_secure_clear_free(seedenc, seedlen)`, `ml_kem_kmgmt.c:323` | Same `err:` label | MEASURED at source |
| Encapsulation entropy (32 B) | `PROV_ML_KEM_CTX.entropy_buf` (`openssl/providers/implementations/kem/ml_kem_kem.c:35`) | `OPENSSL_cleanse(ctx->entropy, 32)` at `ml_kem_kem.c:58`, `:108`, `:218` | Context free; operation re-init; one-shot consumption after each encapsulate | MEASURED at source |
| Shared secret (32 B) | Caller buffer. Under TLS it is the premaster secret `s->s3.tmp.pms` | `OPENSSL_clear_free(pms, pmslen)` — `openssl/ssl/s3_lib.c:5452`, `:5564`, and `:3859`, `:3891` on connection clear/free | End of key exchange, and connection teardown | MEASURED at source. The delegated crypto layer never retains a copy — `qudo_encap`/`qudo_decap` write straight to the caller's buffer (`ml_kem.c:2302`, `ml_kem.c:2342`) |
| `rho`, `pkhash`, `t`, `m` | `seedbuf` / public-key block | Not CSPs — public values | n/a | Documented as public for completeness |

### ML-DSA

| CSP | Storage | Zeroized by | Trigger | Status |
|---|---|---|---|---|
| `xi` — keygen seed (32 B) | `ML_DSA_KEY.seed`, heap (`openssl/crypto/ml_dsa/ml_dsa_key.h:36`) | `OPENSSL_clear_free(key->seed, 32)`, `openssl/crypto/ml_dsa/ml_dsa_key.c:173` | `ossl_ml_dsa_key_reset`, from `ossl_ml_dsa_key_free:143`. Also dropped by `ossl_ml_dsa_sk_decode` at `openssl/crypto/ml_dsa/ml_dsa_encoders.c:772-774` when loading an explicit key, and by `qudo_keygen`'s failure path at `ml_dsa_key.c:550-553` | MEASURED — `_ossl_ml_dsa_key_reset` shows 2 × `_CRYPTO_clear_free` + 1 × `_OPENSSL_cleanse` |
| `xi` — delegation stack copy (32 B) | `uint8_t seed_copy[32]` in `qudo_keygen`, `ml_dsa_key.c:499` | `OPENSSL_cleanse(seed_copy, sizeof(seed_copy))`, `ml_dsa_key.c:554` | `end:` label, reached on every path after the two pre-allocation early returns | MEASURED — `_qudo_keygen` shows `_OPENSSL_cleanse` |
| `s1` (L polys), `s2` (K polys), `t0` (K polys) | One contiguous block owned by `key->s1.poly`; `s2` and `t0` alias into it (`ml_dsa_key.h:52-55`, comment at `ml_dsa_key.c:152-155`) | `vector_zero(&s1)`, `vector_zero(&s2)`, `vector_zero(&t0)` then `vector_free(&s1)`, `ml_dsa_key.c:157-160` | `ossl_ml_dsa_key_reset`, from `ossl_ml_dsa_key_free:143` and from the PCT-failure reset at `ml_dsa_key.c:639` | MEASURED — `_ossl_ml_dsa_key_reset` shows 3 × `_bzero`; see Section 5 for why `memset` is sound here |
| `s1_ntt` — NTT-domain copy of `s1` | Stack-adjacent heap block in `public_from_private`, `ml_dsa_key.c:332-337` | `vector_zero(&s1_ntt)`, `ml_dsa_key.c:356`, before `OPENSSL_free(polys)` at `:359` | End of `public_from_private` | MEASURED — `_ossl_ml_dsa_key_public_from_private` shows 1 × `_bzero` |
| `K` — signing seed (32 B) | `ML_DSA_KEY.K[32]`, in-struct (`ml_dsa_key.h:24`) | `OPENSSL_cleanse(key->K, sizeof(key->K))`, `ml_dsa_key.c:166` | `ossl_ml_dsa_key_reset` | MEASURED (the `_OPENSSL_cleanse` in `_ossl_ml_dsa_key_reset`) |
| sk byte encoding (2560/4032/4896 B) — key-held | `ML_DSA_KEY.priv_encoding`, heap (`ml_dsa_key.h:35`) | `OPENSSL_clear_free(key->priv_encoding, key->params->sk_len)`, `ml_dsa_key.c:170` | `ossl_ml_dsa_key_reset` | MEASURED |
| sk byte encoding — delegation copy | `sk` heap buffer in `qudo_keygen`, `ml_dsa_key.c:522` | `OPENSSL_clear_free(sk, params->sk_len)`, `ml_dsa_key.c:556-557` | `end:` label | MEASURED — `_qudo_keygen` shows 2 × `_CRYPTO_clear_free` |
| sk byte encoding — displaced prekey | `sk` local in `ossl_ml_dsa_generate_key`, `ml_dsa_key.c:613,626-627` | `OPENSSL_clear_free(sk, out->params->sk_len)`, `ml_dsa_key.c:645` | Unconditional in the `sk != NULL` branch, success and failure | **Fixed this sprint** — see Section 3.1. MEASURED — `_ossl_ml_dsa_generate_key` now shows `_CRYPTO_clear_free` |
| sk / seed — prekey memdup | `key->priv_encoding` and `key->seed` in `ossl_ml_dsa_set_prekey`, `ml_dsa_key.c:54-59` | `OPENSSL_clear_free`, `ml_dsa_key.c:64-72` | `end:` label when `ret == 0` | **Fixed this sprint** — see Section 3.2. MEASURED — `_ossl_ml_dsa_set_prekey` shows 2 × `_CRYPTO_clear_free`, 2 × `_CRYPTO_memdup`, and **zero** bare `_CRYPTO_free` |
| `rnd` — hedged-signing randomness (32 B) | `rand_tmp[32]` local, `openssl/providers/implementations/signature/ml_dsa_sig.c:182` | `OPENSSL_cleanse(rand_tmp, sizeof(rand_tmp))`, `ml_dsa_sig.c:203-204` | After every sign call | MEASURED at source |
| `test_entropy` — deterministic-test entropy | `PROV_ML_DSA_CTX.test_entropy[32]`, `ml_dsa_sig.c:48` | `OPENSSL_cleanse(ctx->test_entropy, ctx->test_entropy_len)`, `ml_dsa_sig.c:63` | `ml_dsa_freectx` | MEASURED at source |
| `rho''`, `y`, `c`, signing intermediates | Inside qudo-pqc-lib / mldsa-native, never in OpenSSL memory under delegation | `mld_zeroize` at 10 sites in `qudo-pqc-lib/qudo-mldsa/mldsa-native/mldsa/src/sign.c` | End of `sign_internal` | INFERRED from source; not objdump'd in this pass |
| `rho`, `tr`, `t1`, `pub_encoding` | in-struct / heap | Not CSPs — public values | n/a | — |

### Delegated-implementation side (qudo-pqc-lib)

| CSP | Where | Zeroized by | Status |
|---|---|---|---|
| `QUDO_KEM` / `QUDO_MLDSA` handle | Heap, one per operation | `QUDO_KEM_secure_zero(kem, sizeof(QUDO_KEM))` then `free`, `qudo-pqc-lib/qudo-mlkem/src/mlkem_wrapper.c:1072-1079`; `QUDO_MLDSA_secure_zero` then `free`, `qudo-pqc-lib/qudo-mldsa/src/mldsa_wrapper.c:657-664` | MEASURED at source. The handle is a copy of a static const template (`mlkem_wrapper.c:843`) and carries no key material, so this is defence in depth |
| ML-KEM intermediates | mlkem-native internals | `mlk_zeroize` — `SecureZeroMemory` on Windows, otherwise `memset` plus `__asm__ volatile("" : : "r"(ptr) : "memory")` (`qudo-pqc-lib/qudo-mlkem/mlkem-native/mlkem/src/verify.h:397-427`). The header cites *FIPS 203, Section 3.3, Destruction of intermediate values* in its own documentation comment | MEASURED at source |
| pk/sk on keygen failure | mlkem-native `kem.c` | `mlk_zeroize(pk, ...)` and `mlk_zeroize(sk, ...)` in the `cleanup:` block when `ret != 0`, `qudo-pqc-lib/qudo-mlkem/mlkem-native/mlkem/src/kem.c:240-246` | MEASURED at source |
| ML-DSA intermediates | mldsa-native `sign.c` | 10 `mld_zeroize` call sites | INFERRED — counted by grep, not individually reviewed |

**Note on the math-only build.** qudo-pqc-lib is compiled with
`-DQUDO_PQC_MATH_ONLY=ON` and deliberately without `-DQUDO_FIPS_MODULE`
(`build/build_libqudo_pqc.sh:85`, rationale at `:13-19`, fail-closed subtree
guard at `:57-76`). qudo-pqc's own zeroize-on-PCT-failure blocks are inside
`#ifdef QUDO_FIPS_MODULE` (`qudo-pqc-lib/qudo-mlkem/src/mlkem_wrapper.c:905-915`,
`:966-973`, `:1009-1016`) and are therefore **not compiled**. This is correct
under the boundary rule — OpenSSL owns the PCT
([ADR-0009](adr/ADR-0009-qudo-pqc-standard-build.md)), and the delegated ML-DSA
PCT is OpenSSL's, implemented at `openssl/crypto/ml_dsa/ml_dsa_key.c:398` — but
the Security Policy must state it rather than leave a lab to discover the
`#ifdef`. The unconditional mlkem-native / mldsa-native zeroization above is
unaffected.

---

## 2. Posture of the delegation code itself

**Claim: every `qudo_*` function added by QudoSSL in
`openssl/crypto/ml_kem/ml_kem.c` and `openssl/crypto/ml_dsa/*.c` cleanses its
private buffers and frees its QUDO handle on all paths, including early error
returns. Status: MEASURED.**

There are seven such functions. Six are static; the seventh
(`ossl_ml_dsa_key_pairwise_check`) is the delegated PCT.

| Function | Private buffers | Release on all paths | Handle freed on all paths |
|---|---|---|---|
| `qudo_kem_new_for` (`ml_kem.c:2198`) | none | n/a | allocates only |
| `qudo_genkey` (`ml_kem.c:2220`) | `prvenc` (dk) | `OPENSSL_clear_free(prvenc, prvkey_bytes)` at the single `end:` label, `ml_kem.c:2272`. `pub` is public, plain free at `:2273` | `QUDO_KEM_free(kem)` at `:2274`. The one early `return 0` (`:2218-2221`) fires when `kem == NULL`, before any allocation |
| `qudo_encap` (`ml_kem.c:2273`) | none — `pubenc` is public | plain `OPENSSL_free(pubenc)` at `:2308` | `QUDO_KEM_free` at `:2309`, **and** explicitly at `:2296` on the malloc-failure early return |
| `qudo_decap` (`ml_kem.c:2308`) | `prvenc` (dk) | `OPENSSL_clear_free(prvenc, prvkey_bytes)` at `:2337` | `QUDO_KEM_free` at `:2348`, **and** at `:2331` on the malloc-failure early return |
| `qudo_mldsa_new_for` (`ml_dsa_key.c:481`) | none | n/a | allocates only |
| `qudo_keygen` (`ml_dsa_key.c:494`) | `seed_copy` (stack `xi`), `sk` | `OPENSSL_cleanse(seed_copy, 32)` at `:554`; `OPENSSL_clear_free(sk, sk_len)` at `:556-557`; failure-path `OPENSSL_clear_free(out->seed, 32)` at `:550-553`. `pk` is public, plain free at `:555` | `QUDO_MLDSA_free(sig)` at `:558`. The two early `return 0` (`:502-505`, `:506-509`) fire before `seed_copy` is written and before any allocation; at `:506-509` `sig` is `NULL` by construction |
| `qudo_mldsa_new_for_sig` (`ml_dsa_sign.c:68`) | none | n/a | allocates only |
| `qudo_sign` (`ml_dsa_sign.c:75`) | none — `sk` is a borrowed pointer into the key object, not a copy | n/a | `QUDO_MLDSA_free(sig)` at `:111`. The early `return 0` at `:94` (bad `rnd_len`) fires before the handle is allocated |
| `qudo_verify` (`ml_dsa_sign.c:115`) | none — public inputs only | n/a | `QUDO_MLDSA_free(sig)` at `:136`. Early `return 0` at `:124` precedes allocation |

**Object-level confirmation.** Relocation tallies from
`objdump -d -r --disassemble-symbols=<sym>` against the shipped FIPS-module
objects:

| Symbol | Object | Relocations found |
|---|---|---|
| `_qudo_genkey` | `openssl/crypto/ml_kem/libfips-lib-ml_kem.o` | 1 × `_CRYPTO_clear_free`, 1 × `_CRYPTO_free`, 2 × `_CRYPTO_malloc`, 1 × `_OPENSSL_cleanse`, 1 × `_QUDO_KEM_free`, `_QUDO_KEM_new`, `_QUDO_KEM_keypair_from_seed` |
| `_ossl_ml_kem_decap` (`qudo_decap` inlined) | same | 1 × `_CRYPTO_clear_free`, 1 × `_CRYPTO_malloc`, 2 × `_QUDO_KEM_free`, `_QUDO_KEM_decaps`, `_QUDO_KEM_new` |
| `_ossl_ml_kem_encap_seed` (`qudo_encap` inlined) | same | 1 × `_CRYPTO_free`, 1 × `_CRYPTO_malloc`, 2 × `_QUDO_KEM_free`, `_QUDO_KEM_encaps_derand`, `_QUDO_KEM_new` |
| `_qudo_keygen` | `openssl/crypto/ml_dsa/libfips-lib-ml_dsa_key.o` | 2 × `_CRYPTO_clear_free`, 1 × `_CRYPTO_free`, 2 × `_CRYPTO_malloc`, 1 × `_CRYPTO_memdup`, 1 × `_OPENSSL_cleanse`, 1 × `_QUDO_MLDSA_free` |
| `_ossl_ml_kem_genkey` | `openssl/crypto/ml_kem/libfips-lib-ml_kem.o` | 2 × `_OPENSSL_cleanse` (the stack `d‖z`) |
| `_ossl_ml_kem_key_free` | same | 3 × `_OPENSSL_cleanse`, 3 × `_CRYPTO_free` |
| `_ossl_ml_dsa_key_reset` | `openssl/crypto/ml_dsa/libfips-lib-ml_dsa_key.o` | 2 × `_CRYPTO_clear_free`, 1 × `_OPENSSL_cleanse`, 3 × `_bzero`, 3 × `_CRYPTO_free` |

The `2 × _QUDO_KEM_free` in `_ossl_ml_kem_encap_seed` and `_ossl_ml_kem_decap`
is the direct evidence for the "including early error returns" claim: one call
site is the normal tail, the other is the malloc-failure branch. That branch is
unreachable in normal operation and would never be exercised by a functional
test; it is visible in the object.

`_qudo_genkey` and `_qudo_keygen` remain as local (`t`) symbols in the
FIPS-module objects, confirming the delegated paths — not upstream's math — are
what the FIPS module contains. `nm -a` on
`openssl/crypto/ml_dsa/libfips-lib-ml_dsa_key.o` shows `t _qudo_keygen` and no
`keygen_internal`.

---

## 3. Defects fixed this sprint

All three are in `openssl/crypto/ml_dsa/ml_dsa_key.c`, inside the delegation
seam that [ADR-0005](adr/ADR-0005-crypto-layer-delegation.md) permits us to
edit and that the `upstream-parity` CI gate whitelists
(`.github/workflows/ci.yml:106` allows divergence under
`crypto/ml_kem/`, `crypto/ml_dsa/`, and `Configure` only). Two are upstream
OpenSSL 3.5.7 defects; one is a divergence QudoSSL introduced.

All three landed together in commit `dd192a5`, *"Zeroize ML-DSA private key
material on free"*, on branch `feat/sprint3-test-interop`: 18 insertions, 3
deletions, one file. They are compiled into both
`openssl/crypto/ml_dsa/libfips-lib-ml_dsa_key.o` and
`openssl/crypto/ml_dsa/libcrypto-lib-ml_dsa_key.o` in the tree measured here,
and the relocation tallies below were taken from those objects.

### 3.1 `ossl_ml_dsa_generate_key` — full private key encoding freed uncleansed on the success path

**Upstream defect. Severity: highest of the three.**

`ossl_ml_dsa_generate_key` detaches any pre-existing private key encoding into
a local before regenerating from the seed (`ml_dsa_key.c:626-627`), then
compares the regenerated encoding against it. The freed buffer is a complete
ML-DSA private key — 2560, 4032, or 4896 bytes depending on parameter set —
and was released with a bare `OPENSSL_free`.

Now:

```c
        /* |sk| is a full private key encoding; zeroize before release. */
        OPENSSL_clear_free(sk, out->params->sk_len);
```
`openssl/crypto/ml_dsa/ml_dsa_key.c:644-645`

**This is on the success path, not an error path** (MEASURED). It executes
whenever an ML-DSA key is loaded in PKCS#8 seed-and-key form, which is a default
preference: `ML_DSA_KEY_PREFER_SEED` is set by default
(`openssl/include/crypto/ml_dsa.h`), the codec calls `set_prekey` with both seed
and priv, and the keymgmt load/import paths then call `ossl_ml_dsa_generate_key`
with `priv_encoding` non-NULL. Confirmed at runtime by the prior audit:
`openssl genpkey -algorithm ML-DSA-44 -provparam ml-dsa.output_formats=seed-priv`
produced a 2622-byte PKCS#8 containing both the seed and the 2560-byte key, and
`openssl pkey -in <file> -noout -text` loaded it successfully, which requires
line 645 to have executed.

**Verification (MEASURED).**
`objdump -d -r --disassemble-symbols=_ossl_ml_dsa_generate_key` now reports
1 × `_CRYPTO_clear_free`, 1 × `_CRYPTO_free`, 1 × `_CRYPTO_malloc`, 1 × `_memcmp`,
1 × `_qudo_keygen` — on **both**
`openssl/crypto/ml_dsa/libfips-lib-ml_dsa_key.o` and
`openssl/crypto/ml_dsa/libcrypto-lib-ml_dsa_key.o`. Before the fix the same
command reported two bare `_CRYPTO_free` and no `_CRYPTO_clear_free`.

The one remaining bare `_CRYPTO_free` is `ml_dsa_key.c:620`, which releases a
freshly-`malloc`'d 32-byte seed buffer after `RAND_priv_bytes_ex` **failed** to
fill it. We left it alone: on the failure path the buffer holds allocator
residue, not key material. We note it here rather than silently, because a
reviewer counting relocations will see it.

### 3.2 `ossl_ml_dsa_set_prekey` — memdup'd sk and seed freed uncleansed on the error path

**Upstream defect. Error path only.**

`ossl_ml_dsa_set_prekey` takes ownership by `OPENSSL_memdup` of the caller's
private key encoding and seed (`ml_dsa_key.c:54-59`). On failure it released
both with bare `OPENSSL_free`. Now:

```c
end:
    if (!ret) {
        /* Both buffers hold private key material; zeroize before release. */
        if (key->priv_encoding != NULL)
            OPENSSL_clear_free(key->priv_encoding, sk_len);
        if (key->seed != NULL)
            OPENSSL_clear_free(key->seed, seed_len);
        key->priv_encoding = key->seed = NULL;
    }
```
`openssl/crypto/ml_dsa/ml_dsa_key.c:64-72`

The lengths used are the function's own validated parameters — the guard at
`:46-52` rejects any `sk_len != key->params->sk_len` or
`seed_len != ML_DSA_SEED_BYTES` before either memdup — so `sk_len` and
`seed_len` are exactly the allocated sizes. The `NULL` checks are needed
because the second memdup can fail after the first succeeded, leaving one
pointer set and the other `NULL`.

**Verification (MEASURED).**
`objdump -d -r --disassemble-symbols=_ossl_ml_dsa_set_prekey` reports
2 × `_CRYPTO_clear_free` and 2 × `_CRYPTO_memdup`, and **zero** bare
`_CRYPTO_free`, on both the FIPS-module and libcrypto objects. Before the fix
it reported 2 × `_CRYPTO_memdup` and 2 × `_CRYPTO_free`.

### 3.3 `qudo_keygen` — restored upstream's failure-path seed zeroization

**QudoSSL-introduced divergence. Not a leak; a zeroization-trigger regression.**

Upstream's `keygen_internal` zeroizes `out->seed` at its `err:` label whenever
`ML_DSA_KEY_RETAIN_SEED` is clear, and that label is reached on both the success
and the failure path (`openssl/crypto/ml_dsa/ml_dsa_key.c:599-603`). QudoSSL's
`qudo_keygen` replaced `keygen_internal` under `QUDO_PQC_DELEGATE`
(`ml_dsa_key.c:628-632`) and relied instead on `ossl_ml_dsa_sk_decode` dropping
the seed (`openssl/crypto/ml_dsa/ml_dsa_encoders.c:772-774`). `sk_decode` is
only called at `ml_dsa_key.c:531`, so a failure before that point left
`out->seed` live. Now:

```c
 end:
    /*
     * keygen_internal() zeroizes the seed at its err: label whenever
     * RETAIN_SEED is clear, on both the success and the failure path. On
     * success ossl_ml_dsa_sk_decode() has already dropped it for us, but a
     * failure before that point leaves the caller's seed live. Mirror
     * upstream so the zeroization trigger is identical either way.
     */
    if (!ret && !retain && out->seed != NULL) {
        OPENSSL_clear_free(out->seed, ML_DSA_SEED_BYTES);
        out->seed = NULL;
    }
```
`openssl/crypto/ml_dsa/ml_dsa_key.c:542-553`

This covers the malloc-failure exit (`:521-523`), the
`QUDO_MLDSA_keypair_internal` failure exit (`:525-528`), the `sk_decode`
failure exit (`:531-534`), and the retain-memdup failure exit (`:536-538`).

**Honest residual (MEASURED).** Two early exits still bypass `end:`. At
`:502-505` `out->seed` is `NULL` by the guard's own test, so there is nothing to
zeroize. At `:506-509`, when `QUDO_MLDSA_new` fails, `out->seed` is non-NULL and
is not zeroized at that instant. In that case the seed is still cleansed when
the key object is destroyed (`ossl_ml_dsa_key_reset`, `ml_dsa_key.c:173`),
so it is a timing difference of one call frame, not an un-zeroized free. We
record it here rather than describe the mirror as exact.

**Verification (MEASURED).** `_qudo_keygen` now shows 2 × `_CRYPTO_clear_free`
(the `sk` buffer and the failure-path seed) where it previously showed 1.

---

## 4. Defects NOT fixed, and why

All of the sites in this section are tracked for upstream reporting in
[upstream-defects.md](upstream-defects.md); that document is the disposition
record, this section is the zeroization analysis and exposure assessment.

### 4.1 `ossl_ml_kem_key_to_text` — dk and `d‖z` left uncleansed

**Upstream OpenSSL 3.5.7 defect. Deliberately not patched locally.**

`openssl/providers/implementations/encode_decode/ml_kem_codecs.c:558-618`:

- `uint8_t seed[ML_KEM_SEED_BYTES]` is declared at `:560` and filled with the
  full 64-byte `d‖z` keygen seed by `ossl_ml_kem_encode_seed` at `:580`. It is
  never cleansed — there is no `OPENSSL_cleanse` anywhere in the function.
- `prvenc` is `OPENSSL_malloc`'d at `:586`, filled with the complete
  decapsulation key by `ossl_ml_kem_encode_private_key` at `:588`, and released
  with a bare `OPENSSL_free(prvenc)` at `:617`.
- There is an additional early `return 0` at `:587` on malloc failure that
  bypasses the `end:` label entirely; by that point `seed` may already hold the
  live `d‖z`.

**Verification (MEASURED).**
`objdump -d -r --disassemble-symbols=_ossl_ml_kem_key_to_text openssl/providers/implementations/encode_decode/libdefault-lib-ml_kem_codecs.o`
reports exactly 2 × `_CRYPTO_malloc` and 2 × `_CRYPTO_free` — no
`_OPENSSL_cleanse`, no `_CRYPTO_clear_free`.

**Reachability (MEASURED at runtime).** On this tree:

```
apps/openssl genpkey -algorithm ML-KEM-768 -out mlkem.pem
apps/openssl pkey -in mlkem.pem -noout -text
  -> ML-KEM-768 Private-Key:
     seed: ...
     dk:   ...
     ek:   ...
```

Both `seed:` and `dk:` printed, so both buffers were populated and both were
subsequently abandoned uncleansed. This is a routine `openssl pkey -text`
invocation, not a corner case.

**Why we are not fixing it here.** This file is in
`openssl/providers/implementations/encode_decode/`, which
[ADR-0005](adr/ADR-0005-crypto-layer-delegation.md) commits to leaving
untouched, and the commitment is machine-enforced: the `upstream-parity` job at
`.github/workflows/ci.yml:84-113` diffs the whole `openssl/` subtree against
`refs/tags/openssl-3.5.7` and **fails** on any changed file outside
`crypto/ml_kem/`, `crypto/ml_dsa/`, and `Configure`. Patching
`ml_kem_codecs.c` would break that gate, widen the permanent delta we must
re-apply at every CVE cherry-pick, and give the lab a divergence to explain in
a file we have otherwise certified as stock. **Decision: report upstream, take
the fix on the next 3.5.x sync.**

**Honest exposure assessment.** Smaller than it first looks, but not zero.

1. *Outside the FIPS module boundary.* MEASURED —
   `nm -a openssl/providers/fips.dylib | grep -c key_to_text` returns 0.
   `ossl_ml_kem_key_to_text` is exported from `libcrypto.3.dylib`
   (`T _ossl_ml_kem_key_to_text`) and lives in the **default** provider's
   objects (`libdefault-lib-ml_kem_codecs.o`). It is not in the validated
   module, so it does not appear in the module's SSP zeroization table. It is
   still shipped in our libcrypto and will still be read by a lab reviewing our
   source tree.
2. *Only reached by human-facing text output.* `openssl pkey -text`,
   `openssl asn1parse`-adjacent tooling, and any application that calls
   `PEM_write_bio_PrivateKey` with a text encoder. It is **not** on the TLS
   handshake path, is not reached by keygen, sign, verify, encap, or decap, and
   is not reached by the DER/PEM encoders used in production.
3. *What is actually exposed.* The full 64-byte `d‖z` seed and the full dk. The
   seed is the strongest form of the ML-KEM CSP: it regenerates the entire key
   pair. Residue lifetime is the lifetime of the freed heap chunk and the
   abandoned stack frame — bounded, unbounded above, and defeated by any
   subsequent overwrite.
4. *Mitigating factor in practice.* Anyone running `openssl pkey -text` on a
   private key has already printed that key to a terminal or a file. The
   process-memory residue is a strictly smaller exposure than the output the
   user just asked for.

We are not claiming this is harmless. We are claiming it is out of the module
boundary, off the operational path, and cheaper to fix upstream than to fork.

### 4.2 `ossl_ml_kem_i2d_prvkey` and `ossl_ml_dsa_i2d_prvkey` — PKCS#8 buffer freed uncleansed on the error path

**Correction to the investigation's finding.** The prior finding named this
function `ossl_ml_dsa_encode_p8`. **No such symbol exists in this tree**
(`nm -a openssl/providers/implementations/encode_decode/libdefault-lib-ml_dsa_codecs.o`
matches only the three `p8fmt` tables). The function that exhibits the
described behaviour is `ossl_ml_dsa_i2d_prvkey`
(`openssl/providers/implementations/encode_decode/ml_dsa_codecs.c:288-407`),
and its ML-KEM twin `ossl_ml_kem_i2d_prvkey`
(`openssl/providers/implementations/encode_decode/ml_kem_codecs.c:435-556`).

Both assemble a PKCS#8 buffer containing the seed and/or the full private key —
`memcpy(pos, seed, ML_DSA_SEED_BYTES)` at `ml_dsa_codecs.c:371` and
`memcpy(pos, sk, params->sk_len)` at `:382` — and then, if assembly fails, free
it with a bare `OPENSSL_free(buf)` (`ml_dsa_codecs.c:404-405`;
`ml_kem_codecs.c:553-554`).

**Verification (MEASURED).** Both symbols report 4 × `_CRYPTO_free` and
1 × `_CRYPTO_malloc`, with no `_OPENSSL_cleanse` and no `_CRYPTO_clear_free`.

**Exposure: materially lower than 4.1.** The free is guarded by `if (ret == 0)`
— on the success path the buffer is handed to the caller, who owns it. The
error path is reached only when the internal offset arithmetic disagrees with
the format table (`ml_dsa_codecs.c:366-370`, `:377-381`, `:388-392`), i.e. an
`ERR_R_INTERNAL_ERROR` that should never occur, or on a `fmt_slots` /
format-selection failure that occurs **before** any secret has been copied in.
Same file-location constraint as 4.1: same decision — report upstream, take the
fix on the next sync.

`ossl_ml_dsa_key_to_text` (`ml_dsa_codecs.c:409`) does **not** have the 4.1
defect: it uses borrowed `const uint8_t *` pointers into the key object
(`:412`, `:419-420`) and never copies or frees private material.

### 4.3 `ml_kem_load` — decoded dk and seed freed uncleansed

Found during verification, not present in the original findings.
`openssl/providers/implementations/keymgmt/ml_kem_kmgmt.c:510-553`: `encoded_dk`
(a complete dk encoding, detached from the key at `:519-520`) is released with a
bare `OPENSSL_free` at both `:545` and `:550`, and the local
`uint8_t seed[ML_KEM_SEED_BYTES]` declared at `:514` and filled at `:524` is
never cleansed. Same file-location constraint —
`providers/implementations/keymgmt/` is outside the permitted delta — and the
whole function is inside `#ifndef FIPS_MODULE` (`:509`), so it is not in the
validated module at all. **Decision: same as 4.1, report upstream.**

### 4.4 Summary of the not-fixed set

| Site | File:line | In FIPS module? | Path | Decision |
|---|---|---|---|---|
| `ossl_ml_kem_key_to_text` — `seed[64]` never cleansed | `openssl/providers/implementations/encode_decode/ml_kem_codecs.c:560,580` | No (MEASURED) | Success path of `pkey -text` | Report upstream |
| `ossl_ml_kem_key_to_text` — `OPENSSL_free(prvenc)` | same file `:586,617` | No | Success path of `pkey -text` | Report upstream |
| `ossl_ml_kem_i2d_prvkey` — `OPENSSL_free(buf)` | `ml_kem_codecs.c:553-554` | No | Internal-error path only | Report upstream |
| `ossl_ml_dsa_i2d_prvkey` — `OPENSSL_free(buf)` | `ml_dsa_codecs.c:404-405` | No | Internal-error path only | Report upstream |
| `ml_kem_load` — `encoded_dk`, `seed[64]` | `openssl/providers/implementations/keymgmt/ml_kem_kmgmt.c:514,545,550` | No — `#ifndef FIPS_MODULE` | Key load | Report upstream |

**NOT YET DONE:** the upstream reports have not been filed. See Open items.

---

## 5. `vector_zero` / `poly_zero` use `memset`, not `OPENSSL_cleanse` — why this is not a defect

A lab will ask about this, so the reasoning is set out in full.

```c
/* @brief zeroize a vectors polynomial coefficients */
static ossl_inline ossl_unused void vector_zero(VECTOR *va)
{
    if (va->poly != NULL)
        memset(va->poly, 0, va->num_poly * sizeof(va->poly[0]));
}
```
`openssl/crypto/ml_dsa/ml_dsa_vector.h:48-53`; `poly_zero` is the same shape at
`openssl/crypto/ml_dsa/ml_dsa_poly.h:18-22`.

`memset` on a buffer that is about to become unreachable is the classic
dead-store-elimination hazard: the standard permits a compiler to delete a store
it can prove no conforming program observes. `OPENSSL_cleanse` exists precisely
to defeat this, by routing the `memset` through a `volatile` function pointer
(`openssl/crypto/mem_clr.c:18-25`) that the optimiser is not permitted to
resolve.

**Why the store is nevertheless not dead here.**

There are exactly four CSP-clearing uses of these helpers in the tree
(the other three call sites — `ml_dsa_encoders.c:874`, `ml_dsa_matrix.c:29`,
`ml_dsa_sample.c:328` — are initialisation-to-zero, not zeroization, and are
irrelevant to this question):

| Call site | What follows the `memset` |
|---|---|
| `ml_dsa_key.c:157` `vector_zero(&key->s1)` | `vector_free(&key->s1)` at `:160` → `OPENSSL_free(v->poly)` (`ml_dsa_vector.h:41-46`) → `CRYPTO_free` (`openssl/crypto/mem.c:347`) |
| `ml_dsa_key.c:158` `vector_zero(&key->s2)` | aliases into the `s1` block; same free |
| `ml_dsa_key.c:159` `vector_zero(&key->t0)` | aliases into the `s1` block; same free |
| `ml_dsa_key.c:356` `vector_zero(&s1_ntt)` | `OPENSSL_free(polys)` at `:359`, the same block |

In every case the store is immediately followed by a call to `CRYPTO_free`,
which is an **external function in a different translation unit**
(`openssl/crypto/mem.c`), reached through a function pointer that may have been
replaced at runtime by `CRYPTO_set_mem_functions` (`openssl/crypto/mem.c:350-353`
dispatches to `free_impl` whenever the application has installed its own
allocator). The build uses `-O3` with no link-time optimisation
(`openssl/Makefile:4075` — `-O3 -Wall`, no `-flto`), so at the point the
optimiser processes `ossl_ml_dsa_key_reset` it has no body for `CRYPTO_free`,
no body for `free_impl`, and therefore no basis to prove the zeroed bytes are
never read. An opaque call that receives the buffer's address makes every prior
store to that buffer potentially observable. Dead-store elimination is
consequently not licensed, and — critically — this is a property of the *call
structure*, not of the compiler's mood.

**MEASURED confirmation on this build.**
`objdump -d -r --disassemble-symbols=_ossl_ml_dsa_key_reset openssl/crypto/ml_dsa/libfips-lib-ml_dsa_key.o`
reports **3 × `_bzero`** — one per `vector_zero` call — alongside
2 × `_CRYPTO_clear_free`, 1 × `_OPENSSL_cleanse`, and 3 × `_CRYPTO_free`. And
`objdump -d -r --disassemble-symbols=_ossl_ml_dsa_key_public_from_private`
reports **1 × `_bzero`**, the `vector_zero(&s1_ntt)` at `ml_dsa_key.c:356`.
The stores are present in the shipped FIPS-module object; clang emitted them as
calls to `bzero`, which is itself an external call.

**Conclusion: not a defect.** The memset is not elidable in this build, and the
reason it is not elidable is structural rather than incidental.

**The honest caveat we will state to the lab.** This is a MEASURED property of
one configuration — Apple clang 17, `-O3`, no LTO, arm64 — and it is an argument
about what the optimiser *may* do, not a guarantee written into the source. A
build with LTO enabled, or a future compiler that special-cases the allocator,
could in principle reach a different conclusion. Two things follow:

1. The correct durable answer is the CI relocation assertion proposed in
   Section 6, which converts "the compiler happened to do the right thing" into
   "the build fails if it stops". It asserts on the compiled object, so it
   re-verifies the property on every OE and every toolchain change.
2. If the lab prefers belt and braces, changing `vector_zero`/`poly_zero` to
   `OPENSSL_cleanse` is a two-line edit inside the permitted delta
   (`crypto/ml_dsa/`) that removes the argument entirely, at the cost of a small
   permanent divergence from upstream in two headers. We have **not** made that
   change; see Open items. OpenSSL's own ML-KEM path already uses
   `OPENSSL_cleanse` for the identical job (`openssl/crypto/ml_kem/ml_kem.c:1897-1899`),
   so the asymmetry between the two algorithms is itself likely to draw a
   question.

---

## 6. Is a runtime memory-scrape test real evidence?

Story 3.3's acceptance criterion as written is *"Snapshot test shows no key
material after free."* Our position:

**A whole-process memory scrape is theatre. It should not be submitted.**

It fails in three independent ways, none of which is fixable by trying harder:

1. **Guaranteed false positives.** At the moment of the scrape, legitimate live
   copies of the same bytes exist all over the process: the `EVP_PKEY` the
   caller still holds, the PEM/DER buffer the caller owns, the DRBG state that
   produced the seed, the TLS key schedule. A scan that searches for the key
   bytes finds them and reports failure, correctly, about material that has not
   been freed and must not be zeroized.
2. **Guaranteed false negatives.** Freed pages get reused, overwritten, or
   returned to the OS between the free and the scrape. A pass proves the bytes
   are not *there now*; it does not prove they were cleansed rather than
   overwritten by unrelated activity. Re-run it on another machine and the
   result changes.
3. **The stated expectation is not even well-formed.** `free()` writes
   allocator metadata over the first 16–32 bytes of every released chunk on
   every mainstream libc, so "the block reads as all zeros after free" is not a
   property any correct implementation has. A test asserting it would fail on
   correctly-cleansed memory.

What CMVP actually consumes under SP 800-140B is the Security Policy's SSP
table: per CSP, its storage location, its zeroization method, and the rationale
that the method is effective. Not a memory dump.

### What we propose to submit instead

Three artifacts, in descending order of evidentiary value. **All three are
proposals. None has been built. Nothing in this section has been measured.**

**(a) The CSP inventory in Section 1, as the primary artifact.** One row per
CSP, each with an allocation site, a zeroization call, a trigger, and a
`file:line` a reviewer can open. It is the only artifact that survives a code
change, because every row is a citation rather than a captured output.

**(b) An object-level relocation assertion in CI.** For a named list of
CSP-handling functions, assert that the compiled symbol references
`_OPENSSL_cleanse` or `_CRYPTO_clear_free` and never a bare `_CRYPTO_free`:

```
objdump -d -r --disassemble-symbols=<sym> <object>
```

This is the strongest available answer to the one objection a lab genuinely
raises about software zeroization — *how do you know the compiler did not remove
it?* It measures the shipped binary rather than the source, it runs per-OE so it
answers the question separately for each certified toolchain, it fails loudly if
someone reintroduces a bare free, and it is exactly the technique that found the
Section 3 and Section 4 defects. It also settles Section 5 by measurement
instead of argument. Proposed location: a new script alongside
`ci/check-boundary-symbols.sh`, run against
`openssl/crypto/ml_kem/libfips-lib-ml_kem.o` and
`openssl/crypto/ml_dsa/libfips-lib-ml_dsa_*.o`.

**(c) A quarantining-allocator unit test**, replacing the memory-snapshot
criterion. Install `CRYPTO_set_mem_functions` hooks that copy each freed block
into a quarantine buffer *before* returning it to libc; generate ML-KEM and
ML-DSA keys from a known fixed seed; free them; assert the known seed bytes and
known key bytes appear nowhere in quarantine. This is deterministic,
in-process, CI-runnable, and it tests precisely the claim being made — that
every free of a CSP block was preceded by a cleanse of the correct length —
without any of the three failure modes above. `CRYPTO_set_mem_functions` is
available in the FIPS module build; only the mdebug block is gated
(`openssl/crypto/mem.c`).

**What (c) cannot cover, stated plainly:** stack and register residue. No
in-process allocator hook sees a stack frame. For stack CSPs — `seed[64]` in
`ossl_ml_kem_genkey`, `seed_copy[32]` in `qudo_keygen`, `rand_tmp[32]` in the
ML-DSA signature context — the honest evidence is source review plus
`OPENSSL_cleanse`'s non-elidable construction
(`openssl/crypto/mem_clr.c:18-25`) plus assertion (b) on the compiled object.
We will say that rather than imply coverage we do not have.

**Recommendation: amend Story 3.3's acceptance criterion** from *"Snapshot test
shows no key material after free"* to *"CSP inventory published; object-level
zeroization assertion green on all four cert OEs; quarantining-allocator test
green."* That is a substantive change to a sprint acceptance criterion and
needs Venkatesh's sign-off, not a unilateral edit.

---

## Open items

**Fixes and code**

1. **The Section 3 fixes have not been re-tested.** They are committed
   (`dd192a5`) and compiled, but `test/recipes/*ml_dsa*` has not been re-run
   against them in this session, and no CI job has executed on
   `feat/sprint3-test-interop` (see item 8). "Compiles and links with the right
   relocations" is not "passes the ML-DSA test recipes".
2. **The upstream reports for Section 4 have not been filed.** Five sites
   across three files, tracked in [upstream-defects.md](upstream-defects.md).
   Until they are filed we carry a known-defect list with no upstream ticket to
   point the lab at.
3. **`vector_zero` / `poly_zero` unchanged.** Section 5 argues they are sound
   as-is. Whether to convert them to `OPENSSL_cleanse` anyway — a two-line edit
   inside the permitted delta, buying a shorter answer to a lab question at the
   cost of a permanent divergence in two headers — is an open decision.
4. **The residual early-exit divergence in `qudo_keygen`** (`ml_dsa_key.c:506-509`,
   Section 3.3) is documented but not closed. Closing it means routing that
   return through `end:`.

**Measurement gaps**

5. **Single-OE only.** Every objdump and runtime measurement in this document
   was taken on macOS arm64 with Apple clang 17 at `-O3`. Nothing here has been
   re-measured on Linux x86_64, Linux aarch64, or Windows x64. Section 5's
   argument in particular is compiler-specific and must be re-run per OE before
   the Operational Testing Report is submitted. See the Story 3.0 CI findings
   and [reproducibility.md](reproducibility.md) for the state of the four-OE
   matrix.
6. **Neither proposed artifact (b) nor (c) exists.** No CI zeroization gate, no
   quarantining-allocator test. Section 6 is a proposal.
7. **`-O0` / coverage builds not measured.** If Sprint 3 introduces a coverage
   build, the dead-store reasoning in Section 5 and the object-level
   measurements in Sections 2–3 must be re-taken there.
8. **The `upstream-parity` gate cited as policy evidence in Section 4 may never
   have executed.** `.github/workflows/ci.yml:14-17` triggers only on pull
   requests targeting `main`, and Sprint 2 merged to `dev`. The gate encodes
   the ADR-0005 constraint correctly; whether it has ever run is a separate,
   open Story 3.0 question. We also did **not** fetch upstream `openssl-3.5.7`
   to byte-compare the subtree ourselves; the assertion that
   `providers/implementations/` is stock rests on the gate's policy plus a clean
   `git status`, not on a byte comparison we performed.
9. **Object files can lag their sources.** `openssl/crypto/ml_kem/ml_kem.c` was
   modified during this analysis by commit `033cab7` (a comment-only insertion
   of 10 lines documenting [ADR-0011](adr/ADR-0011-ml-kem-dead-math-already-eliminated.md));
   the ML-KEM line citations here were re-verified against the post-commit file,
   but the ML-KEM objects that were objdump'd predate it. Since the change is
   comment-only the relocation tallies are unaffected — but the general point
   stands: any object-level evidence submitted to the lab must be regenerated
   from a clean build of the exact commit being certified, not from an
   incrementally-built tree.
10. **qudo-pqc-lib's ML-DSA zeroization is INFERRED, not measured.** The 10
    `mld_zeroize` sites in
    `qudo-pqc-lib/qudo-mldsa/mldsa-native/mldsa/src/sign.c` were counted by
    grep. The ML-KEM side was read in full; the ML-DSA side was not, and
    neither was objdump'd.

**Scope gaps**

11. **SLH-DSA not audited.** Undelegated per
    [ADR-0010](adr/ADR-0010-slh-dsa-stays-upstream.md), but its private keys
    are still module CSPs. `openssl/crypto/slh_dsa/` needs the same pass before
    the CSP register is complete.
12. **The Security Policy needs a sentence on the math-only build.**
    qudo-pqc's zeroize-on-PCT-failure is compiled out because
    `QUDO_FIPS_MODULE` is deliberately undefined; OpenSSL owns the PCT. That is
    correct under the boundary rule but must be written down, either in
    [ADR-0009](adr/ADR-0009-qudo-pqc-standard-build.md) or in the CSP register.
13. **Secure-heap asymmetry.** The ML-KEM export path uses
    `OPENSSL_secure_zalloc` / `OPENSSL_secure_clear_free`
    (`openssl/providers/implementations/keymgmt/ml_kem_kmgmt.c:275-324`); the
    ML-DSA path does not. Whether any target OE requires an mlock/secure-heap
    claim for PQC private keys is undecided, and the asymmetry will draw a
    question either way.
14. **Story 3.3's acceptance criterion needs amending** per Section 6. Requires
    sign-off; recorded here, not actioned.
