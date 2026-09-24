# Cross-implementation PQC TLS interop harness

Drives real TLS 1.3 handshakes between QudoSSL and independent implementations,
for every PQ hybrid group, and records the negotiated group. This is the
reproducible harness behind [`docs/interop-report.md`](../../docs/interop-report.md);
run it from a clean checkout instead of trusting a one-off transcript.

## Why there is Go source in a C product

The evidence question interop answers is: *does QudoSSL's delegated ML-KEM
produce wire-compatible handshakes with implementations we did not write?*
Testing our OpenSSL fork against stock OpenSSL only partly answers that — the
two share code lineage. A **clean-room** peer is stronger evidence.

Go's `crypto/tls` with in-stdlib ML-KEM (Go 1.24+) is that peer, and it is the
lowest-friction one available: the two `.go` files import **only the standard
library** (`go.mod` has no `require` block), so the peer needs a Go toolchain
and nothing else. BoringSSL and AWS-LC — the durable peers Story 3.2's exit gate
requires — each need a full C library build; `oqs-provider` is retained in-tree
but its submodule is not initialized and running it inside our own OpenSSL would
not be independent. Go is therefore the **provisional first** independent peer,
not the permanent answer. See ADR-0012.

The Go peer is **test-only and strictly optional**:

- It is not referenced by any `.github/workflows/` build job and never enters
  the product or any OE build.
- `matrix.sh` builds it **lazily**, only for cases that actually use it. The
  OpenSSL-only cases (`qudo`/`brew`) touch no Go toolchain.
- A `go` case on a machine without Go prints `SKIP` and exits 0, so it never
  breaks a run of C-only cases.

## Peers

| Tag | Implementation | Independent of us? | Dependency |
|-----|----------------|--------------------|------------|
| `qudo` | QudoSSL (`openssl/apps/openssl`) | this is the system under test | built in-tree |
| `brew` | stock OpenSSL 3.x | shares OpenSSL lineage | system OpenSSL |
| `go`   | Go `crypto/tls` (stdlib ML-KEM) | yes, clean-room | Go 1.24+ (optional) |

`brew` **must** be neutralised with `OPENSSL_CONF=/dev/null` (the harness does
this) — a developer machine may have QudoSSL's own provider activated in the
system OpenSSL config, which would make the peer run our code. See
`docs/interop-report.md` §1.

## Usage

```sh
# one case: <server> <client> <group> <port>   (server/client in: qudo|brew|go)
ci/interop/matrix.sh qudo brew X25519MLKEM768 24101

# full matrix, both directions, all hybrids
for g in X25519MLKEM768 SecP256r1MLKEM768 SecP384r1MLKEM1024; do
  ci/interop/matrix.sh qudo brew "$g" 24101
  ci/interop/matrix.sh brew qudo "$g" 24102
  ci/interop/matrix.sh qudo go   "$g" 24103
  ci/interop/matrix.sh go   qudo "$g" 24104
done
```

Environment overrides: `QUDOSSL_DIR` (built OpenSSL tree, default `../../openssl`),
`BREW_OPENSSL` (path to the stock `openssl` binary), `INTEROP_WORK` (scratch dir
for the generated cert, built Go peers, and per-case logs).

## Files

- `matrix.sh` — the driver
- `server/main.go`, `client/main.go` — the Go peer (one group per case, pinned
  on both sides, so a PASS proves the group was actually negotiated)
- `go.mod` — stdlib only, no dependencies

## Adding the durable peers

To satisfy the ≥3-peer exit gate, add `bssl` (BoringSSL) and `awslc` (AWS-LC)
cases to the `case ${S}`/`case ${C}` blocks in `matrix.sh`, mirroring the
existing OpenSSL invocations. Both are built on the Linux CI runners, not on a
dev workstation.
