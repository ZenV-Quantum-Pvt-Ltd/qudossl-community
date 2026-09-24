# Operating Environment

FIPS 140-3 (ISO/IEC 19790 Section 7.6) — Operational Environment Requirements.

---

## 1. Supported Platforms

The QUDO PQC Crypto Library has been tested on, and is targeted for CMVP
validation on, the following operational environments. CMVP validation is
**pending** — these environments are not yet CAVP/CMVP-certified:

| Platform | Architecture | OS Version | Compiler |
|----------|-------------|------------|----------|
| Ubuntu 22.04 LTS | x86-64 | 5.15+ kernel | GCC 11+ |
| Ubuntu 24.04 LTS | x86-64, ARM64 | 6.8+ kernel | GCC 13+ |
| RHEL 9.x | x86-64 | 5.14+ kernel | GCC 11+ |
| macOS 14+ (Sonoma) | ARM64 (Apple Silicon) | Darwin 23+ | Apple Clang 15+ |
| Windows Server 2022 | x86-64 | NT 10.0 | MSVC 2019+ (v16.11+) |
| Windows 11 | x86-64 | NT 10.0 | MSVC 2022 (v17+) |

### 1.1 CPU Feature Requirements

| Feature                    | Requirement                                  | Used For                                                |
| -------------------------- | -------------------------------------------- | ------------------------------------------------------- |
| x86-64 baseline            | Required when target is x86-64               | All operations                                          |
| ARM64 baseline (ARMv8-A)   | Required when target is ARM64                | All operations                                          |
| AVX2 (x86-64)              | Optional; runtime-detected and dispatched    | ML-KEM / ML-DSA acceleration                            |
| AES-NI (x86-64)            | Optional; runtime-detected and dispatched    | AES-256 CTR_DRBG (constant-time C fallback otherwise)   |
| NEON (ARM64)               | Optional; runtime-detected and dispatched    | ML-KEM / ML-DSA acceleration                            |
| RDRAND / RDSEED            | Not required and not consumed by the module  | OS entropy source may use if available                  |

Runtime CPU dispatch (`MLKEM_DIST_BUILD=ON`, `MLDSA_DIST_BUILD=ON`) automatically
selects the optimal implementation. Fallback to portable C reference code is
always available.

## 2. Operating Mode

### 2.1 Single-Operator Mode

The module operates in **single-operator mode** as defined by FIPS 140-3
Section 7.4.3. The operating system provides the operational environment:

- Process isolation (virtual memory, separate address spaces)
- File system access controls
- User/group permission model

The module does not implement its own authentication mechanism. The operating
system's access control mechanisms are relied upon to restrict access to the
module and its key material.

### 2.2 Process Isolation

The operating system must provide:

- **Memory protection**: Virtual memory preventing unauthorized access to the
  module's address space from other processes
- **Process separation**: Each process loading the module receives an independent
  instance of the module state machine
- **File permissions**: Module binary files (`libqudo-pqc.so`) must be readable
  only by authorized users/processes

## 3. Dependencies

### 3.1 Build Dependencies

| Dependency | Version | Purpose |
|-----------|---------|---------|
| CMake | 3.15+ | Build system |
| C99 compiler | GCC 10+, Clang 14+, MSVC 2019+ | Compilation |

### 3.2 Runtime Dependencies

| Dependency         | Version                                                                                                      | Purpose                                |
| ------------------ | ------------------------------------------------------------------------------------------------------------ | -------------------------------------- |
| C standard library | **glibc 2.25+ on Linux** (required for `getentropy(3)` primary entropy path); system libc on macOS / Windows | Memory allocation, I/O, entropy access |
| OS kernel          | See platform table above                                                                                     | Entropy source, process isolation      |
| Threading runtime  | pthreads (POSIX) or Win32 thread API                                                                         | Locking around DRBG and state machine  |

Pre-glibc-2.25 Linux distributions fall back to the `getrandom(2)` syscall, and ultimately to `/dev/urandom` if neither is available; the `/dev/urandom` fallback is outside the approved configuration (see §4.2).

**The module has NO runtime dependency on OpenSSL or any external cryptographic
library.** All cryptographic operations (AES-256, CTR-DRBG, HMAC-SHA-256, ML-KEM,
ML-DSA, SLH-DSA) are implemented internally within the module boundary.

## 4. Entropy Source

### 4.1 Internal DRBG

