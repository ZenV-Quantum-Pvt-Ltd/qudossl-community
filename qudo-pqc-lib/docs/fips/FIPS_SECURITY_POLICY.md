# QUDO PQC — FIPS 140-3 Security Policy

| Field                | Value                                                |
| -------------------- | ---------------------------------------------------- |
| Module name          | QUDO PQC Cryptographic Module                        |
| Module version       | 1.0.0                                                |
| Module type          | Software                                             |
| Overall security level | **Level 1**                                        |
| Standard             | FIPS PUB 140-3 (ISO/IEC 19790:2012(E), as referenced) |

---

## 1. Module Specification

### 1.1 Module description

The QUDO PQC Cryptographic Module is a software-only library distributed
as `libqudo-pqc.so` (Linux), `libqudo-pqc.dylib` (macOS), or
`libqudo-pqc.dll` (Windows). It implements NIST post-quantum
cryptographic algorithms:

- **ML-KEM** (FIPS 203) — Key Encapsulation Mechanism
- **ML-DSA** (FIPS 204) — Digital Signature Algorithm
- **SLH-DSA** (FIPS 205) — Stateless Hash-Based Digital Signature Algorithm

The module exposes a C99 API for key generation, encapsulation /
decapsulation, signing, and verification, plus module-lifecycle and
status services.

An OpenSSL 3.x provider (`qudoprovider.so`) is distributed *separately*
in the `qudo-provider` repository. The provider is **outside the
cryptographic module boundary** and acts only as a thin translation
layer between OpenSSL's EVP API and this module's public C API. The
module operates as a standalone library in all deployments; the
provider is optional and consumes the module rather than wrapping it.

### 1.2 Module boundary

The cryptographic boundary is the single shared-library file listed in
§1.1. All cryptographic algorithms, key management, self-tests, and
state-machine logic reside within this boundary.

The boundary is physically anchored by two linker sentinel symbols
emitted from
`src/qudo_fips_boundary_start.c` and
`src/qudo_fips_boundary_end.c`:

- `qudo_fips_module_start` — first function in the integrity-covered region
- `qudo_fips_module_end` — last function in the integrity-covered region

