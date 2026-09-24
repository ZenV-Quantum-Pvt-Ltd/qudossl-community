# C API Reference

## Migration note — `QUDO_KEM_keypair_derand()` capacity parameter

`QUDO_KEM_keypair_derand()` takes a trailing `size_t seed_out_len` capacity
parameter that bounds writes to `seed_out`, eliminating a buffer-overflow
class (P1-07). This is the signature shipped in **1.0.0** (SONAME
`libqudo-pqc.so.1`). If you are updating code written against an earlier,
pre-release four-argument form, add the capacity argument to each caller:

```c
/* Earlier (pre-release) form — no capacity argument */
uint8_t seed[QUDO_KEM_SEED_BYTES];
QUDO_KEM_keypair_derand(kem, pk, sk, seed);

/* Current (1.0.0) — pass the buffer capacity */
uint8_t seed[QUDO_KEM_SEED_BYTES];
QUDO_KEM_keypair_derand(kem, pk, sk, seed, sizeof(seed));

/* Current — discard the seed (permitted): NULL buffer, 0 capacity */
QUDO_KEM_keypair_derand(kem, pk, sk, NULL, 0);
```

## Module Lifecycle

```c
#include "qudo_pqc.h"

/* Initialize the module (seeds DRBG, runs POST in FIPS mode) */
int qudo_pqc_init(const qudo_pqc_config_t *config);  /* 1=success, 0=failure */

/* Check if module is operational */
int qudo_pqc_is_running(void);      /* 1=RUNNING, 0=otherwise */
int qudo_pqc_get_state(void);       /* QUDO_PQC_STATE_INIT/SELFTEST/RUNNING/ERROR */
int qudo_pqc_is_fips(void);         /* 1=FIPS build, 0=standard */

/* On-demand self-test (re-runs all KATs) */
int qudo_pqc_self_test(void);       /* 1=pass, 0=fail */

/* Algorithm approval and security level */
int qudo_pqc_is_fips_approved(const char *name);      /* 1=approved */
int qudo_pqc_get_security_level(const char *name);     /* NIST level: 1,2,3,5 */
void qudo_pqc_set_min_security_level(int level);       /* Set minimum */

/* CAST status */
int qudo_pqc_get_cast_status(int cast_id);  /* QUDO_CAST_STATE_* */
int qudo_pqc_run_all_casts(void);           /* Run all CASTs on-demand */
```

## ML-KEM (FIPS 203)

```c
#include "mlkem_wrapper.h"

/* Create / free */
QUDO_KEM *QUDO_KEM_new(const char *algorithm);         /* "ML-KEM-512", "ML-KEM-768", "ML-KEM-1024" */
QUDO_KEM *QUDO_KEM_new_by_level(QUDO_KEM_security_level_t level);  /* QUDO_KEM_512, _768, _1024 */
void QUDO_KEM_free(QUDO_KEM *kem);

/* Key generation */
QUDO_KEM_status_t QUDO_KEM_keypair(const QUDO_KEM *kem, uint8_t *pk, uint8_t *sk);
QUDO_KEM_status_t QUDO_KEM_keypair_from_seed(const QUDO_KEM *kem, uint8_t *pk, uint8_t *sk, const uint8_t *seed);
/* Randomized keygen that also emits the 64-byte seed (d||z) used internally.
 * seed_out_len is the caller buffer capacity; must be >= QUDO_KEM_SEED_BYTES
 * (= 64). A NULL seed_out is permitted — the seed is generated in an
 * internal scratch buffer and zeroized before return. */
QUDO_KEM_status_t QUDO_KEM_keypair_derand(const QUDO_KEM *kem, uint8_t *pk, uint8_t *sk,
                                          uint8_t *seed_out, size_t seed_out_len);

/* Encapsulation */
QUDO_KEM_status_t QUDO_KEM_encaps(const QUDO_KEM *kem, uint8_t *ct, uint8_t *ss, const uint8_t *pk);
QUDO_KEM_status_t QUDO_KEM_encaps_derand(const QUDO_KEM *kem, uint8_t *ct, uint8_t *ss, const uint8_t *pk, const uint8_t rand[32]);

/* Decapsulation */
QUDO_KEM_status_t QUDO_KEM_decaps(const QUDO_KEM *kem, uint8_t *ss, const uint8_t *ct, const uint8_t *sk);

/* Key sizes (read from kem->length_public_key, kem->length_secret_key, etc.) */
```

