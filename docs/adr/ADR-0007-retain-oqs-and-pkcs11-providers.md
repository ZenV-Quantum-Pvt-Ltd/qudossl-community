# ADR-0007 — Retain oqs-provider and pkcs11-provider in the build

- **Status:** Accepted
- **Date:** 2026-07-22 (post-freeze amendment)
- **Design ref:** amends §3 Phase 0 and Story 1.5
- **Sprint 1 change ref:** C1

## Context

The design instructs that `oqs-provider` and `pkcs11-provider` be dropped from
the build at Configure time during Phase 0. A reviewer comment on that line was
left unresolved: *"it may effect any test codes or not need to see"* →
*"probably will keep as is without removing and we can revisit later."*

## Decision

**Retain both.** No Configure-time exclusion is added.

## What they actually are (verified 2026-07-22)

Both **are** present in upstream `openssl-3.5.7`, but as **git submodules**, not
as source directories:

```
160000 7bc597c04b534ddea9b6654481deb31ded8e1bbc 0  openssl/oqs-provider
160000 663dea335c80bec7fd96d544ff875af08d6461a9 0  openssl/pkcs11-provider
```

Mode `160000` is a gitlink. They sit alongside upstream's other external test
dependencies — `krb5`, `gost-engine`, `wycheproof`, `tlsfuzzer`,
`pyca-cryptography`, `python-ecdsa`, `tlslite-ng`, `cloudflare-quiche`,
`fuzz/corpora` — all declared in `openssl/.gitmodules`.

Because QudoSSL clones without `--recursive` (D4), these directories are
**empty** in our tree. They are not compiled by `./Configure && make`; upstream
uses them only for its own interop and fuzz CI.

## Consequences

- **Retaining them is zero work.** There is nothing to build, exclude, or
  maintain. The gitlinks ride along inside the subtree as inert entries.
- Conversely, *dropping* them would have meant editing `.gitmodules` and
  removing gitlink entries — a gratuitous diff against upstream that would add
  noise to every future CVE cherry-pick, for no build-time benefit. That is the
  strongest argument for this decision.
- **No cert impact.** Neither appears in
  `openssl/providers/fips.module.sources`; neither is inside the boundary.
- The oqs hybrid-group path is unaffected — §18.1 lists loss of it as a low
  risk, and nothing here changes it either way.
- Revisit only if a future sprint needs `--recursive` for interop testing, at
  which point these submodules would actually be populated.
