# QUDO PQC

Production C implementation of NIST Post-Quantum Cryptography — [FIPS 203][fips203]
(ML-KEM), [FIPS 204][fips204] (ML-DSA), and [FIPS 205][fips205] (SLH-DSA) — built as a
single, dependency-free library designed for FIPS 140-3 validation.

**Version:** 1.0.0 · **License:** Apache-2.0 AND MIT · See [CHANGELOG.md](CHANGELOG.md)
for in-development changes.

## Architecture

QUDO PQC ships as a single library, `libqudo-pqc`, that aggregates three algorithm
families. Each family is a wrapper around a formally-verified upstream reference
(`mlkem-native`, `mldsa-native`, `slhdsa-native`) vendored in-tree and pinned to
specific upstream commits — see [docs/UPSTREAM_MAINTENANCE.md](docs/maintenance/UPSTREAM_MAINTENANCE.md).

```
Application
    |
    v
libqudo-pqc  ──  qudo_pqc.h            (module lifecycle, FIPS state, audit)
    │            mlkem_wrapper.h       ──> mlkem-native    (FIPS 203)
    │            mldsa_wrapper.h       ──> mldsa-native    (FIPS 204)
    └──          slhdsa_wrapper.h      ──> slhdsa-native   (FIPS 205)
                 │
                 └──> internal AES-256-CTR-DRBG, HMAC-SHA-256, SHA-2, SHA-3
                      (no OpenSSL or external crypto)
```

An OpenSSL 3.x provider that exposes all three families via the EVP API, plus 22
additional hybrid/composite combinations, lives in the separate `qudo-provider`
repository.

## Algorithms

**18 FIPS-approved parameter sets.** Full sizes and selection guidance in
[docs/ALGORITHMS.md](docs/reference/ALGORITHMS.md).

| Family   | FIPS | Parameter Sets                                              |
| -------- | ---- | ----------------------------------------------------------- |
| ML-KEM   | 203  | ML-KEM-512, ML-KEM-768, ML-KEM-1024                         |
| ML-DSA   | 204  | ML-DSA-44, ML-DSA-65, ML-DSA-87                             |
| SLH-DSA  | 205  | SLH-DSA-{SHA2,SHAKE}-{128,192,256}{s,f}  (12 sets)          |

Runtime CPU dispatch picks AVX2 on x86_64 and NEON on ARM64 automatically.

## Quick Start

```bash
./build.sh
cd build && ctest --output-on-failure
```

Or the manual CMake path:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

See [INSTALL.md](INSTALL.md) for prerequisites (CMake 3.15+, C99 compiler) and
platform notes. Windows builds use [build_windows.bat](build_windows.bat).

## Build Options

| Option                      | Default | Purpose                                                                |
| --------------------------- | ------- | ---------------------------------------------------------------------- |
| `QUDO_FIPS_MODULE`          | OFF     | Build as FIPS 140-3 module (POST, KATs, integrity, state machine)      |
| `QUDO_PQC_BUILD_TESTS`      | ON      | Build the library test suite                                           |
| `BUILD_ACVP`                | OFF     | Build ACVP runners for NIST algorithm validation                       |
| `QUDO_BUILD_EMBEDDED_OBJ`   | OFF     | Static archive + partial-link object for embedded/firmware (FIPS only) |
| `BUILD_SHARED_LIBS`         | ON      | Shared (`.so`/`.dylib`/`.dll`) vs static library                       |
| `ENABLE_SANITIZERS`         | OFF     | ASan + UBSan                                                           |
| `ENABLE_MSAN`               | OFF     | MemorySanitizer (Clang only; mutually exclusive with the others)       |
| `ENABLE_TSAN`               | OFF     | ThreadSanitizer (mutually exclusive with the others)                   |
| `ENABLE_COVERAGE`           | OFF     | gcov/lcov instrumentation; see [coverage.sh](coverage.sh)              |

## API at a Glance

Each algorithm family exposes two C APIs:

- **Object API** — heap-allocated handles, runtime algorithm selection, automatic
  resource management. Use this for general application code.
- **Direct API** — zero-allocation, caller-provided buffers, compile-time
  parameter selection. Use this for embedded and constrained systems.

```c
#include "qudo_pqc.h"
#include "mlkem_wrapper.h"

if (!qudo_pqc_init(NULL))                          // 1 = success, 0 = failure
    return EXIT_FAILURE;

QUDO_KEM *kem = QUDO_KEM_new("ML-KEM-768");
uint8_t pk[1184], sk[2400], ct[1088], ss_a[32], ss_b[32];

QUDO_KEM_keypair(kem, pk, sk);                     // wrappers: 0 = success
QUDO_KEM_encaps(kem, ct, ss_a, pk);
QUDO_KEM_decaps(kem, ss_b, ct, sk);                // ss_a == ss_b

QUDO_KEM_free(kem);
```

The full API for all three families — including DER/PEM serialization, deterministic
and hedged signing, context strings, and the v1→v2 migration notes — is in
[docs/API.md](docs/reference/API.md).

