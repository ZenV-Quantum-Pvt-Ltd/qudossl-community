# FIPS 140-3 Compliance

## Overview

QUDO PQC targets FIPS 140-3 (ISO/IEC 19790:2012) validation as a
**software cryptographic module at overall Security Level 1**. When built
with `-DQUDO_FIPS_MODULE=ON`, the library compiles in:

- Finite state machine (`INIT → SELFTEST → RUNNING` / `ERROR`)
- Pre-operational self-tests (POST): module integrity + algorithm and
  primitive KATs
- Conditional self-tests: pairwise consistency tests (PCT) on every
  keygen and key import; conditional algorithm self-tests (CAST) on
  first use of each variant
- Internal AES-256 CTR_DRBG (SP 800-90A) with SP 800-90B health tests
  on the OS-supplied entropy
- Per-operation FIPS approval indicator
- Structured audit log for security-relevant events
- Secure SSP zeroization on key destroy and on PCT failure

The formal Security Policy lives in
[FIPS_SECURITY_POLICY.md](FIPS_SECURITY_POLICY.md); build-time
requirements are in [FIPS_BUILD_GUIDE.md](FIPS_BUILD_GUIDE.md); operator
deployment requirements are in
[OPERATING_ENVIRONMENT.md](OPERATING_ENVIRONMENT.md).

## Approved Algorithms

All 18 parameter sets implemented are FIPS-approved.

| Family   | FIPS | Parameter sets                                                  |
| -------- | ---- | --------------------------------------------------------------- |
| ML-KEM   | 203  | ML-KEM-512, ML-KEM-768, ML-KEM-1024                             |
| ML-DSA   | 204  | ML-DSA-44, ML-DSA-65, ML-DSA-87                                 |
| SLH-DSA  | 205  | SLH-DSA-{SHA2,SHAKE}-{128,192,256}{s,f}  (12 sets)              |

Mapping to NIST security categories (1/2/3/5 — Category 4 does not
exist in the PQC standardisation):

| Category | ML-KEM     | ML-DSA    | SLH-DSA                                         |
| -------- | ---------- | --------- | ----------------------------------------------- |
| 1        | ML-KEM-512  | —         | SLH-DSA-{SHA2,SHAKE}-128{s,f}                   |
| 2        | —           | ML-DSA-44 | —                                               |
| 3        | ML-KEM-768  | ML-DSA-65 | SLH-DSA-{SHA2,SHAKE}-192{s,f}                   |
| 5        | ML-KEM-1024 | ML-DSA-87 | SLH-DSA-{SHA2,SHAKE}-256{s,f}                   |

## Module State Machine

```
Module Load → INIT → SELFTEST → RUNNING
                         │
                         └── any POST failure ──→ ERROR (no automatic exit)
```

| State      | Constant                       | Meaning                                                       |
| ---------- | ------------------------------ | ------------------------------------------------------------- |
| `INIT`     | `QUDO_PQC_STATE_INIT` (0)      | Module loaded, `qudo_pqc_init()` not yet called               |
| `SELFTEST` | `QUDO_PQC_STATE_SELFTEST` (1)  | Integrity check + POST KATs running                           |
| `RUNNING`  | `QUDO_PQC_STATE_RUNNING` (2)   | All POST passed; cryptographic services available             |
| `ERROR`    | `QUDO_PQC_STATE_ERROR` (3)     | Fatal failure; all cryptographic operations blocked; no automatic exit |

**No cryptographic output is produced until the module reaches
`RUNNING`.** State queries:

```c
int qudo_pqc_get_state(void);            /* one of the constants above */
int qudo_pqc_is_running(void);
int qudo_pqc_is_self_testing(void);
int qudo_pqc_is_running_or_selftest(void);
```

