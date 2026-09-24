# Duplicate hash implementation disclosure

**Document ID:** QSSL-DISC-001
**Sprint 3, Story 3.8**
**Status:** For CMVP laboratory pre-engagement
**Date of measurement:** 2026-07-23
**Module under measurement:** `openssl/providers/fips.dylib`, built from
QudoSSL `feat/sprint3-test-interop` @ `33adc62`

## Summary

The QudoSSL FIPS 140-3 module contains two independent implementations of the
SHA-2 and SHA-3/SHAKE/Keccak-f[1600] families. One is OpenSSL's own, from
`crypto/sha/`; it is the implementation we claim, it serves every fetchable
digest service, and it is the only one registered in the provider dispatch
tables. The second is linked in from `qudo-pqc-lib`, which supplies the ML-KEM
and ML-DSA math the module delegates to; it is reachable only from inside those
two algorithms and is not exposed as a service by any name. We measured 121
qudo-supplied hash symbols across 22 linked object-file instances inside the
module, approximately 23.7 KB of the 2,011,976-byte module (~1.2%). We are
disclosing this because FIPS 140-3 cert review treats two equivalent approved
functions inside one boundary as a finding, and because ADR-0003 — the record
that first flagged it — describes only part of what is actually there. This
document states what is duplicated, why one class of it cannot be removed and
the other class can, what evidence shows correctness is unaffected, what the
duplication costs, and exactly what we are asking the laboratory to accept.

## Scope

**In scope.** The SHA-2, SHA-3, SHAKE and Keccak-f[1600] code inside the single
certified artifact `providers/fips.so` (`fips.dylib` on macOS), on the macOS
arm64 operational environment, at the subtree pins recorded in
[subtree-pins.md](subtree-pins.md).

**Out of scope.** The lattice arithmetic itself (ML-KEM NTT, ML-DSA NTT and
sampling) is not duplicated in any relevant sense and is not discussed here
except where a hash feeds it. Randomness, integrity, self-test orchestration
and the FIPS state machine are single-sourced from OpenSSL and are covered by
[ADR-0009](adr/ADR-0009-qudo-pqc-standard-build.md); this document confirms by
measurement that no qudo-supplied component of those services is inside the
module, but does not otherwise analyse them.

**Relationship to prior records.** This document supersedes the
"Consequences" bullet in
[ADR-0003](adr/ADR-0003-epic2-boundary-dedupe-closure.md) that reads *"the
boundary retains qudo-pqc's inlined Keccak … it is PQC algorithm code, not a
separately claimed SHA-3 implementation."* That statement remains true, but it
is not complete — see §2.4. ADR-0003 is itself marked superseded by ADR-0009;
its dedupe reasoning was retained, and this document is the measured version of
that reasoning.

## 0. Measurement basis

Every quantitative claim below was produced on 2026-07-23 against build
artifacts already present in the tree. No rebuild was performed.

| Item | Value |
|---|---|
| Platform | Darwin 25.5.0, arm64 (Apple Silicon) |
| QudoSSL commit | `33adc62`, branch `feat/sprint3-test-interop` |
| `openssl/` subtree | tag `openssl-3.5.7` (`8cf17aa…`) — [subtree-pins.md](subtree-pins.md) |
| `qudo-pqc-lib/` subtree | `main` @ `73e5499…` — [subtree-pins.md](subtree-pins.md) |
| FIPS module | `openssl/providers/fips.dylib`, 2,011,976 bytes |
| qudo archive | `qudo-pqc-lib/build/lib/libqudo-pqc.a`, 925,080 bytes |
| Delegation active | `openssl/configdata.pm:178` lists `QUDO_PQC_DELEGATE` among configured defines |
| Archive build mode | `-DQUDO_PQC_MATH_ONLY=ON` (`build/build_libqudo_pqc.sh:85`) |

Primary commands:

```sh
nm -a  openssl/providers/fips.dylib
nm -g -A qudo-pqc-lib/build/lib/libqudo-pqc.a
ar t   qudo-pqc-lib/build/lib/libqudo-pqc.a
```

Symbol-set membership was computed by joining defined symbols in the archive
against defined symbols in the module. Byte sizes for symbol groups are
**address-extent estimates** (address of the next symbol minus address of this
symbol, over the address-sorted `__TEXT` symbol table); they are accurate to
within padding and are labelled "approx." wherever used. They are not derived
from a differential build, because a no-delegation module was not built.

> **NOT MEASURED — other operational environments.** Every symbol figure in this
> document is from the macOS arm64 artifact. The Linux x86_64, Linux aarch64 and
> Windows x64 modules were not available to measure. The *shape* of the finding
> will be the same on all four (the same archive members are compiled on every
> target), but the *names and count* of the Class A symbols will differ on
> x86_64, where `qudo-pqc-lib/qudo-mlkem/CMakeLists.txt:81-83,262-265` selects
> `x86_64` backends and `:44` enables AVX2 in place of the AArch64/NEON
> backends measured here. Per-OE inventories are an open item (§7).

## 1. What is duplicated inside the boundary

### 1.1 OpenSSL's implementation — the one we claim

OpenSSL's SHA-2 and SHA-3/Keccak are present in the module and are the
implementations behind every registered digest service. Verified defined in
`fips.dylib`:

- Keccak-f[1600] permutation and sponge: `KeccakF1600`, `KeccakF1600_int`,
  `KeccakF1600_ce`, `KeccakF1600_cext`, `_SHA3_absorb`, `_SHA3_squeeze`,
  `_SHA3_absorb_cext`, `_SHA3_squeeze_cext`, `_armsha3_sha3_absorb`