On Linux, the linker script
`cmake/fips_module.ld` pins the boundary
objects via `KEEP(...)` sections. On all platforms, the per-target
`-fno-function-sections` flag prevents function-section fragmentation.
See [FIPS_BUILD_GUIDE.md §1](FIPS_BUILD_GUIDE.md#1-contiguous-text-region).

### 1.3 Hardware / software configuration

| Component        | Specification                                                       |
| ---------------- | ------------------------------------------------------------------- |
| Processor        | x86-64 (optional AVX2 dispatch); ARM64 (optional NEON dispatch)     |
| Operating system | Linux, macOS, Windows (MSVC or MinGW-w64)                           |
| Compiler         | GCC 10+ / Clang 14+ / Apple Clang 14+ / MSVC 2019+ (MSVC 2022+ for ARM64) |
| Build system     | CMake 3.15+                                                         |

The table above states the minimum supported configuration. The specific
operational environments on which the module has been tested (and that
are targeted for CMVP validation) are enumerated authoritatively in
`OPERATING_ENVIRONMENT.md` §1; that document is the single source of
record for the tested-OE list.

### 1.4 Module security level

| FIPS 140-3 area                                | Level |
| ---------------------------------------------- | ----- |
| Cryptographic Module Specification             | 1     |
| Cryptographic Module Interfaces                | 1     |
| Roles, Services, and Authentication            | 1     |
| Software / Firmware Security                   | 1     |
| Operational Environment                        | 1     |
| Physical Security                              | N/A (software module) |
| Non-Invasive Security                          | N/A (software module) |
| Sensitive Security Parameter Management        | 1     |
| Self-Tests                                     | 1     |
| Life-Cycle Assurance                           | 1     |
| Mitigation of Other Attacks                    | N/A   |

---

## 2. Cryptographic Module Interfaces

All interfaces are logical, defined by the public C API. The module has
no physical interfaces (software module).

### 2.1 Data Input

Carried through function parameters:

- Key material (public keys, secret keys, deterministic-keygen seeds)
- Messages to be signed
- Signatures to be verified
- Ciphertexts to be decapsulated
- Configuration values at init (module path, expected integrity HMAC, callback function pointers)
- Optional operator-supplied file-I/O callbacks
  (`io_open` / `io_read` / `io_close` in `qudo_pqc_config_t`,
  `include/qudo_pqc.h`) used by
  the integrity layer to read the external integrity config when the
  module is deployed without an embedded HMAC

### 2.2 Data Output

Carried through caller-provided buffers and return values:

- Generated key pairs (public + secret key buffers)
- Ciphertexts and shared secrets (ML-KEM)
- Signatures (ML-DSA, SLH-DSA)
- Verification results (1 = valid, 0 = invalid)
- Status / error codes

### 2.3 Control Input

Service-entry API:

- `qudo_pqc_init()` — initialize the module
- `qudo_pqc_self_test()` — re-run POST KATs on demand
- `qudo_pqc_run_all_casts()` — run all conditional algorithm self-tests on demand
- `qudo_pqc_set_min_security_level()` — set minimum approved NIST security category

### 2.4 Status Output

Status query API:

- `qudo_pqc_get_state()` — current state-machine value
- `qudo_pqc_is_running()` / `qudo_pqc_is_self_testing()` / `qudo_pqc_is_running_or_selftest()`
- `qudo_pqc_is_fips()` — whether the loaded module is a FIPS build
- `qudo_pqc_is_fips_approved(const char *)` — whether a named algorithm is approved
- `qudo_pqc_get_security_level(const char *)` — NIST security category for a named algorithm
- `qudo_pqc_get_min_security_level(void)` — currently configured minimum approved NIST category
- `qudo_pqc_get_cast_status(int)` — per-variant CAST status
- `qudo_pqc_post_drbg_kat_passed()` / `qudo_pqc_post_integrity_passed()` — granular POST results
- `qudo_fips_ind_is_approved(const qudo_fips_ind_t *)` — per-operation FIPS indicator
- `qudo_fips_ind_check_operation(const char *)` — combined indicator (state + algorithm + DRBG)
- Audit callback events — structured security-event log (see §3.4)

---

## 3. Roles, Services, and Authentication

### 3.1 Roles

| Role               | Description |
| ------------------ | ----------- |
| **Crypto Officer** | Installs, configures, and maintains the module: runs `qudo_fipsinstall` (embed or external-config), deploys `qudofipsmodule.cnf` if applicable, verifies module integrity after installation, and configures the OpenSSL provider when the deployment uses one. |
| **User**           | Application code that calls the module's cryptographic API (keygen, sign, verify, encaps, decaps) and the lifecycle entry points (`qudo_pqc_init`, status queries). |

### 3.2 Authentication

The module is a Level 1 software module. **No authentication is required
or performed for either role.** Access control is enforced by the host
operating environment (filesystem permissions, process isolation, code
signing).

### 3.3 Services

| Service                                | Description                                                              | Role         | Status        |
| -------------------------------------- | ------------------------------------------------------------------------ | ------------ | ------------- |
| Key Generation                         | Generate ML-KEM, ML-DSA, or SLH-DSA key pair                             | User         | Approved      |
| Key Generation (deterministic)         | Generate a key pair from a caller-supplied seed (FIPS 203/204/205 spec inputs) | User    | Approved      |
| Key Encapsulation                      | ML-KEM encapsulate                                                       | User         | Approved      |
| Key Encapsulation (deterministic)      | ML-KEM encapsulate with caller-supplied randomness (`encaps_derand`)     | User         | Approved      |
| Key Decapsulation                      | ML-KEM decapsulate (with FIPS 203 implicit-rejection on invalid input)   | User         | Approved      |
| Digital Signature Generation           | ML-DSA / SLH-DSA sign (hedged)                                           | User         | Approved      |
| Digital Signature Generation (deterministic) | ML-DSA / SLH-DSA sign with `rnd = 0` (deterministic mode per FIPS 204/205) | User   | Approved      |
| Digital Signature Verification         | ML-DSA / SLH-DSA verify                                                  | User         | Approved      |
| SSP Zeroization                        | Explicit zeroization of secret-key material on `_free()`                 | User         | Approved      |
| Module Initialization                  | Run integrity check + POST                                               | CO           | Non-approved support |
| On-demand POST                         | Re-execute all POST KATs                                                 | CO / User    | Non-approved support |
| On-demand CAST                         | Re-execute all conditional algorithm self-tests                          | CO / User    | Non-approved support |
| Status query                           | State, indicator, CAST status, security-level lookup, granular POST     | CO / User    | Non-approved support |

Per IG terminology, self-tests are non-approved support services even
in FIPS mode; the cryptographic primitives they exercise remain
approved.

### 3.4 Audit log

The module emits structured audit events via an application-registered
callback. The callback signature, severity levels, error codes, and
component tags are defined in
`include/qudo_pqc_audit.h` and
`include/qudo_pqc_err.h`. Events cover state
transitions, POST progress and results, integrity check results, PCT
outcomes, CAST status changes, DRBG seed/reseed/health events, and
indicator queries that resolve "not approved." See
[FIPS.md §Audit logging](FIPS.md#audit-logging) for the component-tag
inventory.

Four codes are defined and registered with reason strings but reserved
for future use — no current code path raises them: `QUDO_ERR_CAST_NOT_RUN`
(CAST not-run is a transient pre-POST state), `QUDO_ERR_KEY_INVALID` and
`QUDO_ERR_KEY_SIZE_MISMATCH` (per-call input validation reports through
API status codes, not audit events), and `QUDO_ERR_KEY_ZEROIZE_FAIL`
(zeroization has no detectable failure path).

---

## 4. Finite State Model

### 4.1 States

| State    | Constant                       | Description                                                       |
| -------- | ------------------------------ | ----------------------------------------------------------------- |
| INIT     | `QUDO_PQC_STATE_INIT` (0)      | Module loaded, integrity check and POST not yet executed          |
| SELFTEST | `QUDO_PQC_STATE_SELFTEST` (1)  | Integrity check + POST KATs running                               |
| RUNNING  | `QUDO_PQC_STATE_RUNNING` (2)   | Fully operational; cryptographic services available               |
| ERROR    | `QUDO_PQC_STATE_ERROR` (3)     | Fatal failure; all cryptographic services blocked; no automatic exit (recovery per transition 5, §4.2) |

### 4.2 State transitions

```
                            Module Load
                                 │
                                 ▼
                            +---------+
                            |  INIT   |
                            +---------+
                                 │
              shared-library constructor (Linux/macOS) or
              DllMain DLL_PROCESS_ATTACH (Windows), or
              first call to qudo_pqc_init()
                                 │
                                 ▼
                          +-----------+
                          | SELFTEST  |
                          +-----------+
                              │       │
                       POST passes    POST or integrity fails
                              │       │
                              ▼       ▼
                       +---------+  +-------+
                       | RUNNING |  | ERROR |
                       +---------+  +-------+
                              │       ▲
                              └───────┘
                  PCT-on-keygen failure
                  (with conditional_errors = 1)
```

### 4.3 Transition rules

1. **INIT → SELFTEST** is triggered by whichever of these comes first:
   - The shared-library constructor on Linux/macOS
     (`__attribute__((constructor))`) — see `QUDO_DEP_INIT_ATTRIBUTE`
     in `src/qudo_pqc_init.c`
   - `DllMain(DLL_PROCESS_ATTACH)` on Windows — see `DllMain()` in
     `src/qudo_pqc_init.c`
   - An explicit application call to `qudo_pqc_init()` — see
     `src/qudo_pqc_init.c`
2. **SELFTEST → RUNNING** on full pass of the integrity check and all
   POST KATs.
3. **SELFTEST → ERROR** on any integrity, KAT, or DRBG-seed failure
   during POST.
4. **RUNNING → ERROR** on a PCT-on-keygen failure when
   `conditional_errors = 1`, on a DRBG health-test failure during
   seed/reseed, or on any explicit fatal-error path.
5. **ERROR → (none).** `ERROR` blocks all cryptographic services.
   Calling `qudo_pqc_init()` while in `ERROR` returns 0 and does not
   re-arm the state machine — see the ERROR-state check in
   `qudo_pqc_init()` (`src/qudo_pqc_init.c`). Recovery requires either
   unloading and reloading the shared library (process restart in most
   deployments), or an explicit `qudo_pqc_fini()` — which zeroizes the
   DRBG state and clears module callbacks and status — followed by
   `qudo_pqc_init()`, which performs a complete re-initialization:
   integrity verification and all pre-operational self-tests must pass
   again before any service resumes.

If the library constructor already ran POST successfully, subsequent
calls to `qudo_pqc_init()` from `RUNNING` are idempotent: they update
the callbacks supplied in the config and return success without
re-running POST. To re-run POST on demand, call
`qudo_pqc_self_test()`.

### 4.4 No cryptographic output before POST

Per ISO/IEC 19790:2012 §7.9.1 (pre-operational self-tests) and §7.4
(module-interface rules during error states), the module emits no
cryptographic output until it reaches the `RUNNING` state and produces
none while in `ERROR`. The gate is enforced by `qudo_pqc_is_running()`,
which returns 0 for `INIT`, `SELFTEST`, and `ERROR`.

---

## 5. Physical Security

Not applicable — software-only module at overall Security Level 1.

---

## 6. Operational Environment

### 6.1 Tested platforms

The module operates on general-purpose operating systems. Validation
testing covers the platforms listed in [§1.3](#13-hardware--software-configuration).
See [OPERATING_ENVIRONMENT.md](OPERATING_ENVIRONMENT.md) for full
target-OE detail and [FIPS_BUILD_GUIDE.md §7](FIPS_BUILD_GUIDE.md#7-platform-enforcement-matrix)
for the per-platform enforcement matrix.

### 6.2 Environment requirements

- The operating system MUST provide process isolation.
- The operating system MUST provide a cryptographically secure RNG
  callable from user mode. The module consumes this RNG as its noise
  source via the **ESV Vendor Affirmation** pathway (see §7.6 and
  [VENDOR_EVIDENCE.md](VENDOR_EVIDENCE.md#51-entropy-source--esv-vendor-affirmation-sp-800-90b))
  — the module does **not** validate the entropy source itself.
- The module binary's integrity-covered region MUST NOT be modified
  after `qudo_fipsinstall` computes the integrity HMAC (embed mode) or
  after the external `qudofipsmodule.cnf` is generated (external-config
  mode). Stripping, `signtool` (Authenticode), or patching the covered
  bytes invalidates the integrity value and MUST NOT be performed after
  installation.
- macOS code-signing exception: ad-hoc code-signing (`codesign -s -`)
  is a REQUIRED step on macOS and is applied automatically as the final
  build action, after the HMAC embed (`build.sh`, `release.yml`). It is
  integrity-neutral — the signature resides in the `LC_CODE_SIGNATURE`
  load command and the `__LINKEDIT` segment, outside the `.text`
  integrity window the HMAC covers (§9), so the module-integrity
  self-test still passes on the signed binary. macOS 26+ refuses to load
  a binary whose signature no longer matches its bytes, making the
  post-embed re-sign mandatory for the module to load at all.
- For external-config deployments, the application MUST populate
  `module_path` and `module_checksum_hex` in `qudo_pqc_config_t` from
  the cnf before calling `qudo_pqc_init()`. The library does not
  auto-discover the cnf (verified by
  `tests/test_integrity_path_pinning.c`).

---

## 7. Cryptographic Key Management

### 7.1 Keys and Sensitive Security Parameters (SSPs)

| SSP                       | Standard       | Size (bytes)                       | Generation                          | Storage              | Zeroization                          |
| ------------------------- | -------------- | ---------------------------------- | ----------------------------------- | -------------------- | ------------------------------------ |
| ML-KEM-512 SK             | FIPS 203       | 1632                               | Internal keygen (DRBG-seeded)       | Caller buffer        | `qudo_cleanse()` (§7.4)              |
| ML-KEM-512 PK             | FIPS 203       | 800                                | Internal keygen                     | Caller buffer        | N/A (public)                         |
| ML-KEM-768 SK             | FIPS 203       | 2400                               | Internal keygen                     | Caller buffer        | `qudo_cleanse()`                     |
| ML-KEM-768 PK             | FIPS 203       | 1184                               | Internal keygen                     | Caller buffer        | N/A (public)                         |
| ML-KEM-1024 SK            | FIPS 203       | 3168                               | Internal keygen                     | Caller buffer        | `qudo_cleanse()`                     |
| ML-KEM-1024 PK            | FIPS 203       | 1568                               | Internal keygen                     | Caller buffer        | N/A (public)                         |
| ML-DSA-44 SK              | FIPS 204       | 2560                               | Internal keygen                     | Caller buffer        | `qudo_cleanse()`                     |
| ML-DSA-65 SK              | FIPS 204       | 4032                               | Internal keygen                     | Caller buffer        | `qudo_cleanse()`                     |
| ML-DSA-87 SK              | FIPS 204       | 4896                               | Internal keygen                     | Caller buffer        | `qudo_cleanse()`                     |
| SLH-DSA SK                | FIPS 205       | 64 / 96 / 128 (sets 128 / 192 / 256) | Internal keygen                     | Caller buffer        | `qudo_cleanse()`                     |
| ML-KEM Shared Secret      | FIPS 203       | 32                                 | KEM encaps / decaps                  | Stack or caller buffer | `qudo_cleanse()` on internal copies |
| DRBG Seed                 | SP 800-90A     | 48 (32 key + 16 V)                 | OS RNG; consumed without df         | DRBG internal state  | `qudo_cleanse()` on free / fail     |
| DRBG Key (V, K)           | SP 800-90A     | 32 + 16                            | DRBG instantiate / reseed            | DRBG internal state  | `qudo_cleanse()` on free / fail     |
| Module-Integrity HMAC Key | FIPS 198-1     | 32                                 | Compiled-in constant                 | `.rodata`            | N/A (public integrity key, FIPS 140-3 IG D.B) |

The module performs no persistent SSP storage. All SSPs are either in
caller-supplied buffers (key material) or in DRBG internal state, and
are zeroized on `_free()` or on cryptographic-error paths.

### 7.2 Key generation

All key generation uses the internal AES-256 CTR_DRBG seeded from the
OS-supplied entropy source (§7.6). Two approved-mode entry points are
exposed per family:

- **Hedged (default):** Randomness drawn from the internal DRBG. This
  is the typical service for application use.
- **Deterministic from caller-supplied seed:** The FIPS 203/204/205
  algorithms define a deterministic keygen variant taking a fixed-length
  input seed. The module exposes this variant
  (`QUDO_KEM_keypair_from_seed()`, etc.) as an approved service for
  applications that supply their own NIST-compliant seed material (for
  example, deterministic-derivation use cases and ACVP test harnesses).

Both variants produce keys with the standard FIPS 140-3 PCT applied
before the call returns.

### 7.3 Key storage

The module does not persistently store any key material. All SSPs reside
in caller-supplied buffers, in stack-resident scratch storage, or in
DRBG internal state. Lifetime is bounded by the calling service, and
internal copies are zeroized before return.

### 7.4 SSP zeroization

Zeroization is centralised in `qudo_cleanse()` in
`src/qudo_pqc_platform.c`, which
calls the `qudo_secure_clear()` macro defined in
`src/fips/qudo_fips_aes.h`.
The macro selects, in preprocessor order:

1. `SecureZeroMemory()` on Windows
2. `memset_s()` when C11 Annex K bounds-checking interfaces are
   advertised by the toolchain (`__STDC_LIB_EXT1__`)
3. `explicit_bzero()` where available (OpenBSD, glibc ≥ 2.25)
4. A fallback implementation using a volatile-qualified function pointer
   plus a memory barrier, which the C standard prohibits the compiler
   from optimizing away

Zeroization is invoked on every `_free()` API, on PCT failure (both
keygen and import paths), on DRBG-context destruction, and on any
internal scratch buffer holding intermediate SSP-derived material.

### 7.5 Key transport

The module does not provide key wrapping, key transport, or key-derivation
services beyond what is inherent to ML-KEM (encapsulation produces a
shared secret as defined by FIPS 203). ML-DSA and SLH-DSA private signing
keys are never transported by the module.

### 7.6 Entropy source and DRBG

#### Construction

| Property              | Value                                                          |
| --------------------- | -------------------------------------------------------------- |
| DRBG                  | AES-256 CTR_DRBG **without derivation function** (SP 800-90A §10.2.1) |
| Seed length           | 48 bytes (key 32 + V 16), supplied as a full block             |
| Reseed interval       | 2²⁰ generate calls (compile-time constant `RESEED_INTERVAL` in `src/fips/qudo_fips_rand.c`) |
| Additional input      | Supported by the primitive; **not used** on the standard generate path |
| Prediction resistance | **Not used** on the standard generate path                     |

#### Entropy source

The DRBG is seeded from the operating system's cryptographically secure
RNG. Validation is via the **ESV Vendor Affirmation** pathway — see
[VENDOR_EVIDENCE.md](VENDOR_EVIDENCE.md#51-entropy-source--esv-vendor-affirmation-sp-800-90b).
The noise-source primary entry points are:

| Platform | Primary source                                       | Reachable fallback                                                                                       |
| -------- | ---------------------------------------------------- | -------------------------------------------------------------------------------------------------------- |
| Linux    | `getentropy(3)` via glibc ≥ 2.25                     | `getrandom(2)` syscall (non-glibc Linux); `/dev/urandom` (last-resort; **non-approved configuration**)   |
| macOS    | `getentropy(3)` (`<sys/random.h>`)                    | —                                                                                                        |
| Windows  | `BCryptGenRandom(BCRYPT_USE_SYSTEM_PREFERRED_RNG)`   | —                                                                                                        |

Approved-mode deployments MUST run on a platform offering one of the
primary sources. The `/dev/urandom` last-resort fallback on Linux is
reached only when neither `getentropy()` nor the `getrandom` syscall is
available and is outside the approved configuration.

#### SP 800-90B health tests on the entropy source

Per SP 800-90B §4.4, the module applies continuous health tests to
**raw noise-source output before** it is consumed by the DRBG for
seeding or reseeding:

- **Repetition Count Test (RCT)** — detects a stuck noise source
- **Adaptive Proportion Test (APT)** — detects bias / low-entropy windows

Critical thresholds are tabulated in
`src/fips/qudo_fips_rand.c`
(`rct_critical[]`, `apt_critical[]`). A health-test failure emits
`QUDO_ERR_DRBG_HEALTH_FAIL` and aborts the seed/reseed attempt; no
output from the DRBG is produced from a failed seed.

#### Per-algorithm randomness consumption

| Operation                | Bytes drawn from DRBG                                |
| ------------------------ | ---------------------------------------------------- |
| ML-KEM keygen            | 64 (`d ‖ z` seed)                                    |
| ML-KEM encaps            | 32 (`m` randomness)                                  |
| ML-DSA keygen            | 32 (`ξ` seed)                                        |
| ML-DSA hedged sign       | 32 (`rnd`)                                           |
| SLH-DSA keygen           | 3 × n (`SK.seed`, `SK.prf`, `PK.seed`; n = 16/24/32) |
| SLH-DSA hedged sign      | n (`addrnd`)                                         |

All randomness is obtained through `qudo_pqc_rand_bytes()`. No external
DRBG is used within the module boundary.

---

## 8. Self-Tests

### 8.1 Pre-operational self-tests (POST)

POST runs automatically at module-load and again on any explicit
`qudo_pqc_init()` call from a non-RUNNING state. All POST elements
must pass before the state machine transitions to `RUNNING`; any
failure transitions to `ERROR`.

| # | Test                       | Type                | Coverage                                       | Source                                                                         |
| - | -------------------------- | ------------------- | ---------------------------------------------- | ------------------------------------------------------------------------------ |
| 1 | Module integrity           | HMAC-SHA-256        | `.text` region between boundary sentinels      | `src/qudo_pqc_integrity.c`                        |
| 2 | SHA-256 KAT                | KAT                 | Underlying digest primitive                    | `src/qudo_pqc_post.c`                         |
| 3 | HMAC-SHA-256 KAT           | KAT                 | Underlying MAC primitive (used for integrity)  | `src/qudo_pqc_post.c`                         |
| 4 | CTR_DRBG-AES-256 KAT       | KAT                 | DRBG construction (instantiate + generate)     | `src/qudo_pqc_post.c`                         |
| 5 | ML-KEM KAT                 | KAT (keygen for ML-KEM-512/768/1024; encaps + decaps + implicit-rejection on ML-KEM-512) | Keygen all 3 sets; encaps/decaps representative; per IG 10.3.A §14 Note22 | `src/qudo_self_test_data.inc` |
| 6 | ML-DSA-65 KAT              | KAT (keygen + sign + verify)               | Single representative; per IG 10.3.A §15 Note26 | `src/qudo_self_test_data.inc`      |
| 7 | SLH-DSA-SHA2-128f KAT      | KAT (keygen + sign + verify)               | SHA2 representative; per IG 10.3.A §16 Note29/Note30 | `src/qudo_self_test_data.inc` |
| 8 | SLH-DSA-SHAKE-128f KAT     | KAT (keygen + sign + verify)               | SHAKE representative; per IG 10.3.A §16 Note29/Note30 | `src/qudo_self_test_data.inc` |

**Per-parameter-set scope.** POST KAT coverage follows FIPS 140-3 IG
10.3.A §14 Note22 (ML-KEM), §15 Note26 (ML-DSA), and §16 Note29/Note30
(SLH-DSA: one SHA2 plus one SHAKE representative at the smallest tree
height to bound POST time). ML-KEM runs keygen KATs for **all three**
parameter sets (ML-KEM-512/768/1024) with the encaps/decaps KAT on the
representative ML-KEM-512; ML-DSA and SLH-DSA run keygen + sign/verify
KATs on their representative set(s). The choice of representative
parameter set is recorded inline at the source above. Full
per-parameter-set functional coverage for the sets not exercised by POST
is delivered **outside** POST through the test suite in `tests/` and the
ACVP harness.

ML-KEM-512 KAT vector #5 exercises the FIPS 203 Algorithm 18
implicit-rejection path explicitly via the `reject_ss` vector at
`src/qudo_self_test_data.inc`;
it is not a separate POST element.

### 8.2 Conditional self-tests

| Test                          | Trigger                                    | Algorithms                            | Failure behavior                                                                                                            |
| ----------------------------- | ------------------------------------------ | ------------------------------------- | --------------------------------------------------------------------------------------------------------------------------- |
| PCT-on-keygen                 | Every key generation (hedged or deterministic) | All 18 parameter sets                | `conditional_errors=1`: zeroize key and transition to ERROR; `conditional_errors=0` (non-approved): zeroize key only |
| PCT-on-import                 | Every key import                            | All 18 parameter sets                | **Always transient**: zeroize the failing key, reject the import, leave module in RUNNING (DoS-prevention design)            |
| DRBG entropy health (RCT, APT) | Every seed / reseed of the CTR_DRBG        | AES-256 CTR_DRBG noise source         | Abort seed/reseed; emit `QUDO_ERR_DRBG_HEALTH_FAIL`; module transitions to ERROR if no current healthy state exists           |

The PCT logic itself is in
`src/qudo_pqc_pct.c` and the zeroize-on-failure
behavior is verified by
`tests/test_pct_fail_zeroize.c`.

The RCT/APT tests apply to **raw entropy from the OS RNG**, not to
DRBG output — see §7.6.

### 8.3 Conditional algorithm self-test (CAST)

Per FIPS 140-3 IG 10.3.A, CASTs run before first use of each algorithm.
The POST KATs above satisfy this requirement at module-load for the
algorithm families and representative parameter sets they cover. For
the remaining variants, the module tracks a per-variant CAST status
flag (`QUDO_CAST_STATE_INIT / PROCESSING / SUCCESS / FAILURE`) and runs
a full keygen + sign-and-verify (or encaps-and-decaps) round-trip on
first use of that variant. Full mapping in
[CAST_MAPPING.md](CAST_MAPPING.md).

`qudo_pqc_run_all_casts()` forces a run of every CAST on demand.

### 8.4 On-demand self-test

`qudo_pqc_self_test()` re-verifies module integrity (the embedded HMAC, or
the external-config checksum) and then re-executes all POST KATs. If either the
integrity check or any KAT fails, the module transitions to `ERROR`. On-demand
re-test is callable only from `RUNNING`.

### 8.5 Self-test failure behavior

| Failure                         | State outcome                                                       |
| ------------------------------- | ------------------------------------------------------------------- |
| Integrity check failure         | `ERROR` (no automatic exit)                                                  |
| Any POST KAT failure            | `ERROR` (no automatic exit)                                                  |
| DRBG seed / health-test failure during POST | `ERROR` (no automatic exit)                                      |
| PCT-on-keygen failure (`conditional_errors=1`) | `ERROR` (no automatic exit); failing key zeroized              |
| PCT-on-keygen failure (`conditional_errors=0`, non-approved) | Failing key zeroized; module remains in `RUNNING` |
| PCT-on-import failure           | Failing key zeroized; module remains in `RUNNING` (DoS-prevention)  |
| DRBG health-test failure during runtime reseed | Affected service returns error; module transitions to `ERROR` if no healthy state exists |

---

## 9. Design Assurance

### 9.1 Configuration management

- Source code under Git revision control.
- CMake-based reproducible build pipeline.
- CI/CD with automated build, test, sanitizer, and FIPS-config matrix on
  all supported platforms (see [MAINTENANCE.md](../maintenance/MAINTENANCE.md)).
- Pinned upstream commits for `mlkem-native`, `mldsa-native`, and
  `slhdsa-native`; provenance recorded in
  [docs/UPSTREAM_MAINTENANCE.md](../maintenance/UPSTREAM_MAINTENANCE.md).

### 9.2 Testing

- NIST Known-Answer Tests (KATs) in POST cover one representative
  parameter set per algorithm family (see [§8.1](#81-pre-operational-self-tests-post))
  plus SHA-256, HMAC-SHA-256, and CTR_DRBG primitive KATs.
  Per-parameter-set functional coverage for the remaining 14 parameter
  sets is delivered through the test suite under `tests/` —
  `pqc_*`, `mlkem_*`, `mldsa_*`, `slhdsa_*` test binaries.
- NIST ACVP test-vector harness for all approved parameter sets — see
  `acvp/` and [docs/ACVP.md](ACVP.md). The harness drives the
  module with NIST-supplied prompt files and validates expected
  responses; per-parameter-set vector counts are reproducible by running
  `acvp/run_acvp.sh`.
- Pairwise consistency tests under
  `tests/test_pct_*`, including TSan-based concurrent-PCT
  coverage.
- AddressSanitizer, UndefinedBehaviorSanitizer, MemorySanitizer, and
  ThreadSanitizer runs in CI.
- Constant-time exercise of secret-key paths under Valgrind / memcheck
  (uninitialised-memory tracking is used as a proxy for secret-dependent
  branches; see [docs/TESTING.md](../development/TESTING.md)).

### 9.3 Code quality

- C99 standard, with stricter warning sets enabled across compilers.
- No undefined behavior tolerated (UBSan-clean in CI).
- No memory leaks (Valgrind / ASan-clean in CI).
- Cross-platform validation: Linux (GCC + Clang), macOS (Apple Clang),
  Windows (MSVC + MinGW-w64).

---

## 10. Mitigation of Other Attacks

Mitigation of Other Attacks is rated **N/A** at overall Level 1. The
following design properties are documented as good-practice
implementations and not as claims against §7.11 of ISO/IEC 19790.

### 10.1 Side-channel resistance

- **Constant-time comparison** of all key, MAC, and shared-secret
  buffers via `qudo_memcmp_ct()`
  (`src/qudo_pqc_platform.c`) — used inside
  POST verification (`src/qudo_pqc_post.c`),
  the integrity check, and PCT comparison.
- **Implicit rejection** in ML-KEM decapsulation: invalid ciphertext
  produces a pseudorandom shared secret per FIPS 203 §7.3 (Algorithm 18),
  preventing decryption-failure timing leaks. Exercised by the
  ML-KEM-512 POST KAT.
- **Fixed-time polynomial arithmetic** in the vendored
  `mlkem-native` / `mldsa-native` reference (both reference C and AVX2 /
  NEON dispatched paths).
- **No secret-dependent branches** in the wrapper or DRBG layers
  (exercised under Valgrind / memcheck during CI).

### 10.2 DRBG security

- AES-256 CTR_DRBG per SP 800-90A §10.2.1 (no derivation function;
  full-block seed input).
- Seeded from an OS noise source consumed through the ESV
  vendor-affirmation pathway (§7.6).
- SP 800-90B continuous health tests (RCT, APT) applied on every raw
  entropy fetch.
- Automatic reseed after a compile-time-constant interval of 2²⁰
  generate calls; this value is fixed in the FIPS module and not
  operator-tunable.
- AES-256-ECB is the underlying block function of the CTR_DRBG. It is
  ACVP-tested as a prerequisite component of the DRBG, but it is **not
  offered as a standalone approved service** — the module exposes no
  general-purpose AES encrypt/decrypt interface. AES correctness is
  therefore covered functionally by the CTR_DRBG-AES-256 KAT (POST
  test #4) and by ACVP DRBG testing; no independent AES CAST is
  required (FIPS 140-3 IG 10.3.A).

---

## Appendix A — Approved algorithms

ACVP certificate numbers will be filled in after submission; see
`certification-package/02-algorithms/acvp-certificates/`.

| Algorithm           | Standard | NIST security category | CAVP cert # |
| ------------------- | -------- | ---------------------- | ----------- |
| ML-KEM-512          | FIPS 203 | 1                      | _pending_   |
| ML-KEM-768          | FIPS 203 | 3                      | _pending_   |
| ML-KEM-1024         | FIPS 203 | 5                      | _pending_   |
| ML-DSA-44           | FIPS 204 | 2                      | _pending_   |
| ML-DSA-65           | FIPS 204 | 3                      | _pending_   |
| ML-DSA-87           | FIPS 204 | 5                      | _pending_   |
| SLH-DSA-SHA2-128{s,f}  | FIPS 205 | 1                   | _pending_   |
| SLH-DSA-SHA2-192{s,f}  | FIPS 205 | 3                   | _pending_   |
| SLH-DSA-SHA2-256{s,f}  | FIPS 205 | 5                   | _pending_   |
| SLH-DSA-SHAKE-128{s,f} | FIPS 205 | 1                   | _pending_   |
| SLH-DSA-SHAKE-192{s,f} | FIPS 205 | 3                   | _pending_   |
| SLH-DSA-SHAKE-256{s,f} | FIPS 205 | 5                   | _pending_   |
| AES-256 CTR_DRBG    | SP 800-90A | n/a (DRBG)           | _pending_   |
| HMAC-SHA-256        | FIPS 198-1 | n/a (integrity-only) | _pending_   |
| SHA-256             | FIPS 180-4 | n/a (digest)         | _pending_   |
| SHA-3 family used internally | FIPS 202 | n/a (used by ML-KEM/ML-DSA/SLH-DSA per spec) | _pending_ |

NIST Category 4 does not exist in the PQC standardisation; the
category column uses 1 / 2 / 3 / 5 only.

## Appendix B — API-to-service mapping

| Function                              | Service                                            |
| ------------------------------------- | -------------------------------------------------- |
| `QUDO_KEM_keypair()`                  | Key Generation                                     |
| `QUDO_KEM_keypair_from_seed()`        | Key Generation (deterministic)                     |
| `QUDO_KEM_encaps()`                   | Key Encapsulation                                  |
| `QUDO_KEM_encaps_derand()`            | Key Encapsulation (deterministic)                  |
| `QUDO_KEM_decaps()`                   | Key Decapsulation                                  |
| `QUDO_KEM_free()`                     | SSP Zeroization                                    |
| `QUDO_MLDSA_keypair()`                | Key Generation                                     |
| `QUDO_MLDSA_sign()`                   | Signature Generation                               |
| `QUDO_MLDSA_sign_internal()`          | Signature Generation (internal, deterministic / external-mu) |
| `QUDO_MLDSA_verify()`                 | Signature Verification                             |
| `QUDO_MLDSA_verify_internal()`        | Signature Verification (internal)                  |
| `QUDO_MLDSA_free()`                   | SSP Zeroization                                    |
| `QUDO_SLHDSA_keypair()`               | Key Generation                                     |
| `QUDO_SLHDSA_sign()`                  | Signature Generation                               |
| `QUDO_SLHDSA_verify()`                | Signature Verification                             |
| `QUDO_SLHDSA_free()`                  | SSP Zeroization                                    |
| `qudo_pqc_init()`                     | Module Initialization (non-approved support)       |
| `qudo_pqc_self_test()`                | On-demand POST (non-approved support)              |
| `qudo_pqc_run_all_casts()`            | On-demand CAST (non-approved support)              |
| `qudo_pqc_is_running()`               | Status query                                       |
| `qudo_pqc_get_state()`                | Status query                                       |
| `qudo_pqc_is_fips()`                  | Status query                                       |
| `qudo_pqc_is_fips_approved()`         | Status query                                       |
| `qudo_pqc_get_security_level()`       | Status query                                       |
| `qudo_pqc_set_min_security_level()`   | Control input                                      |
| `qudo_pqc_get_min_security_level()`   | Status query                                       |
| `qudo_pqc_get_cast_status()`          | Status query                                       |
| `qudo_pqc_post_drbg_kat_passed()`     | Status query (granular POST)                       |
| `qudo_pqc_post_integrity_passed()`    | Status query (granular POST)                       |
| `qudo_pqc_version()`                  | Status query (library version)                     |
| `qudo_fips_ind_is_approved()`         | Status query (per-operation indicator)             |
| `qudo_fips_ind_check_operation()`     | Status query (combined indicator)                  |

---

## 11. Related Documents

| Document                                             | Maps to                  | Description                                                |
| ---------------------------------------------------- | ------------------------ | ---------------------------------------------------------- |
| [OPERATING_ENVIRONMENT.md](OPERATING_ENVIRONMENT.md) | ISO 19790 §7.6           | Tested platforms, dependencies, entropy claims             |
| [LIFE_CYCLE.md](LIFE_CYCLE.md)                       | ISO 19790 §7.11          | FSM, source inventory, configuration management, delivery  |
| [CRYPTO_OFFICER_GUIDANCE.md](CRYPTO_OFFICER_GUIDANCE.md) | ISO 19790 §7.11.6     | Installation, verification, error recovery                 |
| [USER_GUIDANCE.md](USER_GUIDANCE.md)                 | ISO 19790 §7.11.7        | API usage, approved algorithms, key handling               |
| [FIPS.md](FIPS.md)                                   | —                        | Runtime FIPS overview and quick-start                      |
| [FIPS_BUILD_GUIDE.md](FIPS_BUILD_GUIDE.md)           | ISO 19790 §7.10–7.11     | Build constraints, boundary enforcement, integrity tooling |
| [VENDOR_EVIDENCE.md](VENDOR_EVIDENCE.md)             | SP 800-140A              | CST-lab evidence index, ESV vendor-affirmation letter      |
| [CAST_MAPPING.md](CAST_MAPPING.md)                   | FIPS 140-3 IG 10.3.A     | CAST → algorithm → test mapping                            |
| [MAINTENANCE.md](../maintenance/MAINTENANCE.md)                     | CMVP                     | Post-validation maintenance plan                           |
| [ALGORITHMS.md](../reference/ALGORITHMS.md)                       | —                        | Parameter-set inventory with sizes and selection           |
| [API.md](../reference/API.md)                                     | —                        | Full public C API reference                                |
| [ACVP.md](ACVP.md)                                   | —                        | ACVP harness for NIST algorithm validation                 |