## FIPS 140-3 Module

QUDO PQC is structured for FIPS 140-3 (CMVP) validation. With
`-DQUDO_FIPS_MODULE=ON` the build adds:

- **Module integrity** — HMAC-SHA-256 over a contiguous `.text` region between
  the boundary sentinels, verified at module load and at every explicit init.
- **Power-On Self-Tests** — KATs for the primitives (SHA-256, HMAC-SHA-256,
  AES-256 CTR_DRBG) plus one representative per algorithm family (ML-KEM-512,
  ML-DSA-65, SLH-DSA-SHA2-128f, SLH-DSA-SHAKE-128f). Scope follows FIPS 140-3
  IG 10.3.A §15 Note26 and §16 Note29/Note30; per-parameter-set functional
  coverage lives outside POST in [tests/](tests) and [acvp/](acvp).
- **Pairwise Consistency Tests** — encaps/decaps PCT (ML-KEM) and sign/verify
  PCT (ML-DSA, SLH-DSA) on every generated and imported keypair, with
  fail-zeroize.
- **State machine** — explicit `INIT → SELFTEST → RUNNING / ERROR` transitions
  enforced before any cryptographic operation. `ERROR` is terminal; recovery
  requires shared-library reload.
- **Per-operation FIPS indicators** — `qudo_fips_ind_t` embedded in every
  algorithm context, combined with module state and DRBG readiness.
- **Audit log** — structured callback for `STATE`, `POST`, `INTEGRITY`, `PCT`,
  `CAST`, `DRBG`, `KEY`, and `INDICATOR` events.
- **Security-level enforcement** — `qudo_pqc_set_min_security_level()` maps each
  parameter set to its NIST category (1, 2, 3, or 5 — no Cat 4 in PQC).
- **Defined module boundary** — pinned by linker-ordered
  [src/qudo_fips_boundary_start.c](src/qudo_fips_boundary_start.c) and
  [src/qudo_fips_boundary_end.c](src/qudo_fips_boundary_end.c).

After building, finalize the integrity HMAC. Two modes are supported:

```bash
# Embedded HMAC — recommended for Linux/macOS. Patches the HMAC into a
# reserved slot inside the library; qudo_pqc_init(NULL) needs no extra config.
./build/tools/qudo_fipsinstall -embed -module build/lib/libqudo-pqc.so

# External config — required on Windows (build_windows.bat does this
# automatically) and useful when policy mandates a separately-signed manifest.
# The application must read the cnf and populate qudo_pqc_config_t fields
# before calling qudo_pqc_init() — the library does NOT auto-discover it.
./build/tools/qudo_fipsinstall -module build/lib/libqudo-pqc.so \
                               -out   build/qudofipsmodule.cnf
```

Validation-grade documents — security policy, crypto-officer and user guidance,
operating environment, life-cycle, vendor evidence — live under [docs/](docs).
See [docs/FIPS_BUILD_GUIDE.md](docs/fips/FIPS_BUILD_GUIDE.md) for the full
per-platform enforcement matrix and verification checklist.

## Examples

Each sub-library carries runnable examples:

- [qudo-mlkem/examples/](qudo-mlkem/examples) — `simple_kem`, `speed_mlkem`,
  client/server (object + direct APIs), DER/PEM round-trip, heap-allocation demo,
  benchmark vs. reference.
- [qudo-mldsa/examples/](qudo-mldsa/examples) — deterministic keygen and signing,
  context strings, prehash (SHA-256), external mu, concat format, speed.
- [qudo-slhdsa/examples/](qudo-slhdsa/examples) — `simple_sign`, DER/PEM,
  `speed_slhdsa`.

## Testing

```bash
ctest --test-dir build --output-on-failure
```

30+ test binaries in [tests/](tests) cover POST, integrity path-pinning, PCT
fail-zeroize, DRBG limits and CAVP vectors, concurrent threading (TSan),
OpenSSL PEM interop, parameter-set bounds, sign/verify roundtrips, and full
wrapper-level coverage. See [docs/TESTING.md](docs/development/TESTING.md).

ACVP test harness for NIST algorithm validation is in [acvp/](acvp) — build
with `-DBUILD_ACVP=ON` and run via [acvp/run_acvp.sh](acvp/run_acvp.sh). See
[docs/ACVP.md](docs/fips/ACVP.md).

A full local CI pass (build + sanitizers + tests + coverage) is wrapped by
[run_all_tests.sh](run_all_tests.sh).

## Installation

```bash
cmake --install build --prefix /usr/local
```

This installs the library to `lib/`, headers to `include/qudo-pqc/`, and CMake
package config to `lib/cmake/qudo-pqc/`. Consume from another project with:

```cmake
find_package(qudo-pqc 1.0 REQUIRED)
target_link_libraries(myapp PRIVATE qudo::qudo-pqc)
```

## Documentation

### Using the library (`docs/reference/`)

