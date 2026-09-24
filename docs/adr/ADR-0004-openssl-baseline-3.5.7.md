# ADR-0004 — OpenSSL baseline is 3.5 LTS, pinned to tag `openssl-3.5.7`

- **Status:** Accepted
- **Date:** 2026-07-14 (baseline freeze)
- **Design ref:** Decision Log D1; §4.2, §4.3

## Context

QudoSSL vendors upstream OpenSSL as a git subtree at `openssl/`. The baseline
version determines the classical algorithm set inside the cert boundary, the
API surface the PQC delegation attaches to, and how long upstream will publish
CVE fixes we can cherry-pick.

## Decision

Pin the subtree to the **released tag `openssl-3.5.7`** (commit
`8cf17aaeb4599f8af87fefd810b5b5fee90fe69e`).

The design's worked example in §4.2 subtrees from the `openssl-3.5` *branch*.
That is a moving target and cannot support a reproducible cert SHA, so the tag
is used instead. This is a correction to the example, not to the decision — D1
already specifies the tag.

## Alternatives considered

- **OpenSSL 4.0.1 (latest).** Rejected. Likely ~2-year support window, major
  version churn requiring re-validation of every in-place touch point, and its
  new PQC surface is irrelevant to us because PQC is sourced from qudo-pqc-lib.
- **Track the `openssl-3.5` branch.** Rejected. No fixed SHA, so neither the
  cert artifact nor the reproducible build could be pinned.

## Consequences

- 3.5 LTS support runs to approximately 2030, covering the cert's life.
- The 3.5.7 FIPS provider algorithm set was verified complete for our classical
  needs.
- ZenV owns product support for qudossl and for the certification, so this
  choice is not constrained by OpenSSL's own LTS or cert timeline.
- Upstream sync happens on `main` only, via `git subtree pull`. Cert release
  branches never take subtree pulls — CVE cherry-picks only, using
  `-Xsubtree=openssl` for path translation.
- Advancing to a newer LTS later requires re-validating exact symbol names and
  struct fields against that version before committing.