- SHA-3 sponge wrapper and provider dispatch:
  `_ossl_sha3_init/_reset/_update/_final/_squeeze`, and the dispatch tables
  `_ossl_sha3_224_functions` … `_ossl_sha3_512_functions`
- SHA-2: `_SHA256_Init/_Update/_Final/_Transform`,
  `_SHA512_Init/_Update/_Final/_Transform`, `_SHA384_Init/_Update/_Final`,
  the compression cores `_sha256_block_data_order`, `_sha512_block_data_order`,
  `sha256_block_armv8`, `sha512_block_armv8`, `_sha256_block_neon`, and the
  AArch64 capability probes `__armv8_sha256_probe`, `__armv8_sha512_probe`

Approximate extent of this group: 30 text symbols, ~19.3 KB.

These are the only hash implementations reachable by name. Measured with the
FIPS provider active (base + fips only):

```
OPENSSL_CONF=<fips-and-base.cnf> apps/openssl list -digest-algorithms -provider fips
```

returns 15 provided algorithms, every one an OpenSSL name and every one tagged
`@ fips`: SHA-1, SHA-224/256/384/512, SHA-512/224, SHA-512/256, SHA3-224/256/384/512,
SHAKE-128, SHAKE-256, KECCAK-KMAC-128, KECCAK-KMAC-256. No qudo implementation
appears.

### 1.2 Class A — qudo's per-family SHAKE/Keccak (92 symbols)

This is the class ADR-0003 described. It is the FIPS 202 code that
`qudo-pqc-lib` compiles *into* each PQC algorithm, once per algorithm family and
once per CPU backend, with a mangled symbol prefix so the copies do not collide.
All 92 are defined in `fips.dylib`:

| Prefix | Hash symbols in module | Meaning |
|---|---|---|
| `_mlkem_ref_*` | 18 | ML-KEM, portable C backend |
| `_mlkem_neon_*` | 21 | ML-KEM, AArch64/NEON backend |
| `_mldsa_ref_*` | 25 | ML-DSA, portable C backend |
| `_mldsa_neon_*` | 28 | ML-DSA, AArch64/NEON backend |
| **Total** | **92** | |

Source objects (6 distinct object files, 18 linked instances):
`fips202.c.o` ×4, `fips202x4.c.o` ×4, `keccakf1600.c.o` ×4,
`keccakf1600_round_constants.c.o` ×2, `keccak_f1600_x1_v84a_aarch64_asm.S.o` ×2,
`keccak_f1600_x2_v84a_aarch64_asm.S.o` ×2.

Concretely this places inside the module, alongside OpenSSL's single
Keccak-f[1600]:

- four independently compiled copies of the permutation
  (`_mlkem_ref_keccakf1600_permute`, `_mlkem_neon_keccakf1600_permute`,
  `_mldsa_ref_keccakf1600_permute`, `_mldsa_neon_keccakf1600_permute`),
  each with a 4-lane sibling (`…_keccakf1600x4_permute`);
- four AArch64 assembly Keccak kernels
  (`_mlkem_neon_keccak_f1600_x1_v84a_aarch64_asm`, `…_x2_v84a_…`, and the
  `_mldsa_neon_` pair);
- SHA3-256 and SHA3-512 under the ML-KEM prefixes (`_mlkem_ref_sha3_256`,
  `_mlkem_ref_sha3_512`, and the `_neon` pair) — these are FIPS 203's H and G;
- SHAKE128/SHAKE256 incremental and 4-way-batched APIs under all four
  prefixes (`…_shake128_absorb_once`, `…_shake128x4_squeezeblocks`,
  `…_shake256x4_absorb_once`, and so on).

Approximate extent: ~14.4 KB.

### 1.3 Class B — qudo's generic SHA-2/SHA-3 API (29 symbols)

**This class is not described in ADR-0003 and is new information for the
laboratory.** It is a standalone, generically named SHA-2 and SHA-3
implementation — not inlined into anything — sourced from the *SLH-DSA* subtree
even though SLH-DSA is deliberately not delegated
([ADR-0010](adr/ADR-0010-slh-dsa-stays-upstream.md)).

Sources: `qudo-pqc-lib/qudo-slhdsa/slhdsa-native/{sha2_256.c, sha2_512.c,
sha3_api.c, sha3_f1600.c}`.

All 29 exported symbols are defined in `fips.dylib` (verified individually):

- `sha2_256.c.o` (9): `_sha2_224`, `_sha2_224_init`, `_sha2_256`,
  `_sha2_256_compress`, `_sha2_256_copy`, `_sha2_256_final_len`,
  `_sha2_256_final_pad`, `_sha2_256_init`, `_sha2_256_update`
- `sha2_512.c.o` (13): `_sha2_384`, `_sha2_384_init`, `_sha2_512`,
  `_sha2_512_224`, `_sha2_512_224_init`, `_sha2_512_256`, `_sha2_512_256_init`,
  `_sha2_512_compress`, `_sha2_512_copy`, `_sha2_512_final_len`,
  `_sha2_512_final_pad`, `_sha2_512_init`, `_sha2_512_update`
- `sha3_api.c.o` (6): `_sha3`, `_sha3_init`, `_sha3_update`, `_sha3_final`,
  `_shake`, `_shake_out`
- `sha3_f1600.c.o` (1): `_keccak_f1600`

Approximate extent: ~9.2 KB.

**Class B is not called by anything QudoSSL invokes.** It is present because of
static-link granularity, not because any delegated operation uses it. The chain,
verified at object level:

