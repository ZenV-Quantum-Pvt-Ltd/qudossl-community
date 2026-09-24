# ADR-0005 — PQC integration by crypto-layer delegation, not a provider adapter

- **Status:** Accepted
- **Date:** 2026-07-14 (baseline freeze)
- **Design ref:** Decision Log D2
- **Supersedes:** design §9, §10, §12, §20, §22.2–22.3, §23.3, Appendix A

## Context

qudo-pqc-lib supplies the ML-KEM, ML-DSA and SLH-DSA math. Something in the
OpenSSL tree has to route EVP operations to it. Two seams were available:

1. **Provider level** — rewrite the bodies of
   `providers/implementations/{kem,signature,keymgmt}/*.c`.
2. **Crypto-core level** — delegate from the `crypto/ml_kem`, `crypto/ml_dsa`
   and `crypto/slh_dsa` entry points that the provider calls into.

The document body was written during design exploration and describes option 1
throughout. This ADR records the decision to use option 2.

## Decision

Delegate at the **crypto-core layer**:

- Edit the ~24 `ossl_ml_kem_*` entry points and their ML-DSA / SLH-DSA
  equivalents in `crypto/ml_kem`, `crypto/ml_dsa`, `crypto/slh_dsa` to call
  into qudo-pqc.
- Leave `providers/implementations/{keymgmt,signature,kem,encode_decode}`
  **untouched**.
- Keep OpenSSL's key structs and standard encoders.
- Compile out OpenSSL's PQC math internals while keeping the entry-point shims.
- Inherit OpenSSL's FIPS self-tests as a cross-check on qudo-pqc.

## Rationale

The crypto↔provider seam is a clean, narrow byte API, and the provider never
reaches into key-struct internals. Delegating there is a small, localized change
that **inherits** OpenSSL's mature provider, EVP, codec, self-test and TLS
integration. A provider-level adapter would have to rebuild all of it, and would
have to keep rebuilding it as upstream evolves.

It also aligns with the standing instruction to minimize changes to upstream
OpenSSL: delegation touches ~24 functions in three directories, where the
adapter approach rewrites six provider files plus their helpers.

## Consequences

- Epic 3 is recast from "Adapter Layer" to "Crypto-Layer Delegation":
  roughly 29 tasks down to 22, and ~21.5–33 dev-days down to ~15–24.
- Epic 6 keeps the `crypto/ml_*` entry-point shims and compiles out the math
  internals, instead of dropping the directories from the build entirely.
- The listed design sections are **reference-only**. Only §10, §12 and Epic 3
  carry an inline supersession note; §9, §20, §22.2–22.3, §23.3 and Appendix A
  do not, and will mislead a reader who follows them literally. In particular,
  Appendix A's `ml_kem_kem.c` adapter example is not the design to implement.
- One PQC implementation serves the whole product: because delegation happens
  below the provider layer, the default provider routes through qudo-pqc too.
