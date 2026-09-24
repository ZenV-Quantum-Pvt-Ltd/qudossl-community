# ACVP Harness for libqudo-pqc

This directory contains the Automated Cryptographic Validation Protocol
(ACVP) round-trip harness used to obtain NIST CAVP algorithm validation
certificates for the approved post-quantum primitives in
`libqudo-pqc.so`.

The certificates produced from this harness are referenced in the
FIPS 140-3 module Security Policy; they are an input to CMVP
submission but are an independent artifact from the module-level
certification itself.

## Purpose

NIST's CAVP program validates algorithm implementations against
published test vectors using the ACVP JSON protocol
(see `https://pages.nist.gov/ACVP/`). This harness:

1. Fetches published NIST ACVP request / expected-result vector sets.
2. Replays every prompt against `libqudo-pqc.so` via small C drivers.
3. Writes a response file in the ACVP JSON schema and compares it to
   the NIST expected-result file.

Successful comparison over every prompt constitutes algorithm
validation evidence for the approved parameter sets.

## Layout

```
qudo-pqc/acvp/
├── CMakeLists.txt           build rules for the C drivers
├── run_acvp.sh              driver script (ML-KEM, ML-DSA, SLH-DSA)
├── acvp_common.[ch]         JSON parsing and hex helpers
├── acvp_client_mlkem.py     ACVP client for FIPS 203 ML-KEM
├── acvp_client_mldsa.py     ACVP client for FIPS 204 ML-DSA
├── acvp_client_slhdsa.py    ACVP client for FIPS 205 SLH-DSA
├── acvp_mlkem.c             C driver invoking libqudo-pqc ML-KEM
├── acvp_mldsa.c             C driver invoking libqudo-pqc ML-DSA
├── acvp_slhdsa.c            C driver invoking libqudo-pqc SLH-DSA
└── .acvp-data/<version>/files/
                             locally staged NIST ACVP vector sets
```

The `.acvp-data/v1.1.0.41/files/` cache currently holds the request
and expected-result files for the following algorithm suites, fetched
from `usnistgov/ACVP-Server` on GitHub:

- `ML-KEM-keyGen-FIPS203`, `ML-KEM-encapDecap-FIPS203`
- `ML-DSA-keyGen-FIPS204`, `ML-DSA-sigGen-FIPS204`, `ML-DSA-sigVer-FIPS204`
- `SLH-DSA-keyGen-FIPS205`, `SLH-DSA-sigGen-FIPS205`, `SLH-DSA-sigVer-FIPS205`
- `ACVP-AES-CTR-1.0`, `ACVP-AES-ECB-1.0`
- `HMAC-SHA2-256-2.0`, `HMAC-SHA2-384-2.0`, `HMAC-SHA2-512-2.0`
- `SHA2-256-1.0`, `SHA2-384-1.0`, `SHA2-512-1.0`
- `SHA3-256-2.0`, `SHA3-384-2.0`, `SHA3-512-2.0`
- `ctrDRBG-1.0`

## Vector source

The Python clients fetch test vectors from

```
https://raw.githubusercontent.com/usnistgov/ACVP-Server/<version>/gen-val/json-files
```

on first run and cache them under `.acvp-data/<version>/files/`.
`--version v1.1.0.41` is the default pinned revision. No credentials
and no live connection to the NIST ACVP demo server are required for
replay; CAVP submission itself uses NIST's production ACVP server out
of band.

## Running

1. Build with the ACVP drivers enabled:

   ```sh
   cd qudo-pqc
   ./build.sh --acvp
   ```

2. Run all three suites:

   ```sh
   bash acvp/run_acvp.sh
   ```

   Useful flags:
   - `--build-dir PATH`  select a non-default build tree
   - `--version v1.1.0.41`  pin an ACVP vector version
   - `--skip-slhdsa`  omit the SLH-DSA suite (slowest)

   The script sets `LD_LIBRARY_PATH` to the built `libqudo-pqc.so`,
   invokes each Python client against the matching C driver, and
   exits non-zero if any prompt fails to match the NIST expected
   result.

## Relationship to POST KATs

The POST Known-Answer Tests embedded in
`qudo-pqc/src/qudo_self_test_data.inc` and executed by
`qudo_pqc_post.c` serve a different purpose and must not be confused
with this harness:

| Concern          | POST KATs (`qudo_self_test_data.inc`) | ACVP harness (this dir) |
|------------------|---------------------------------------|-------------------------|
| When it runs     | Every module load                     | On demand, pre-cert     |
| What it checks   | Implementation drift vs. frozen build | Algorithm correctness   |
| Vector source    | Deterministic, module-local seeds     | NIST ACVP vector sets   |
| Required by      | ISO/IEC 19790 Sec. 7.10 (CAST) via FIPS 140-3 IG 10.3.A | NIST CAVP program |
| Output           | Pass / fail at load time              | CAVP certificate number |

In short: POST proves the binary in the user's hands is the binary
that was certified; ACVP proves the certified algorithm is the
approved algorithm. FIPS 140-3 requires both, and each is evidenced
independently in the Security Policy.

## Module boundary

`libqudo-pqc.so` is the FIPS 140-3 module boundary. The C drivers in
this directory are test clients of that boundary — they are NOT part
of the validated module. `qudoprovider.so` is likewise outside the
module boundary and does not participate in the ACVP harness.