1. `qudo-pqc-lib/qudo-mldsa/CMakeLists.txt:499-504` compiles those four
   `slhdsa-native` files into the ML-DSA target, with the in-tree comments
   *"SHA3/SHAKE from slhdsa-native for `QUDO_MLDSA_shake256_*` wrapper"* and
   *"SHA-2 from slhdsa-native for HashML-DSA pre-hash mode (FIPS 204 §5.4)"*.
2. `mldsa_wrapper.c.o` has undefined references to `_sha2_224`, `_sha2_256`,
   `_sha2_384`, `_sha2_512`, `_sha2_512_224`, `_sha2_512_256`, `_sha3`,
   `_shake`. Every one of those calls is inside
   `static size_t compute_pre_hash(...)` at
   `qudo-pqc-lib/qudo-mldsa/src/mldsa_wrapper.c:1574`, whose call sites are
   `QUDO_MLDSA_sign_pre_hash` (`:1658`) and `QUDO_MLDSA_verify_pre_hash`
   (`:1684`) only.
3. The same translation unit also defines
   `_QUDO_MLDSA_keypair_internal`, `_QUDO_MLDSA_sign_internal` and
   `_QUDO_MLDSA_verify_internal` — the entry points QudoSSL *does* call. The
   linker therefore pulls the whole object, and with it the SHA-2/SHA-3
   dependencies. `sha3_api.c.o` in turn pulls `sha3_f1600.c.o` for
   `_keccak_f1600`.
4. QudoSSL never calls a pre-hash entry point.
   `grep -oE 'QUDO_[A-Za-z0-9_]+'` over
   `openssl/crypto/ml_dsa/ml_dsa_sign.c`, `openssl/crypto/ml_dsa/ml_dsa_key.c`
   and `openssl/crypto/ml_kem/ml_kem.c` yields exactly:
   `QUDO_KEM_new/free/keypair_derand/keypair_from_seed/encaps_derand/decaps`,
   `QUDO_MLDSA_new/free/keypair_internal/sign_internal/verify_internal`, and the
   algorithm-identifier constants. No `*_pre_hash` symbol appears, and no
   occurrence of `pre_hash` or `prehash` exists anywhere in
   `openssl/crypto/ml_dsa/*.c` or `openssl/crypto/ml_kem/*.c`.
   OpenSSL performs the FIPS 204 §5.4 message encoding itself, in `msg_encode()`
   (`openssl/crypto/ml_dsa/ml_dsa_sign.c:478`, `:518`), before delegating at
   `:486` and `:527`.
5. `mldsa_shake256.c.o`, the other in-archive consumer of `sha3_api`, is **not**
   linked into the module: none of its six exported symbols
   (`_QUDO_MLDSA_shake256*`) is present in `fips.dylib`. `mldsa_wrapper.c.o` is
   therefore the sole reason Class B is inside the boundary.

We state this plainly rather than presenting it as equivalent to Class A,
because it is a materially weaker finding — and, unlike Class A, it is
removable (§2.3).

### 1.4 What is NOT duplicated

Measured, for completeness, because it bounds the finding:

- **Zero** qudo FIPS-infrastructure symbols in the module. A case-insensitive
  match for `qudo_aes`, `qudo_fips_*`, `qudo_ctrdrbg`, `qudo_pqc_post`,
  `qudo_pqc_pct`, `qudo_pqc_init`, `qudo_pqc_integrity`, `qudo_audit`,
  `qudo_*_indicator`, `qudo_*_embedded` over `nm -a fips.dylib` returns 0. There
  is no second AES, no second CTR-DRBG, no second HMAC, no second POST engine
  and no second integrity check inside the boundary.
- **Zero** qudo SLH-DSA in the module: `slh_sha2.c.o`, `slh_shake.c.o`,
  `slh_dsa.c.o`, `slhdsa_wrapper.c.o`, `slh_prehash.c.o` all have exported
  symbols and none appears in `fips.dylib`. This confirms ADR-0010's reduction
  claim by measurement.
- `openssl/crypto/slh_dsa/` is byte-identical to upstream `openssl-3.5.7`:
  `git diff --name-only 5943b28 HEAD -- openssl/` returns exactly four files —
  `Configure`, `crypto/ml_dsa/ml_dsa_key.c`, `crypto/ml_dsa/ml_dsa_sign.c`,
  `crypto/ml_kem/ml_kem.c`.

### 1.5 Totals

| Class | Symbols | Object instances | Distinct objects | Approx. bytes |
|---|---:|---:|---:|---:|
| A — per-family SHAKE/Keccak | 92 | 18 | 6 | ~14,440 |
| B — generic SHA-2/SHA-3 | 29 | 4 | 4 | ~9,236 |
| **qudo total** | **121** | **22** | **10** | **~23,676** |
| OpenSSL SHA-2/SHA-3 core (claimed) | 30 | — | — | ~19,348 |

qudo's hash code is approximately **1.2%** of the 2,011,976-byte module.

### 1.6 A related observation outside the module boundary

The same duplication is visible in `libcrypto.3.dylib`: 92 Class A and 29
Class B symbols are defined there too, and `nm -gU libcrypto.3.dylib` reports
154 *exported* symbols matching `_QUDO_`, `_mlkem_(ref|neon)`,
`_mldsa_(ref|neon)` or `_sha2_256`. This is a consequence of
`openssl/Configure:1582` pushing the archive onto the global link flags
(`push @{$config{lflags}}, $withargs{qudo_pqc_archive}`) rather than into the
FIPS module's own source manifest. `libcrypto` is outside the cryptographic
module boundary, so this does not add to the duplicate-implementation finding,
but it is disclosed here because it is visible to the same `nm` inspection a
reviewer will run, and because the related source-manifest gap is an open item
(§7).

