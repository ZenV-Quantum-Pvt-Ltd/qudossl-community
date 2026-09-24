# ADR-0008 — qudo-pqc-lib is vendored at `qudo-pqc-lib/`, parallel to `openssl/`

- **Status:** Accepted
- **Date:** 2026-07-22 (post-freeze amendment)
- **Design ref:** amends D4 and §1.1, §4.2, §4.3, §9, §12.3, §14.3, §19.2, §22.3
- **Sprint 1 change ref:** C9

## Context

The design vendors the PQC subtree at `external/qudo-pqc/` — an `external/`
parent acting as a mount point, with the leaf name shortened to `qudo-pqc`.

## Decision

Drop the `external/` parent and keep the upstream repo name as the leaf. The
subtree lands at **`qudo-pqc-lib/`**, parallel to `openssl/` at the repo root.
The subtree mechanism itself is unchanged.

```
qudossl/
├── openssl/       <- git subtree, tag openssl-3.5.7
├── qudo-pqc-lib/  <- git subtree, pinned release SHA
├── build/ ci/ docs/ cert/ reproducible-build/
└── design/ sprint/
```

## Rationale

§1.4 already states that both subtrees are equally "QUDO OWNS", so an
`external/` parent understated qudo-pqc-lib's status. Naming is now consistent:
`openssl/` mirrors the `openssl/openssl` repo, `qudo-pqc-lib/` mirrors the
`qudo-pqc-lib` repo.

## Consequences

Mechanical but broad — every path reference moves.

- **Sprint 1:** subtree prefix (Story 1.3), build wrapper paths (Story 1.4),
  acceptance test E1.1.
- **Later:** `--with-qudo-pqc-dir` / `--with-qudo-pqc-archive` (Story 6.1); the
  `fips.module.sources` entries, which become `../qudo-pqc-lib/...` (Story 6.2);
  `providers/fips/build.info` (Story 6.3); ACVP runner paths in Epic 7.
- §4.2's "what does Qudo own" command becomes:
  `git ls-files | grep -v '^openssl/' | grep -v '^qudo-pqc-lib/'`
- **The built archive is still `libqudo-pqc.a`.** Only the directory is renamed,
  not the CMake output. Do not rename the archive to match — the
  `--with-qudo-pqc-archive` path depends on it.
- No cert or boundary impact.

`fips.module.sources` does not glob; every path in it is enumerated by hand.
Settling this name before Story 6.2 runs avoids a large mechanical rename later.
