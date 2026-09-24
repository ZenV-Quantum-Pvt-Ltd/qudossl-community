# ADR-0003 — Epic 2 boundary dedupe closes as Story 2.1 + Story 2.5

- **Status:** **Superseded by ADR-0009** (2026-07-22)
- **Date:** 2026-07-14 (baseline freeze)
- **Design ref:** Decision Log D5; §6.1, §6.3; Part V Epic 2

> **Superseded.** ADR-0009 consumes qudo-pqc-lib as a **standard** build, so
> `QUDO_FIPS_MODULE` and the boundary-dedupe flags are not used at all. Story
> 2.1 (host DRBG) is unnecessary because randomness is passed in as a seed
> argument from OpenSSL's approved DRBG; Story 2.5 (disable embedded HMAC) is
> unnecessary because the embedded HMAC is never invoked. Epic 2 has no
> remaining QudoSSL scope. This record is retained for the reasoning below,
> which still explains why flags 2.2/2.3/2.4 were never viable.

## Context

Both OpenSSL's FIPS provider and qudo-pqc-lib independently implement AES, SHA,
HMAC and a DRBG, because each was designed to be self-contained. Inside one
FIPS 140-3 boundary there must be exactly one implementation of each primitive,
otherwise cert review raises a "two equivalent approved functions" finding.

Epic 2 was originally scoped as seven stories (2.1–2.7), each adding a
`QUDO_PQC_USE_HOST_*` build flag to route one primitive to the host.
Investigation during design showed that most of those flags are either
unnecessary or not implementable.

## Decision

Epic 2 closes as **two** stories:

- **Story 2.1** — route qudo-pqc randomness through the host DRBG via
  `qudo_pqc_set_rand_provider()`, **fail-closed** if no provider is registered.
- **Story 2.5** — disable qudo-pqc's embedded HMAC and integrity check so that
  OpenSSL's `fipsmodule.cnf` flow owns integrity for the boundary.

The remaining stories are dispositioned as follows:

- **2.2** (host hashes) — **not routable.** Keccak is inlined into the PQC
  algorithm implementations for performance; there is no call seam to redirect.
- **2.3** (host AES) — **no-op.** AES is only referenced by qudo-pqc's own DRBG,
  which Story 2.1 already removes from the boundary.
- **2.4** (host HMAC), **2.6**, **2.7** — folded into the Epic 5 build
  configuration rather than carried as separate flag work.

## Consequences

- AES/SHA/HMAC dedupe is achieved through the Epic 5 build configuration, not
  through per-primitive runtime hooks.
- Because 2.2 is not routable, the boundary retains qudo-pqc's inlined Keccak.
  This must be declared to the lab during pre-engagement; it is PQC algorithm
  code, not a separately claimed SHA-3 implementation.
- Epic 2 shrinks from roughly 8–12 days to the 2.1 + 2.5 pair.
- qudo-pqc-lib's standalone build is unaffected — all changes are additive and
  guarded by build flags, so non-QudoSSL consumers keep their own primitives.