For the avoidance of doubt in the other direction: the **static** archive
`openssl/libcrypto.a` contains **none** of this code. `nm -g libcrypto.a` yields
zero Class A symbols, zero Class B symbols and zero `qudo_*` symbols. Static
linking is a link-time composition, so `libcrypto.a` never absorbs the qudo
archive; a reviewer inspecting `libcrypto.a` will correctly find nothing and
should not conclude from that absence that the module is free of the duplication.
The module artifact `providers/fips.so` is the one to inspect.

## 2. Why de-duplication was not possible

### 2.1 Class A: there is no injection seam (measured)

The design's §6.3 specified a host-hash routing API; it was never implemented.

- `grep -rn 'set_hash_provider|qudo_pqc_hash_fn|hash_provider|qudo_pqc_set_hash'`
  over the whole of `qudo-pqc-lib/` returns **nothing**.
- `qudo-pqc-lib/CMakeLists.txt:47` opens a block headed
  *"Boundary-dedupe placeholders (recognised for QudoSSL build-flag
  compatibility)"*, and `:51-52` records in-tree:
  *"`USE_HOST_HASHES` : the PQC algorithms hash via their own inlined Keccak
  (not a swappable layer); the only routable infra SHA lives under HMAC. No live
  routing here."*
- `CMakeLists.txt:56` declares the option itself as
  `"[placeholder] Infra-SHA host routing; completed with Epic 5 KAT migration."`

So the flag exists, is accepted, and does nothing. There is no function pointer,
no registration call and no `#ifdef` that would let OpenSSL's Keccak be
substituted. Routing Class A to the host would require modifying `qudo-pqc-lib`'s
algorithm sources, not configuring them.

### 2.2 Class A: routing is also semantically blocked, not merely unimplemented

Even if a seam were added, a substantial part of the workload has no counterpart
in OpenSSL's digest API. ML-KEM and ML-DSA rejection sampling and matrix
expansion in `qudo-pqc-lib` use **4-way lane-interleaved SHAKE**:
`_mlkem_ref_shake128x4_absorb_once`, `_mlkem_ref_shake128x4_squeezeblocks`,
`_mldsa_ref_shake256x4_absorb_once`, and their NEON equivalents, all measured
present in the module. OpenSSL's provider surface offers only the single-lane
sponge (`_SHA3_absorb` / `_SHA3_squeeze`, reached through `EVP_MD_CTX`); it has
no batched form. The AArch64 `x1`/`x2` v8.4-A Keccak kernels exist specifically
to exploit that batching. Substituting `EVP_MD_CTX` calls would not be a
re-routing; it would be a rewrite of the sampling loops to a different, slower
algorithmic shape.

Upstream OpenSSL avoids the duplication precisely because it accepts that cost:
its own ML-KEM hashes through `EVP_DigestInit_ex` on `key->shake128_md` /
`key->sha3_256_md` (`openssl/crypto/ml_kem/ml_kem.c:663-696`), and its own
ML-DSA hashes through `shake_xof()` / `shake_xof_2()` / `shake_xof_3()` over
`EVP_MD_CTX` (`openssl/crypto/ml_dsa/ml_dsa_hash.h:13,22,32`). Adopting that approach inside
`qudo-pqc-lib` would discard the performance characteristics that are the
reason for delegating to it at all.

**This is the technical claim we ask the reviewer to accept for Class A:
the duplication is a property of the algorithm implementation, not of the build
configuration, and cannot be removed by configuring or linking differently.**

### 2.3 Class B: removable in principle — stated as such

Class B is a different case and we do not claim it is unroutable.

It is present only because `compute_pre_hash()` shares a translation unit with
the entry points QudoSSL calls (§1.3). Two upstream changes to `qudo-pqc-lib`
would remove all 29 symbols and 4 objects from the boundary:

1. move `compute_pre_hash()` and the `QUDO_MLDSA_*_pre_hash` wrappers into their
   own translation unit, so the linker no longer pulls the SHA-2/SHA-3
   dependencies when only `*_internal` is referenced; or
2. stop compiling `slhdsa-native/{sha2_256.c, sha2_512.c, sha3_api.c,
   sha3_f1600.c}` into the ML-DSA target
   (`qudo-pqc-lib/qudo-mldsa/CMakeLists.txt:499-504`) in the
   `QUDO_PQC_MATH_ONLY` configuration.

**NOT YET DONE.** Neither change has been made, requested or scheduled. It is a
`qudo-pqc-lib` change, and it would move the subtree pin, which conflicts with
freezing that pin on an immutable tag before the certification branch. We offer
it to the laboratory as an available remediation and will act on it if the lab
prefers the residue reduced (§6, §7).

### 2.4 What ADR-0003 got right, and where it is incomplete

Verified against the tree rather than restated:

| ADR-0003 statement | Verification |
|---|---|
| "**2.2** (host hashes) — **not routable.** Keccak is inlined into the PQC algorithm implementations for performance; there is no call seam to redirect." | **Confirmed.** No seam exists (§2.1); the batched-SHAKE argument strengthens it (§2.2). |
| "**2.3** (host AES) — **no-op.** AES is only referenced by qudo-pqc's own DRBG" | **Confirmed by outcome.** Zero qudo AES symbols are in the module (§1.4), though under [ADR-0009](adr/ADR-0009-qudo-pqc-standard-build.md) this is achieved by the `QUDO_PQC_MATH_ONLY` build never compiling them, not by the mechanism ADR-0003 anticipated. |
| "the boundary retains qudo-pqc's **inlined Keccak**. This must be declared to the lab … it is PQC algorithm code, not a separately claimed SHA-3 implementation." | **True but incomplete.** It describes Class A only. It omits Class B entirely — 29 generic, *non*-inlined SHA-2/SHA-3 symbols that are not PQC algorithm code and are not called by any delegated operation. |
| "AES/SHA/HMAC dedupe is achieved through the Epic 5 build configuration" | **Superseded.** ADR-0009 replaced Epic 5's flag work with `QUDO_PQC_MATH_ONLY`; ADR-0003 is marked superseded on its own face. The dedupe that was achieved is the FIPS-infrastructure dedupe (§1.4), not hash dedupe. |

