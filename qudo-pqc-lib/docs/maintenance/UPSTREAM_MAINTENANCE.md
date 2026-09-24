# QUDO PQC Upstream Maintenance Plan

## Overview

The QUDO PQC crypto library wraps three upstream NIST reference implementations.
This document tracks the relationship with each upstream project and defines
the maintenance strategy.

## Upstream Dependencies

### mlkem-native (ML-KEM / FIPS 203)

| Field | Value |
|-------|-------|
| Upstream | https://github.com/pq-code-package/mlkem-native |
| Local path | `qudo-mlkem/mlkem-native/` |
| Based on | `183f2809` (main, 2026-06-02) — 68 commits past the 2026-04-09 snapshot |
| Upstream status | **Active** — formal verification, AVX2 assembly, new platforms |
| Core crypto modified | No — vanilla upstream |
| QUDO additions | `qudo_runtime_dispatch/` (CPU feature detection) |

**Last updated**: 2026-06-05 — pulled 68 commits from `11b58a7d` to latest main.

**Key changes in this update**:
- Performance: Keccak x4 AVX2 stack frame 32-byte aligned (HOL-Light proofs updated)
- Assembly: backend symbol/file naming unified — all `.S` files renamed to
  `<name>_<arch>_asm.S` (in-place renames, covered by existing CMake globs)
- Verification: CBMC 6.9, quantifier-limit enforcement, contract lint;
  rej-uniform lookup-table declaration correctness fix
- Testing: backend unit tests for rej_uniform and compress/decompress under valgrind
- Docs: function banners converted to Doxygen style (large comment-only churn)
- Not adopted/not relevant: RISC-V RVV backend, Armv8.1-M MVE, Cortex-M33 platform, CI infra

**Update strategy**: Track upstream main branch. Public API unchanged.

### mldsa-native (ML-DSA / FIPS 204)

| Field | Value |
|-------|-------|
| Upstream | https://github.com/pq-code-package/mldsa-native |
| Local path | `qudo-mldsa/mldsa-native/` |
| Based on | `93a4fc74` (main, 2026-06-05) — ~140 commits past the 2026-04-09 snapshot |
| Upstream status | **Active** — formal verification, stack optimization, new platforms |
| Core crypto modified | No — vanilla upstream |
| QUDO additions | `qudo_runtime_dispatch/` (CPU feature detection) |

**Last updated**: 2026-06-05 — pulled ~140 commits from `9c62dedc` to latest main.

**Key changes in this update**:
- Correctness: `mld_sign` returns `smlen = 0` on failure (was `mlen`)
- Performance: x86_64 Keccak x4 C implementation (`KeccakP_1600_times4_SIMD256.c`)
  replaced by AVX2 assembly (`keccak_f1600_x4_avx2_asm.S`) — required adding the
  `fips202/native/x86_64/src/*.S` glob to `qudo-mldsa/CMakeLists.txt` (both blocks)
- Verification: HOL Light proofs for x86_64 NTT/INTT/nttunpack/caddq and AArch64
  rejection sampling (constant-time + memory safety); Isabelle Neon NTT
  formalization; new `SOUNDNESS.md` (coverage partial, tracked upstream in
  issues #912/#918)
- Assembly: backend symbol/file naming unified to `<name>_<arch>_asm.S`
- New configs (unused by QUDO): disable keygen/sign/verify APIs individually;
  `lowram` matrix streaming
- Not adopted/not relevant: RISC-V, Armv8.1-M MVE, CI infra

**Update strategy**: Track upstream main branch. Public API unchanged.

### slhdsa-c (SLH-DSA / FIPS 205)

| Field | Value |
|-------|-------|
| Upstream | https://github.com/pq-code-package/slhdsa-c |
| Local path | `qudo-slhdsa/slhdsa-native/` |
| Based on | Latest upstream (April 2026) |
| Upstream status | **Stale** — work in progress, no releases |
| Core crypto modified | No — vanilla upstream |
| QUDO additions | None in slhdsa-native (wrapper in `qudo-slhdsa/src/`) |

**Risk assessment**: Upstream explicitly states "DO NOT RECOMMEND RELYING ON
THIS LIBRARY IN A PRODUCTION ENVIRONMENT." Recent commits are CI dependency
bumps only. Last substantive algorithm change: February 2026.

**Mitigation strategy**:
1. QUDO effectively owns maintenance of this code
2. Core algorithm files (9 source files, ~3,000 LOC) are well-tested:
   - Passes all 1,248 NIST ACVP test vectors
   - All 12 parameter sets (SHA2 + SHAKE, 128/192/256, small/fast)
3. Monitor upstream for critical CVEs in hash functions or tree algorithms
4. Apply security patches manually if upstream remains inactive
5. Consider contributing fixes back to upstream

## QUDO Modification Inventory

Files added by QUDO (not present in upstream):

### mlkem-native
- `qudo_runtime_dispatch/cpu_features.h` — CPU feature detection API
- `qudo_runtime_dispatch/cpu_features.c` — AVX2/NEON detection with caching

### mldsa-native
- `qudo_runtime_dispatch/cpu_features.h` — CPU feature detection API
- `qudo_runtime_dispatch/cpu_features.c` — AVX2/NEON detection with caching

### slhdsa-native
- No modifications to upstream files
- QUDO wrapper layer is in `qudo-slhdsa/src/` (separate from native code)

## Update Procedure

1. Check upstream for new releases/commits:
   ```bash
   git ls-remote https://github.com/pq-code-package/mlkem-native refs/tags/*
   git ls-remote https://github.com/pq-code-package/mldsa-native refs/tags/*
   ```

2. Copy new upstream files into local directory (preserve QUDO additions)

3. Run KAT tests from the repository root:
   ```bash
   cmake -S . -B build -DQUDO_PQC_BUILD_TESTS=ON
   cmake --build build -j
   ctest --test-dir build --output-on-failure
   ```

4. Run NIST ACVP test vectors if available

5. Update this document with new version and date

6. Update CHANGELOG.md