- [INSTALL.md](INSTALL.md) — prerequisites, build, install (all platforms)
- [Algorithms & parameter sets](docs/reference/ALGORITHMS.md) — all 18 sets, sizes, selection
- [C API reference](docs/reference/API.md) — complete API for all three families
- [Usage examples](docs/reference/EXAMPLES.md) — task-oriented cookbook
- [Platform notes — Unix](docs/reference/NOTES_UNIX.md) · [Windows](docs/reference/NOTES_WINDOWS.md)

### Architecture & design (`docs/design/`)

- [Architecture & Design Document](docs/design/ARCHITECTURE.md) — authoritative, source-derived design

### Security analysis (`docs/security/`)

- [Threat Model](docs/security/THREAT_MODEL.md) — assets, adversaries, mitigations
- [SSP & Key Management](docs/security/SSP_KEY_MANAGEMENT.md) — keys/CSPs, zeroization
- [Side-Channel & Constant-Time Analysis](docs/security/SIDE_CHANNEL_ANALYSIS.md)

### FIPS 140-3 (`docs/fips/`)

- [FIPS compliance overview](docs/fips/FIPS.md) — self-tests, state machine
- [FIPS Security Policy](docs/fips/FIPS_SECURITY_POLICY.md) — the formal Security Policy
- [Finite State Model](docs/fips/FINITE_STATE_MODEL.md) — states, transitions, output inhibition
- [FIPS build guide](docs/fips/FIPS_BUILD_GUIDE.md) — build constraints, integrity
- [Crypto Officer Guidance](docs/fips/CRYPTO_OFFICER_GUIDANCE.md) · [User Guidance](docs/fips/USER_GUIDANCE.md)
- [Operating Environment](docs/fips/OPERATING_ENVIRONMENT.md) · [Life-Cycle Assurance](docs/fips/LIFE_CYCLE.md)
- [Entropy Source Assessment (SP 800-90B)](docs/fips/ENTROPY_ASSESSMENT.md) — noise source, health tests, DRBG, ESV
- [CAST mapping](docs/fips/CAST_MAPPING.md) — conditional algorithm self-tests
- [ACVP test runner](docs/fips/ACVP.md) — NIST CAVP algorithm validation
- [Vendor evidence](docs/fips/VENDOR_EVIDENCE.md) · [Requirements traceability](docs/fips/REQUIREMENTS_TRACEABILITY.md)

### Development (`docs/development/`)

- [Development guide](docs/development/DEVELOPMENT.md) — dev workflow, FIPS build constraints
- [Test suite overview](docs/development/TESTING.md) — suite, test types, ACVP, self-tests
- [Debugging](docs/development/DEBUGGING.md) — sanitizers, valgrind, coverage, fuzzing
- [Troubleshooting](docs/development/TROUBLESHOOTING.md) — symptom → cause → fix
- [Porting & embedded](docs/development/PORTING.md) — cross-compile, embedded, entropy hookup

### Maintenance & release (`docs/maintenance/`)

- [Post-validation maintenance](docs/maintenance/MAINTENANCE.md) · [Versioning policy](docs/maintenance/VERSIONING.md)
- [Upstream maintenance](docs/maintenance/UPSTREAM_MAINTENANCE.md) — `*-native` tracking
- [CHANGELOG.md](CHANGELOG.md)

For OpenSSL provider documentation (TLS integration, hybrid algorithms, provider
configuration) see the separate `qudo-provider` repository.

## Support

- **Bugs and feature requests** — [GitHub Issues][github-issues]
- **Security vulnerabilities** — see [SECURITY.md](SECURITY.md) for the
  responsible-disclosure policy. Do not file security issues publicly.

## Contributing

Pull requests are welcome. Please read [CONTRIBUTING.md](CONTRIBUTING.md) and
follow the [Code of Conduct](CODE_OF_CONDUCT.md). All contributions must include
SPDX license headers and pass the lint + sanitizer matrix configured in
[.github/](.github).

## Legalities

QUDO PQC implements cryptographic algorithms. The use, import, and export of
cryptographic software may be restricted by the laws of your country. Check the
export-control regulations that apply in your jurisdiction before using or
distributing this software.

The NIST post-quantum algorithms implemented here (ML-KEM, ML-DSA, SLH-DSA) are
published standards. The formally-verified upstream references vendored under
`qudo-mlkem/mlkem-native/`, `qudo-mldsa/mldsa-native/`, and
`qudo-slhdsa/slhdsa-native/` carry their own licenses — see the `LICENSE` file
inside each directory and the aggregated [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Copyright (c) 2024-2026 QUDO Security. Licensed under Apache-2.0 AND MIT — see
[LICENSE](LICENSE).

[fips203]: https://csrc.nist.gov/pubs/fips/203/final
[fips204]: https://csrc.nist.gov/pubs/fips/204/final
[fips205]: https://csrc.nist.gov/pubs/fips/205/final
[github-issues]: https://github.com/ZenVInnovations/qudo-pqc-lib/issues
