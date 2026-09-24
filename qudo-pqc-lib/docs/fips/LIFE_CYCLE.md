# Life-Cycle Assurance

FIPS 140-3 (ISO/IEC 19790 Section 7.11) — Life-Cycle Assurance Requirements.

---

## 1. Finite State Model

### 1.1 Module States

| State | Value | Description |
|-------|-------|-------------|
| INIT | 0 | Module loaded, not yet initialized |
| SELFTEST | 1 | Power-up self-tests in progress |
| RUNNING | 2 | All POST passed, cryptographic services available |
| ERROR | 3 | Fatal failure, all services permanently blocked |

### 1.2 State Transition Diagram

```
                        Module Load
                            │
                            ▼
                    +---------------+
                    |     INIT      |
                    +---------------+
                            │
                Library constructor on Linux/macOS,
                DllMain on Windows,
                or first qudo_pqc_init() call
                            │
                            ▼
                    +---------------+
                    |   SELFTEST    |
                    +---------------+
                     /             \
              POST passes        POST fails
                   /                 \
                  ▼                   ▼
        +---------------+    +---------------+
        |    RUNNING    |    |     ERROR     |
        +---------------+    +---------------+
                │                     ▲
       PCT-on-keygen failure          │
       (conditional_errors=1) ────────┘
       or DRBG health failure
```

### 1.3 State Transition Rules

| From     | To       | Trigger                                                                                                                    | Condition                                                                |
| -------- | -------- | -------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------ |
| INIT     | SELFTEST | Shared-library constructor (`__attribute__((constructor))` on Linux/macOS; `DllMain(DLL_PROCESS_ATTACH)` on Windows) **or** first explicit `qudo_pqc_init()` call | Triggered whichever happens first; see `src/qudo_pqc_init.c` |
| SELFTEST | RUNNING  | POST completes                                                                                                             | Module integrity check + all primitive and algorithm KATs pass            |
| SELFTEST | ERROR    | POST fails                                                                                                                 | Any integrity check, KAT, or DRBG seed/health test fails                  |
| RUNNING  | ERROR    | PCT-on-keygen failure (`conditional_errors = 1`, FIPS-required); or DRBG health-test failure during reseed; or any explicit fatal-error path | PCT-on-import failures are transient and do **not** transition state      |
| ERROR    | (none)   | Terminal                                                                                                                   | `qudo_pqc_init()` from ERROR returns 0; recovery requires shared-library reload (process restart) |

### 1.4 Data Output Inhibition

No cryptographic output is produced until the module reaches RUNNING state.
All public API functions check `qudo_pqc_is_running()` before processing.
During SELFTEST, internal cryptographic operations are permitted only for
self-test execution.

## 2. Configuration Management

### 2.1 Source Code Management

| Aspect | Tool/Process |
|--------|-------------|
| Version control | Git |
| Repository hosting | GitHub (private repository) |
| Branch protection | `main` branch requires pull request review |
| Code review | Mandatory review before merge |

### 2.2 Version Numbering

The module uses semantic versioning: `MAJOR.MINOR.PATCH`

- **MAJOR**: Incompatible API changes or re-certification required
- **MINOR**: New algorithms or features (backward compatible)
- **PATCH**: Bug fixes, security patches

The version is embedded in the module and queryable via `qudo_pqc_version()`.

### 2.3 Build Reproducibility

FIPS builds require reproducible output for integrity verification. The build system enforces:

