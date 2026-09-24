# QUDO PQC — SSP & Key-Management

> **Source of truth.** This Markdown file is the canonical source for this document. The branded Word edition is generated from it; regenerate rather than editing the Word copy.

# 1 Purpose & Scope

This document inventories every Sensitive Security Parameter (SSP) the QUDO PQC module handles and describes its full lifecycle — generation, establishment, storage, use and zeroisation — as implemented in the source. FIPS 140-3 distinguishes Critical Security Parameters (CSPs — secret material whose disclosure or modification compromises security) from Public Security Parameters (PSPs — public keys whose modification compromises security). Both are catalogued here. The module is software-only; all SSPs reside in process memory (caller-owned buffers for the Direct API, or transient module scratch).

# 2 SSP Inventory

Each row is derived from the wrapper, PCT, DRBG and integrity sources. 'Zeroised' indicates the module applies \`qudo_secure_clear\`/\`qudo_cleanse\` at the listed trigger; caller-owned buffers (Direct API outputs) are the caller's responsibility to clear.

| **SSP** | **Class** | **Generation / source** | **Storage** | **Zeroisation trigger** |
|----|----|----|----|----|
| ML-KEM secret key | CSP | ML-KEM.KeyGen via CTR_DRBG (or seed) | Caller buffer / module scratch | On PCT failure (pk+sk); CAST temp key before free |
| ML-KEM public key | PSP | ML-KEM.KeyGen | Caller buffer | On PCT failure (with sk) |
| ML-KEM shared secret | CSP | Encaps/Decaps output | Caller buffer / PCT scratch | PCT scratch cleansed (pct.c:76-78) |
| ML-KEM keygen seed / encaps randomness | CSP | 64-byte seed / 32-byte randomness from CTR_DRBG | Stack scratch | Cleansed after use; PCT entropy cleansed |
| ML-DSA secret key | CSP | ML-DSA.KeyGen via CTR_DRBG (or seed) | Caller buffer / scratch | On PCT failure; CAST temp before free |
| ML-DSA public key | PSP | ML-DSA.KeyGen | Caller buffer | On PCT failure |
| ML-DSA signing randomness (rnd) | CSP | 32 bytes from CTR_DRBG (hedged sign) | Stack scratch | PCT rnd cleansed (pct.c:134) |
| SLH-DSA secret key (sk_seed, sk_prf) | CSP | SLH-DSA.KeyGen via CTR_DRBG (or 3 seeds) | Caller buffer / scratch | On PCT failure; CAST temp before free |
| SLH-DSA public key (pk_seed, pk_root) | PSP | SLH-DSA.KeyGen | Caller buffer | On PCT failure |
| SLH-DSA additional randomness (addrnd) | CSP | From CTR_DRBG (hedged sign) | Stack scratch | Cleansed after sign |
| CTR_DRBG key K | CSP | Derived from seed material (SP 800-90A update) | DRBG context (heap/static) | ctx free + reseed (ctrdrbg.c:72-73,183) |
| CTR_DRBG value V | CSP | Derived from seed material | DRBG context | ctx free + reseed |
| Entropy seed (48 bytes) | CSP | Platform RNG (getentropy/getrandom/BCrypt/urandom) | Stack scratch | Cleansed after instantiate (rand.c:358) |
| Module integrity key (fixed_key\[32\]) | Non-CSP (fixed/known) | Compile-time constant (qudo_fipskey.h) | .rodata | Not secret — integrity MAC key, public by design |

Table 4 — Sensitive Security Parameter inventory (CSP = critical/secret, PSP = public).

# 3 Key Lifecycle

```text
   platform entropy ──► CTR_DRBG (AES-256, SP 800-90A) ──► randomness
                                                              │
                              ┌───────────────────────────────┤
                              ▼                               ▼
                         KeyGen (sk, pk)                 signing rnd / addrnd
                              │                               │
                       PCT (round-trip)                  Sign / Encaps
                       fail ─► zeroise sk,pk ─► ERROR         │
                              ▼                               ▼
                         in-use by caller (Object/Direct)  output to caller
                              │
                       free / fail / CAST ─► qudo_secure_clear ─► [zeroised]
```

Table 5 — SSP lifecycle: generation → PCT → use → zeroisation.

Generation. All module-generated key material and signing randomness derive from the internal AES-256 CTR_DRBG, which is seeded from platform entropy and gated by SP 800-90B health tests. Deterministic paths (\`\*\_from_seed\`, \`\*\_internal\`) instead take caller-supplied seeds.

Use & access. In FIPS builds, every key-generating operation runs a pairwise consistency test before the key is released; a failure zeroises the candidate key and drives the module to ERROR. Cryptographic services are unavailable unless the module is RUNNING, the DRBG is ready and the requested algorithm meets the configured minimum security level.

# 4 Zeroisation

The single zeroisation primitive is \`qudo_secure_clear\` (src/fips/qudo_fips_aes.h:18-37), which selects, in order, \`SecureZeroMemory\` (Windows), \`memset_s\` (C11 Annex K), \`explicit_bzero\` (OpenBSD/glibc ≥ 2.25), or a volatile-write-plus-barrier fallback that the compiler cannot elide. The public wrapper \`qudo_pqc_cleanse\` is NULL/0-guarded. Around fifteen source files invoke it; \`\*\_is_zeroized\` predicates let tests confirm the result (verified by test_pct_fail_zeroize.c).

| **Trigger** | **What is zeroised** |
|----|----|
| PCT failure (keygen) | Both candidate public and secret keys, then ERROR state |
| Handle / context free | DRBG K/V, HMAC key/pad/context, CAST temporary secret keys |
| After use (transient) | Seeds, signing randomness, entropy buffers, KAT/PCT scratch |
| DRBG reseed | Prior DRBG state buffers before re-key |

Table 6 — Zeroisation triggers and targets.

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
