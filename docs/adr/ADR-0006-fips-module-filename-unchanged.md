# ADR-0006 — The FIPS module build output stays `fips.so`

- **Status:** Accepted
- **Date:** 2026-07-22 (post-freeze amendment)
- **Design ref:** amends §7.1, §1.2, §1.5, §5, §15.1; deletes Story 6.4
- **Sprint 1 change ref:** C8

## Context

The design renames the FIPS module build output from `fips.so` to
`qudo-fips.so`, via the `MODULES[]` name in `providers/fips/build.info`. It
describes this as "the ONLY externally-visible Qudo branding" and treats the
renamed binary as the artifact the certificate binds to.

## Decision

**No rename.** The module builds and ships as `openssl/providers/fips.so`,
identical to upstream.

## Consequences

- **Story 6.4 is deleted** and **Story 6.5 becomes a no-op**, because upstream's
  `apps/fipsinstall.c` default module path already points at `fips.so`. Saves
  approximately 1.5–2 days in Epic 6.
- Combined with ADR-0007, the rebrand list inside `openssl/` is now **empty**.
  Every source line stays identical to upstream, which is the maximum-cleanliness
  case for the CVE cherry-picks described in §15.4 — and §7.2's stated goal.
- **Cert identity moves entirely to the paperwork.** The design deliberately kept
  the filename as the one carrier of Qudo identity. With the rename cancelled,
  the Security Policy, module version and repo SHA carry it instead. §15.1's
  boundary description must be reworded before submission — it currently names
  `qudo-fips.so` as the boundary.
- **Install-path collision must be checked.** The installed module at
  `<libdir>/ossl-modules/fips.so` is now filename-identical to a stock OpenSSL
  FIPS provider. Confirm the install path cannot collide on a host that also has
  system OpenSSL FIPS installed.
- Acceptance test E1.2 in §23.1 asserts `qudo-fips.so`; it now checks `fips.so`,
  permanently.