**There is no automatic exit from `ERROR`.** Calling `qudo_pqc_init()`
while the module is in `ERROR` returns `0` and does *not* re-arm the
state machine. Recovery requires an explicit operator action: either
unloading and reloading the shared library (process restart in most
deployments), or `qudo_pqc_fini()` — which zeroizes the DRBG and clears
module callbacks and status — followed by `qudo_pqc_init()`, which
re-runs the integrity check and all pre-operational self-tests before
any service resumes. See `src/qudo_pqc_init.c`.

## Initialization

### Return convention

`qudo_pqc_init()` returns `1` on success, `0` on failure. The doc
examples below treat `!qudo_pqc_init(&cfg)` as the failure path,
consistent with that convention.

### Configuration struct

```c
typedef struct qudo_pqc_config_st {
    qudo_st_event_cb self_test_cb;    void *self_test_cb_arg;   /* POST progress */
    qudo_error_cb    error_cb;        void *error_cb_arg;       /* Error callback */
    qudo_io_open_fn  io_open;                                   /* Optional: app-supplied */
    qudo_io_read_fn  io_read;                                   /* file I/O for reading   */
    qudo_io_close_fn io_close;                                  /* integrity config       */
    const char      *module_path;                               /* Absolute path of loaded library */
    const char      *module_checksum_hex;                       /* Expected HMAC, hex string */
    int              conditional_errors;                        /* MUST be 1 for FIPS */
    qudo_audit_cb    audit_cb;        void *audit_cb_arg;       /* Audit callback */
} qudo_pqc_config_t;
```

Passing `NULL` to `qudo_pqc_init()` uses a zero-initialised default
with `conditional_errors = 1`; this is the correct call for an
embedded-HMAC FIPS build that needs no external integrity config.

### Integrity setup — two deployment patterns

| Pattern                       | When                                                                                                | What the application does                                                                                                            |
| ----------------------------- | --------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------ |
| **Embedded HMAC**             | Linux / macOS, after `qudo_fipsinstall -embed`                                                       | Call `qudo_pqc_init(NULL)` (or with a zeroed config). The integrity HMAC is read from the binary's reserved slot.                    |
| **External `qudofipsmodule.cnf`** | Windows MSVC builds, or any deployment that requires a separate signed integrity manifest          | Read the cnf, parse out the absolute module path and the hex checksum, set `cfg.module_path` and `cfg.module_checksum_hex`, then call `qudo_pqc_init(&cfg)`. |

