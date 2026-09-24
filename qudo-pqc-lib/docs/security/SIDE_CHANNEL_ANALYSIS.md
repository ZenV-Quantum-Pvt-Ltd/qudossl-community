# QUDO PQC — Side-Channel & Constant-Time Analysis

> **Source of truth.** This Markdown file is the canonical source for this document. The branded Word edition is generated from it; regenerate rather than editing the Word copy.

# 1 Purpose & Scope

This report documents the timing-side-channel posture of the QUDO PQC module: the constant-time design measures present in the code and the empirical timing screening executed by the sub-library test suites. It is an informal screening and design review, not an accredited side-channel laboratory assessment.

# 2 Methodology

- Design review — inspection of comparison, AES and dispatch code for secret-dependent branches or memory-access patterns.

- Empirical screening — dudect-style leakage detection: two input classes (fixed vs random secret), per-operation cycle measurements, Welch's t-statistic vs a 4.5 significance threshold. \|t\| \< 4.5 ⇒ no statistically significant secret-dependent timing difference.

# 3 Constant-Time Design Measures

| **Measure** | **Where (code)** |
|----|----|
| Constant-time AES default | qudo_fips_aes_ct.c (bitsliced/no-table); T-table variant compiled out unless QUDO_AES_ALLOW_TTABLE |
| Constant-time comparison | qudo_memcmp_ct (platform.c:231) — used for ALL KAT/PCT/integrity comparisons (volatile OR-accumulate) |
| ML-KEM implicit rejection | Native decaps returns a pseudo-random shared secret on invalid ciphertext (no secret-dependent error branch) |
| No secret-dependent table lookups (AES) | Constant-time S-box path is the default backend |
| CPU dispatch is data-independent | Backend selected once at init by CPU features, not by secret data |

Table 4 — Constant-time design measures present in the implementation.

# 4 Empirical Results

Measured by the sub-library constant-time suites (test_constant_time) on the platform in the test environment. All measured t-statistics are well below the 4.5 threshold.

| **Operation** | **Measurements** | **t-statistic** | **Threshold** | **Result** |
|----|----|----|----|----|
| ML-DSA sign | 10,000 | 0.688 | 4.5 | PASS |
| ML-DSA verify | 10,000 | 0.935 | 4.5 | PASS |
| SLH-DSA-SHA2-128f verify | 1,000 | 1.28 | 4.5 | PASS |
| SLH-DSA-SHAKE-128f verify | 1,000 | 0.54 | 4.5 | PASS |

Table 5 — dudect-style timing screening results (captured execution).

# 5 Limitations

- Informal screening, not an accredited side-channel (DPA/SPA/EM) laboratory assessment.

- Coverage is the operations exercised by the sub-library suites on a single platform (Apple M4 / macOS); other microarchitectures may differ.

- The lattice/hash kernels originate from the vendored \`\*-native\` upstreams; their constant-time properties are inherited and assumed from those formally-developed sources.

- Timing analysis does not cover power or electromagnetic channels.

# 6 Recommendations

- Commission a formal independent side-channel assessment for the target deployment.

- Extend the dudect screening to ML-KEM operations and to additional platforms/architectures.

- Track upstream constant-time advisories for the vendored implementations.

# Appendix A References & Applicable Standards

The following normative and informative references apply to this document. Bracketed labels are used for in-text citation.

| **Ref** | **Citation** |
|----|----|
| \[FIPS203\] | FIPS 203, Module-Lattice-Based Key-Encapsulation Mechanism Standard, NIST, 2024. |
| \[FIPS204\] | FIPS 204, Module-Lattice-Based Digital Signature Standard, NIST, 2024. |
| \[FIPS205\] | FIPS 205, Stateless Hash-Based Digital Signature Standard, NIST, 2024. |
| \[FIPS140-3\] | FIPS 140-3, Security Requirements for Cryptographic Modules, NIST, 2019. |
| \[IG\] | NIST CMVP, Implementation Guidance for FIPS 140-3 and the CMVP. |
| \[SP800-90A\] | NIST SP 800-90A Rev. 1, Recommendation for Random Number Generation Using Deterministic Random Bit Generators, 2015. |
| \[SP800-90B\] | NIST SP 800-90B, Recommendation for the Entropy Sources Used for Random Bit Generation, 2018. |
| \[FIPS180-4\] | FIPS 180-4, Secure Hash Standard (SHA-2), NIST, 2015. |
| \[FIPS202\] | FIPS 202, SHA-3 Standard: Permutation-Based Hash and Extendable-Output Functions (SHAKE), NIST, 2015. |
| \[FIPS198-1\] | FIPS 198-1, The Keyed-Hash Message Authentication Code (HMAC), NIST, 2008. |
| \[ACVP\] | NIST ACVP — Automated Cryptographic Validation Protocol; test vectors usnistgov/ACVP-Server v1.1.0.41. |
| \[QUDO-DOCS\] | QUDO PQC repository documentation set (README, FIPS Security Policy, Crypto-Officer & User Guidance, CAST mapping, Vendor Evidence). |

Table 6 — References and applicable standards.

# Appendix B Acronyms & Definitions

| **Term** | **Definition** |
|----|----|
| ACVP | Automated Cryptographic Validation Protocol (NIST) — known-answer test vectors and protocol. |
| AES | Advanced Encryption Standard (FIPS 197). |
| CAST | Cryptographic Algorithm Self-Test (FIPS 140-3). |
| CAVP | Cryptographic Algorithm Validation Program (NIST). |
| CMVP | Cryptographic Module Validation Program (NIST/CCCS). |
| CTR_DRBG | Counter-mode Deterministic Random Bit Generator (SP 800-90A). |
| DRBG | Deterministic Random Bit Generator. |
| FIPS | Federal Information Processing Standard. |
| HMAC | Keyed-Hash Message Authentication Code (FIPS 198-1). |
| IG | Implementation Guidance (CMVP). |
| KAT | Known Answer Test. |
| KEM | Key Encapsulation Mechanism. |
| ML-DSA | Module-Lattice-Based Digital Signature Algorithm (FIPS 204). |
| ML-KEM | Module-Lattice-Based Key-Encapsulation Mechanism (FIPS 203). |
| MLWE / MSIS | Module Learning-With-Errors / Module Short-Integer-Solution (lattice problems). |
| NEON | Arm Advanced SIMD instruction set (aarch64). |
| NIST | National Institute of Standards and Technology. |
| PCT | Pairwise Consistency Test (conditional self-test on key generation). |
| POST | Power-On Self-Test. |
| PQC | Post-Quantum Cryptography. |
| SHA / SHAKE | Secure Hash Algorithm (FIPS 180-4) / SHA-3 extendable-output function (FIPS 202). |
| SLH-DSA | Stateless Hash-Based Digital Signature Algorithm (FIPS 205). |
| SP | NIST Special Publication. |
| SSP | Sensitive Security Parameter (FIPS 140-3). |

Table 7 — Acronyms and definitions.