Restating ADR-0003 to the laboratory unchanged would understate the finding.
That is the reason this document exists.

## 3. Why this does not affect correctness

Both implementations compute the same standardised functions — FIPS 180-4 SHA-2,
FIPS 202 SHA-3/SHAKE/Keccak-f[1600]. Correctness of the qudo-side copies is
established transitively and by direct measurement.

### 3.1 Measured: 525 NIST ACVP vectors pass through the delegated path under the FIPS provider

Genuine NIST ACVP vectors for ML-KEM and ML-DSA ship in the tree, transcribed
from `usnistgov/ACVP-Server` `internalProjection.json` (version 42) — the file
headers state the source URL explicitly, e.g.
`openssl/test/recipes/30-test_evp_data/evppkey_ml_kem_keygen.txt:8-10`.

We executed them on 2026-07-23 against the built module, with only the `base`
and `fips` providers active, driving the public `EVP_PKEY_*` API through
`test/evp_test`. Reproduction (note that the shipped `test/fips-and-base.cnf`
uses a *relative* `.include fipsmodule.cnf`, so the include must be made
absolute or the command run from `openssl/test/`; otherwise the config silently
fails to load the FIPS provider and the run is not evidence of anything):

```sh
sed 's|^\.include fipsmodule.cnf|.include '"$PWD"'/test/fipsmodule.cnf|' \
    test/fips-and-base.cnf > /tmp/fips-abs.cnf
export OPENSSL_CONF=/tmp/fips-abs.cnf OPENSSL_MODULES=$PWD/providers \
       DYLD_LIBRARY_PATH=$PWD
apps/openssl list -providers          # must show exactly: base (active), fips (active)
test/evp_test test/recipes/30-test_evp_data/<file>.txt
```

| Vector file | ACVP source | Result |
|---|---|---|
| `evppkey_ml_kem_keygen.txt` | ML-KEM-keyGen-FIPS203 | **75 tests, 0 errors** |
| `evppkey_ml_kem_encap_decap.txt` | ML-KEM-encapDecap-FIPS203 | **105 tests, 0 errors** |
| `evppkey_ml_dsa_keygen.txt` | ML-DSA-keyGen-FIPS204 | **75 tests, 0 errors** |
| `evppkey_ml_dsa_siggen.txt` | ML-DSA-sigGen-FIPS204 | **180 tests, 0 errors** |
| `evppkey_ml_dsa_sigver.txt` | ML-DSA-sigVer-FIPS204 | **90 tests, 0 errors** |
| **Total** | | **525 tests, 0 errors** |

These are bit-exact comparisons: the vectors supply a fixed seed or fixed
entropy and compare the full public key, private key, ciphertext, shared secret
or signature byte-for-byte against the NIST expected value. Because
`ossl_ml_kem_*` and `ossl_ml_dsa_*` delegate under `QUDO_PQC_DELEGATE`, every H,
G, J, PRF, SHAKE-expansion and rejection-sampling call inside `qudo-pqc-lib` is
exercised by these runs. A defective Keccak on the qudo side cannot produce a
byte-exact FIPS 203 ciphertext or a byte-exact FIPS 204 signature.

The same files are registered for both the default and FIPS provider
configurations in `openssl/test/recipes/30-test_evp.t:41,43` (default and
`fips-and-base.cnf` configs) and `:103-126` (PQC vector files pushed into the
list that runs against every config), so this is CI coverage, not an ad-hoc
check.

**Note on Class B:** the 525 vectors do **not** exercise Class B, because
nothing exercises Class B — it is unreachable from every service the module
offers (§1.3). Its correctness is therefore not established by these vectors.
It is also not relied upon by any operation. We state this rather than implying
the vector coverage extends to it.

### 3.2 Measured: the module's own CASTs drive the delegated path

`openssl/providers/fips/self_test_kats.c` contains OpenSSL's ML-KEM and ML-DSA
conditional algorithm self-tests, including the ML-KEM CASTs mandated by
FIPS 140-3 IG 10.3.A resolution 14 (`:657`, `:713`), covering encapsulation,
decapsulation non-rejection, decapsulation implicit-rejection and key generation
(`:775-778`). These run against `ossl_ml_kem_*` / `ossl_ml_dsa_*`, which
delegate. Self-test descriptors exist for ML-KEM/ML-DSA keygen, ML-DSA sign and
both PCTs (`openssl/include/openssl/self_test.h:56-57, 71, 91-92`).

### 3.3 Measured: the integrity HMAC covers the duplicated code

`openssl/providers/fips/self_test.c:377-379` — *"Always check the integrity of
the fips module"* — computes HMAC-SHA-256 over the entire module file. Since
qudo's objects are linked into that file, the duplicated hash code is inside the
integrity-protected image. There is exactly one integrity mechanism, OpenSSL's;
qudo's embedded integrity HMAC is not compiled in the `QUDO_PQC_MATH_ONLY`
configuration and is measured absent (§1.4).

## 4. Why this does not weaken the security claim — and what it costs

### 4.1 Why the security claim is unaffected

