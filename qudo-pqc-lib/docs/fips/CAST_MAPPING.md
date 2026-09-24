# Conditional Algorithm Self-Tests (CAST) Mapping

FIPS 140-3 Implementation Guidance 10.3.A requires that a Conditional Algorithm Self-Test
runs before the first operational use of each cryptographic algorithm. This document maps
each CAST ID to its algorithm, test procedure, and pass/fail criteria.

## CAST IDs

| CAST ID | Value | Algorithm | FIPS Standard | NIST Security Level |
|---------|-------|-----------|---------------|---------------------|
| `QUDO_CAST_ML_KEM_512` | 0 | ML-KEM-512 | FIPS 203 | Level 1 (128-bit) |
| `QUDO_CAST_ML_KEM_768` | 1 | ML-KEM-768 | FIPS 203 | Level 3 (192-bit) |
| `QUDO_CAST_ML_KEM_1024` | 2 | ML-KEM-1024 | FIPS 203 | Level 5 (256-bit) |
| `QUDO_CAST_ML_DSA_44` | 3 | ML-DSA-44 | FIPS 204 | Level 2 (128-bit) |
| `QUDO_CAST_ML_DSA_65` | 4 | ML-DSA-65 | FIPS 204 | Level 3 (192-bit) |
| `QUDO_CAST_ML_DSA_87` | 5 | ML-DSA-87 | FIPS 204 | Level 5 (256-bit) |
| `QUDO_CAST_SLH_DSA_SHA2_128` | 6 | SLH-DSA-SHA2-128s/f | FIPS 205 | Level 1 (128-bit) |
| `QUDO_CAST_SLH_DSA_SHA2_192` | 7 | SLH-DSA-SHA2-192s/f | FIPS 205 | Level 3 (192-bit) |
| `QUDO_CAST_SLH_DSA_SHA2_256` | 8 | SLH-DSA-SHA2-256s/f | FIPS 205 | Level 5 (256-bit) |
| `QUDO_CAST_SLH_DSA_SHAKE_128` | 9 | SLH-DSA-SHAKE-128s/f | FIPS 205 | Level 1 (128-bit) |
| `QUDO_CAST_SLH_DSA_SHAKE_192` | 10 | SLH-DSA-SHAKE-192s/f | FIPS 205 | Level 3 (192-bit) |
| `QUDO_CAST_SLH_DSA_SHAKE_256` | 11 | SLH-DSA-SHAKE-256s/f | FIPS 205 | Level 5 (256-bit) |

Total: 12 CAST IDs (`QUDO_CAST_COUNT = 12`) covering 3 algorithm families (ML-KEM, ML-DSA, SLH-DSA).

Defined in: `include/qudo_pqc.h`

## CAST State Machine

Each CAST ID tracks its own state independently:

```
QUDO_CAST_STATE_INIT (0)       — Not yet tested
    ↓ (triggered by POST or on-demand)
QUDO_CAST_STATE_PROCESSING (1) — Test in progress
    ↓ (pass)                ↓ (fail)
QUDO_CAST_STATE_SUCCESS (2)    QUDO_CAST_STATE_FAILURE (3)
```

## Test Procedure Per Algorithm Family

### ML-KEM (CAST IDs 0-2) — Pairwise Consistency Test

**Source**: `qudo_pqc_init.c:run_cast_mlkem()` calling `qudo_pqc_mlkem_pct()`

**Procedure** (per FIPS 203 Section 7):
1. Generate a temporary ML-KEM key pair via `QUDO_KEM_keypair()`
2. Encapsulate: generate ciphertext + shared secret using the public key
3. Decapsulate: recover shared secret using the secret key and ciphertext
4. Compare: verify the encapsulated and decapsulated shared secrets match
5. Zeroize: securely erase all temporary key material

**Pass criteria**: Decapsulated shared secret equals encapsulated shared secret (constant-time comparison).

**Fail criteria**: Shared secret mismatch, allocation failure, or any operation returns error.

**On failure**: CAST status set to `QUDO_CAST_STATE_FAILURE`, audit event logged (`QUDO_SEV_ERROR`; `QUDO_ERR_CAST_KEYGEN_FAIL` if keygen fails, otherwise `QUDO_ERR_CAST_FAIL` on a consistency mismatch).

### ML-DSA (CAST IDs 3-5) — Pairwise Consistency Test

**Source**: `qudo_pqc_init.c:run_cast_mldsa()` calling `qudo_pqc_mldsa_pct()`

**Procedure** (per FIPS 204 Section 8):
1. Generate a temporary ML-DSA key pair via `QUDO_MLDSA_keypair()`
2. Sign: produce a signature over a test message using the secret key
3. Verify: check the signature using the public key
4. Zeroize: securely erase all temporary key material

**Pass criteria**: Signature verification succeeds.

**Fail criteria**: Verification fails, allocation failure, or any operation returns error.

### SLH-DSA (CAST IDs 6-11) — Pairwise Consistency Test

**Source**: `qudo_pqc_init.c:run_cast_slhdsa()` calling `qudo_pqc_slhdsa_pct()`

