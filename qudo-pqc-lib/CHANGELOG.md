# Changelog

All notable changes to the QUDO PQC Software project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.0.0-alpha] - 2026-06-18

### Changed

- Updated vendored `mlkem-native` to upstream `183f2809` (2026-06-02) and
  `mldsa-native` to upstream `93a4fc74` (2026-06-05). Highlights: ML-DSA
  `mld_sign` returns `smlen = 0` on failure; x86_64 Keccak x4 moved from C
  to AVX2 assembly (added the `fips202/native/x86_64/src/*.S` glob to
  `qudo-mldsa/CMakeLists.txt`); Keccak x4 AVX2 stack frame 32-byte aligned;
  expanded HOL Light proof coverage (mlkem-native: all x86_64/aarch64
  assembly; mldsa-native: partial, see its `SOUNDNESS.md`); upstream
  assembly files renamed to `<name>_<arch>_asm.S`. Verified with the full
  FIPS suite, standalone subproject suites, and all ACVP vectors.

### Changed (BREAKING)

- **BREAKING**: `QUDO_KEM_keypair_derand()` now takes `size_t seed_out_len`
  as the final parameter. Callers must pass the capacity of the `seed_out`
  buffer. Closes buffer-overflow (P1-07). This pre-cert ABI correction is
  folded into the initial 1.0.0 release; the library SONAME is
  `libqudo-pqc.so.1` (`SOVERSION 1`) and is unchanged.

### Fixed

- **P0-06**: SLH-DSA sign out-of-bounds write via `*signature_len == 0`
  sentinel. All five sign entry points (`QUDO_SLHDSA_sign_ex`,
  `QUDO_SLHDSA_sign_internal`, `internal_sign`, `internal_sign_with_ctx`,
  `QUDO_SLHDSA_sign_pre_hash`) now apply a FIPS 205 size-query pattern:
  `signature == NULL` returns required capacity; otherwise an unconditional
  `*signature_len >= expected` check runs before any native dispatch. The
  caller buffer is zero-initialized before dispatch to the vendored
  `slh_sign()` native (no native ABI change — vendored upstream).
- **P0-07**: ML-DSA wrappers now enforce capacity and exact-length bounds.
  `QUDO_MLDSA_sign` / `QUDO_MLDSA_sign_with_context` check
  `*signature_len >= sig->length_signature` before dispatch;
  `QUDO_MLDSA_verify` / `QUDO_MLDSA_verify_with_context` reject
  `signature_len != sig->length_signature` with
  `QUDO_MLDSA_ERROR_INVALID_SIGNATURE`. Vendored `mld_sign_signature()`
  native ABI unchanged — wrapper is the enforcement boundary.
- **P2-11**: Added `QUDO_FIPS_IND_CHECK` to `QUDO_KEM_keypair_derand`,
  `QUDO_KEM_keypair_from_seed`, `QUDO_MLDSA_keypair_internal`, and
  `QUDO_SLHDSA_keypair_internal` for internal framework consistency
  with the other public entry points.

### Added

- **Audit/Logging Framework**: Structured event logging for all security-relevant
  operations (POST, PCT, DRBG, state transitions). `qudo_pqc_audit.h` with
  `qudo_audit_cb` callback and 32 categorized error codes in `qudo_pqc_err.h`.

- **Per-Operation FIPS Indicator**: `qudo_fips_ind_t` embedded in all algorithm
  contexts. Tracks approved/unapproved status per operation with strict/tolerant
  modes. `qudo_fips_ind_check_operation()` combines module state, algorithm
  approval, and DRBG readiness.

- **Security Level Enforcement**: `qudo_pqc_set_min_security_level()` with
  algorithm-to-NIST-level mapping for all 18 parameter sets.

- **CAST On-Demand**: `qudo_pqc_run_all_casts()` now executes real keygen + PCT
  per algorithm family (was previously cached status only).

- **FIPS 140-3 Security Policy**: Formal document per ISO/IEC 19790 with all 10
  required sections (`docs/FIPS_SECURITY_POLICY.md`).

- **FIPS Build Guide**: Build constraints documentation (`docs/FIPS_BUILD_GUIDE.md`).

- **Upstream Maintenance Plan**: Tracking document for mlkem-native, mldsa-native,
  slhdsa-c dependencies (`docs/UPSTREAM_MAINTENANCE.md`).

- **CI Workflow**: GitHub Actions CI with 8-job matrix (standard, FIPS, sanitizers
  across Linux/macOS/Windows).

- **CMake Package Config**: `find_package(qudo-pqc)` support with
  `qudo-pqc-config.cmake` and version compatibility.

### Changed

- **OpenSSL dependency removed**: Library now uses internal AES-256-CTR-DRBG
  (SP 800-90A) for all builds. Zero external crypto dependencies.

- **DRBG enforcement**: Compile-time `#error` guards prevent conflicting RNG
  paths. Runtime `qudo_fips_rand_is_ready()` check before every crypto operation.