- **No second approved service is offered.** The duplicated code is not
  registered in any provider dispatch table and is not fetchable. Measured:
  15 digest algorithms provided by the FIPS provider, all OpenSSL (§1.1). A
  caller cannot select qudo's SHA-256 or qudo's SHAKE-128 by any name, by any
  property query, or by accident.
- **There is one FIPS state machine, one approved DRBG, one integrity check,
  one indicator authority and one self-test orchestrator** — all OpenSSL's.
  Measured: zero qudo FIPS-infrastructure symbols in the module (§1.4). This is
  the condition ADR-0009 set out to establish and it holds.
- **No CSP crosses into the duplicated code except as algorithm-internal
  intermediate state.** The values qudo's Keccak sees are the ML-KEM/ML-DSA
  seeds, coefficients and sampling streams already covered by those algorithms'
  security claims; they are not routed there by any service-level API.
- **Randomness never originates in the duplicated code.** Every delegated call
  is a seeded/derandomised entry point taking randomness from OpenSSL's approved
  DRBG as an argument — `QUDO_KEM_keypair_from_seed`, `QUDO_KEM_encaps_derand`,
  `QUDO_MLDSA_keypair_internal`, `QUDO_MLDSA_sign_internal` (§1.3 item 4, and
  [ADR-0009](adr/ADR-0009-qudo-pqc-standard-build.md) "The rule this creates").

### 4.2 What it costs — stated plainly

**a) Module size.** ~23.7 KB of the 2,011,976-byte module, ~1.2%. Small in
absolute terms. We do not present this as negligible: it is 22 object instances
and 121 symbols a reviewer will see in any symbol dump of the module, and every
one of them will need an answer.

**b) Duplicate CAST/POST surface — and a real asymmetry.** OpenSSL's SHA-2 and
SHA-3 have their own KATs: `providers/fips/self_test_data.inc:199-218` registers
exactly three digest tests — SHA1, SHA512, SHA3-256 — and
`providers/fips/self_test_kats.c:37` runs each via
`EVP_MD_fetch(libctx, t->algorithm, NULL)`, i.e. **by name**, which can only ever
resolve to OpenSSL's implementation. qudo's hash code has **no KAT of
its own and will not get one**, because it is not a claimed service. Its
correctness is asserted only transitively, through the ML-KEM and ML-DSA CASTs
and the 525 ACVP vectors (§3). That is defensible for Class A — those hashes are
on the critical path of every ML-KEM/ML-DSA operation, so a fault in them fails
the algorithm CAST immediately. **It is not defensible for Class B**, which is
on no path at all: a fault in Class B would be detected by nothing in the module.
We do not argue otherwise. Our position is that unreachable code cannot produce a
wrong answer, not that it is tested.

**c) Algorithm-testing implications.** We are not seeking CAVP certificates for
qudo's SHA-2 or SHA-3, and we are not listing them as approved algorithms in the
Security Policy. The consequence the lab should weigh is that the module's
approved-algorithm list will name a set of SHA implementations that is smaller
than the set of SHA implementations physically present in the binary, and the
Security Policy must say so. If the laboratory's position is that any
Keccak-f[1600] inside the boundary must be separately validated, that changes
the ACVP scope materially, and we would rather discover it at pre-engagement
than at review.

**d) Per-OE variance.** The Class A symbol names and count are
architecture-dependent (§0). Four operational environments will produce four
different symbol inventories for the same finding. Producing and reconciling
those four inventories is work that has not been done.

**e) Source-manifest inconsistency.** `openssl/providers/fips.module.sources`
(732 lines) contains **zero** occurrences of "qudo"; `git log` shows that file,
`fips.checksum` and `providers/fips/build.info` have not been modified since the
subtree import (`5943b28`). The archive reaches the module through global link
flags (`openssl/Configure:1582`) instead. The boundary's source enumeration
therefore does not currently list any of the code discussed in this document.
This is a documentation/manifest defect, not a behavioural one, and it is an
open item (§7).

**f) Maintenance.** Two Keccak code bases inside one certified artifact means
two places to assess for any future FIPS 202 CVE or errata, and both are inside
the integrity-protected image, so either one changing forces a re-validation of
the module hash.

## 5. Which implementation serves which algorithm

This is the mapping a laboratory needs to associate an operation with a
validated implementation. Every row is verified against the source at the cited
location.

