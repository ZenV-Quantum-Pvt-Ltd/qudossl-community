# ADR-0012 — Interop peers: Go now, BoringSSL/AWS-LC for the exit gate

- **Status:** Accepted
- **Date:** 2026-07-27
- **Design ref:** Sprint 3 Story 3.2; design §7.3–7.5, §16.2; Open Question Q4
- **Depends on:** ADR-0005 (crypto-layer delegation), ADR-0007 (retain oqs/pkcs11)

## Context

Story 3.2 requires evidence that QudoSSL's delegated ML-KEM interoperates with
implementations we did not write. The harness must be reproducible from a clean
checkout, which means the peer it drives has to be committed or clearly sourced.

The immediate question raised in review: **why does a C/OpenSSL product carry Go
source?** It deserves a recorded answer, because a lab reviewer will ask the
same thing.

## Decision

The interop harness (`ci/interop/`) uses **stock OpenSSL** and a **Go
`crypto/tls` peer** as its two independent implementations for now. Go is the
**provisional first** clean-room peer. **BoringSSL and AWS-LC are the durable
peers** required to close the story's ≥3-external-peer exit gate; they are added
on the Linux CI runners, not on a dev workstation.

The Go peer is test-only and optional: not in any build job, built lazily by
`matrix.sh` only for cases that use it, and skipped (exit 0) when no Go toolchain
is present.

## Rationale

**Why an independent peer at all.** QudoSSL is an OpenSSL fork. A handshake
between our fork and stock OpenSSL shares code lineage on both ends, so it is
weaker evidence than a handshake against a clean-room stack.

**Why Go specifically, for the first peer.** Go's `crypto/tls` uses an in-stdlib
ML-KEM (Go 1.24+) with a completely separate provenance from both OpenSSL and
qudo-pqc. It is also the lowest-dependency independent peer: the peer sources
import only the Go standard library (`go.mod` has no `require` block), so it
needs a Go toolchain and nothing else. It was verified working — 18/18
handshakes, both directions, all three hybrids — before this decision.

**Why not oqs-provider, despite ADR-0007 retaining it.** `openssl/oqs-provider`
is a submodule that is not initialized in this tree, and building it needs
liboqs. More decisively, oqs-provider loaded into *our* OpenSSL is not an
independent peer — it is our TLS stack with a different KEM provider. Using it as
a genuine peer would require a *separate* stock OpenSSL build plus liboqs plus
oqs-provider: more friction than Go, for no independence gain over it.

**Why Go is provisional, not permanent.** The exit gate names BoringSSL and
AWS-LC (Q4), both C, both built in CI. Once they are wired in, Go is redundant
and may be kept as a bonus clean-room peer or dropped to keep the tree C-only —
a call for when those peers land, not now.

## Consequences

- The repo gains a Go toolchain as an **optional, test-only** dependency. It is
  isolated under `ci/interop/`, absent from every product and OE build, and
  degrades to `SKIP` when Go is missing. It cannot affect the shipped artifacts
  or the certified module.
- Interop evidence is reproducible from a clean checkout via `ci/interop/`.
- Two independent peers exist today (stock OpenSSL, Go); the third and fourth
  (BoringSSL, AWS-LC) remain open work for the exit gate.

## Revisit if

- A repo policy forbids non-C toolchains outright — then drop Go and accept
  weaker interim evidence until BoringSSL/AWS-LC land.
- BoringSSL and AWS-LC are wired in — re-decide whether Go still earns its place.
- oqs-provider's submodule is initialized and built as part of a separate stock
  peer — it then becomes an additional C-ecosystem peer option.
