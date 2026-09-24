# Crypto Officer Guidance

FIPS 140-3 (ISO/IEC 19790 Section 7.11.6) — Crypto Officer Guidance.

This document provides procedures for the Crypto Officer role to install,
configure, verify, and maintain the QUDO PQC Crypto Library in FIPS mode.

---

## 1. Roles

| Role | Responsibilities |
|------|-----------------|
| Crypto Officer | Module installation, integrity verification, configuration, error recovery |
| User | Cryptographic operations via the module API |

At Security Level 1, role authentication is not required. The operating system's
access control mechanisms implicitly separate the roles.

## 2. Installation

### 2.1 Build from Source (FIPS Mode)

From the repository root:

```bash
# Build as FIPS module (Linux / macOS)
cmake -S . -B build-fips -DQUDO_FIPS_MODULE=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-fips -j

# Embed integrity HMAC into the binary (strip, if used, must run first)
./build-fips/tools/qudo_fipsinstall \
    -embed -module build-fips/lib/libqudo-pqc.so      # .dylib on macOS

# macOS only: re-sign as the FINAL step. The embed invalidates the
# Mach-O code signature and macOS refuses to load the library. The
# signature lives outside the integrity-HMAC region, so re-signing
# does not disturb the FIPS integrity check.
codesign --remove-signature build-fips/lib/libqudo-pqc.dylib 2>/dev/null || true
codesign -s - -f build-fips/lib/libqudo-pqc.dylib

# Verify the embedded HMAC
./build-fips/tools/qudo_fipsinstall \
    -verify-embed -module build-fips/lib/libqudo-pqc.so
```

