# Third-Party Notices

`libqudo-pqc` is distributed under **Apache-2.0 AND MIT** (see [LICENSE](LICENSE)).
It includes third-party components vendored in-tree. Their copyright notices and
license terms are reproduced below; the authoritative, full license text for each
ships alongside the code at the path indicated.

The QUDO wrapper layer (`src/`, `include/`, `qudo-*/src`, `qudo-*/include`) is
QUDO-authored; the components below provide the underlying lattice/hash
mathematics and are **not** modified in-tree.

---

## 1. mlkem-native (ML-KEM / FIPS 203)

- **Path:** `qudo-mlkem/mlkem-native/`
- **Full license:** `qudo-mlkem/mlkem-native/LICENSE`
- **Origin:** a fork of the public-domain Kyber reference implementation
  (https://github.com/pq-crystals/kyber).
- **License (production code in `mlkem/*`, `dev/*`):** Apache-2.0 **OR** ISC **OR** MIT (recipient's choice).
- **Copyright:**
  - Copyright (c) The mlkem-native project authors
  - Copyright (c) 2020 Dougall Johnson
  - Copyright (c) 2022 Arm Limited
- **Note:** test-only code (`test/notrandombytes/*`, `test/hal/hal.c`) carries
  separate permissive/public-domain terms and is **not** part of the shipped
  library.

## 2. mldsa-native (ML-DSA / FIPS 204)

- **Path:** `qudo-mldsa/mldsa-native/`
- **Full license:** `qudo-mldsa/mldsa-native/LICENSE`
- **Origin:** a fork of the public-domain Dilithium reference implementation
  (https://github.com/pq-crystals/dilithium).
- **License (new + derived files):** Apache-2.0 **OR** ISC **OR** MIT (recipient's choice).
- **Copyright:**
  - Copyright (c) The mldsa-native project authors
  - Copyright (c) The mlkem-native project authors
  - Copyright (c) 2020 Dougall Johnson
  - Copyright (c) 2022 Arm Limited
- **Note:** test-only code carries separate terms and is not part of the shipped library.

## 3. slhdsa-native / slhdsa-c (SLH-DSA / FIPS 205)

- **Path:** `qudo-slhdsa/slhdsa-native/`
- **Full license:** `qudo-slhdsa/slhdsa-native/LICENSE`
- **Origin:** the slhdsa-c project; originally written by
  Markku-Juhani O. Saarinen (2023–2025) and donated to the slhdsa-c project and
  derived projects.
- **License:** ISC **OR** MIT (recipient's choice).
- **Copyright:**
  - Copyright (c) The slhdsa-c project authors

---

## License texts

The complete text of every license referenced above is reproduced verbatim in
the corresponding `LICENSE` file under each component directory. Where a
component is offered under a choice of licenses ("A OR B OR C"), recipients of
`libqudo-pqc` may select any one of the offered licenses for that component.

This NOTICE is provided to satisfy the attribution requirements of the Apache
License 2.0 (§4(d)) and the reproduction-of-copyright requirements of the ISC
and MIT licenses. It does not modify any of the referenced licenses.