### Example

```c
QUDO_KEM *kem = QUDO_KEM_new("ML-KEM-768");
uint8_t pk[1184], sk[2400], ct[1088], ss_enc[32], ss_dec[32];

QUDO_KEM_keypair(kem, pk, sk);
QUDO_KEM_encaps(kem, ct, ss_enc, pk);
QUDO_KEM_decaps(kem, ss_dec, ct, sk);
/* ss_enc == ss_dec */

QUDO_KEM_free(kem);
```

## ML-DSA (FIPS 204)

```c
#include "mldsa_wrapper.h"

/* Create / free */
QUDO_MLDSA *QUDO_MLDSA_new(const char *algorithm);  /* "ML-DSA-44", "ML-DSA-65", "ML-DSA-87" */
void QUDO_MLDSA_free(QUDO_MLDSA *sig);

/* Key generation */
QUDO_MLDSA_status_t QUDO_MLDSA_keypair(QUDO_MLDSA *sig, uint8_t *pk, uint8_t *sk);

/* Sign */
QUDO_MLDSA_status_t QUDO_MLDSA_sign(QUDO_MLDSA *sig, uint8_t *signature, size_t *sig_len,
                                    const uint8_t *msg, size_t msg_len, const uint8_t *sk);

/* Sign with context string */
QUDO_MLDSA_status_t QUDO_MLDSA_sign_with_context(QUDO_MLDSA *sig, uint8_t *signature, size_t *sig_len,
                                                 const uint8_t *msg, size_t msg_len,
                                                 const uint8_t *ctx, size_t ctx_len, const uint8_t *sk);

/* Verify */
QUDO_MLDSA_status_t QUDO_MLDSA_verify(QUDO_MLDSA *sig, const uint8_t *signature, size_t sig_len,
                                      const uint8_t *msg, size_t msg_len, const uint8_t *pk);
```

### Example

```c
QUDO_MLDSA *sig = QUDO_MLDSA_new("ML-DSA-65");
uint8_t pk[1952], sk[4032], signature[3309];
size_t sig_len;
const uint8_t msg[] = "message to sign";
size_t msg_len = sizeof(msg) - 1;

QUDO_MLDSA_keypair(sig, pk, sk);
QUDO_MLDSA_sign(sig, signature, &sig_len, msg, msg_len, sk);
int valid = (QUDO_MLDSA_verify(sig, signature, sig_len, msg, msg_len, pk) == 0);

QUDO_MLDSA_free(sig);
```

## SLH-DSA (FIPS 205)

```c
#include "slhdsa_wrapper.h"

/* Create / free */
QUDO_SLHDSA *QUDO_SLHDSA_new(const char *algorithm);  /* e.g., "SLH-DSA-SHA2-128f" */
void QUDO_SLHDSA_free(QUDO_SLHDSA *sig);

/* Key generation */
QUDO_SLHDSA_status_t QUDO_SLHDSA_keypair(const QUDO_SLHDSA *sig, uint8_t *pk, uint8_t *sk);

/* Sign */
QUDO_SLHDSA_status_t QUDO_SLHDSA_sign(const QUDO_SLHDSA *sig, uint8_t *signature, size_t *sig_len,
                                      const uint8_t *msg, size_t msg_len, const uint8_t *sk);

/* Sign with context + caller-supplied additional randomness (addrnd).
   Pass addrnd = NULL for the deterministic variant. */
QUDO_SLHDSA_status_t QUDO_SLHDSA_sign_ex(const QUDO_SLHDSA *sig, uint8_t *signature, size_t *sig_len,
                                         const uint8_t *msg, size_t msg_len,
                                         const uint8_t *ctx, size_t ctx_len,
                                         const uint8_t *addrnd, const uint8_t *sk);

/* Verify */
QUDO_SLHDSA_status_t QUDO_SLHDSA_verify(const QUDO_SLHDSA *sig, const uint8_t *msg, size_t msg_len,
                                        const uint8_t *signature, size_t sig_len, const uint8_t *pk);
```