**Procedure** (per FIPS 205 Section 11):
1. Generate a temporary SLH-DSA key pair via `QUDO_SLHDSA_keypair()` using the `…-f` (fast) parameter set of that CAST ID's own hash/security-level family — e.g. `SLH-DSA-SHA2-128f` for the SHA2-128 CAST, `SLH-DSA-SHAKE-192f` for the SHAKE-192 CAST, and so on (see `run_cast_slhdsa()` call sites in `qudo_pqc_init.c`)
2. Sign: produce a signature over a test message using the secret key
3. Verify: check the signature using the public key
4. Zeroize: securely erase all temporary key material

**Pass criteria**: Signature verification succeeds.

**Fail criteria**: Verification fails, allocation failure, or any operation returns error.

**Note**: SLH-DSA has 6 CAST IDs (one per hash/security-level family), each covering the two `s`/`f` parameter set variants that share the same hash and tree dimensions. The representative parameter set per family is used at PCT time because the `s` and `f` variants share the same algorithmic structure (Merkle tree + WOTS+ + FORS) — only the tree dimensions and signature size differ.

## When CASTs Execute

### Automatic (Power-On Self-Test)

During `qudo_pqc_init()` (or the library constructor), the POST phase runs KAT (Known Answer Test) vectors per FIPS 140-3 IG 10.3.A §14 Note22 (ML-KEM), §15 Note26 (ML-DSA), and §16 Note29/Note30 (SLH-DSA: one SHA2 + one SHAKE; smallest tree height). POST coverage is:

- **ML-KEM** — keygen KATs for **all three** parameter sets (ML-KEM-512/768/1024); the encaps/decaps KAT uses the representative ML-KEM-512 (`qudo_post_keygen_mlkem()` / `qudo_kat_kem_tests[]` in `src/qudo_pqc_post.c`).
- **ML-DSA** — keygen + sign/verify KAT on the representative ML-DSA-65.
- **SLH-DSA** — keygen + sign/verify KAT on one SHA2 and one SHAKE representative (SLH-DSA-SHA2-128f, SLH-DSA-SHAKE-128f).

All KATs use fixed NIST test vectors. After POST completes, all 12 CAST statuses are set to `QUDO_CAST_STATE_SUCCESS` via `qudo_pqc_set_cast_status()` (`src/qudo_pqc_post.c`). Full per-parameter-set functional coverage for the parameter sets not exercised by POST lives outside POST in `tests/` and the ACVP harness.

### On-Demand

The function `qudo_pqc_run_all_casts()` provides on-demand CAST execution for auditors
or compliance verification. Unlike POST (which uses fixed KAT vectors), on-demand CASTs
generate fresh random key pairs and run live PCT operations.

### Per-Operation (Keygen/Import)

Every key generation and key import operation runs a PCT automatically:

- ML-KEM keygen: `qudo_pqc_mlkem_pct()` (`src/qudo_pqc_pct.c`), called from the library's `QUDO_KEM_keypair*` entry points
- ML-DSA keygen: `qudo_pqc_mldsa_pct()` (`src/qudo_pqc_pct.c`), called from `QUDO_MLDSA_keypair*`
- SLH-DSA keygen: `qudo_pqc_slhdsa_pct()` (`src/qudo_pqc_pct.c`), called from `QUDO_SLHDSA_keypair*`
- Key import: PCT runs on private key import; inside the corresponding `*_from_seed` / import path

## API Reference

```c
/* Query CAST status for a specific algorithm */
int qudo_pqc_get_cast_status(int cast_id);
/* Returns: QUDO_CAST_STATE_INIT/PROCESSING/SUCCESS/FAILURE */

/* Run all CASTs on-demand */
int qudo_pqc_run_all_casts(void);
/* Returns: 1 if all passed, 0 if any failed */

/* Set CAST status (internal use — called by CAST runner) */
void qudo_pqc_set_cast_status(int cast_id, int status);
```

## FIPS 140-3 Traceability

| Requirement | Implementation | Source File |
|-------------|---------------|-------------|
| IG 10.3.A: Conditional self-tests | 12 CASTs covering all approved algorithms | `qudo_pqc_init.c` |
| ISO/IEC 19790 Section 9.4: PCT | Encaps/decaps roundtrip (KEM), sign/verify roundtrip (DSA) | `qudo_pqc_pct.c` |
| FIPS 203 Section 7: ML-KEM validation | PCT after keygen, CAST before first use | `qudo_pqc_pct.c` |
| FIPS 204 Section 8: ML-DSA validation | PCT after keygen, CAST before first use | `qudo_pqc_pct.c` |
| FIPS 205 Section 11: SLH-DSA validation | PCT after keygen, CAST before first use | `qudo_pqc_pct.c` |

## Test Coverage

The CAST implementation is tested by `tests/test_pqc_cast.c` with 5 CTest entries (registered in `CMakeLists.txt`, gated on `QUDO_FIPS_MODULE=ON`):

| CTest Name              | What It Tests                                                  |
| ----------------------- | -------------------------------------------------------------- |
| `pqc_cast`              | Full CAST suite (all 12 IDs) — default invocation              |
| `pqc_cast_init`         | Initial CAST state after POST (`--init`)                       |
| `pqc_cast_individual`   | Per-algorithm CAST status queries (`--individual`)             |
| `pqc_cast_runall`       | On-demand `qudo_pqc_run_all_casts()` execution (`--runall`)    |
| `pqc_cast_invalid`      | Negative path: invalid CAST IDs and state queries (`--invalid`) |
