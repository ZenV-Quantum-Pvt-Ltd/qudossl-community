# User Guidance

FIPS 140-3 (ISO/IEC 19790 Section 7.11.7) — User Guidance.

This document provides guidance for application developers using the QUDO PQC
Crypto Library in FIPS-approved mode.

---

## 1. Approved Mode vs Non-Approved Mode

### 1.1 Approved Algorithms (FIPS Mode)

All operations using the following algorithms are FIPS-approved:

| Family | Algorithms | Standard |
|--------|-----------|----------|
| ML-KEM | ML-KEM-512, ML-KEM-768, ML-KEM-1024 | FIPS 203 |
| ML-DSA | ML-DSA-44, ML-DSA-65, ML-DSA-87 | FIPS 204 |
| SLH-DSA | SLH-DSA-SHA2-128s/f, SLH-DSA-SHA2-192s/f, SLH-DSA-SHA2-256s/f, SLH-DSA-SHAKE-128s/f, SLH-DSA-SHAKE-192s/f, SLH-DSA-SHAKE-256s/f | FIPS 205 |

### 1.2 Non-Approved Operations

Hybrid algorithms combine a PQC algorithm with a classical algorithm. These
are NOT FIPS-approved because the classical components (X25519, ECDH, RSA,
ECDSA) are not part of this module's FIPS validation:

- Hybrid KEM: X25519MLKEM768, SecP256r1MLKEM768, p256_mlkem512, etc.
- Hybrid Signatures: p256_mldsa44, rsa2048_mldsa44, p384_mldsa65, etc.

Using hybrid algorithms does not invalidate the module — the PQC component
still runs through the FIPS-validated code path. However, the overall operation
is classified as non-approved.

### 1.3 Checking Approval Status

Three complementary checks are available:

```c
#include <qudo_pqc.h>
#include <qudo_pqc_indicator.h>

/* (a) Is the named algorithm on the approved list? */
if (qudo_pqc_is_fips_approved("ML-KEM-768")) {
    /* Approved — safe for FIPS-regulated use */
}

/* (b) Combined check: module is RUNNING, algorithm is approved, DRBG is healthy. */
if (qudo_fips_ind_check_operation("ML-KEM-768")) {
    /* Operation, as a whole, is approved right now */
}

/* (c) Build the per-operation indicator yourself, e.g. to enforce a
 *     minimum NIST security level (qudo_pqc_set_min_security_level(int)). */
qudo_fips_ind_t ind;
qudo_pqc_check_security_level("ML-KEM-768", &ind);
if (qudo_fips_ind_is_approved(&ind)) {
    /* Algorithm meets the minimum security level configured for this process */
}
```

`qudo_pqc_check_security_level()` is defined at `src/qudo_pqc_security.c`; `qudo_fips_ind_is_approved()` is an inline helper in `include/qudo_pqc_indicator.h`.

## 2. Key Generation

### 2.1 ML-KEM Key Generation

```c
#include <qudo_pqc.h>
#include "mlkem_wrapper.h"

/* Object API */
QUDO_KEM *kem = QUDO_KEM_new("ML-KEM-768");
if (kem == NULL) { /* handle error */ }

uint8_t pk[ML_KEM_768_PUBLIC_KEY_BYTES];
uint8_t sk[ML_KEM_768_SECRET_KEY_BYTES];

int rc = QUDO_KEM_keypair(kem, pk, sk);
if (rc != QUDO_KEM_SUCCESS) { /* handle error */ }

/* Use pk/sk... */

/* Zeroize secret key when done */
qudo_cleanse(sk, sizeof(sk));
QUDO_KEM_free(kem);
```

### 2.2 ML-DSA Key Generation

```c
#include "mldsa_wrapper.h"

QUDO_MLDSA *sig = QUDO_MLDSA_new("ML-DSA-65");
if (sig == NULL) { /* handle error */ }

uint8_t pk[ML_DSA_65_PUBLIC_KEY_BYTES];
uint8_t sk[ML_DSA_65_SECRET_KEY_BYTES];

int rc = QUDO_MLDSA_keypair(sig, pk, sk);
if (rc != QUDO_MLDSA_SUCCESS) { /* handle error */ }

/* Use pk/sk... */

qudo_cleanse(sk, sizeof(sk));
QUDO_MLDSA_free(sig);
```