### Example

```c
QUDO_SLHDSA *sig = QUDO_SLHDSA_new("SLH-DSA-SHA2-128f");
uint8_t pk[32], sk[64], signature[17088];
size_t sig_len;
const uint8_t msg[] = "message to sign";
size_t msg_len = sizeof(msg) - 1;

QUDO_SLHDSA_keypair(sig, pk, sk);
QUDO_SLHDSA_sign(sig, signature, &sig_len, msg, msg_len, sk);
int valid = (QUDO_SLHDSA_verify(sig, msg, msg_len, signature, sig_len, pk) == 0);

QUDO_SLHDSA_free(sig);
```

## Return Codes

The library uses **two distinct conventions** depending on the API layer:

| Layer                                          | Convention                  |
| ---------------------------------------------- | --------------------------- |
| Algorithm wrappers (`QUDO_KEM_*`, `QUDO_MLDSA_*`, `QUDO_SLHDSA_*`) — covered in this document | **0 = success, negative = error** |
| Lifecycle and status (`qudo_pqc_init`, `qudo_pqc_self_test`, `qudo_pqc_is_running`, etc.) | **1 = success / true, 0 = failure / false** |
| Audit-callback `code` parameter (`qudo_err_t`) | positive 16-bit error code grouped by category — see `include/qudo_pqc_err.h` |

### Wrapper error enum (this document's APIs)

| Code                          | Value | Meaning                                              |
| ----------------------------- | ----- | ---------------------------------------------------- |
| `QUDO_*_SUCCESS`              | 0     | Success                                              |
| `QUDO_*_ERROR`                | -1    | Unspecified error                                    |
| `QUDO_*_ERROR_INVALID_ARG`    | -2    | Invalid argument                                     |
| `QUDO_*_ERROR_NULL_PTR`       | -3    | NULL pointer passed                                  |
| `QUDO_*_ERROR_ALLOC`          | -4    | Memory allocation failed                             |
| `QUDO_*_ERROR_RNG`            | -5    | Random number generation failed                      |
| `QUDO_*_ERROR_VERIFY`         | -6    | Verification / decapsulation-match failed            |
| `QUDO_*_ERROR_DECODE`         | -7    | DER / PEM decoding failed                            |
| `QUDO_*_ERROR_NOT_IMPL`       | -8    | Feature not available in this build                  |
| `QUDO_*_ERROR_CRYPTO`         | -9    | Cryptographic operation failed                       |
| `QUDO_*_ERROR_FILE_IO`        | -10   | File I/O error                                       |
| `QUDO_*_ERROR_ENCODE`         | -12   | DER / PEM encoding failed                            |
| `QUDO_*_ERROR_BUFFER_TOO_SMALL` | -13 | Output buffer too small                              |

Full enums per family: [qudo-mlkem/include/mlkem_types.h](../../qudo-mlkem/include/mlkem_types.h), [qudo-mldsa/include/mldsa_types.h](../../qudo-mldsa/include/mldsa_types.h), [qudo-slhdsa/include/slhdsa_types.h](../../qudo-slhdsa/include/slhdsa_types.h).

Note that the `verify` examples above test `== 0` for "valid", consistent with this convention.

## FIPS Configuration

```c
qudo_pqc_config_t cfg = {0};
cfg.self_test_cb = my_callback;        /* Optional: self-test event callback */
cfg.error_cb = my_error_handler;       /* Optional: error callback */
cfg.audit_cb = my_audit_logger;        /* Optional: structured audit logging */
cfg.conditional_errors = 1;            /* 1=PCT failures enter ERROR state */
cfg.module_path = "/path/to/libqudo-pqc.so";    /* FIPS: for integrity check */
cfg.module_checksum_hex = "498A98...";           /* FIPS: expected HMAC */

qudo_pqc_init(&cfg);
```

## Thread Safety

- `qudo_pqc_init()` is thread-safe (uses internal once-init)
- Each `QUDO_KEM` / `QUDO_MLDSA` / `QUDO_SLHDSA` instance is independent
- Multiple threads can use different instances simultaneously
- Do not share a single instance across threads without synchronization
