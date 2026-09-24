# QUDO PQC — Requirements Traceability Matrix

> **Source of truth.** This Markdown file is the canonical source for this document. The branded Word edition is generated from it; regenerate rather than editing the Word copy.

# 1 Purpose & Method

This matrix traces each relevant standard requirement to the implementing component/function and to the executed test evidence (CTest case and/or NIST ACVP vector group). It is intended for audit and security review; it describes implementation alignment and does not assert certification. Evidence references: CTest = the module test suite; ACVP = NIST vectors v1.1.0.41 (2,103 vectors, 0 failures).

# 2 Algorithm Requirements (FIPS 203 / 204 / 205)

| **Requirement** | **Implementation** | **Test evidence** |
|----|----|----|
| ML-KEM KeyGen (FIPS 203) | QUDO_KEM_keypair → mlkem\*\_keypair | ACVP ML-KEM keyGen (75); pqc_mlkem_seed_derand; POST keygen ×3 |
| ML-KEM Encaps/Decaps + implicit reject | QUDO_KEM_encaps/decaps → mlkem\*\_enc/dec | ACVP encapDecap (165); POST KEM 512; pqc_pct_mlkem |
| ML-KEM parameter sets & sizes | mlkem_auto_generated.h; \*\_get\_\*\_size | mlkem_config; ALGORITHMS |
| ML-DSA KeyGen (FIPS 204) | QUDO_MLDSA_keypair → mldsa\*\_keypair | ACVP ML-DSA keyGen (75); pqc_pct_mldsa |
| ML-DSA Sign (pure/context/extmu/prehash) | QUDO_MLDSA_sign\* → mldsa\*\_signature\* | ACVP sigGen (360); pqc_mldsa_seed_context |
| ML-DSA Verify | QUDO_MLDSA_verify\* → mldsa\*\_verify | ACVP sigVer (180); mldsa_sign_verify_bounds |
| SLH-DSA KeyGen (FIPS 205) | QUDO_SLHDSA_keypair → slh_keygen | ACVP SLH-DSA keyGen (120); pqc_pct_slhdsa |
| SLH-DSA Sign (ctx/addrnd/prehash), 12 sets | QUDO_SLHDSA_sign\* → slh_sign\* | ACVP sigGen (624); pqc_slhdsa_paramsets |
| SLH-DSA Verify | QUDO_SLHDSA_verify\* → slh_verify | ACVP sigVer (504); slhdsa_sign_bounds |

Table 4 — Algorithm-standard traceability (FIPS 203/204/205).

# 3 Module Requirements (FIPS 140-3 / SP 800-90A/90B)

| **Requirement** | **Implementation** | **Test evidence** |
|----|----|----|
| Power-on self-tests (§9) | qudo_pqc_run_post\_\* + self_test_data.inc | pqc_post(\_kats/\_negative/\_selftest/\_statemachine) |
| Cryptographic algorithm self-tests (IG 10.3.A) | run_cast\_\* (12 IDs) | pqc_cast(\_individual/\_runall/\_invalid) |
| Pairwise consistency test (IG 10.3.A) | qudo_pqc\_\*\_pct + wrapper invocation | pqc_pct\_\*; test_pct_fail_zeroize |
| SSP zeroisation (§9.7) | qudo_secure_clear / qudo_cleanse | test_pct_fail_zeroize; pqc_utils_cleanse |
| Module integrity (§9) | HMAC-SHA-256 over .text; embedded/cnf | pqc_integrity(\_hmac/\_real/\_negative); path_pinning |
| DRBG (SP 800-90A) | qudo_fips_ctrdrbg.c (AES-256 CTR_DRBG) | pqc_drbg_kat; fips_ctrdrbg_cavp (CAVP) |
| DRBG reseed / limits | RESEED_INTERVAL 2²⁰; request cap 65536 | pqc_drbg_reseed_interval/chunking/explicit_reseed |
| Entropy health (SP 800-90B) | RCT/APT/startup (qudo_fips_rand.c) | pqc_drbg\_\* ; entropy_health_fail path |
| Finite state model (§4) | g_module_state INIT/SELFTEST/RUNNING/ERROR | pqc_post_statemachine; pqc_init\_\* |
| Approved-mode indicator (IG 2.4.C) | qudo_fips_ind_check_operation | pqc_indicator(\_approval/\_unapproved/\_check) |
| Audit (vendor evidence) | qudo_audit_log + callback | pqc_audit(\_severity/\_components/\_rate) |

Table 5 — Module-requirement traceability (FIPS 140-3 + SP 800-90A/90B).

Coverage summary: every requirement row maps to at least one executed CTest case and, for the approved algorithms, to a passing NIST ACVP vector group. Aggregate executed evidence: 116 CTest cases (100% pass) and 2,103 ACVP vectors (0 failures) on the platform recorded in the test reports.

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