The module implements a NIST SP 800-90A CTR_DRBG (AES-256-CTR) using its own
internal AES implementation. The DRBG is seeded from the platform entropy source
during module initialization.

### 4.2 Platform Entropy

| Platform | Primary source                                       | Reachable fallback                                                                                       | Approved? |
| -------- | ---------------------------------------------------- | -------------------------------------------------------------------------------------------------------- | --------- |
| Linux    | `getentropy(3)` via glibc ≥ 2.25 (`<sys/random.h>`)  | `getrandom(2)` syscall (non-glibc Linux); `/dev/urandom` (last-resort)                                   | Primary and `getrandom` fallback: Yes (validated via ESV vendor affirmation). `/dev/urandom` fallback: **No** — outside the approved configuration. |
| macOS    | `getentropy(3)` (`<sys/random.h>`)                    | —                                                                                                        | Yes |
| Windows  | `BCryptGenRandom(BCRYPT_USE_SYSTEM_PREFERRED_RNG)`   | —                                                                                                        | Yes |

The selection is compile-time per platform — see `raw_platform_entropy()`
in `src/fips/qudo_fips_rand.c` (≈ lines 73-115).

The platform entropy source is used for:
1. Initial DRBG seeding during `qudo_pqc_init()` (or the library constructor)
2. DRBG reseeding after every `2²⁰` generate calls (`RESEED_INTERVAL` in `src/fips/qudo_fips_rand.c`)
3. Key generation randomness (via the internal CTR_DRBG)

The DRBG construction is AES-256 CTR_DRBG **without derivation function** (SP 800-90A §10.2.1); the seed is supplied as a full 48-byte block. See [docs/FIPS_SECURITY_POLICY.md §7.6](FIPS_SECURITY_POLICY.md#76-entropy-source-and-drbg).

### 4.3 Health Tests (SP 800-90B)

Continuous health tests are applied to **raw entropy from the OS source** before the entropy is consumed by the DRBG for seeding or reseeding (not to DRBG output):

- **Repetition Count Test (RCT)** — detects a stuck noise source
  (critical value `rct_critical[FIPS_ENTROPY_H=8] = 4` consecutive repeats; α = 2⁻²⁰)
- **Adaptive Proportion Test (APT)** — detects a biased noise source
  (critical value `apt_critical[8] = 13` matches within a `FIPS_APT_WINDOW = 512`-sample window; α = 2⁻²⁰)
- **Startup test** on `FIPS_ENTROPY_STARTUP_SAMPLES = 1024` raw samples at first use; no entropy is released to the DRBG until startup passes

Critical-value tables live in `src/fips/qudo_fips_rand.c` lines 47-53. A health-test failure emits `QUDO_ERR_DRBG_HEALTH_FAIL` and aborts the seed/reseed attempt; the module transitions to `ERROR` state if no healthy seeded state remains.

## 5. File System Requirements

### 5.1 Module Files

| File                                                                        | Permission             | Purpose                                                              |
| --------------------------------------------------------------------------- | ---------------------- | -------------------------------------------------------------------- |
| `libqudo-pqc.so` / `libqudo-pqc.dylib` / `libqudo-pqc.dll`                  | Read + Execute (0555)  | FIPS cryptographic module                                            |
| `qudofipsmodule.cnf` (external-config deployments only — typically Windows) | Read-only (0444)       | HMAC-SHA-256 integrity manifest; populated by `qudo_fipsinstall -out` |

Linux/macOS embedded-HMAC deployments do not require `qudofipsmodule.cnf`. See [docs/FIPS_BUILD_GUIDE.md §5.1](FIPS_BUILD_GUIDE.md#51-choosing-between-embedded-hmac-and-external-config) for the trade-off and [docs/FIPS_BUILD_GUIDE.md §5.2](FIPS_BUILD_GUIDE.md#52-runtime-pickup-of-the-external-config) for how the application supplies the cnf data to `qudo_pqc_init()` (the library does **not** auto-discover the cnf).

### 5.2 Integrity Protection

The module binary must not be modified after `qudo_fipsinstall` computes the
integrity HMAC. Any modification (stripping, code signing, patching) invalidates
the integrity checksum and prevents the module from loading.

## 6. Security Assumptions

1. The operating system kernel is not compromised
2. The platform entropy source provides cryptographically secure random bytes
3. Process isolation prevents unauthorized memory access
4. The module binary file is protected from unauthorized modification
5. The system clock is not relied upon for security (no time-based operations)