- **Upstream mlkem-native updated**: `11b58a7d` (+146 commits). Includes AVX2
  assembly for compression/Keccak, formal verification proofs, security fixes.

- **Upstream mldsa-native updated**: `9c62dedc` (+64 commits). Includes buffer
  overread fix, stack optimization (~8% less in signing), formal verification proofs.

### Removed

- OpenSSL `RAND_bytes` dependency from all sub-libraries
- Dead `qudo_integrity.c` from provider (integrity now in `qudo-pqc`)

## [1.0.0] - 2026-02-17

### Added

- **qudo-mlkem**: ML-KEM key encapsulation library (FIPS 203)
  - Pure KEM: ML-KEM-512, ML-KEM-768, ML-KEM-1024
  - Object API (`QUDO_KEM_new`) and Direct API (`QUDO_KEM_keypair_generate`)
  - DER/PEM serialization (X.509 SubjectPublicKeyInfo, PKCS#8 PrivateKeyInfo)
  - Runtime CPU dispatch (reference C, AVX2, NEON)
  - Known Answer Tests (KAT) and NIST test vectors

- **qudo-mldsa**: ML-DSA digital signature library (FIPS 204)
  - Signature algorithms: ML-DSA-44, ML-DSA-65, ML-DSA-87
  - Object API (`QUDO_MLDSA_new`) and Direct API
  - DER/PEM serialization
  - Runtime CPU dispatch (reference C, AVX2, NEON)
  - Known Answer Tests (KAT) and NIST test vectors
  - Constant-time operation verification

- **qudo-slhdsa**: SLH-DSA stateless hash-based signature library (FIPS 205)
  - All 12 parameter sets:
    - SLH-DSA-SHA2-128s, SLH-DSA-SHA2-128f
    - SLH-DSA-SHA2-192s, SLH-DSA-SHA2-192f
    - SLH-DSA-SHA2-256s, SLH-DSA-SHA2-256f
    - SLH-DSA-SHAKE-128s, SLH-DSA-SHAKE-128f
    - SLH-DSA-SHAKE-192s, SLH-DSA-SHAKE-192f
    - SLH-DSA-SHAKE-256s, SLH-DSA-SHAKE-256f
  - Object API (`QUDO_SLHDSA_new`) and Direct API
  - Context string support
  - DER/PEM serialization
  - Extended signing API (`QUDO_SLHDSA_sign_ex`) with deterministic/hedged/test-entropy modes (FIPS 205)

- **qudo-provider**: OpenSSL 3.x provider integration
  - 3 pure KEM algorithms (ML-KEM-512, ML-KEM-768, ML-KEM-1024)
  - 3 IETF hybrid KEM algorithms (X25519MLKEM768, SecP256r1MLKEM768, SecP384r1MLKEM1024)
  - 10 composite KEM algorithms (p256_mlkem512, x25519_mlkem512, bp256_mlkem512, p384_mlkem768, x25519_mlkem768, x448_mlkem768, bp384_mlkem768, p521_mlkem1024, bp512_mlkem1024, x448_mlkem1024)
  - 3 ML-DSA signature algorithms (ML-DSA-44, ML-DSA-65, ML-DSA-87)
  - 9 hybrid ML-DSA signature algorithms (p256_mldsa44, p384_mldsa65, p521_mldsa87, bp256_mldsa65, bp384_mldsa87, rsa2048_mldsa44, rsa2048_pkcs15_mldsa44, rsa3072_mldsa65, rsa4096_mldsa65)
  - 12 SLH-DSA signature algorithms (all parameter sets)
  - PEM/DER encoding and decoding
  - TLS 1.3 group registration for hybrid KEMs
  - Full EVP API support (keygen, encaps/decaps, sign/verify)
  - Streaming SHAKE-256 mu computation for pure ML-DSA signing/verification (FIPS 204 constant-memory)
  - One-shot signing for SLH-DSA (FIPS 205 — message used twice internally, no streaming)
  - Three-mode SLH-DSA randomness: deterministic, hedged, test-entropy (FIPS 205 `addrnd`)
  - ML-DSA signature length validation in verify paths (FIPS 204 fixed-length: 2420/3309/4627 bytes)
  - SLH-DSA settable context params: `CONTEXT_STRING`, `TEST_ENTROPY`, `DETERMINISTIC`, `MESSAGE_ENCODING`
  - Safe `key_has` early-return-on-failure pattern across all keymgmt files
  - Safe `key_free` with size-before-free, pointer NULLing, and `OPENSSL_secure_clear_free` for private keys
  - Safe `qudo_store_u32_be`/`qudo_load_u32_be` inline functions replacing unsafe macros
  - Hybrid KEM uses `classical_pkey` directly (no intermediate parameter copy — fixes X25519/X448)

- Project governance files (LICENSE, CONTRIBUTING, SECURITY, CODE_OF_CONDUCT)
- GitHub issue and pull request templates
- SPDX license headers on all provider source files