| Operation | Hash implementation actually executed | Evidence |
|---|---|---|
| All fetchable digest services (SHA-1, SHA-2 family, SHA-3 family, SHAKE, KECCAK-KMAC) | **OpenSSL only** | 15 provided digests, all `@ fips` (§1.1) |
| HMAC, KDFs, DRBG, RSA/ECDSA/EdDSA message digesting, module integrity | **OpenSSL only** | No qudo symbol is reachable from these paths; zero qudo FIPS-infra symbols (§1.4) |
| **SLH-DSA** (all 12 parameter sets), keygen/sign/verify | **OpenSSL only** | Not delegated; `crypto/slh_dsa/` byte-identical to upstream; zero qudo SLH-DSA symbols in module ([ADR-0010](adr/ADR-0010-slh-dsa-stays-upstream.md), §1.4) |
| **ML-KEM** keygen — lattice + H/G/PRF/A-matrix sampling | **qudo Class A** (`_mlkem_{ref,neon}_*`) | `ml_kem.c:2220` `qudo_genkey` → `:2237` `QUDO_KEM_keypair_from_seed` |
| **ML-KEM** encapsulation | **qudo Class A** | `ml_kem.c:2283` `qudo_encap` → `:2302` `QUDO_KEM_encaps_derand` |
| **ML-KEM** decapsulation (incl. implicit rejection) | **qudo Class A** | `ml_kem.c:2318` `qudo_decap` → `:2342` `QUDO_KEM_decaps` |
| **ML-KEM** public-key parse: pkhash `H(ek)` and A-matrix pre-expansion | **OpenSSL** (SHA3-256 + SHAKE128 via `EVP_MD_CTX`) | `ml_kem.c:1613-1614` `hash_h()` + `matrix_expand()`, outside any delegation guard; `hash_h` at `:691`, `single_keccak` at `:663` |
| **ML-DSA** keygen — seed expansion, S-vector sampling, A-matrix, signature math | **qudo Class A** (`_mldsa_{ref,neon}_*`) | `ml_dsa_key.c:628-629` selects `qudo_keygen`; `:525` `QUDO_MLDSA_keypair_internal` |
| **ML-DSA** keygen — public-key rederivation and `tr` recomputation performed by OpenSSL's decoder afterwards | **OpenSSL** (SHAKE128 + SHAKE256 via `EVP_MD_CTX`) | `ml_dsa_key.c:531` `ossl_ml_dsa_sk_decode` → `ml_dsa_encoders.c:815` → `ossl_ml_dsa_key_public_from_private` (`ml_dsa_key.c:363`) → `matrix_expand_A` (`:341`) and `shake_xof` (`:376`) |
| **ML-DSA** sign | **qudo Class A** | `ml_dsa_sign.c:486` `qudo_sign` → `:103` `QUDO_MLDSA_sign_internal` |
| **ML-DSA** verify | **qudo Class A** | `ml_dsa_sign.c:527` `qudo_verify` → `:131` `QUDO_MLDSA_verify_internal` |
| **ML-DSA** FIPS 204 §5.4 message encoding (domain separation) | **OpenSSL** (`msg_encode`, no hash for pure ML-DSA) | `ml_dsa_sign.c:478`, `:518` — applied before delegating at `:486` / `:527` |
| **HashML-DSA** pre-hash (qudo's `compute_pre_hash`) | **Never executed** | No `*_pre_hash` entry point is called (§1.3 item 4) |
| Anything served by qudo Class B (`_sha2_*`, `_sha3*`, `_shake*`, `_keccak_f1600`) | **Never executed** | Sole linked referencer is `compute_pre_hash`, which is unreachable (§1.3) |

Two rows above deserve emphasis because they are easy to state incorrectly:
**for ML-KEM public-key parsing and for ML-DSA key decoding, both implementations
run.** OpenSSL computes the public-key hash and the A-matrix with its own
Keccak; qudo computes them again internally during encap/decap/sign/verify. This
is redundant work, not a correctness problem — both compute the same FIPS 202
functions on the same inputs, and the 525 ACVP vectors pass end to end. We
disclose it so the lab does not conclude from the delegation description that
OpenSSL's Keccak is dormant for the PQC algorithms. It is not.

## 6. What we are asking the laboratory to accept

We ask the laboratory to accept the following four propositions.

1. **That qudo-pqc-lib's SHA-2, SHA-3, SHAKE and Keccak-f[1600] code inside
   `fips.so` is internal to the ML-KEM (FIPS 203) and ML-DSA (FIPS 204)
   implementations, and is not a separately claimed approved SHA-2 or SHA-3
   implementation** — in the same sense that the H, G, J and PRF functions
   written inline in any FIPS 203 reference implementation are internal to that
   implementation. It is consequently not proposed for its own CAVP validation
   and will not appear in the module's approved-algorithm list.

2. **That the module's single claimed SHA-2/SHA-3 implementation is OpenSSL's**,
   from `crypto/sha/`, which serves every fetchable digest service, the integrity
   HMAC, the DRBG, all KDFs and all classical signature schemes, and which holds
   the module's single set of digest self-test KATs.

3. **That the Class A duplication could not be removed**, on the technical
   grounds in §2.1 and §2.2: `qudo-pqc-lib` exposes no hash-injection seam
   (verified by exhaustive grep, and recorded in its own CMakeLists at :51-52),
   and part of the workload — 4-way lane-interleaved SHAKE — has no counterpart
   in OpenSSL's single-lane digest API, so substitution would be a rewrite of
   the sampling loops rather than a configuration change.

4. **That the Class B residue is disclosed as removable, and that we will remove
   it if the laboratory wants it removed.** We are not claiming it is
   unroutable. It is 29 symbols and ~9.2 KB of unreachable generic SHA-2/SHA-3
   pulled in by static-link granularity from a translation unit whose pre-hash
   half QudoSSL never calls (§1.3). Two identified upstream changes eliminate it
   (§2.3). We have deliberately not made them yet because they would move the
   `qudo-pqc-lib` subtree pin, and we would rather move it once, deliberately,
   with the laboratory's input, than twice.

Supporting history we place on the record voluntarily: SLH-DSA delegation was
implemented, tested green, measured at 1.6× slower than OpenSSL's, and then
**reverted specifically to shrink this finding**
([ADR-0010](adr/ADR-0010-slh-dsa-stays-upstream.md)). ADR-0010 records that
reversal as removing 79 `QUDO_SLHDSA` symbols plus the `slh_sha2` / `slh_shake`
objects from the module. Re-measured today at the current pin: the archive
defines **94** `_QUDO_SLHDSA*` symbols and **zero** of them are in `fips.dylib`;
`slh_sha2.c.o`, `slh_shake.c.o`, `slh_dsa.c.o`, `slhdsa_wrapper.c.o` and
`slh_prehash.c.o` export 81 symbols between them and none is linked in. (The
79-vs-94 difference is a different counting basis and/or subtree state in the
ADR; the material fact — zero in the module — holds either way.) We mention this
history not as advocacy but because it is
evidence of how the boundary was arrived at, and because it shows the residual
duplication is what remains after the removable parts were removed, with one
identified exception (Class B) that we are offering to remove as well.

## 7. Open items

Items **1–3** must close before this document is final for laboratory
submission. Items **4–7** are recorded so they are not lost.

1. **Per-OE symbol inventories — NOT MEASURED.** Every figure here is macOS
   arm64. Linux x86_64, Linux aarch64 and Windows x64 inventories must be
   produced and reconciled; on x86_64 the NEON Class A objects are replaced by
   AVX2 objects (inferred from `qudo-pqc-lib/qudo-mlkem/CMakeLists.txt:44,81-83,262-265`,
   not measured). The four inventories should be attached to this document
   before pre-engagement.

2. **Class B decision — NOT DECIDED.** Ask `qudo-pqc-lib` to split
   `compute_pre_hash()` into its own translation unit (or stop compiling
   `slhdsa-native`'s SHA-2/SHA-3 into the ML-DSA target under
   `QUDO_PQC_MATH_ONLY`), removing 29 symbols and 4 objects from the boundary —
   or accept the residue and defend it as unreachable. The decision interacts
   with freezing the `qudo-pqc-lib` subtree pin on an immutable tag: the pin is
   currently a branch head (`main` @ `73e5499`, see
   [subtree-pins.md](subtree-pins.md)), which cannot support a reproducible
   certification SHA. Sequence the split before the tag, or not at all.

3. **Boundary source manifest — NOT DONE.** `providers/fips.module.sources`,
   `providers/fips/build.info` and `providers/fips.checksum` have not been
   amended since the subtree import and contain no reference to `qudo-pqc`
   (§4.2e). The certified boundary's source enumeration does not currently list
   the code this document discloses. This must be corrected, and the correction
   will change the module's own definition of its boundary — it is not a
   documentation-only edit.

4. **A regression assertion for the *hash* counts — NOT BUILT.** The
   FIPS-infrastructure half of this invariant *is* gated.
   `ci/check-boundary-symbols.sh` derives its forbidden set from **symbol**
   names across every object in the archive (`:55` `FIPS_SYM_RE`, applied at
   `:79-80`), and fails closed if that set ever comes back empty (`:85-90`).
   Executed 2026-07-23: *"objects in archive: 84 / defined symbols: 441 /
   forbidden symbols: 26 / qudo symbols linked in: 123 / FIPS-infra violations:
   0 / BOUNDARY GATE PASSED"*, exit 0. That independently corroborates §1.4.
   What is **not** gated is this document's own numbers: nothing asserts that
   the Class A symbol count stays at 92, that Class B stays at 29 and does not
   grow, or that the qudo hash code stays absent from the digest dispatch
   tables. Adding those three assertions would make this document
   self-verifying in CI. Separately, the script's header comment at
   `ci/check-boundary-symbols.sh:28-31` still reads "CURRENT STATUS: FAILS",
   which is stale and will mislead a reviewer who reads the script; it should be
   corrected before the script is handed to the laboratory.

5. **`qudo_pqc_integrity_hmac_is_patched` — residual, currently benign.** This
   symbol is defined `T` in `qudo_pqc_utils.c.o` inside the archive and is one
   of the 26 symbols in the gate's forbidden set. It is **not** linked into
   `fips.dylib` (verified: zero matches, §1.4), so it is not part of this
   finding today — but it is FIPS-infrastructure code sitting in a
   general-utility object under `QUDO_PQC_MATH_ONLY`, one link-graph change away
   from entering the boundary. It should be split out upstream, or its exclusion
   should be asserted explicitly rather than relying on it happening not to be
   referenced.

6. **Class B correctness is unestablished and unrelied-upon.** No test in the
   tree exercises it (§3.1). If the laboratory's position is that any code in
   the boundary must be covered by a self-test, item 2 becomes mandatory rather
   than optional.

7. **Non-delegated OpenSSL PQC math also remains in the module.**
   `ossl_ml_dsa_key_public_from_private` runs OpenSSL's own ML-DSA lattice
   arithmetic on every private-key decode (`ml_dsa_key.c:363` → `:341`), and
   `fips.dylib` still exports `_ossl_ml_dsa_matrix_expand_A`,
   `_ossl_ml_dsa_matrix_mult_vector`, `_ossl_ml_dsa_poly_ntt`,
   `_ossl_ml_dsa_poly_ntt_inverse`, `_ossl_ml_dsa_vector_expand_S`. That is a
   duplicate *lattice* implementation, outside this document's scope, and needs
   its own disposition before the Security Policy is drafted.

## References

- [ADR-0003 — Epic 2 boundary dedupe closes as Story 2.1 + Story 2.5](adr/ADR-0003-epic2-boundary-dedupe-closure.md) (superseded; the origin of this disclosure)
- [ADR-0005 — Crypto-layer delegation](adr/ADR-0005-crypto-layer-delegation.md)
- [ADR-0009 — qudo-pqc-lib is consumed as a standard build, not a FIPS module](adr/ADR-0009-qudo-pqc-standard-build.md)
- [ADR-0010 — SLH-DSA is not delegated; OpenSSL's implementation is used](adr/ADR-0010-slh-dsa-stays-upstream.md)
- [design-errata.md](design-errata.md) — §6.1, §15.2 and §22.3 corrections
- [subtree-pins.md](subtree-pins.md) — the exact upstream commits measured
- [reproducibility.md](reproducibility.md) — per-OE build determinism status
