# ADR-0010 — SLH-DSA is not delegated; OpenSSL's implementation is used

- **Status:** Accepted (provisional — revisit with x86-64 data)
- **Date:** 2026-07-23
- **Design ref:** narrows Decision Log D2 / ADR-0005; affects §6.1, §15.2, Sprint 2 Story 2.3
- **Depends on:** ADR-0005 (crypto-layer delegation), ADR-0009 (math-only build)

## Context

D2 delegates all three PQC families to qudo-pqc. ML-KEM and ML-DSA are done.
SLH-DSA delegation was implemented, tested green, and then measured — and the
measurements did not support keeping it.

## Decision

**`crypto/slh_dsa/` stays byte-identical to upstream `openssl-3.5.7`.** SLH-DSA
is served by OpenSSL's own implementation. ML-KEM and ML-DSA remain delegated.

## Evidence

**1. qudo's SLH-DSA is slower.** macOS arm64, spawn-adjusted, 10 iterations:

| Variant | Upstream OpenSSL | qudo (delegated) | |
|---|---|---|---|
| SLH-DSA-SHA2-128s | **145 ms** | 231 ms | 1.6× slower |
| SLH-DSA-SHA2-128f | **8 ms** | 13 ms | 1.6× slower |
| SLH-DSA-SHAKE-128s | **345 ms** | 361 ms | ~equal |

This is structural rather than an implementation defect. SLH-DSA is almost
entirely repeated hashing over a hypertree — there is no lattice arithmetic for
qudo's optimised code to accelerate — and OpenSSL's SHA-2/SHA-3 assembly is
long-tuned. The SHA-2 gap exceeds the SHAKE gap, consistent with qudo having
hand-written AArch64 Keccak but leaning on generic SHA-2.

**2. It duplicates approved functions inside the boundary.** qudo-pqc ships its
own SHA-2, SHA-3/Keccak and SHAKE (`fips202.c.o`, `sha2_256.c.o`,
`keccakf1600.c.o`, `slh_sha2.c.o`, `slh_shake.c.o`, plus AArch64 assembly).
§6.1 assigns SHA to OpenSSL's `crypto/sha/`. Delegating SLH-DSA placed a second
SHA-2 and Keccak inside the certified module — the "two equivalent approved
functions" finding the dedupe exists to prevent.

Measured effect of this decision on the module:

```
QUDO_SLHDSA symbols   79 -> 0
slh_sha2 / slh_shake  present -> absent
sha2_256 / keccak_f1600   still present (pulled in by ML-KEM / ML-DSA)
```

**3. OpenSSL's SLH-DSA is complete.** Verified on the pinned tag: all 12
parameter sets keygen/sign/verify with FIPS 205-correct signature sizes, correct
NIST OIDs (`2.16.840.1.101.3.4.3.20`–`.31`), X.509 certificates generate and
self-verify, 21 `slh_dsa` entries in `fips.module.sources`, and the FIPS module
exposes all 12 after `fipsinstall` (`INSTALL PASSED`).

**4. SLH-DSA cannot be used in TLS today.** OpenSSL 3.5.7 registers zero
SLH-DSA TLS signature algorithms (ML-DSA has one), because the IETF codepoints
are not assigned. Its uses are certificates, firmware and document signing —
none latency-critical, and none where 1.6× matters commercially.

## Consequences

- **Sprint 2 Story 2.3 is closed as "not required"** rather than done. The
  delegation code was written and verified before being reverted; it is
  recoverable from git history if this decision is revisited.
- **Smaller CAVP surface.** qudo's SLH-DSA no longer needs algorithm validation
  within our boundary, since it is not inside it.
- **The duplicate-hash problem is reduced, not solved.** ML-KEM and ML-DSA still
  pull in `fips202` and `sha2_*`; ADR-0003 already established that their SHAKE
  use is inlined and not routable to a host provider. That remains open and
  must be declared to the lab.
- **Divergence from upstream shrinks** to `Configure`, `crypto/ml_kem/` and
  `crypto/ml_dsa/`. `crypto/slh_dsa/` is byte-identical, so SLH-DSA CVEs
  cherry-pick cleanly with no conflict.
- **§15.2 needs correcting** before the Security Policy is drafted: it lists
  SLH-DSA as sourced from qudo-pqc-lib. It is not.

## Revisit criteria

This is provisional on one machine and one OS. Re-measure on **Linux x86-64**,
where qudo's AVX2 paths may change the result, and reconsider if qudo's SLH-DSA
becomes materially faster than upstream's. The commercial argument — one vendor
for all three PQC families — is legitimate but was not judged sufficient against
slower code and a larger cert boundary.
