# QUDO PQC — Threat Model & Security Architecture

> **Source of truth.** This Markdown file is the canonical source for this document. The branded Word edition is generated from it; regenerate rather than editing the Word copy.

# 1 Purpose & Scope

This document records the threat model for the QUDO PQC cryptographic module: the assets it protects, the trust boundaries and attack surface, the adversaries considered, and the threats with the implemented mitigations. It supports security review and informs (but does not replace) the FIPS 140-3 Security Policy. Scope is the module (\`libqudo-pqc\`); the separate OpenSSL provider and host application are out of scope except as callers.

# 2 Assets

- Secret key material (ML-KEM/ML-DSA/SLH-DSA secret keys, shared secrets) and signing randomness — see the SSP inventory (QUDO-PQC-SSP-001).

- DRBG internal state (key K, value V, seed/entropy) — compromise affects all generated keys.

- Module integrity — the unmodified \`.text\` of the cryptographic boundary.

- Correctness of results — shared-secret agreement and signature unforgeability.

# 3 Trust Boundaries & Architecture

```text
 ┌──────────────── host process (untrusted w.r.t. the module) ────────────────┐
 │  application code  ─── calls ───►  PUBLIC API (validated, gated)            │
 │                                       │                                     │
 │   ┌─────────────── cryptographic module boundary (.text, HMAC-pinned) ───┐ │
 │   │  wrappers ─► *-native math ─► FIPS primitives (AES/DRBG/HMAC/SHA)     │ │
 │   │  FIPS services: state machine · POST · CAST · PCT · integrity ·       │ │
 │   │                 indicators · audit                                    │ │
 │   └──────────────────────────────┬────────────────────────────────────────┘ │
 │                                   │ entropy request                        │
 └───────────────────────────────────┼─────────────────────────────────────────┘
                                      ▼
                          operating-system RNG (trusted entropy source)
```

Table 4 — Trust boundaries. The module boundary is the HMAC-protected .text region; the caller and OS sit outside it.

# 4 Adversary Model

| **Adversary** | **Capability assumed** |
|----|----|
| Network attacker | Observes/modifies ciphertexts, signatures and messages in transit |
| Malicious / buggy caller | Supplies malformed, oversized, NULL or tampered inputs to the API |
| Local attacker (same host) | May attempt to read process memory or substitute the module image |
| Timing/side-channel observer | Measures execution time of secret-dependent operations |
| Supply-chain attacker | Targets the vendored upstream or build pipeline |

Table 5 — Adversaries considered.

# 5 Attack Surface

- Public API inputs — keys, ciphertexts, signatures, messages, context strings, seeds.

- Serialization parsers — DER/PEM import (variable-length, attacker-influenced).

- Randomness — the CTR_DRBG and its platform entropy source.

- Integrity configuration — the external \`qudofipsmodule.cnf\` and module file path (Windows).

- Vendored \`\*-native\` code and the build/link pipeline.

# 6 Threats & Mitigations

| **Threat** | **Mitigation in the implementation** |
|----|----|
| Tampered module image / code substitution | HMAC-SHA-256 over the pinned .text boundary at init; path-pinning (Windows cnf); fails closed if un-patched |
| Weak / inconsistent keys (RNG or generation fault) | PCT on every generated/imported keypair with fail-zeroise; SP 800-90B DRBG health tests; ERROR latch |
| Predictable randomness | SP 800-90A AES-256 CTR_DRBG seeded from OS entropy; reseed every 2²⁰; RCT/APT continuous + startup health tests |
| Malformed / hostile input | NULL/length/bounds validation at every entry; check_pk/check_sk; context-length caps; 65 536-byte DRBG request cap |
| Timing side-channel | Constant-time AES (T-table compiled out); constant-time comparison (qudo_memcmp_ct) for all KAT/PCT/integrity checks; see QUDO-PQC-SCA-001 |
| Forged signature / wrong shared secret | Verification rejects any tamper; ML-KEM implicit rejection; KAT/ACVP conformance |
| Secret recovery from freed memory | qudo_secure_clear at every free and failure path; secure-alloc (mlock) option |
| Operating in a degraded state | Finite-state machine gates all crypto on RUNNING + DRBG-ready + approved + min-security-level; ERROR is terminal until fini() |
| Supply-chain compromise | Formally-verified upstreams pinned in-tree; CycloneDX SBOM; symbol-visibility version script; reproducible FIPS boundary checks |

Table 6 — Threat → mitigation matrix.

# 7 Residual Risks & Assumptions

- Side-channel resistance is screened informally (dudect-style), not by an accredited lab; the lattice math originates from the vendored upstream.

- The OS entropy source and platform RNG are trusted; the module adds health testing but does not itself characterise the noise source (see entropy-assessment gap).

- Memory protection relies on the host OS; the module zeroises but cannot prevent a privileged attacker from reading live process memory.

- Results are evidence for the tested platform; other environments require their own runs.

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

Table 7 — References and applicable standards.

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

Table 8 — Acronyms and definitions.