- Fixed compiler version and flags (documented per platform in [FIPS_BUILD_GUIDE.md](FIPS_BUILD_GUIDE.md))
- **LTO forbidden**: the CMake configure step emits `FATAL_ERROR` if `CMAKE_INTERPROCEDURAL_OPTIMIZATION` is on while `QUDO_FIPS_MODULE=ON` — LTO would reorder functions across the integrity boundary
- **`-fno-function-sections`** added per target on GCC / Clang / Apple Clang / MinGW; the configure step emits `FATAL_ERROR` if `-ffunction-sections` is in `CMAKE_C_FLAGS`
- On Linux, the supplemental linker script [cmake/fips_module.ld](../../cmake/fips_module.ld) pins the boundary objects with `KEEP(...)` sections; a POST_BUILD coverage check ([cmake/check_fips_boundary.sh](../../cmake/check_fips_boundary.sh)) verifies every FIPS-core symbol resolves inside the integrity region
- Restricted symbol export via the linker version script `config/libqudo-pqc.map`
- Post-build integrity HMAC: `qudo_fipsinstall -embed` (Linux/macOS) or `qudo_fipsinstall -out qudofipsmodule.cnf` (Windows external-config). See [FIPS_BUILD_GUIDE.md §5](FIPS_BUILD_GUIDE.md#5-post-build-integrity-qudo_fipsinstall).

## 3. Source File Inventory

### 3.1 Files Within FIPS Module Boundary

| Directory | Files | Purpose |
|-----------|-------|---------|
| `include/` | `qudo_pqc.h`, `qudo_pqc_audit.h`, `qudo_pqc_indicator.h`, `qudo_pqc_err.h`, `qudo_pqc_platform.h`, `qudo_fipskey.h` | Public API headers |
| `src/` | `qudo_pqc_init.c`, `qudo_pqc_post.c`, `qudo_pqc_pct.c`, `qudo_pqc_integrity.c`, `qudo_pqc_embedded_hmac.c`, `qudo_pqc_platform.c`, `qudo_pqc_audit.c`, `qudo_pqc_indicator.c`, `qudo_pqc_security.c`, `qudo_pqc_utils.c` | FIPS infrastructure |
| `src/fips/` | `qudo_fips_aes.c`, `qudo_fips_aes_ct.c`, `qudo_fips_aes_ni.c`, `qudo_fips_ctrdrbg.c`, `qudo_fips_hmac.c`, `qudo_fips_rand.c` | Internal crypto primitives |
| `qudo-mlkem/` | ML-KEM wrapper + mlkem-native | FIPS 203 implementation |
| `qudo-mldsa/` | ML-DSA wrapper + mldsa-native | FIPS 204 implementation |
| `qudo-slhdsa/` | SLH-DSA wrapper + slhdsa-native | FIPS 205 implementation |

### 3.2 Files Outside FIPS Module Boundary

| Component             | Location                                   | Purpose                                                                  |
| --------------------- | ------------------------------------------ | ------------------------------------------------------------------------ |
| `qudoprovider.so`     | **Separate `qudo-provider` repository**    | OpenSSL 3.x provider (thin wrapper); consumes this module via public API |
| `qudo_fipsinstall`    | `tools/qudo_fipsinstall.c`                 | Standalone build tool for integrity HMAC computation; not loaded at runtime |
| `tests/`              | `tests/`                                   | Test suite (not part of validated module)                                |
| Boundary anchor files | `src/qudo_fips_boundary_start.c`, `..._end.c` | Tiny sentinel objects; their *symbols* mark the boundary inside the module, but the .c files themselves are part of the build-time infrastructure |

## 4. Development Process

### 4.1 Design Phase

- Algorithm implementations follow NIST FIPS 203/204/205 specifications
- FIPS infrastructure follows OpenSSL FIPS provider patterns (validated reference)
- Security requirements documented before implementation

### 4.2 Implementation Phase

- C99 standard for maximum portability
- No external cryptographic dependencies within module boundary
- All sensitive data paths include zeroization
- Cross-platform compatibility (Linux, macOS, Windows)

### 4.3 Testing Phase

- Unit tests for all algorithms and FIPS infrastructure
- Known Answer Tests (KAT) with NIST test vectors
- Pairwise Consistency Tests for all keygen operations
- Integrity verification tests
- AddressSanitizer and UBSan for memory safety
- Valgrind for leak detection
- Constant-time verification for timing resistance

### 4.4 Delivery

Release packages include:

1. Source-code archive (for building from source on any supported OE)
2. Pre-built binaries per OE:
   - Linux / macOS: `libqudo-pqc.{so,dylib}` with **embedded** integrity HMAC patched in by `qudo_fipsinstall -embed`
   - Windows: `qudo-pqc.dll` plus an accompanying `qudofipsmodule.cnf` generated by `qudo_fipsinstall -out` (Windows uses the external-config path because MSVC `/OPT:ICF` folding and stripped COFF symbol tables make in-binary patching unreliable — see [FIPS_BUILD_GUIDE.md §5.1](FIPS_BUILD_GUIDE.md#51-choosing-between-embedded-hmac-and-external-config))
3. The `qudo_fipsinstall` tool itself, for re-`embed` / re-`out` after Crypto-Officer-side post-processing (codesign, signtool, strip)
4. Documentation package (this document set)

## 5. Installation

See [Crypto Officer Guidance](CRYPTO_OFFICER_GUIDANCE.md) for detailed
installation procedures.

## 6. Decommissioning

To securely decommission the module:

1. Unload the module from all running processes
2. Delete the module binary (`libqudo-pqc.so`)
3. Delete the integrity configuration (`qudofipsmodule.cnf`)
4. Securely erase any key material stored by applications using the module
5. Remove the module from system configuration files