On Windows, use `build_windows.bat --fips`, which builds with `QUDO_FIPS_MODULE=ON` and additionally generates `build-fips\qudofipsmodule.cnf` via `qudo_fipsinstall -out` (Windows uses the external-config integrity path; see [FIPS_BUILD_GUIDE.md §5.1](FIPS_BUILD_GUIDE.md#51-choosing-between-embedded-hmac-and-external-config)).

### 2.2 Install the Module

```bash
# Copy module to system library path
sudo cp build-fips/lib/libqudo-pqc.so /usr/local/lib/
sudo ldconfig

# Copy fipsinstall tool
sudo cp build-fips/tools/qudo_fipsinstall /usr/local/bin/
```

### 2.3 Generate Integrity Configuration (external-config deployments)

For deployments that use an external `qudofipsmodule.cnf` (e.g. Windows) instead
of the embedded HMAC, generate it over the module:

```bash
qudo_fipsinstall \
    -module /usr/local/lib/libqudo-pqc.so \
    -out    /etc/ssl/qudofipsmodule.cnf
```

The application must read this cnf and populate the `qudo_pqc_config_t` integrity
fields (`module_path`, `module_checksum_hex`) before calling `qudo_pqc_init()`.
On Linux/macOS the embedded-HMAC path (§2.1) needs no external configuration.

## 3. Integrity Verification

### 3.1 At Installation Time

After installing the module, verify integrity in the mode appropriate for your deployment:

**Embedded-HMAC deployment (Linux / macOS, recommended):**

```bash
qudo_fipsinstall -verify-embed -module /usr/local/lib/libqudo-pqc.so
```

**External-config deployment (Windows, or any deployment using `qudofipsmodule.cnf`):**

```bash
qudo_fipsinstall -verify \
    -module /path/to/libqudo-pqc.so \
    -in    /etc/ssl/qudofipsmodule.cnf
```

A successful run exits 0 with no error output; a mismatch exits non-zero and prints the failure detail. Full flag reference: [FIPS_BUILD_GUIDE.md §5.3](FIPS_BUILD_GUIDE.md#53-qudo_fipsinstall-command-reference).

### 3.2 At Runtime

The module automatically verifies its own integrity during initialization:

1. HMAC-SHA-256 KAT runs first (verifies HMAC algorithm works)
2. In-memory integrity check computes HMAC of the code region between
   `qudo_fips_module_start` and `qudo_fips_module_end` boundary symbols
3. Computed HMAC compared against the embedded HMAC (patched by `qudo_fipsinstall -embed`)
4. Mismatch transitions to ERROR state

### 3.3 After System Updates

If the operating system updates modify shared library loading behavior or
the module binary is replaced during a package update, re-run:

```bash
qudo_fipsinstall -embed -module /path/to/libqudo-pqc.so
```

**The integrity HMAC must be recomputed after any modification to the binary**
(including strip, code signing, or package rebuild).

## 4. Verifying FIPS Mode

### 4.1 Programmatic Check

```c
#include <qudo_pqc.h>

qudo_pqc_config_t cfg = {0};
cfg.conditional_errors = 1;

if (qudo_pqc_init(&cfg) != 1) {
    /* Module failed initialization — check logs */
    int state = qudo_pqc_get_state();
    /* state == 3 means ERROR */
}

if (qudo_pqc_is_running()) {
    /* Module is in FIPS RUNNING state */
}

if (qudo_pqc_is_fips()) {
    /* Compiled with QUDO_FIPS_MODULE=ON */
}
```

## 5. Self-Test Verification

### 5.1 Power-Up Self-Tests

POST runs automatically during module initialization. Verify via audit log
or self-test callback:

- All KATs pass (keygen, KEM, signature for all algorithm families)
- Integrity check passes
- Module state = RUNNING

### 5.2 On-Demand Self-Test

Trigger a runtime re-test:

```c
int result = qudo_pqc_self_test();   /* 1 = all self-tests passed */
```

### 5.3 Conditional Algorithm Self-Tests (CAST)

CAST runs automatically before first use of each algorithm variant:

| CAST ID | Algorithm | Trigger |
|---------|-----------|---------|
| QUDO_CAST_ML_KEM_512 | ML-KEM-512 | First keygen/encaps/decaps |
| QUDO_CAST_ML_KEM_768 | ML-KEM-768 | First keygen/encaps/decaps |
| QUDO_CAST_ML_KEM_1024 | ML-KEM-1024 | First keygen/encaps/decaps |
| QUDO_CAST_ML_DSA_44 | ML-DSA-44 | First keygen/sign/verify |
| QUDO_CAST_ML_DSA_65 | ML-DSA-65 | First keygen/sign/verify |
| QUDO_CAST_ML_DSA_87 | ML-DSA-87 | First keygen/sign/verify |
| QUDO_CAST_SLH_DSA_SHA2_128 | SLH-DSA-SHA2-128s/f | First keygen/sign/verify |
| QUDO_CAST_SLH_DSA_SHA2_192 | SLH-DSA-SHA2-192s/f | First keygen/sign/verify |
| QUDO_CAST_SLH_DSA_SHA2_256 | SLH-DSA-SHA2-256s/f | First keygen/sign/verify |
| QUDO_CAST_SLH_DSA_SHAKE_128 | SLH-DSA-SHAKE-128s/f | First keygen/sign/verify |
| QUDO_CAST_SLH_DSA_SHAKE_192 | SLH-DSA-SHAKE-192s/f | First keygen/sign/verify |
| QUDO_CAST_SLH_DSA_SHAKE_256 | SLH-DSA-SHAKE-256s/f | First keygen/sign/verify |

## 6. Error Recovery

### 6.1 ERROR State

When the module enters `ERROR`, all cryptographic services are blocked and `qudo_pqc_init()` called from `ERROR` returns 0 without re-arming the state machine (see `src/qudo_pqc_init.c`). There is no automatic exit from `ERROR`.

Recovery requires an explicit Crypto Officer action — either restarting the process (most deployments), or an in-process re-initialization: `qudo_pqc_fini()` (zeroizes the DRBG and clears module callbacks and status) followed by `qudo_pqc_init()`, which re-runs the integrity check and all pre-operational self-tests; no service resumes unless every self-test passes again. If the host process supports `dlclose()` / `FreeLibrary()` (and no other thread holds a reference), the full reload sequence is:

```c
qudo_pqc_fini();    /* zeroizes the DRBG and clears callbacks/status;
                       process-lifetime locks are retained until unload */
/* dlclose(handle) on Linux/macOS; FreeLibrary(hModule) on Windows */
/* dlopen(...) / LoadLibrary(...) again */
qudo_pqc_config_t cfg = {0};
cfg.conditional_errors = 1;
qudo_pqc_init(&cfg);
```

Recovery from `ERROR` requires re-initialising the module: call
`qudo_pqc_fini()` to reset the state to `INIT`, then `qudo_pqc_init()` again. If
the host cannot reload the shared library — typical for long-running daemons that
linked it at startup — process restart is the only supported recovery.

### 6.2 Common Error Causes

| Error | Cause | Resolution |
|-------|-------|-----------|
| Integrity failure | Binary modified after HMAC patching | Re-run `qudo_fipsinstall -embed` |
| KAT failure | Implementation error or memory corruption | Reinstall module from trusted source |
| PCT failure | Key generation produced inconsistent keypair | Retry keygen; persistent failure indicates hardware issue |
| DRBG failure | Entropy source unavailable | Verify OS CSPRNG is operational |

## 7. Audit Logging

The module produces audit events for security-relevant operations. Register a callback to capture events. The actual callback signature (per `include/qudo_pqc_audit.h`) is:

```c
#include <qudo_pqc.h>
#include <qudo_pqc_audit.h>
#include <syslog.h>

void my_audit_cb(qudo_sev_t severity, qudo_err_t code,
                 const char *component, const char *detail,
                 void *cb_arg)
{
    int prio = (severity >= QUDO_SEV_ERROR) ? LOG_ERR
             : (severity == QUDO_SEV_WARN)  ? LOG_WARNING
                                            : LOG_INFO;
    syslog(LOG_AUTH | prio,
           "QUDO FIPS [%s] code=0x%04x: %s",
           component, (unsigned)code, detail ? detail : "");
}

qudo_pqc_config_t cfg = {0};
cfg.conditional_errors = 1;
cfg.audit_cb           = my_audit_cb;
cfg.audit_cb_arg       = NULL;
qudo_pqc_init(&cfg);
```

`component` is one of the string constants `STATE`, `POST`, `INTEGRITY`, `PCT`, `CAST`, `DRBG`, `KEY`, `INDICATOR` — see [FIPS.md §Audit logging](FIPS.md#audit-logging). Events cover state transitions, POST start / per-test pass-or-fail / completion, integrity verification, PCT pass/fail/zeroize, CAST status changes, DRBG seed/reseed/health-test failures, key generation/import/zeroize, and indicator queries that resolve "not approved." Operators SHOULD retain events at `WARN` and above per their logging policy.

## 8. Module Update Procedure

1. Obtain the updated module binary from the vendor.
2. Verify the binary signature / checksum against the vendor's published values.
3. Stop all applications using the module.
4. Replace the module binary in place.
5. Apply binary-rewriting post-processing (strip; on Windows also
   Authenticode/signtool, since the external cnf hashes the file as
   shipped) **before** the integrity step.
6. Recompute the integrity HMAC:
   - **Embedded:** `qudo_fipsinstall -embed -module /path/to/libqudo-pqc.so`,
     then on macOS re-sign as the final step:
     `codesign --remove-signature <lib> 2>/dev/null || true && codesign -s - -f <lib>`
     (use a real signing identity with `--timestamp --options runtime`
     instead of ad-hoc for distributed binaries)
   - **External cnf:** `qudo_fipsinstall -module /path/to/libqudo-pqc.so -out /path/to/qudofipsmodule.cnf`
     (all post-processing, including signing, must be complete first)
7. Verify the result:
   - **Embedded:** `qudo_fipsinstall -verify-embed -module /path/to/libqudo-pqc.so`
   - **External cnf:** `qudo_fipsinstall -verify -module /path/to/libqudo-pqc.so -in /path/to/qudofipsmodule.cnf`
8. Restart applications.
9. Confirm the module loads successfully — check that the audit log shows POST passing and the state transitions to `RUNNING`.
