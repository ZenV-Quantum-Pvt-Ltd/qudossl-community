# Architecture Decision Records

Each ADR records one decision that overrides or narrows the frozen design
(`design/QudoSSL_Design.docx`). Precedence, per `CLAUDE.md`:
**post-freeze ADR → v1.0 Decision Log (D1–D5) → design body.**
`docs/design-errata.md` maps the affected design sections to the deciding ADR.

| ADR | Decision | Status |
|-----|----------|--------|
| [0003](ADR-0003-epic2-boundary-dedupe-closure.md) | Epic 2 boundary-dedupe closes as Story 2.1 + 2.5 | Superseded by 0009 |
| [0004](ADR-0004-openssl-baseline-3.5.7.md) | OpenSSL baseline pinned to tag `openssl-3.5.7` | Accepted |
| [0005](ADR-0005-crypto-layer-delegation.md) | PQC by crypto-layer delegation, not a provider adapter | Accepted |
| [0006](ADR-0006-fips-module-filename-unchanged.md) | FIPS module stays `fips.so` (no rename) | Accepted |
| [0007](ADR-0007-retain-oqs-and-pkcs11-providers.md) | Retain oqs-provider and pkcs11-provider in the build | Accepted |
| [0008](ADR-0008-repo-layout-subtree-paths.md) | qudo-pqc-lib vendored at `qudo-pqc-lib/`, parallel to `openssl/` | Accepted |
| [0009](ADR-0009-qudo-pqc-standard-build.md) | qudo-pqc-lib consumed as a standard build (see its Correction) | Accepted |
| [0010](ADR-0010-slh-dsa-stays-upstream.md) | SLH-DSA is not delegated; OpenSSL's implementation is used | Accepted (provisional) |
| [0011](ADR-0011-ml-kem-dead-math-already-eliminated.md) | ML-KEM's superseded math is not gated; the compiler already removes it | Accepted |
| [0012](ADR-0012-interop-peers.md) | Interop peers: Go now, BoringSSL/AWS-LC for the exit gate | Accepted |

ADR-0011 and 0012 were added in Sprint 3. Numbering begins at 0003; 0001–0002
are not present in this repository.