**There is no environment-variable auto-discovery of the cnf inside
`libqudo-pqc`.** Production deployments must either embed the HMAC at
build time, or arrange for the application wrapping the module to parse
the cnf and supply the fields directly. The `io_open` / `io_read` /
`io_close` callbacks let an application delegate the actual file read
to the integrity layer if preferred. See
[FIPS_BUILD_GUIDE.md §5.2](FIPS_BUILD_GUIDE.md#52-runtime-pickup-of-the-external-config).

### Example — embedded HMAC

```c
qudo_pqc_config_t cfg = {0};
cfg.conditional_errors = 1;           /* Required for FIPS */
cfg.audit_cb = my_audit_callback;     /* Optional but recommended */

if (!qudo_pqc_init(&cfg)) {
    fprintf(stderr, "FIPS init failed; module state=%d\n",
            qudo_pqc_get_state());
    return EXIT_FAILURE;
}
```

### Example — external cnf

```c
/* Application reads qudofipsmodule.cnf itself and extracts the values. */
qudo_pqc_config_t cfg = {0};
cfg.conditional_errors    = 1;
cfg.module_path           = "/opt/myapp/lib/libqudo-pqc.so";
cfg.module_checksum_hex   = checksum_from_cnf;            /* hex string, e.g. "498A98..." */
cfg.audit_cb              = my_audit_callback;

if (!qudo_pqc_init(&cfg)) { /* handle failure */ }
```

## Self-Tests

### Pre-Operational Self-Tests (POST)

Run automatically during `qudo_pqc_init()`. All must pass before the
state machine transitions to `RUNNING`; any failure transitions to
`ERROR`.

| # | Test                       | What it verifies                                            | Source                                                                            |
| - | -------------------------- | ----------------------------------------------------------- | --------------------------------------------------------------------------------- |
| 1 | Module integrity           | HMAC-SHA-256 over the integrity-covered `.text` region      | `src/qudo_pqc_integrity.c`                           |
| 2 | SHA-256 KAT                | Underlying digest primitive                                 | `qudo_pqc_post.c:458`                                                             |
| 3 | HMAC-SHA-256 KAT           | Underlying MAC primitive (used for integrity)               | `qudo_pqc_post.c:476`                                                             |
| 4 | CTR_DRBG-AES-256 KAT       | Underlying DRBG construction                                | `qudo_pqc_post.c:498`                                                             |
| 5 | ML-KEM-512 KAT             | Asymmetric keygen + encaps/decaps + implicit-rejection path | `src/qudo_self_test_data.inc`          |
| 6 | ML-DSA-65 KAT              | Asymmetric keygen + sign + verify                           | `src/qudo_self_test_data.inc`          |
| 7 | SLH-DSA-SHA2-128f KAT      | Asymmetric keygen + sign + verify                           | `src/qudo_self_test_data.inc`          |
| 8 | SLH-DSA-SHAKE-128f KAT     | Asymmetric keygen + sign + verify                           | same                                                                              |

**Parameter-set coverage rationale (lab will ask).** POST KAT coverage
follows FIPS 140-3 IG 10.3.A §14 Note22 (ML-KEM), §15 Note26 (ML-DSA),
and §16 Note29/Note30 (SLH-DSA: one SHA2 + one SHAKE variant; smallest
tree height to bound POST time). ML-KEM runs keygen KATs for **all three**
parameter sets (ML-KEM-512/768/1024, via `qudo_post_keygen_mlkem()`),
with the encaps/decaps KAT on the representative ML-KEM-512; ML-DSA and
SLH-DSA run keygen + sign/verify KATs on their representative set(s).
Full per-parameter-set functional coverage for the sets not exercised by
POST lives outside POST — in `tests/` and the ACVP harness. The IG
rationale is cited inline in `src/qudo_self_test_data.inc`.

### Pairwise Consistency Tests (PCT)

Run on every keygen and every key import:

| Family   | PCT                                                                                  |
| -------- | ------------------------------------------------------------------------------------ |
| ML-KEM   | Encapsulate with the freshly-generated/imported public key, decapsulate with the secret key, verify shared secrets match |
| ML-DSA   | Sign a fixed message with the secret key, verify with the public key                 |
| SLH-DSA  | Sign a fixed message with the secret key, verify with the public key                 |

PCT *failure* behavior depends on the trigger and on `conditional_errors`
— see [§Conditional error handling](#conditional-error-handling). On any
PCT failure the failing keypair's secret-key material is zeroized
before the function returns; verified by
`tests/test_pct_fail_zeroize.c`.

### Conditional Algorithm Self-Tests (CAST)

CAST runs on first use of each algorithm variant (deferred from POST to
bound startup time). Each CAST does a full keygen + sign-and-verify (or
encaps-and-decaps) round-trip and updates a per-variant status flag.

| CAST ID                       | Coverage                          |
| ----------------------------- | --------------------------------- |
| `QUDO_CAST_ML_KEM_512`        | ML-KEM-512                        |
| `QUDO_CAST_ML_KEM_768`        | ML-KEM-768                        |
| `QUDO_CAST_ML_KEM_1024`       | ML-KEM-1024                       |
| `QUDO_CAST_ML_DSA_44`         | ML-DSA-44                         |
| `QUDO_CAST_ML_DSA_65`         | ML-DSA-65                         |
| `QUDO_CAST_ML_DSA_87`         | ML-DSA-87                         |
| `QUDO_CAST_SLH_DSA_SHA2_128`  | SLH-DSA-SHA2-128s and -128f       |
| `QUDO_CAST_SLH_DSA_SHA2_192`  | SLH-DSA-SHA2-192s and -192f       |
| `QUDO_CAST_SLH_DSA_SHA2_256`  | SLH-DSA-SHA2-256s and -256f       |
| `QUDO_CAST_SLH_DSA_SHAKE_128` | SLH-DSA-SHAKE-128s and -128f      |
| `QUDO_CAST_SLH_DSA_SHAKE_192` | SLH-DSA-SHAKE-192s and -192f      |
| `QUDO_CAST_SLH_DSA_SHAKE_256` | SLH-DSA-SHAKE-256s and -256f      |

```c
int s = qudo_pqc_get_cast_status(QUDO_CAST_ML_KEM_768);
/* QUDO_CAST_STATE_INIT       = 0  — never run yet
 * QUDO_CAST_STATE_PROCESSING = 1  — currently running on another thread
 * QUDO_CAST_STATE_SUCCESS    = 2  — passed
 * QUDO_CAST_STATE_FAILURE    = 3  — failed; variant blocked
 */
```

The full CAST → algorithm → test mapping is in
[CAST_MAPPING.md](CAST_MAPPING.md).

### On-demand re-test

```c
qudo_pqc_self_test();        /* FIPS build: re-verify integrity, then re-run all POST KATs */
qudo_pqc_run_all_casts();    /* Force-run every CAST (typically used by tests) */
```

## FIPS approval indicator

The indicator combines three signals: module state is `RUNNING`,
algorithm is approved, DRBG is seeded and healthy. Public API:

```c
qudo_pqc_is_fips_approved("ML-KEM-768");    /* algorithm only — returns 1 */
qudo_fips_ind_check_operation("ML-KEM-768"); /* combined: state + algorithm + DRBG */
qudo_pqc_set_min_security_level(3);          /* reject parameter sets below NIST Cat 3 */
qudo_pqc_get_security_level("ML-KEM-512");   /* returns 1 — would be rejected after the set above */
```

`qudo_fips_ind_t` is also embedded in every algorithm context. Indicator
events emit through the audit log under the `INDICATOR` component.

## Audit logging

Register a callback to receive structured security-relevant events.

```c
void my_audit(qudo_sev_t severity, qudo_err_t code,
              const char *component, const char *detail, void *cb_arg) {
    /* Log to syslog/file/SIEM as policy requires. */
}

qudo_pqc_config_t cfg = {0};
cfg.conditional_errors = 1;
cfg.audit_cb           = my_audit;
qudo_pqc_init(&cfg);
```

`severity` is one of `QUDO_SEV_INFO / WARN / ERROR / FATAL`. `code` is a
`qudo_err_t` from `include/qudo_pqc_err.h`.
`component` is one of the following string constants (see
`include/qudo_pqc_audit.h`):

| Constant                       | String        | Emitted by                                 |
| ------------------------------ | ------------- | ------------------------------------------ |
| `QUDO_AUDIT_COMP_STATE`        | `"STATE"`     | Every state-machine transition             |
| `QUDO_AUDIT_COMP_POST`         | `"POST"`      | POST start / per-test pass-or-fail / completion |
| `QUDO_AUDIT_COMP_INTEGRITY`    | `"INTEGRITY"` | Integrity verification result              |
| `QUDO_AUDIT_COMP_PCT`          | `"PCT"`       | PCT pass / fail / zeroize                  |
| `QUDO_AUDIT_COMP_CAST`         | `"CAST"`      | CAST run / status change                   |
| `QUDO_AUDIT_COMP_DRBG`         | `"DRBG"`      | Seeding, reseed, health-test failure       |
| `QUDO_AUDIT_COMP_KEY`          | `"KEY"`       | Key generation, import, zeroize            |
| `QUDO_AUDIT_COMP_INDICATOR`    | `"INDICATOR"` | Indicator query that resolves "not approved" |

Production deployments SHOULD capture every event at `WARN` and above
and retain them per the operator's logging policy.

## Conditional error handling

`conditional_errors` controls how a PCT failure on **freshly generated**
key material is treated:

| Setting                  | PCT-on-keygen failure                                                                              | FIPS valid? |
| ------------------------ | -------------------------------------------------------------------------------------------------- | ----------- |
| `conditional_errors = 1` | Transitions module to `ERROR`; all subsequent cryptographic operations blocked (ISO 19790 §7.4.4)  | **Required** |
| `conditional_errors = 0` | Failing key is rejected; module stays in `RUNNING`. Development/testing convenience.                | **No**       |

**PCT failure on key *import* is always transient** regardless of the
setting: the failing key is rejected (and zeroized), the module stays
in `RUNNING`. This is a deliberate DoS-prevention design — a malicious
caller passing a corrupted key cannot brick the module. The distinct
error codes are `QUDO_ERR_PCT_KEYGEN_FAIL` (0x0300, fatal under
`conditional_errors=1`) and `QUDO_ERR_PCT_IMPORT_FAIL` (0x0301,
transient). Verified by
`tests/test_pct_fail_zeroize.c`.

## Error codes

Complete registry in `include/qudo_pqc_err.h`.

| Range  | Category          | Example codes                                                                              |
| ------ | ----------------- | ------------------------------------------------------------------------------------------ |
| 0x01xx | Module lifecycle  | `QUDO_ERR_STATE_INVALID = 0x0100`                                                          |
| 0x02xx | POST / integrity  | `_POST_INTEGRITY_FAIL = 0x0200`, `_POST_KAT_FAIL = 0x0201`, `_POST_KEYGEN_KAT_FAIL = 0x0202`, `_POST_KEM_KAT_FAIL = 0x0203`, `_POST_SIG_KAT_FAIL = 0x0204` |
| 0x03xx | PCT               | `_PCT_KEYGEN_FAIL = 0x0300`, `_PCT_IMPORT_FAIL = 0x0301`, `_PCT_ENCAPS_FAIL = 0x0302`, `_PCT_SIGN_FAIL = 0x0303` |
| 0x04xx | CAST              | `_CAST_NOT_RUN = 0x0400`, `_CAST_FAIL = 0x0401`, `_CAST_KEYGEN_FAIL = 0x0402`              |
| 0x05xx | DRBG              | `_DRBG_NOT_SEEDED = 0x0500`, `_DRBG_SEED_FAIL = 0x0501`, `_DRBG_HEALTH_FAIL = 0x0502`, `_DRBG_RESEED_FAIL = 0x0503`, `_DRBG_GENERATE_FAIL = 0x0504` |
| 0x06xx | Key management    | `_KEY_INVALID = 0x0600`                                                                    |
| 0x0Fxx | Generic           | `_ALLOC = 0x0F00`                                                                          |

Use `qudo_err_string(qudo_err_t err)` for human-readable conversion.

## Entropy and DRBG

### Construction

| Property            | Value                                                                  |
| ------------------- | ---------------------------------------------------------------------- |
| DRBG                | AES-256 **CTR_DRBG, without derivation function** (SP 800-90A §10.2.1) |
| Seed length         | 48 bytes (KEYLEN 32 + BLOCKLEN 16) — supplied as a full block, no df  |
| Reseed interval     | `2²⁰` generate calls (`RESEED_INTERVAL` in `src/fips/qudo_fips_rand.c`) |
| Additional input    | Supported by the primitive; not used on the standard generate path      |
| Prediction resistance | Not used on the standard generate path                                 |

Source: `src/fips/qudo_fips_ctrdrbg.c`
and `src/fips/qudo_fips_rand.c`. The DRBG
POST KAT vectors are the `ctr_drbg_aes256_*` arrays in
`src/qudo_self_test_data.inc`.

### Entropy source

The DRBG is seeded from the operating system's RNG. Validation is via
the **ESV Vendor Affirmation** pathway (the module does not validate
the noise source itself — see
[VENDOR_EVIDENCE.md](VENDOR_EVIDENCE.md#51-entropy-source--esv-vendor-affirmation-sp-800-90b)).

| Platform | Primary source                                  | Fallback                                                                                         |
| -------- | ----------------------------------------------- | ------------------------------------------------------------------------------------------------ |
| Linux    | `getentropy(3)` (glibc ≥ 2.25)                   | `getrandom(2)` syscall (non-glibc Linux); `/dev/urandom` (last-resort; non-approved configuration) |
| macOS    | `getentropy(3)`                                  | —                                                                                                |
| Windows  | `BCryptGenRandom(BCRYPT_USE_SYSTEM_PREFERRED_RNG)` | —                                                                                                |

The `/dev/urandom` fallback on Linux is reachable only when neither
`getentropy()` nor the `getrandom` syscall is available. Approved-mode
deployments must run on a platform that provides one of the primary
sources; consumers SHOULD confirm at startup that the primary path is
in use.

### Health tests (SP 800-90B)

Applied to entropy output before reseeding the DRBG:

- **Repetition Count Test (RCT)** — detects a stuck noise source
- **Adaptive Proportion Test (APT)** — detects a biased noise source

The threshold tables are at the top of
`src/fips/qudo_fips_rand.c`
(`rct_critical[]`, `apt_critical[]`). Health-test failures emit
`QUDO_ERR_DRBG_HEALTH_FAIL` and terminate the seeding attempt.

### Per-algorithm randomness consumption

| Operation                     | Bytes from DRBG                  |
| ----------------------------- | -------------------------------- |
| ML-KEM keygen                 | 64  (`d \|\| z` seed)            |
| ML-KEM encaps                 | 32  (`m` randomness)             |
| ML-DSA keygen                 | 32  (`xi` seed)                  |
| ML-DSA hedged sign            | 32  (`rnd`)                      |
| SLH-DSA keygen                | 3 × n  (`SK.seed`, `SK.prf`, `PK.seed`) |
| SLH-DSA hedged sign           | n  (`addrnd`)                    |

All randomness is obtained via the internal `qudo_pqc_rand_bytes()`.
No external DRBG is used within the module boundary.

## Build constraints

See [FIPS_BUILD_GUIDE.md](FIPS_BUILD_GUIDE.md) for compiler flags,
boundary enforcement, LTO restrictions, post-build integrity patching,
per-platform enforcement matrix, and the verification checklist.

## FIPS documentation suite

| Document                                                | Contents                                                |
| ------------------------------------------------------- | ------------------------------------------------------- |
| [FIPS_SECURITY_POLICY.md](FIPS_SECURITY_POLICY.md)      | Formal FIPS 140-3 Security Policy                       |
| [FIPS_BUILD_GUIDE.md](FIPS_BUILD_GUIDE.md)              | Build constraints, boundary enforcement, integrity tool |
| [OPERATING_ENVIRONMENT.md](OPERATING_ENVIRONMENT.md)    | Supported platforms, dependencies, entropy claims       |
| [LIFE_CYCLE.md](LIFE_CYCLE.md)                          | FSM diagram, source inventory, CM, delivery             |
| [CRYPTO_OFFICER_GUIDANCE.md](CRYPTO_OFFICER_GUIDANCE.md)| Installation, verification, error recovery              |
| [USER_GUIDANCE.md](USER_GUIDANCE.md)                    | API usage, approved algorithms, key handling            |
| [VENDOR_EVIDENCE.md](VENDOR_EVIDENCE.md)                | CST-lab evidence index, ESV vendor affirmation          |
| [CAST_MAPPING.md](CAST_MAPPING.md)                      | CAST → algorithm → test mapping                         |
| [MAINTENANCE.md](../maintenance/MAINTENANCE.md)                        | Post-validation maintenance plan                        |
| [ALGORITHMS.md](../reference/ALGORITHMS.md)                          | All 18 parameter sets, sizes, selection                 |
| [API.md](../reference/API.md)                                        | Full C API reference                                    |
| [ACVP.md](ACVP.md)                                      | ACVP harness for NIST algorithm validation              |