### 2.3 SLH-DSA Key Generation

```c
#include "slhdsa_wrapper.h"

QUDO_SLHDSA *sig = QUDO_SLHDSA_new("SLH-DSA-SHA2-128f");
if (sig == NULL) { /* handle error */ }

uint8_t pk[SLH_DSA_SHA2_128F_PUBLIC_KEY_BYTES];
uint8_t sk[SLH_DSA_SHA2_128F_SECRET_KEY_BYTES];

int rc = QUDO_SLHDSA_keypair(sig, pk, sk);
if (rc != QUDO_SLHDSA_SUCCESS) { /* handle error */ }

/* Use pk/sk... */

qudo_cleanse(sk, sizeof(sk));
QUDO_SLHDSA_free(sig);
```

## 3. Encapsulation / Decapsulation (ML-KEM)

```c
/* Encapsulate (sender) */
uint8_t ct[ML_KEM_768_CIPHERTEXT_BYTES];
uint8_t ss_enc[ML_KEM_768_SHARED_SECRET_BYTES];

rc = QUDO_KEM_encaps(kem, ct, ss_enc, pk);
if (rc != QUDO_KEM_SUCCESS) { /* handle error */ }

/* Decapsulate (receiver) */
uint8_t ss_dec[ML_KEM_768_SHARED_SECRET_BYTES];

rc = QUDO_KEM_decaps(kem, ss_dec, ct, sk);
if (rc != QUDO_KEM_SUCCESS) { /* handle error */ }

/* ss_enc and ss_dec are identical shared secrets */

/* Zeroize shared secrets when done */
qudo_cleanse(ss_enc, sizeof(ss_enc));
qudo_cleanse(ss_dec, sizeof(ss_dec));
```

## 4. Signing / Verification (ML-DSA)

```c
/* Sign */
uint8_t sig_buf[ML_DSA_65_SIGNATURE_BYTES];
size_t sig_len = sizeof(sig_buf);

rc = QUDO_MLDSA_sign(sig, sig_buf, &sig_len,
                     message, message_len, sk);
if (rc != QUDO_MLDSA_SUCCESS) { /* handle error */ }

/* Verify */
rc = QUDO_MLDSA_verify(sig, sig_buf, sig_len,
                       message, message_len, pk);
if (rc != QUDO_MLDSA_SUCCESS) {
    /* Signature verification failed */
}
```

## 5. Key Management

### 5.1 Key Storage

