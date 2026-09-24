# QUDO PQC — Architecture & Design

> **Source of truth.** This Markdown file is the canonical source for this document. The branded Word edition is generated from it; regenerate rather than editing the Word copy.

# 1 Executive Summary

QUDO PQC (\`libqudo-pqc\`, version 1.0.0) is a production C implementation of the three NIST post-quantum standards — ML-KEM (FIPS 203, key encapsulation), ML-DSA (FIPS 204, lattice signatures) and SLH-DSA (FIPS 205, stateless hash-based signatures). It is built as a single, dependency-free shared/static library structured for FIPS 140-3 (CMVP) validation: it carries its own SP 800-90A AES-256 CTR_DRBG, HMAC-SHA-256, SHA-2 and SHA-3 primitives and links no external cryptographic library.

Architecturally the library aggregates three standalone sub-libraries — \`qudo-mlkem\`, \`qudo-mldsa\`, \`qudo-slhdsa\` — each a thin object-handle wrapper over a vendored, formally-verified upstream (\`mlkem-native\`, \`mldsa-native\`, \`slhdsa-native\`) that performs the lattice/hash mathematics. The wrapper layer adds object lifecycle, parameter-set selection, input validation, FIPS service gating, pairwise-consistency testing, DER/PEM serialization and error mapping. Around the algorithm layer sits a FIPS module core (\`src/\`, \`src/fips/\`) providing power-on self-tests, cryptographic algorithm self-tests, module-integrity verification, a CTR_DRBG with SP 800-90B health tests, an explicit finite-state machine, per-operation approval indicators and a structured audit log.

| **Attribute** | **Value (from code)** |
|----|----|
| Library / version | libqudo-pqc 1.0.0, SOVERSION 1 (CMakeLists.txt:12,273) |
| Algorithm families | ML-KEM ×3, ML-DSA ×3, SLH-DSA ×12 — 18 FIPS parameter sets |
| Standards | FIPS 203/204/205; FIPS 140-3; SP 800-90A (CTR_DRBG), SP 800-90B (health tests) |
| Primitives (in-tree) | AES-256 (AES-NI + constant-time), HMAC-SHA-256, SHA-2, SHA-3/SHAKE |
| External crypto deps | None (no OpenSSL in the module boundary) |
| APIs | Object API (heap handles, runtime selection) + Direct API (zero-alloc, caller buffers) |
| Licence | Apache-2.0 AND MIT |

Table 4 — Library at a glance.

Intended use cases: post-quantum key establishment (TLS and secure-messaging session setup), code/firmware and document signing, and any application requiring NIST-standardised, FIPS-structured PQC primitives with a defined cryptographic-module boundary. This document derives the design directly from the source; where behaviour is conditional or assumed it is stated explicitly.

# 2 System Overview

The library's responsibility is to expose correct, FIPS-aligned implementations of the three NIST PQC families behind a uniform C API, and — when built as a FIPS module — to enforce the FIPS 140-3 service discipline around every cryptographic operation. Scope is the cryptographic module itself; the OpenSSL provider that exposes these algorithms via the EVP API lives in a separate \`qudo-provider\` repository and is out of scope here.

## 2.1 Major capabilities

- ML-KEM (FIPS 203) — key generation, encapsulation, decapsulation, deterministic/derandomised variants, public/secret-key validation; sets ML-KEM-512/768/1024 (NIST category 1/3/5).

- ML-DSA (FIPS 204) — key generation, signing and verification with context strings, external-mu, prehash (HashML-DSA), concat signed-message and internal/deterministic modes; sets ML-DSA-44/65/87 (category 2/3/5).

- SLH-DSA (FIPS 205) — key generation, signing and verification with context strings, additional randomness (hedged/deterministic) and prehash; all 12 sets (SHA2/SHAKE × 128/192/256 × s/f).

- FIPS module services — POST, CAST, module integrity (HMAC-SHA-256), PCT with fail-zeroize, SP 800-90A CTR_DRBG with SP 800-90B health tests, finite-state machine, approval indicators, and a structured audit log.

- Serialization — DER/PEM import/export for public and (signature-family) private keys; liboqs interoperability.

## 2.2 Supported standards

| **Standard** | **Coverage in the implementation** |
|----|----|
| FIPS 203 (ML-KEM) | qudo-mlkem wrapper over mlkem-native; keygen/encaps/decaps + derand/seed paths |
| FIPS 204 (ML-DSA) | qudo-mldsa wrapper over mldsa-native; pure/context/extmu/prehash/internal signing |
| FIPS 205 (SLH-DSA) | qudo-slhdsa wrapper over slhdsa-native; 12 sets, ctx/addrnd/prehash |
| FIPS 140-3 | Module boundary, POST/CAST, integrity, PCT, state machine, indicators, audit |
| SP 800-90A | AES-256 CTR_DRBG (no-df), reseed interval 2²⁰ (src/fips/qudo_fips_rand.c) |
| SP 800-90B | RCT + APT continuous health tests, startup test (src/fips/qudo_fips_rand.c) |
| FIPS 198-1 / 180-4 / 202 | HMAC-SHA-256, SHA-2, SHA-3/SHAKE (src/fips/) |

Table 5 — Supported standards and where they are implemented.

# 3 Architecture Overview

## 3.1 Logical architecture

The library is layered. Applications call either the Object API (heap handles, runtime algorithm selection) or the Direct API (zero-allocation, caller-provided buffers). Both resolve to a per-family wrapper, which validates inputs, applies FIPS gating, and dispatches to the vendored \`\*-native\` mathematics. A self-contained FIPS primitive core supplies all randomness and hashing, and a set of FIPS module services wraps the whole lifecycle.

```text
+--------------------------------------------------------------------------+
|                            Application code                              |
+--------------------------------------------------------------------------+
            |  Object API (handles)            |  Direct API (buffers)
            v                                   v
+--------------------------------------------------------------------------+
|                       Public API layer  (include/)                       |
|   qudo_pqc.h  |  mlkem_wrapper.h  |  mldsa_wrapper.h  |  slhdsa_wrapper.h |
+--------------------------------------------------------------------------+
            |                 |                 |
            v                 v                 v
+----------------+   +----------------+   +----------------+
|  qudo-mlkem    |   |  qudo-mldsa    |   |  qudo-slhdsa   |   ALGORITHM
|  wrapper       |   |  wrapper       |   |  wrapper       |   LAYER
|  (FIPS 203)    |   |  (FIPS 204)    |   |  (FIPS 205)    |
+-------+--------+   +-------+--------+   +-------+--------+
        |                    |                    |
        v                    v                    v
+----------------+   +----------------+   +----------------+
|  mlkem-native  |   |  mldsa-native  |   |  slhdsa-native |   VENDORED
|  (verified)    |   |  (verified)    |   |  (verified)    |   MATH
+----------------+   +----------------+   +----------------+
        |                    |                    |
        +---------+----------+----------+---------+
                  v                     v
        +------------------------------------------------+
        |   FIPS primitive core   (src/fips/)            |   CRYPTO
        |   AES-256 (NI + CT) | CTR_DRBG | HMAC | SHA2/3 |   PRIMITIVES
        +------------------------------------------------+
+--------------------------------------------------------------------------+
|   FIPS module services (src/)                                            |
|   state machine | POST | CAST | integrity (HMAC over .text) | PCT |      |
|   approval indicators | audit log | security-level enforcement          |
+--------------------------------------------------------------------------+
```

Table 6 — Logical (layered) architecture. Arrows denote 'depends on / calls into'.

## 3.2 Runtime interaction view — module initialisation

At load, a platform constructor (\`DllMain\` on Windows, \`\_\_attribute\_\_((constructor))\` on GCC/Clang) moves the module to SELFTEST (src/qudo_pqc_init.c:43-107,135-138). The first \`qudo_pqc_init()\` then runs the gated sequence below; only on full success does the module enter RUNNING and permit cryptographic operations.

```text
App        qudo_pqc_init        integrity        POST             DRBG          state
 |   init(config)  |               |              |               |             |
 |---------------->|  verify image |              |               |             |
 |                 |-------------->| HMAC-SHA-256 over [start,end) |             |
 |                 |<--------------|  ok           |               |             |
 |                 |  primitive KATs (SHA-256, HMAC, CTR_DRBG)     |             |
 |                 |---------------------------->  |               |             |
 |                 |  seed DRBG from platform entropy              |             |
 |                 |------------------------------------------->   |  RCT/APT    |
 |                 |  algorithm KATs (ML-KEM/ML-DSA/SLH-DSA)       |             |
 |                 |---------------------------->  |               |             |
 |                 |  all pass -> SELFTEST -> RUNNING ------------------------->  |
 |<----------------|  return 1 (success)                                        |
 |                 |  (any failure -> SELFTEST -> ERROR, return 0)              |
```

Table 7 — Runtime initialisation sequence (src/qudo_pqc_init.c:497-669).

## 3.3 Dependency flow

Dependencies flow strictly downward — the algorithm layer depends on the FIPS primitive core for randomness and hashing, and the FIPS module services observe and gate the algorithm layer. The vendored \`\*-native\` trees are leaf dependencies pinned in-tree; no module component depends on an external crypto library. The full graph is in Appendix D.

# 4 Repository Structure

The repository is a CMake super-project: a parent \`libqudo-pqc\` plus three sub-library sub-projects, each carrying a vendored upstream. The table maps each significant directory to its responsibility and key components.

| **Directory** | **Purpose** | **Key components** |
|----|----|----|
| src/ | FIPS module lifecycle & services | qudo_pqc_init/post/pct/integrity/security/indicator/audit/platform/utils.c, self_test_data.inc, boundary\_\*.c, embedded_hmac.c |
| src/fips/ | Self-contained FIPS primitives | qudo_fips_rand.c (entropy+DRBG front end), qudo_fips_ctrdrbg.c, qudo_fips_aes{,\_ct,\_ni}.c, qudo_fips_hmac.c, sha2/sha3 headers |
| include/ | Public module headers | qudo_pqc.h, qudo_pqc_err.h, qudo_pqc_indicator.h, qudo_pqc_audit.h, qudo_pqc_platform.h, build_config.h.in |
| qudo-mlkem/ | ML-KEM sub-library (FIPS 203) | include/ (wrapper API), src/ (wrapper, error, memory, interop), mlkem-native/ (vendored), examples/, tests/, fuzz/, config/ |
| qudo-mldsa/ | ML-DSA sub-library (FIPS 204) | same layout; mldsa-native/; adds mldsa_shake256.c |
| qudo-slhdsa/ | SLH-DSA sub-library (FIPS 205) | same layout; slhdsa-native/; adds randombytes.c |
| tests/ | Module-level test suite | 32 C files → 116 CTest cases (POST, PCT, integrity, DRBG, CAST, indicator, audit, interop, threading, fuzz replay) |
| acvp/ | NIST ACVP validation harness | acvp\_{mlkem,mldsa,slhdsa}.c runners, python clients, run_acvp.sh, .acvp-data/v1.1.0.41 vectors |
| tools/ | Build-time utilities | qudo_fipsinstall.c (integrity HMAC embed/cnf), gen_slhdsa_kat.c |
| cmake/ | FIPS build machinery | fips_module.ld (linker script), fips_partial_link.cmake, check_fips_boundary.sh, toolchains/ (5 cross files) |
| config/ | Symbol-visibility map | libqudo-pqc.map (ELF version script — public-symbol allow-list) |
| scripts/ · .github/ | Hooks & CI | install-hooks.sh, test_standalone.sh; 11 CI workflows |

Table 8 — Repository directory responsibilities.

Each sub-library follows an identical internal layout — \`include/\` (wrapper API + types/config), \`src/\` (wrapper, error/TLS, memory, interop), \`\*-native/\` (the vendored verified implementation, with a \`qudo_runtime_dispatch/\` extension for CPU detection), plus \`examples/\`, \`tests/\`, \`fuzz/\`, and \`config/\` (per-library symbol map).

# 5 Component Design

This section describes each architectural component by responsibility, interface, dependencies, inputs and outputs.

## 5.1 API Layer

**Location:** include/\*.h + the three wrapper headers

**Responsibility:** Public surface: module lifecycle/state (qudo_pqc.h), and the per-family Object/Direct APIs.

**Inputs / outputs:** Inputs: caller buffers, algorithm names/levels, config struct. Outputs: status codes, keys, ciphertexts, signatures.

**Dependencies:** Depends on: algorithm layer + FIPS services. No internal crypto exposed (symbols hidden by the version map).

## 5.2 Algorithm Layer

**Location:** qudo-{mlkem,mldsa,slhdsa}/src/\*\_wrapper.c

**Responsibility:** Object handles + vtables, parameter-set selection, input validation, FIPS gating, PCT on keygen, error mapping, DER/PEM.

**Inputs / outputs:** Inputs: validated API calls. Outputs: native dispatch + status.

**Dependencies:** Depends on: \*-native (math), FIPS primitive core (RNG), FIPS services (gating, PCT, indicators).

## 5.3 Crypto Primitives

**Location:** src/fips/

**Responsibility:** AES-256 (runtime AES-NI vs constant-time), SP 800-90A CTR_DRBG, HMAC-SHA-256, SHA-2/SHA-3 access.

**Inputs / outputs:** Inputs: seeds, keys, data. Outputs: random bytes, MACs, digests, ciphertext blocks.

**Dependencies:** Depends on: platform entropy (qudo_fips_rand.c) and sha2/sha3 from the vendored trees.

## 5.4 Validation Layer

**Location:** src/qudo_pqc_post.c, qudo_pqc_pct.c, qudo_pqc_integrity.c, qudo_self_test_data.inc

**Responsibility:** POST KATs, per-keypair PCT, module-integrity HMAC, CAST orchestration; all comparisons constant-time.

**Inputs / outputs:** Inputs: frozen KAT vectors, generated keypairs, the module image. Outputs: pass/fail driving state.

**Dependencies:** Depends on: primitive core + algorithm layer + state machine + audit.

## 5.5 Utility Layer

**Location:** src/qudo_pqc_platform.c, qudo_pqc_utils.c

**Responsibility:** OS abstraction (rwlocks, atomics, once), allocator, secure zeroize (qudo_cleanse), constant-time compare, public RNG/HMAC/CTR_DRBG wrappers.

**Inputs / outputs:** Inputs/outputs: memory buffers, locks.

**Dependencies:** Depends on: OS APIs; src/fips back-end.

## 5.6 Configuration Layer

**Location:** qudo_pqc_config_t, qudofipsmodule.cnf, build_config.h.in

**Responsibility:** Runtime dependency-injection (callbacks, pluggable I/O, integrity path/MAC, conditional-errors flag) and build-time FIPS feature exposure.

**Inputs / outputs:** Inputs: config struct / cnf file. Outputs: configured module behaviour.

**Dependencies:** Depends on: init path (src/qudo_pqc_init.c).

## 5.7 Test Framework

**Location:** tests/, acvp/, qudo-\*/tests/

**Responsibility:** 116 CTest cases + ACVP runners + per-sub-library suites; flag-multiplexed binaries; corpus fuzz replay.

**Inputs / outputs:** Inputs: KAT/CAVP/ACVP vectors, generated inputs. Outputs: pass/fail.

**Dependencies:** Depends on: the public + internal APIs under test.

# 6 Public API Design

The library exposes a module-control API plus three per-family algorithm APIs (each in an Object and a Direct form). Return conventions differ deliberately: module lifecycle/query functions use \`1 = success / 0 = failure\` (or boolean), while the per-family crypto functions return a signed status enum (\`0 = SUCCESS\`, negatives are errors). The tables below catalogue the principal entry points; the full enumeration is in Appendix B.

## 6.1 Module control & lifecycle (include/qudo_pqc.h)

| **Function** | **Purpose** | **Returns** |
|----|----|----|
| qudo_pqc_init(config) | Integrity check + POST + transition to RUNNING | 1/0 |
| qudo_pqc_self_test() | On-demand re-run of self-tests | 1/0 |
| qudo_pqc_run_all_casts() | Run all 12 CASTs | 1/0 |
| qudo_pqc_fini() | Teardown; reset state to INIT | void |
| qudo_pqc_is_running() / \_get_state() | Lifecycle queries | 1/0 ; state 0–3 |
| qudo_pqc_is_fips() / \_is_fips_approved(name) | FIPS-mode / approved-algorithm queries | 1/0 |
| qudo_pqc_get_security_level(name) | NIST category for an algorithm | level |
| qudo_pqc_set_min_security_level / \_get_min_security_level | Enforce / read minimum category | void / level |
| qudo_pqc_get_cast_status(id) / \_post\_\*\_passed() | Granular self-test status | state / 1/0 |
| qudo_pqc_rand_bytes(out,len) | DRBG output | 1/0 |
| qudo_pqc_cleanse(ptr,len) | Secure zeroize | void |
| qudo_pqc_version() | Version string ("1.0.0") | const char\* |

Table 9 — Core module-control API (representative).

## 6.2 ML-KEM API (mlkem_wrapper.h)

| **Function** | **Purpose** |
|----|----|
| QUDO_KEM_new(name) / \_new_by_level(level) / \_free | Construct/destroy a handle |
| QUDO_KEM_keypair(kem,pk,sk) | Generate keypair (PCT in FIPS) |
| QUDO_KEM_keypair_derand / \_keypair_from_seed | Derandomised / seed-deterministic keygen |
| QUDO_KEM_encaps(kem,ct,ss,pk) / \_encaps_derand | Encapsulate (random / caller randomness) |
| QUDO_KEM_decaps(kem,ss,ct,sk) | Decapsulate (implicit rejection inside native) |
| QUDO_KEM_check_pk / \_check_sk | Key validation |
| QUDO_KEM_keypair_generate/encapsulate/decapsulate(level,…) | Direct API (no handle, no PCT/IND) |
| QUDO_KEM_get\_\*\_size(level), \_get_error_string, DER/PEM | Sizes, diagnostics, serialization |

Table 10 — ML-KEM API (Object + Direct).

## 6.3 ML-DSA API (mldsa_wrapper.h)

| **Function** | **Purpose** |
|----|----|
| QUDO_MLDSA_new(alg) / \_free | Construct/destroy a handle |
| QUDO_MLDSA_keypair(sig,pk,sk) | Generate keypair (PCT in FIPS) |
| QUDO_MLDSA_sign / \_sign_with_context | Hedged signing (optional context ≤255 B) |
| QUDO_MLDSA_sign_internal | Raw sign with explicit rnd/prefix/external-mu (determinism knob) |
| QUDO_MLDSA_sign_extmu / \_sign_concat / \_open | External-mu signing; signed-message blob; recover |
| QUDO_MLDSA_sign_pre_hash / \_verify_pre_hash | HashML-DSA (prehash) signing/verification |
| QUDO_MLDSA_verify / \_verify_with_context | Verification |
| QUDO_MLDSA_get\_\*\_bytes, \_get_security_level, DER/PEM | Sizes, metadata, serialization |

Table 11 — ML-DSA API (selected; full list in Appendix B).

## 6.4 SLH-DSA API (slhdsa_wrapper.h)

| **Function** | **Purpose** |
|----|----|
| QUDO_SLHDSA_new(alg) / \_free | Construct/destroy a handle (12 sets) |
| QUDO_SLHDSA_keypair / \_keypair_internal | Keygen (random / 3-seed deterministic); PCT in FIPS |
| QUDO_SLHDSA_sign / \_sign_with_ctx_str | Hedged signing (optional context) |
| QUDO_SLHDSA_sign_ex(…,addrnd,…) | Full control of context + additional randomness (det/hedged) |
| QUDO_SLHDSA_sign_internal / \_sign_pre_hash | Raw internal sign; HashSLH-DSA |
| QUDO_SLHDSA_verify / \_verify_with_ctx_str / \_verify_pre_hash | Verification variants |
| QUDO_SLHDSA\_\<SET\>\_keypair/sign/verify | Per-parameter-set direct functions (12 sets ×3) |

Table 12 — SLH-DSA API (selected; full list in Appendix B).

```text
QUDO_KEM *kem = QUDO_KEM_new("ML-KEM-768");
uint8_t pk[1184], sk[2400], ct[1088], ss_a[32], ss_b[32];
QUDO_KEM_keypair(kem, pk, sk);          /* 0 == SUCCESS; PCT runs in FIPS build */
QUDO_KEM_encaps(kem, ct, ss_a, pk);
QUDO_KEM_decaps(kem, ss_b, ct, sk);     /* ss_a == ss_b */
QUDO_KEM_free(kem);
```

# 7 ML-KEM Design (FIPS 203)

ML-KEM is implemented by \`qudo-mlkem/src/mlkem_wrapper.c\` over \`mlkem-native\`. A \`QUDO_KEM\` handle (mlkem_wrapper.h:57-89) carries the algorithm name, security level, the four artifact sizes, a vtable (\`keypair\`/\`encaps\`/\`decaps\`) wired from one of three static templates, and — in FIPS builds — a \`fips_ind\` indicator. \`QUDO_KEM_new\` gates the module, selects the template by name/level, and copies it into a \`calloc\`-ed handle.

## 7.1 Entry points & internal flow

- Key generation — \`QUDO_KEM_keypair\` → FIPS gate → indicator check → vtable \`keypair\` → \`qudo_mlkem_keypair\_{512,768,1024}\` → native \`mlkem\*\_keypair\` (runtime-dispatched to AVX2/NEON/ref). In FIPS builds, a PCT (encaps→decaps→constant-time compare) runs unless self-testing; on PCT failure the wrapper zeroizes pk+sk and forces ERROR.

- Encapsulation — \`QUDO_KEM_encaps\` → gate → indicator → vtable \`encaps\` → native \`mlkem\*\_enc\` (or \`\*\_enc_derand\` with caller randomness\[32\]).

- Decapsulation — \`QUDO_KEM_decaps\` → gate → indicator → vtable \`decaps\` → native \`mlkem\*\_dec\`. Implicit rejection is internal to the native routine (FIPS 203 returns a pseudo-random shared secret on an invalid ciphertext rather than an error).

## 7.2 Sequence diagrams

```text
KEY GENERATION
App → QUDO_KEM_keypair → [FIPS gate] → [indicator] → vtable.keypair
        → qudo_mlkem_keypair_N → dispatch → mlkem{avx2|neon|ref}N_keypair
        → (FIPS) PCT: encaps_derand → decaps → ct-compare(ss)
        → SUCCESS  (PCT fail ⇒ zeroize pk,sk ⇒ ERROR)

ENCAPSULATION
App → QUDO_KEM_encaps → [gate] → [indicator] → vtable.encaps
        → qudo_mlkem_encaps_N → mlkemN_enc → (ciphertext, shared_secret)

DECAPSULATION
App → QUDO_KEM_decaps → [gate] → [indicator] → vtable.decaps
        → qudo_mlkem_decaps_N → mlkemN_dec → shared_secret
        (invalid ct ⇒ pseudo-random ss, no error: implicit rejection)
```

Table 13 — ML-KEM keygen / encaps / decaps flows (mlkem_wrapper.c:892-1070).

Data structures: the opaque \`QUDO_KEM\` handle and three static templates (\`mlkem\_{512,768,1024}\_kem\`). Error handling maps native \`0/!=0\` to \`QUDO_KEM_SUCCESS\`/\`QUDO_KEM_ERROR_CRYPTO\`; argument faults map to \`INVALID_ARG\`/\`NULL_PTR\`/\`BUFFER_TOO_SMALL\`; \`check_pk/sk\` faults map to \`ERROR_VERIFY\`. The Direct API (\`QUDO_KEM_keypair_generate/encapsulate/decapsulate\`) calls the same per-level statics but performs no PCT and no indicator check.

# 8 ML-DSA Design (FIPS 204)

ML-DSA is implemented by \`qudo-mldsa/src/mldsa_wrapper.c\` over \`mldsa-native\`. The \`QUDO_MLDSA\` handle (mldsa_wrapper.h:38-76) carries metadata, the three sizes, a five-entry vtable (\`keypair\`,\`sign\`,\`sign_with_ctx_str\`,\`verify\`,\`verify_with_ctx_str\`) and a \`config\` pointer. Unlike ML-KEM, the backend is chosen once at \`QUDO_MLDSA_init()\` by storing global \`\*\_impl\` function pointers selected via \`mld_cpu_has_extension(AVX2/NEON)\`.

## 8.1 Flows

- Key generation — \`QUDO_MLDSA_keypair\` → gate/indicator → \`QUDO_MLDSA_ML_DSA\_{44,65,87}\_keypair\` → native \`mldsa\*\_keypair\`; FIPS PCT uses \`sign_internal\` with all-zero \`rnd\` and prefix \`"\x00\x00"\` then \`verify_internal\` (deterministic round trip).

- Signing — \`QUDO_MLDSA_sign\` delegates to \`QUDO_MLDSA_sign_with_context(…,NULL,0,…)\`, which validates, caps the context length, supports a size-query mode (signature==NULL), then calls native \`mldsa\*\_signature\`. This is hedged signing: native draws \`rnd\` internally. \`sign_internal\` exposes the explicit \`rnd\` (the determinism knob); \`sign_extmu\`, \`sign_concat\`/\`open\`, and \`sign_pre_hash\` map to the corresponding native entry points.

- Verification — \`QUDO_MLDSA_verify\` checks \`signature_len == length_signature\` then \`verify_with_context\` → native \`mldsa\*\_verify\`; \`0 → SUCCESS\`, else \`ERROR_VERIFY\`.

```text
KEY GENERATION
App → QUDO_MLDSA_keypair → [gate][ind] → ML_DSA_N_keypair → mldsaN_keypair
        → (FIPS) PCT: sign_internal(rnd=0) → verify_internal → SUCCESS

SIGNING (hedged)
App → QUDO_MLDSA_sign → QUDO_MLDSA_sign_with_context(NULL,0)
        → vtable.sign_with_ctx_str → mldsaN_sign_impl → mldsaN_signature
        (native draws rnd ⇒ hedged; deterministic via sign_internal rnd=0)

VERIFICATION
App → QUDO_MLDSA_verify → (len check) → verify_with_context
        → vtable.verify_with_ctx_str → mldsaN_verify → SUCCESS/ERROR_VERIFY
```

Table 14 — ML-DSA keygen / sign / verify flows (mldsa_wrapper.c:666-822).

# 9 SLH-DSA Design (FIPS 205)

SLH-DSA is implemented by \`qudo-slhdsa/src/slhdsa_wrapper.c\` over \`slhdsa-native\`. The \`QUDO_SLHDSA\` handle (slhdsa_wrapper.h:28-65) adds a \`param_set\` field. There is no SIMD dispatch table; instead each of the 12 parameter sets maps (via \`get_native_param\`) to a \`slh_param_t\*\` constant. The 12 vtable function-sets are generated by an X-macro (\`DEFINE_VARIANT_WRAPPERS\`) and wired in by \`POPULATE_SLHDSA\` at \`QUDO_SLHDSA_new\`.

## 9.1 Parameter-set handling

Selection is by name → \`QUDO_SLHDSA_parameter_set_t\` → native \`slh_param_t\*\`. The fast ('f') vs small ('s') distinction is purely a parameter choice (different FORS/hypertree dimensions and signature sizes); both use the same \`slh_keygen\`/\`slh_sign\`/\`slh_verify\` code path. Per-set direct functions (\`QUDO_SLHDSA\_\<SET\>\_keypair/sign/verify\`) funnel into the same \`internal\_\*\` routines.

## 9.2 Flows

- Key generation — \`QUDO_SLHDSA_keypair\` → gate/indicator → \`internal_keypair(ps,…)\` → native \`slh_keygen(sk,pk,rbg,prm)\`; FIPS PCT signs and verifies a 16-byte message.

- Signing — \`QUDO_SLHDSA_sign\` → \`internal_sign(ps,…)\` draws \`addrnd\` then calls native \`slh_sign(…,addrnd,prm)\` (hedged by default). \`sign_ex\` exposes caller context + \`addrnd\` (deterministic when \`addrnd\` is fixed/zero); \`sign_internal\` and \`sign_pre_hash\` map to \`slh_sign_internal\` / \`hash_slh_sign\`.

- Verification — \`QUDO_SLHDSA_verify\` → \`internal_verify(ps,…)\` → native \`slh_verify(…,prm)\` (returns 1 = valid → SUCCESS, else ERROR_VERIFY).

```text
KEY GENERATION
App → QUDO_SLHDSA_keypair → [gate][ind] → internal_keypair → get_native_param
        → slh_keygen(sk,pk,rbg,prm) → (FIPS) PCT: sign → verify → SUCCESS

SIGNING (hedged)
App → QUDO_SLHDSA_sign → internal_sign → draw addrnd
        → slh_sign(msg,ctx="",sk,addrnd,prm) → signature
        (deterministic: sign_ex with caller-supplied/zero addrnd)

VERIFICATION
App → QUDO_SLHDSA_verify → internal_verify → slh_verify(…,prm)
        → 1=valid ⇒ SUCCESS  /  else ERROR_VERIFY
```

Table 15 — SLH-DSA keygen / sign / verify flows (slhdsa_wrapper.c:415-690).

# 10 Data Structures

The principal structures are the three opaque algorithm handles, the module configuration struct, the indicator, and the status/identifier enums. Handles are metadata + a function-pointer vtable; in FIPS builds each gains a \`fips_ind\` field (so the struct layout differs between FIPS and non-FIPS builds — documented here for both).

## 10.1 Algorithm handles

| **Handle** | **Key fields (from \*\_wrapper.h / \*\_types.h)** |
|----|----|
| QUDO_KEM | algorithm_name; security_level; claimed_nist_level; ind_cca; length\_{public_key,secret_key,ciphertext,shared_secret}; vtable {keypair,encaps,decaps}; (FIPS) fips_ind |
| QUDO_MLDSA | method_name; alg_version; claimed_nist_level; security_bits; euf_cma/suf_cma/sig_with_ctx_support; length\_{public_key,secret_key,signature}; vtable {keypair,sign,sign_with_ctx_str,verify,verify_with_ctx_str}; ctx; config; (FIPS) fips_ind |
| QUDO_SLHDSA | as ML-DSA (minus ctx/config) plus param_set (QUDO_SLHDSA_parameter_set_t); euf_cma=true, suf_cma=false; (FIPS) fips_ind |

Table 16 — Opaque algorithm handles.

## 10.2 Configuration & control structures

| **Structure / enum** | **Fields / values** |
|----|----|
| qudo_pqc_config_t | self_test_cb(+arg); error_cb(+arg); io_open/io_read/io_close (pluggable I/O); module_path; module_checksum_hex; conditional_errors; audit_cb(+arg) |
| module state | QUDO_PQC_STATE_INIT=0, SELFTEST=1, RUNNING=2, ERROR=3 (single atomic g_module_state) |
| qudo_cast_id_t | ML-KEM 512/768/1024=0..2; ML-DSA 44/65/87=3..5; SLH-DSA SHA2 128/192/256=6..8; SHAKE 128/192/256=9..11; COUNT=12 |
| CAST state | INIT=0, PROCESSING=1, SUCCESS=2, FAILURE=3 |
| qudo_fips_ind_t | int approved; int strict; (approved set 0 by set_unapproved; strict gates whether an unapproved op is allowed) |
| audit | components STATE/POST/INTEGRITY/PCT/CAST/DRBG/KEY/INDICATOR; severities INFO/WARN/ERROR/FATAL; rate-limit 100/severity |

Table 17 — Configuration, state and control structures.

## 10.3 Status enums (per family)

| **Value** | **ML-KEM** | **ML-DSA / SLH-DSA** |
|----|----|----|
| 0 | SUCCESS | SUCCESS |
| -1 … -10 | ERROR, INVALID_ARG, NULL_PTR, ALLOC, RNG, VERIFY, DECODE, NOT_IMPL, CRYPTO, FILE_IO | same |
| -11 | (reserved / skipped) | (reserved / skipped) |
| -12, -13 | ENCODE, BUFFER_TOO_SMALL | ENCODE, BUFFER_TOO_SMALL |
| -14 | — (not defined for KEM) | INVALID_SIGNATURE |

Table 18 — Per-family status enums (mlkem/mldsa/slhdsa_types.h). Note −11 is intentionally unused.

# 11 Memory Management

Allocation is two-tier. The Object API uses heap handles (\`\*\_new\`/\`\*\_free\`, \`malloc\`+zero on create, zeroize+free on destroy) and \`qudo_malloc\`-ed variable-length scratch (ciphertext/signature buffers sized from the handle's length fields). The Direct API is zero-allocation: the caller provides every output buffer and the wrappers allocate no key material. Sub-libraries also offer a secure-allocator family (\`\*\_secure_alloc\` = malloc+zero+\`mlock\`/\`VirtualLock\`).

## 11.1 Secure zeroization

The central primitive is \`qudo_secure_clear\` (src/fips/qudo_fips_aes.h:18-37), selected by preprocessor in this order, so a no-op is never silently chosen:

```text
qudo_secure_clear(ptr, len):
   #if Windows          -> SecureZeroMemory(ptr, len)
   #elif __STDC_LIB_EXT1__ -> memset_s(ptr, len, 0, len)
   #elif OpenBSD || glibc>=2.25 -> explicit_bzero(ptr, len)
   #else                -> volatile fn-ptr memset + __asm__ memory barrier
```

Table 19 — Zeroization cascade (defeats dead-store elimination).

The public wrapper is \`qudo_pqc_cleanse\` (NULL/0-guarded). Around fifteen source files invoke a zeroization primitive. Security-sensitive call sites include: DRBG context and per-block temporaries (qudo_fips_ctrdrbg.c); HMAC key/pad/context (qudo_fips_hmac.c); integrity computed buffers (qudo_pqc_integrity.c); every POST KAT output; PCT secret/out/entropy/rnd buffers (qudo_pqc_pct.c); the platform entropy seed (qudo_fips_rand.c); CAST secret keys (cleansed before free); and the PCT-failure path (both keys zeroized). Sensitive fixed-size material lives on the stack and is cleansed before return; only variable-length scratch is heap-allocated. \`\*\_is_zeroized\` predicates allow tests to confirm zeroization.

# 12 Error-Handling Framework

Errors are represented at two levels. Internally, a category-coded registry \`qudo_err_t\` (include/qudo_pqc_err.h) groups codes by subsystem (0x01 state, 0x02 POST/integrity, 0x03 PCT, 0x04 CAST, 0x05 DRBG, 0x06 key, 0x0F generic). Externally, the per-family crypto APIs return signed status enums; module lifecycle/query functions return 1/0 or boolean. All hard failures funnel through \`qudo_pqc_set_error_state\`, which latches the module into ERROR.

| **Category (base)** | **Representative codes** |
|----|----|
| State (0x01) | STATE_INVALID, MODULE_NOT_RUNNING, MODULE_ERROR_STATE, INIT_LOCK/ONCE_FAIL |
| POST/integrity (0x02) | POST_INTEGRITY_FAIL, POST_KAT_FAIL, POST\_{KEYGEN,KEM,SIG}\_KAT_FAIL, INTEGRITY_HMAC_FAIL/MISMATCH/NO_CONFIG |
| PCT (0x03) | PCT_KEYGEN/IMPORT/ENCAPS/SIGN_FAIL |
| CAST (0x04) | CAST_NOT_RUN/FAIL/KEYGEN_FAIL |
| DRBG (0x05) | DRBG_NOT_SEEDED/SEED_FAIL/HEALTH_FAIL/RESEED_FAIL/GENERATE_FAIL |
| Key (0x06) | KEY_INVALID/SIZE_MISMATCH/ZEROIZE_FAIL |
| Generic (0x0F) | ALLOC, PARAM_INVALID, PARAM_NULL, NOT_SUPPORTED, INTERNAL |

Table 20 — Internal error registry (qudo_pqc_err.h).

## 12.1 Failure behaviour & flow

```text
            operation fault
                  |
        +---------+-----------+
        |                     |
  hard failure          conditional PCT failure
  (POST/integrity/       (keygen PCT)
   DRBG health)               |
        |              conditional_errors?
        v                 yes |        no
  set_error_state          set_error      WARN only
        |                  (ERROR)         (no state change)
        v                     |
  state := ERROR (latched) <--+
        |
   is_running()==0  ⇒ all crypto ops rejected at the gate
   (escape only via qudo_pqc_fini() → INIT)
```

Table 21 — Error/failure flow. Import-PCT failures are transient (WARN); the ERROR state is otherwise terminal until fini().

Validation is uniform: every entry point performs NULL and length/bounds checks; the CTR_DRBG rejects requests above 65 536 bytes; FIPS builds additionally gate on module state, DRBG readiness and minimum security level before any operation. On PCT failure the keys are zeroized (fail-zeroize), verified by \`tests/test_pct_fail_zeroize.c\`.

# 13 Security Design

## 13.1 Security boundaries

The cryptographic-module boundary is the contiguous \`.text\` region between the linker-pinned sentinels \`qudo_fips_module_start\` and \`qudo_fips_module_end\`. A supplemental linker script (cmake/fips_module.ld) places the start object before and the end object after \`.text\`; \`-fno-function-sections\`/\`-fno-data-sections\` keep all functions in one section; a POST-build check (check_fips_boundary.sh) asserts every FIPS symbol lies inside the region. The public symbol set is further constrained by an ELF version script (config/libqudo-pqc.map) that exports only the \`QUDO\_\*\`/\`qudo_pqc\_\*\` API and hides all \`qudo_fips\_\*\`/AES/DRBG/HMAC internals.

## 13.2 Self-test discipline & key lifecycle

- Integrity — HMAC-SHA-256 (fixed key, qudo_fipskey.h) over the boundary, preceded by an HMAC KAT; embedded HMAC (Linux/macOS) or external cnf (Windows); an un-patched module fails closed via the \`"QUDO_FIPS_NOT_PATCHED\_\_\_"\` sentinel.

- POST — primitive KATs (SHA-256, HMAC, CTR_DRBG) then per-family algorithm KATs, each wrapped in begin/corrupt/end hooks so the negative path is provably exercised; all comparisons use the constant-time \`qudo_memcmp_ct\`.

- PCT with fail-zeroize — every generated/imported keypair is round-tripped before use; on failure both keys are zeroized and the module errors.

- Key lifecycle — keys are caller-owned buffers (Direct) or transient scratch (Object/CAST); secret material is cleansed at every free and on every failure path; the secure-allocator path additionally \`mlock\`s pages against swap.

## 13.3 Randomness & constant-time

All randomness derives from the internal AES-256 CTR_DRBG seeded from platform entropy (\`BCryptGenRandom\` / \`getentropy\` / \`getrandom\` / \`/dev/urandom\`), with SP 800-90B RCT/APT continuous health tests and a 1 024-sample startup test; a health-test failure latches ERROR. AES defaults to a constant-time implementation (the T-table variant is compiled out unless explicitly enabled), and all KAT/PCT/integrity comparisons are constant-time. Security-critical paths: integrity→KAT chain; DRBG health→ERROR latch; keygen→PCT→zeroize; and the per-operation wrapper gate (running + DRBG-ready + approved + min-security-level).

# 14 FIPS Mapping

The following traceability matrix maps implementation components to the relevant standards and the functions that realise them. This describes implementation alignment only; no certification is claimed.

| **Component** | **Algorithm / service** | **Standard** | **Relevant functions / files** |
|----|----|----|----|
| KEM keygen/encaps/decaps | ML-KEM | FIPS 203 | QUDO_KEM_keypair/encaps/decaps → mlkem\*\_keypair/enc/dec |
| Signature keygen/sign/verify | ML-DSA | FIPS 204 | QUDO_MLDSA_keypair/sign/verify → mldsa\*\_keypair/signature/verify |
| Signature keygen/sign/verify | SLH-DSA | FIPS 205 | QUDO_SLHDSA_keypair/sign/verify → slh_keygen/sign/verify |
| Power-on self-tests | POST KATs | FIPS 140-3 §9; IG 10.3.A | qudo_pqc_run_post\_\* (qudo_pqc_post.c) + self_test_data.inc |
| Algorithm self-tests | CAST ×12 | IG 10.3.A | run_cast_mlkem/mldsa/slhdsa (qudo_pqc_init.c) |
| Pairwise consistency | PCT + zeroize | IG 10.3.A; §7 | qudo_pqc\_{mlkem,mldsa,slhdsa}\_pct (qudo_pqc_pct.c) |
| Module integrity | HMAC-SHA-256 | FIPS 140-3 §9 | qudo_pqc_verify_integrity\[\_embedded\] (qudo_pqc_integrity.c) |
| DRBG | AES-256 CTR_DRBG | SP 800-90A | qudo_fips_ctrdrbg.c, qudo_fips_rand.c |
| Entropy health | RCT/APT/startup | SP 800-90B | rct_test/apt_test/startup_health_test (qudo_fips_rand.c) |
| State model | INIT→SELFTEST→RUNNING/ERROR | FIPS 140-3 §4 | g_module_state, set_error_state (qudo_pqc_init.c) |
| Approved-mode indicator | qudo_fips_ind_t | IG 2.4.C | qudo_fips_ind_check_operation (qudo_pqc_indicator.c) |
| Zeroisation | secure clear | FIPS 140-3 §7 | qudo_secure_clear / qudo_cleanse |

Table 22 — FIPS traceability matrix (implementation alignment; not a certification claim).

# 15 Testing Architecture

Testing spans three tiers: a module-level CTest suite (32 source files → 116 cases), per-sub-library suites, and a NIST ACVP harness. Most test binaries are multiplexed by CLI flags (e.g. \`test_pqc_post --kats\|--negative\|--selftest\|--statemachine\`) and exercise the same code paths used at init.

| **Category** | **Representative tests** | **What it validates** |
|----|----|----|
| POST / state machine | test_pqc_post, test_pqc_init_errors | Self-test sequence, KATs, negative corruption, transitions |
| PCT | test_pqc_pct, test_pct_fail_zeroize, test_pct_concurrent_tsan | Per-family PCT; key zeroize on failure; race-freedom (TSan) |
| Integrity | test_pqc_integrity, test_integrity_path_pinning | HMAC KAT, synthetic/real image, fipsinstall, path pinning |
| DRBG | test_pqc_drbg(\_limits), test_fips_ctrdrbg_cavp | CTR_DRBG KAT, reseed/chunking/health, SP 800-90A CAVP vectors |
| Indicator/CAST/audit | test_pqc_indicator/cast/audit | Approval gating, 12 CASTs, audit severity/rate/strings |
| Algorithm / wrapper | test_pqc_newapi, \*\_seed/\_config/\_bounds/\_paramsets | Object/Direct API, seed determinism, bounds, all sets |
| Interop / threading | test_pqc_interop, test_pqc_threading | DER/PEM round trips, concurrency |
| Fuzz replay | fuzz_corpus_replay.c | Deterministic corpus replay into LLVMFuzzerTestOneInput |

Table 23 — Module test categories.

KAT mechanism — POST uses frozen deterministic vectors compiled into \`qudo_self_test_data.inc\` (public keys, ciphertexts, signatures from fixed non-secret seeds) to catch build/optimizer drift; each KAT is wrapped in begin/corrupt/end so corruption detection is itself tested. ACVP — native runners (\`acvp\_{mlkem,mldsa,slhdsa}.c\`) driven by Python clients against cached NIST vectors (v1.1.0.41) via \`run_acvp.sh\`. Sub-library suites add KAT, constant-time (dudect-style), secure-memory, DER/PEM and liboqs-compat tests; \`run_all_tests.sh\` orchestrates standard/FIPS/ASan/valgrind/clang-tidy passes.

# 16 Performance Considerations

Performance rests on three strategies derived from the code:

- Runtime CPU dispatch — \`mlkem-native\` and \`mldsa-native\` build ref + AVX2 + NEON variants into separate namespaces and select at runtime via cached CPU-feature detection (\`qudo_runtime_dispatch/cpu_features.c\`; AVX2 gated on XSAVE/OSXSAVE/YMM). SLH-DSA is not SIMD-dispatched (portable reference selected by parameter pointer; build-time SHA-NI only).

- Hand-tuned assembly — AVX2 (x86-64) and NEON (aarch64) Keccak and lattice kernels in the vendored trees, compiled per-arch with matching \`-march\` flags.

- One-shot dispatch caching — CPU features and the internal AES backend (AES-NI vs constant-time) are detected once and cached; per-operation overhead is a predicted branch.

Performance-sensitive paths are keygen/encaps/decaps and keygen/sign/verify (wrapper → namespaced native), CTR_DRBG generate (AES-256; auto-reseed every 2²⁰ generations; 65 536-byte request cap), and Keccak/SHA-3. POST/PCT are one-time initialisation costs. Indicative measurements (Apple M4, Release) recorded in the companion test reports: ML-KEM ≈ 0.28 M keypairs/s (ML-KEM-512); ML-DSA keygen sub-100 µs; SLH-DSA signing 16–630 ms depending on set, verification \< 2 ms.

# 17 Build & Deployment Architecture

The build is a CMake super-project (≥ 3.15). The three sub-libraries compile as CMake OBJECT libraries (ref/AVX2/NEON variants) and are merged directly into a single \`libqudo-pqc\`, so all crypto code lands in one image. The FIPS boundary is enforced at link time by source ordering (\`boundary_start\` first, \`boundary_end\` last) plus the supplemental linker script.

| **Option** | **Default** | **Purpose** |
|----|----|----|
| QUDO_FIPS_MODULE | OFF | Build as FIPS 140-3 module (POST/KATs/integrity/state machine) |
| QUDO_PQC_BUILD_TESTS | ON | Build the library test suite |
| BUILD_ACVP | OFF | Build ACVP runner binaries |
| QUDO_FUZZ_REPLAY | OFF | Build fuzz-corpus replay tests |
| QUDO_BUILD_EMBEDDED_OBJ | OFF | Static archive + partial-link object (FIPS only) |
| ENABLE_SANITIZERS / MSAN / TSAN | OFF | ASan+UBSan / MemorySanitizer / ThreadSanitizer |
| ENABLE_COVERAGE | OFF | gcov/lcov instrumentation |
| BUILD_SHARED_LIBS | ON | Shared vs static library |

Table 24 — Top-level build options (CMakeLists.txt:22-30,258).

Dependencies & toolchain — C99, GCC 10+/Clang 14+/Apple Clang 14+/MSVC 2019+; no external crypto library; platform libs \`bcrypt\` (Windows), \`pthread\`/\`dl\` (Unix). Hardening: full RELRO, non-executable stack, the symbol version-script, and (FIPS) the no-function-sections/no-LTO constraints. Packaging — \`build.sh\`/\`build_windows.bat\` drive per-mode build dirs and the FIPS finalisation (strip → \`qudo_fipsinstall -embed\` to patch the in-binary HMAC → re-codesign on macOS; external \`qudofipsmodule.cnf\` on Windows). Install lays out lib/, include/qudo-pqc/ and a CMake package (\`qudo::qudo-pqc\`); a CycloneDX SBOM (sbom.cdx.json) accompanies the release.

# 18 Known Limitations

Derived from the code and build configuration:

- SLH-DSA has no runtime SIMD dispatch — it uses the portable reference (with optional build-time SHA-NI); its performance is correspondingly lower than the lattice families.

- The integrity mechanism is platform-specific in finalisation (embedded HMAC on Linux/macOS; external cnf on Windows), which the deployment process must handle correctly.

- FIPS-build constraints prohibit \`-ffunction-sections\` and LTO (required for a contiguous integrity region); these are enforced as fatal build errors.

- The status enums reserve value −11 (unused) across all three families — an intentional gap that consumers mapping numeric codes should be aware of.

- The OpenSSL provider, hybrid/composite algorithms, and the interop test suite live in the separate \`qudo-provider\` repository and are outside this module's boundary and this document.

- Assumption: the vendored \`\*-native\` upstreams are treated as the verified source of the algorithm mathematics; this document does not re-derive their internal correctness.

# 19 Future Enhancements

| **Area** | **Recommendation** |
|----|----|
| Maintainability | Keep the wrapper↔native boundary documented and the upstream pins tracked (UPSTREAM_MAINTENANCE.md); consider unifying the ML-KEM per-call dispatch and ML-DSA global-pointer dispatch models for consistency. |
| Security hardening | Add formal/independent side-channel assessment (the sub-library dudect screening is informal); evaluate mlock/secure-alloc use on the Object-API hot paths; consider memory-safety fuzzing of the DER/PEM import surface. |
| Certification readiness | Complete CAVP submission and CMVP documentation; reconcile the tested-OE list across docs; capture entropy-source (SP 800-90B) assessment evidence. |
| Performance | Investigate SLH-DSA SIMD/Keccak acceleration; profile CTR_DRBG and Keccak hot paths; expand the benchmark matrix beyond a single platform. |

Table 25 — Recommended future work.

# Appendix A Complete Module Inventory

## A.1 src/ — FIPS module services

| **File** | **Responsibility** |
|----|----|
| qudo_pqc_init.c | Lifecycle/state machine, constructor/DllMain hooks, CAST runners, error-state latch |
| qudo_pqc_post.c | Power-on self-tests: primitive + per-family KATs, CAST status marking |
| qudo_pqc_pct.c | Pairwise consistency tests (encaps/decaps; sign/verify); self-test skip |
| qudo_pqc_integrity.c | HMAC-SHA-256 module-integrity (embedded + external cnf), integrity KAT |
| qudo_pqc_security.c | NIST security-level table; min-level get/set/check |
| qudo_pqc_indicator.c | FIPS approval indicator (running + approved + DRBG-ready) |
| qudo_pqc_audit.c | Audit callback registry, severity rate-limiting, error-string table |
| qudo_pqc_platform.c | OS abstraction: rwlocks, atomics, once, allocator, cleanse, ct-compare |
| qudo_pqc_utils.c | Public RNG/HMAC/CTR_DRBG wrappers; module-boundary/integrity accessors |
| qudo_self_test_data.inc | Frozen KAT vectors for all sets + CTR_DRBG (drift detection) |
| qudo_fips_boundary\_{start,end}.c | Linker-pinned sentinels bracketing the integrity .text region |
| qudo_pqc_embedded_hmac.c | 32-byte integrity HMAC slot in .fips_hmac (patched by fipsinstall) |
| qudo_pqc_selftest.h | Self-test context + begin/corrupt/end event hooks |

## A.2 src/fips/ — primitives

| **File** | **Responsibility** |
|----|----|
| qudo_fips_rand.c | Platform entropy + DRBG front end; RCT/APT/startup health tests; reseed interval |
| qudo_fips_ctrdrbg.c | SP 800-90A AES-256 CTR_DRBG (no-df): update/generate/reseed |
| qudo_fips_aes.c | AES dispatcher: runtime AES-NI vs constant-time |
| qudo_fips_aes_ct.c | Constant-time portable AES (no T-table cache leak) |
| qudo_fips_aes_ni.c | x86/x64 AES-NI intrinsics (compiled on x86 only) |
| qudo_fips_hmac.c | HMAC-SHA-256 (FIPS 198-1); cleanses key/pad/ctx |
| qudo_fips_aes.h | AES types + qudo_secure_clear portability cascade |
| qudo_fips_sha2.h / sha3.h | SHA-2 / SHA-3 constants (pull sha2/sha3 from native trees) |

# Appendix B API Catalog (summary)

Full signatures are in the headers (file:line cited inline in Sections 6–9). Counts and groups:

| **Surface** | **Groups** |
|----|----|
| Module (qudo_pqc.h) | version; lifecycle (init/fini/self_test/run_all_casts); state queries; security level; POST/CAST status; PCT hooks; RNG + CTR_DRBG object API; integrity/HMAC; cleanse |
| ML-KEM (mlkem_wrapper.h) | init/new/free; keypair(+derand/from_seed); encaps(+derand); decaps; check_pk/sk; Direct API (generate/encapsulate/decapsulate); size+metadata getters; DER/PEM; platform getters |
| ML-DSA (mldsa_wrapper.h) | new/free; keypair; sign(+with_context/internal/extmu/concat/pre_hash); verify(+variants); open; per-set direct funcs (44/65/87); getters; DER/PEM; SHAKE-256 helpers; registry |
| SLH-DSA (slhdsa_wrapper.h) | new/free; keypair(+internal); sign(+ctx_str/ex/internal/pre_hash); verify(+variants); 12 per-set direct funcs ×3; getters; DER/PEM |

Table 26 — API catalog summary (exhaustive function lists in the wrapper headers).

# Appendix C Architecture Diagrams (index)

Diagrams in this document: Figure-equivalent ASCII blocks in §3.1 (logical architecture), §3.2 (initialisation sequence), §7.2 / §8.1 / §9.2 (per-family keygen/sign(or encaps)/verify(or decaps) sequences), §11.1 (zeroization cascade), §12.1 (error/failure flow), and the dependency graph in Appendix D.

# Appendix D Dependency Graph

```text
                         application
                              |
                     +--------+--------+
                     |  public API     |   (symbol-filtered export)
                     +--------+--------+
                              |
        +---------------------+---------------------+
        |                     |                     |
   qudo-mlkem            qudo-mldsa            qudo-slhdsa     (algorithm layer)
        |                     |                     |
   mlkem-native          mldsa-native         slhdsa-native   (vendored math)
        |                     |                     |
        +----------+----------+----------+----------+
                   |                     |
            src/fips primitives    FIPS module services (src/)
            AES / CTR_DRBG /        state · POST · CAST · PCT ·
            HMAC / SHA2 / SHA3      integrity · indicator · audit
                   |
            platform entropy (OS RNG)

   No edge leaves this graph to an external crypto library.
```

Table 27 — Module dependency graph (downward = depends-on).

# Appendix E FIPS Traceability Matrix

The consolidated component→standard→function matrix is in Section 14. It maps every FIPS-relevant component (algorithms, POST, CAST, PCT, integrity, DRBG, entropy health, state model, indicator, zeroisation) to its standard and implementing functions/files. This describes implementation alignment only and is not a certification claim.

# Appendix F References & Applicable Standards

The following normative and informative references apply to this document. Bracketed labels are used for in-text citation.

| **Ref** | **Citation** |
|----|----|
| \[FIPS203\] | FIPS 203, Module-Lattice-Based Key-Encapsulation Mechanism Standard, NIST, 2024. |
| \[FIPS204\] | FIPS 204, Module-Lattice-Based Digital Signature Standard, NIST, 2024. |
| \[FIPS205\] | FIPS 205, Stateless Hash-Based Digital Signature Standard, NIST, 2024. |
| \[FIPS140-3\] | FIPS 140-3, Security Requirements for Cryptographic Modules, NIST, 2019. |
| \[IG\] | NIST CMVP, Implementation Guidance for FIPS 140-3 and the CMVP. |
| \[SP800-90A\] | NIST SP 800-90A Rev. 1, Recommendation for Random Number Generation Using Deterministic Random Bit Generators, 2015. |
| \[SP800-90B\] | NIST SP 800-90B, Recommendation for the Entropy Sources Used for Random Bit Generation, 2018. |
| \[FIPS180-4\] | FIPS 180-4, Secure Hash Standard (SHA-2), NIST, 2015. |
| \[FIPS202\] | FIPS 202, SHA-3 Standard: Permutation-Based Hash and Extendable-Output Functions (SHAKE), NIST, 2015. |
| \[FIPS198-1\] | FIPS 198-1, The Keyed-Hash Message Authentication Code (HMAC), NIST, 2008. |
| \[ACVP\] | NIST ACVP — Automated Cryptographic Validation Protocol; test vectors usnistgov/ACVP-Server v1.1.0.41. |
| \[QUDO-DOCS\] | QUDO PQC repository documentation set (README, FIPS Security Policy, Crypto-Officer & User Guidance, CAST mapping, Vendor Evidence). |

Table 28 — References and applicable standards.

# Appendix G Acronyms & Definitions

| **Term** | **Definition** |
|----|----|
| ACVP | Automated Cryptographic Validation Protocol (NIST) — known-answer test vectors and protocol. |
| AES | Advanced Encryption Standard (FIPS 197). |
| CAST | Cryptographic Algorithm Self-Test (FIPS 140-3). |
| CAVP | Cryptographic Algorithm Validation Program (NIST). |
| CMVP | Cryptographic Module Validation Program (NIST/CCCS). |
| CTR_DRBG | Counter-mode Deterministic Random Bit Generator (SP 800-90A). |
| DRBG | Deterministic Random Bit Generator. |
| FIPS | Federal Information Processing Standard. |
| HMAC | Keyed-Hash Message Authentication Code (FIPS 198-1). |
| IG | Implementation Guidance (CMVP). |
| KAT | Known Answer Test. |
| KEM | Key Encapsulation Mechanism. |
| ML-DSA | Module-Lattice-Based Digital Signature Algorithm (FIPS 204). |
| ML-KEM | Module-Lattice-Based Key-Encapsulation Mechanism (FIPS 203). |
| MLWE / MSIS | Module Learning-With-Errors / Module Short-Integer-Solution (lattice problems). |
| NEON | Arm Advanced SIMD instruction set (aarch64). |
| NIST | National Institute of Standards and Technology. |
| PCT | Pairwise Consistency Test (conditional self-test on key generation). |
| POST | Power-On Self-Test. |
| PQC | Post-Quantum Cryptography. |
| SHA / SHAKE | Secure Hash Algorithm (FIPS 180-4) / SHA-3 extendable-output function (FIPS 202). |
| SLH-DSA | Stateless Hash-Based Digital Signature Algorithm (FIPS 205). |
| SP | NIST Special Publication. |
| SSP | Sensitive Security Parameter (FIPS 140-3). |

Table 29 — Acronyms and definitions.