- Store private keys in memory only for the duration needed
- Use `qudo_cleanse()` to zeroize private key buffers immediately after use
- Do not write unencrypted private keys to persistent storage
- For persistent storage, use application-level encryption (e.g., PKCS#8 with passphrase)

### 5.2 Key Zeroization

The module provides `qudo_cleanse()` for secure zeroization that cannot be
optimized away by the compiler:

```c
/* Zeroize a secret key buffer */
qudo_cleanse(secret_key, secret_key_len);

/* Object API automatically zeroizes on free */
QUDO_KEM_free(kem);     /* Zeroizes all internal state */
QUDO_MLDSA_free(sig);   /* Zeroizes all internal state */
QUDO_SLHDSA_free(sig);  /* Zeroizes all internal state */
```

### 5.3 Key Import/Export

DER and PEM formats are supported for key serialization:

```c
/* Export public key to DER */
uint8_t der[4096];
size_t der_len = sizeof(der);
QUDO_KEM_export_public_key_der(pk, pk_len, der, &der_len);

/* Import public key from DER */
QUDO_KEM_import_public_key_der(der, der_len, pk, &pk_len);
```

Private key export/import follows the same pattern. Always zeroize DER/PEM
buffers containing private keys after use.

## 6. Error Handling

### 6.1 Return-value conventions

The module uses **two distinct conventions**; do not conflate them.

**Algorithm-wrapper APIs** (`QUDO_KEM_*`, `QUDO_MLDSA_*`, `QUDO_SLHDSA_*`) follow the Unix-style convention: **`0` = success, negative = error**. The error codes are enums defined per family:

| Wrapper                 | Success constant       | Error range            | Header                                                     |
| ----------------------- | ---------------------- | ---------------------- | ---------------------------------------------------------- |
| `QUDO_KEM_*`            | `QUDO_KEM_SUCCESS = 0` | `-1` to `-13` (`-11` reserved) | [qudo-mlkem/include/mlkem_types.h](../../qudo-mlkem/include/mlkem_types.h) |
| `QUDO_MLDSA_*`          | `QUDO_MLDSA_SUCCESS = 0` | `-1` to `-14` (incl. `…_INVALID_SIGNATURE`) | [qudo-mldsa/include/mldsa_types.h](../../qudo-mldsa/include/mldsa_types.h) |
| `QUDO_SLHDSA_*`         | `QUDO_SLHDSA_SUCCESS = 0` | `-1` to `-14` (incl. `…_INVALID_SIGNATURE`) | [qudo-slhdsa/include/slhdsa_types.h](../../qudo-slhdsa/include/slhdsa_types.h) |

**Lifecycle / status APIs** (`qudo_pqc_init`, `qudo_pqc_self_test`, `qudo_pqc_is_running`, etc.) follow the boolean convention: **`1` = success/true, `0` = failure/false**.

**Module-internal error codes** (`qudo_err_t`, used in audit-callback `code` parameter and in some internal paths) are positive 16-bit values grouped by category:

| Range          | Category          | Header                                                       |
| -------------- | ----------------- | ------------------------------------------------------------ |
| 0x0100–0x01FF  | Module lifecycle  | `include/qudo_pqc_err.h`          |
| 0x0200–0x02FF  | POST / integrity  | same                                                         |
| 0x0300–0x03FF  | PCT               | same                                                         |
| 0x0400–0x04FF  | CAST              | same                                                         |
| 0x0500–0x05FF  | DRBG              | same                                                         |
| 0x0600–0x06FF  | Key management    | same                                                         |
| 0x0F00–0x0FFF  | Generic           | same                                                         |

Use `qudo_err_string(qudo_err_t err)` for human-readable conversion. Note: these `qudo_err_t` codes are not returned by the wrapper APIs above — they appear in audit callbacks and internal logging.

### 6.2 Module State Check

Always verify the module is operational before cryptographic operations. The state value (0 / 1 / 2 / 3 = INIT / SELFTEST / RUNNING / ERROR) is not a `qudo_err_t` and should not be passed to `qudo_err_string()`:

```c
if (!qudo_pqc_is_running()) {
    int state = qudo_pqc_get_state();
    static const char *state_name[] = {"INIT", "SELFTEST", "RUNNING", "ERROR"};
    fprintf(stderr, "QUDO PQC module not operational (state=%s)\n",
            (state >= 0 && state < 4) ? state_name[state] : "?");
    /* Do not attempt cryptographic operations */
}
```

`qudo_err_string()` is for `qudo_err_t` values surfaced via the audit callback's `code` parameter — see §6.1.

### 6.3 Error Recovery

If a cryptographic operation fails, check the module state. If the module
is in ERROR state, see [Crypto Officer Guidance](CRYPTO_OFFICER_GUIDANCE.md)
Section 6 for recovery procedures.

## 7. Security Recommendations

1. **Use the highest security level your performance budget allows**:
   ML-KEM-1024, ML-DSA-87, SLH-DSA-256 variants provide NIST Level 5
2. **Prefer hedged signing** (default) over deterministic signing for
   ML-DSA and SLH-DSA — adds randomness to protect against fault attacks
3. **Verify signatures before trusting** — never skip verification
4. **Zeroize all secret material** immediately after use
5. **Check return codes** from every cryptographic operation
6. **Use the Object API** for automatic resource management and zeroization
7. **Do not reuse KEM shared secrets** — each encapsulation produces a
   fresh shared secret
