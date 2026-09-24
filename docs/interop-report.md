# Cross-Implementation PQC TLS Interoperability Report

Story 3.2 — QudoSSL 3.5.7 (ML-KEM delegated to qudo-pqc-lib under `QUDO_PQC_DELEGATE`)

## Summary

QudoSSL's delegated ML-KEM key exchange interoperated cleanly in every case tested: 18 of
18 TLS 1.3 handshakes involving QudoSSL completed successfully across both directions
(QudoSSL as server and as client) against two genuinely independent peers — stock OpenSSL
3.6.1 and Go 1.26.0 `crypto/tls` — covering all three hybrid groups and, where the peer
supports them, all three pure ML-KEM groups. The single most important result in this
document, however, is methodological rather than numerical: the Homebrew OpenSSL 3.6.1
installed on this machine is **not** an independent implementation as configured, because
`/opt/homebrew/etc/openssl@3/openssl.cnf` activates *our own* `qudoprovider` and sets
`default_properties = ?provider=qudoprovider`, so ML-KEM lookups on that "reference" peer
resolve to our code. Any interop run performed against it without neutralizing that config
tests our ML-KEM against our ML-KEM and is circular — worthless as certification evidence.
Every result reported below was obtained with `OPENSSL_CONF=/dev/null` on every OpenSSL
endpoint (the Go peer has no OpenSSL configuration to neutralize). One limitation should be
read alongside the 18/18 figure: in only 3 of the 18 cases is the negotiated group named
independently by both endpoints; in the other 15 it is named by the client and inferred at
the server from single-group pinning plus a completed handshake (section 3.1). Two secondary
corrections to the Story 3.2 plan are recorded: Go 1.26 supports three ML-KEM hybrids (not
one), and the story's certificate recipe produces a CN-only certificate that Go rejects
outright for reasons unrelated to PQC.

## Scope

**In scope.** TLS 1.3 key exchange using ML-KEM, both as hybrid (X25519MLKEM768,
SecP256r1MLKEM768, SecP384r1MLKEM1024) and pure (MLKEM512, MLKEM768, MLKEM1024) named
groups, between QudoSSL and external implementations, in both client and server roles.
Group negotiation, handshake completion, and application-data flow.

**Out of scope / not exercised.**

- The FIPS provider. Neutralizing the config with `OPENSSL_CONF=/dev/null` also means no
  `fips.dylib` is loaded; the runs below used the **default** provider (measured:
  `list -providers` under the harness environment prints only
  `default / OpenSSL Default Provider / 3.5.7`). The delegated ML-KEM code is present in
  both libraries — `crypto/ml_kem/libcrypto-shlib-ml_kem.o` and
  `crypto/ml_kem/libfips-lib-ml_kem.o` both carry undefined references to `_QUDO_KEM_new`,
  `_QUDO_KEM_keypair_from_seed`, `_QUDO_KEM_encaps_derand`, `_QUDO_KEM_decaps`,
  `_QUDO_KEM_free`, and `providers/fips.dylib` defines `T _QUDO_KEM_decaps`,
  `T _QUDO_KEM_encaps_derand`, `T _QUDO_KEM_keypair_from_seed`, `T _QUDO_KEM_new` — so the
  same delegated math is compiled into the FIPS module, but **no handshake in this report
  went through the FIPS provider**. See [Open items](#open-items).
- ML-DSA on the wire. All handshakes used a P-256 ECDSA certificate
  (`Signature Algorithm: ecdsa-with-SHA256`, `NIST CURVE: P-256`). The delegated ML-DSA
  signing path was not exercised by any handshake in this matrix.
- SLH-DSA, which is deliberately not delegated per
  [ADR-0010](adr/ADR-0010-slh-dsa-stays-upstream.md). Confirmed in the built artifact:
  `nm -u crypto/slh_dsa/libcrypto-shlib-*.o | grep -c QUDO` returns `0`, while
  `crypto/ml_dsa/libcrypto-shlib-ml_dsa_key.o` references `_QUDO_MLDSA_new`,
  `_QUDO_MLDSA_free`, `_QUDO_MLDSA_keypair_internal`.
- Session resumption, HRR (hello retry request), 0-RTT, and DTLS.

**Provenance of the evidence in this document.** Claims about the local environment
(configuration files, installed peers, group lists, symbol tables, certificate contents,
Go source) were measured directly on 2026-07-23 and are cited with `file:line` or the exact
command. The handshake matrix was run against working tree `c69d9b3`
(branch `feat/sprint3-test-interop`); the environment claims were re-verified at `33adc62`
on the same branch. The three intervening commits (`b9cddd3`, `938c32e`, `33adc62`) touch
only `ci/check-boundary-symbols.sh`, `ci/check-delegation-linked.sh`,
`.github/workflows/ci.yml` and `build/build.ps1` — nothing under `openssl/` or
`qudo-pqc-lib/` — so they cannot have changed the binary under test or any measurement
below.

Claims about individual handshake outcomes were taken from the harness log artifacts
produced by the interop run; those logs were read, not re-executed, during the writing of
this report. Where a distinction matters it is stated explicitly. One quoted line — the Go
client's side of the negative control — has **no surviving log artifact**; see section 3,
item 4.

---

## 1. Methodology trap: the reference peer is contaminated (READ FIRST)

### 1.1 What was found

The only OpenSSL peer installed on this machine is Homebrew's `openssl@3` 3.6.1. Its
system configuration file loads and *prefers* QudoSSL's own provider:

```
/opt/homebrew/etc/openssl@3/openssl.cnf:58   alg_section = algorithm_sect
/opt/homebrew/etc/openssl@3/openssl.cnf:60   [algorithm_sect]
/opt/homebrew/etc/openssl@3/openssl.cnf:63   default_properties = ?provider=qudoprovider
/opt/homebrew/etc/openssl@3/openssl.cnf:65   [provider_sect]
/opt/homebrew/etc/openssl@3/openssl.cnf:72   [qudoprovider_sect]
/opt/homebrew/etc/openssl@3/openssl.cnf:74   module = /opt/homebrew/opt/openssl@3/lib/ossl-modules/qudoprovider.dylib
```

Measured, with the machine's default configuration:

```
$ /opt/homebrew/opt/openssl@3/bin/openssl list -providers
Providers:
  default
    name: OpenSSL Default Provider
    version: 3.6.1
    status: active
  qudoprovider
    name: Qudo PQC FIPS Provider
    version: 1.0.0
    status: active
```

The `?provider=qudoprovider` property is a *preference*, not a requirement: anything
`qudoprovider` does not implement falls back to `default`. ML-KEM is exactly what it does
implement, so ML-KEM lookups on this peer resolve to our code.

### 1.2 Why this is fatal to naive interop evidence

An interop matrix run against this peer in its default state does not compare two
independent implementations of FIPS 203. It compares QudoSSL's delegated ML-KEM against a
*different build of the same vendor's* ML-KEM. The handshakes succeed, the logs look green,
and the evidence is circular. A CMVP lab will reject it, and — this is the dangerous part —
it fails silently and looks correct.

Supporting measurements that the contamination is live in the TLS key-exchange path, not
merely a registration artifact:

- Under the default config, `openssl list -tls-groups -tls1_3` on the 3.6.1 peer advertises
  13 additional groups that the same binary does not advertise under
  `OPENSSL_CONF=/dev/null` (section 5.3 below). `qudoprovider` is the only difference
  between the two invocations, so those groups come from it.
- Two default-config 3.6.1 instances negotiated `X25519MLKEM512` — a group absent from
  stock 3.6.1 — with `Negotiated TLS1.3 group: X25519MLKEM512`. *(Read from the surviving
  log artifact `logs/q1.c.log`, not re-executed for this report. This is the measurement
  that shows the contamination reaches the key-exchange path, not just the algorithm
  registry.)*

### 1.3 The neutralization, and the standing requirement

`OPENSSL_CONF=/dev/null` suppresses the system configuration and restores a genuinely
independent peer. Measured:

```
$ OPENSSL_CONF=/dev/null /opt/homebrew/opt/openssl@3/bin/openssl list -providers
Providers:
  default
    name: OpenSSL Default Provider
    version: 3.6.1
    status: active
```

A provider listing alone would not settle the question — vendor code linked *into* the peer's
`libcrypto` would survive any config change. It is not. Measured:

```
$ nm -g /opt/homebrew/Cellar/openssl@3/3.6.1/lib/libcrypto.3.dylib | grep -c QUDO
0
$ otool -L /opt/homebrew/Cellar/openssl@3/3.6.1/lib/libcrypto.3.dylib
  /opt/homebrew/opt/openssl@3/lib/libcrypto.3.dylib
  /usr/lib/libSystem.B.dylib
```

The peer's `libcrypto` carries zero `QUDO` symbols and has no link-time dependency on
`libqudo-pqc`. The contamination is confined to the dynamically loaded provider module, so
suppressing the config is sufficient to remove it — it is not merely hiding it.

> **Standing requirement.** Every interop run against a peer on this machine — and every
> future CI interop job, on any runner — MUST:
>
> 1. Export `OPENSSL_CONF=/dev/null` (or point at a known-clean, vendor-free config) for
>    the peer process, and
> 2. **Assert** before the matrix runs that `openssl list -providers` on the peer prints
>    `default` and nothing else, failing the job if any other provider is active, and
> 3. Record that assertion's output alongside the handshake logs as part of the evidence
>    package.
>
> Step 2 is not optional decoration. Without it the suite silently degrades to testing our
> own math against itself the moment a config file changes, and the failure mode is a
> green run.

The same neutralization is applied to the QudoSSL side of the harness, for the same reason
in reverse: it guarantees the QudoSSL process is running our in-tree delegated libcrypto and
not picking up an installed provider from the machine.

### 1.4 Provenance of the contamination (unresolved)

The Homebrew Cellar carries a full set of vendor artifacts:

```
/opt/homebrew/Cellar/openssl@3/3.6.1/lib/ossl-modules/qudoprovider.dylib   root:admin, Jun 22 15:09
/opt/homebrew/Cellar/openssl@3/3.6.1/lib/libqudo-pqc.1.0.0.dylib           Jun 19 16:31
/opt/homebrew/Cellar/openssl@3/3.6.1/bin/qudo_fipsinstall                  root:admin, Jun  8 19:17
```

`qudoprovider.dylib` and `qudo_fipsinstall` are owned by `root`, so they were installed with
elevated privileges. The Cellar copy of `qudo_pqc.h` (8,497 bytes, dated Jun 8) differs from
the in-tree subtree copy (9,649 bytes) despite both declaring
`QUDO_PQC_VERSION_STRING "1.0.0"` at line 40 of each file (with identical
`QUDO_PQC_VERSION_MAJOR/MINOR/PATCH` at lines 37–39) — i.e. the contaminating library is a
*different revision of our own code* carrying the same version string, masquerading as a
reference implementation. Which revision it is has **not** been established; only that the
headers differ. Who installed it, and whether the same contamination exists on the CI
runners that will produce the official evidence, is **not established**. See
[Open items](#open-items).

---

## 2. Test environment

| Role | Implementation | Version | Binary / source |
|---|---|---|---|
| System under test | QudoSSL (OpenSSL 3.5.7 fork, ML-KEM delegated) | `OpenSSL 3.5.7 9 Jun 2026` | `/Users/venkateshpulimamidi/qudosslwork/qudossl/openssl/apps/openssl` |
| Peer A | Stock OpenSSL, config neutralized | 3.6.1 | `/opt/homebrew/opt/openssl@3/bin/openssl` |
| Peer B | Go `crypto/tls` | go1.26.0 darwin/arm64 | `/opt/homebrew/Cellar/go/1.26.0/libexec` |
| Host | macOS arm64 (Darwin 25.5.0) | — | — |

QudoSSL build under test, from `openssl version -a`:

```
compiler: cc -fPIC -arch arm64 -O3 -Wall ... -DQUDO_PQC_DELEGATE -DNDEBUG
          -I.../qudo-pqc-lib/include -I.../qudo-pqc-lib/qudo-mlkem/include
          -I.../qudo-pqc-lib/qudo-mldsa/include -I.../qudo-pqc-lib/qudo-slhdsa/include
```

`-DQUDO_PQC_DELEGATE` is present, so the ML-KEM/ML-DSA delegation of
[ADR-0005](adr/ADR-0005-crypto-layer-delegation.md) is compiled in.

### 2.1 Candidate peers that were rejected, and why

| Candidate | Status | Evidence |
|---|---|---|
| Homebrew OpenSSL 3.6.1, **default config** | Rejected — not independent | Section 1 |
| Homebrew OpenSSL 3.6.1, `OPENSSL_CONF=/dev/null` | **Accepted** | `list -providers` → `default` only |
| Go 1.26.0 `crypto/tls` | **Accepted** | Section 4.1 |
| GnuTLS 3.8.12 | Rejected — no ML-KEM | `gnutls-cli --list` Groups line: `GROUP-SECP192R1 … GROUP-FFDHE8192`, zero ML-KEM entries |
| LibreSSL (`/usr/bin/openssl`) | Rejected — predates PQC | `LibreSSL 3.3.6` |
| BoringSSL / AWS-LC | Not installed | `command -v bssl boringssl aws-lc` → empty; no matching Cellar directory |
| Stock OpenSSL 3.5.x parity baseline | Not installed | Only `/opt/homebrew/Cellar/openssl@3/3.6.1` exists |

**Only two genuinely independent PQ-capable peers exist on this machine.** Story 3.2's exit
gate calls for at least three. That gate is **not met**; see [Open items](#open-items).

---

## 3. Harness design

The harness is committed at [`ci/interop/`](../ci/interop/): `matrix.sh` (driver),
`server/main.go` and `client/main.go` (the Go peer), plus `go.mod`. The original results
were produced by an equivalent script living in a scratch directory; it has since been
tidied and vendored so the evidence is reproducible from a clean checkout. The vendored
driver derives its own paths, builds the Go peers on first use, and generates a
SAN-bearing certificate into `$INTEROP_WORK` (default `$TMPDIR/qudossl-interop`) rather
than depending on anything preexisting.

Re-verified after vendoring, on the committed harness: `qudo<-brew`, `brew<-qudo`,
`qudo<-go` and `go<-qudo` all PASS for X25519MLKEM768, and `qudo<-go` passes for
SecP384r1MLKEM1024, with application data flowing in each case.

The Go peer is a **provisional first** independent implementation, not the
permanent answer: it is clean-room and dependency-light (in-stdlib ML-KEM, no
third-party modules), and it is built lazily and skipped when absent, so the
OpenSSL-only cases need no Go toolchain. BoringSSL and AWS-LC are the durable
peers required to close the ≥3-peer exit gate (§ open items). See
[`ci/interop/README.md`](../ci/interop/README.md) for the harness and
[ADR-0012](adr/ADR-0012-interop-peers.md) for the peer-choice rationale.

Design properties that make the results auditable:

1. **One group per case, pinned on both sides.** The OpenSSL endpoints are invoked with
   `-groups <G>` (a single group), and the Go endpoints set
   `CurvePreferences: []tls.CurveID{cid}` with exactly one entry. A successful handshake
   therefore *proves* that group was negotiated — there is no other group to fall back to.
2. **Negotiated group captured from the wire, not assumed.** OpenSSL clients report
   `Negotiated TLS1.3 group: <G>`; the Go client reports
   `RESULT=PASS group=<G> negotiated=<G> …` from `ConnectionState().CurveID`; the Go server
   reports `SERVER_OK negotiated=<G> …`.
3. **Application data after the handshake.** The Go client sends `GET / HTTP/1.0` and logs
   the first response line as `APPDATA_RX`, so a PASS means the derived keys actually work,
   not merely that the handshake messages parsed.
4. **A negative control**, to prove the harness can fail. QudoSSL `s_server -groups MLKEM768`
   against a Go client offering only `X25519MLKEM768` produced, in
   `logs/neg.server.log`:

   ```
   error:0A000065:SSL routines:final_key_share:no suitable key share:ssl/statem/extensions.c:1409
   0 client connects that finished
   0 server accepts that finished
   ```

   The client was reported as printing `err=remote error: tls: handshake failure`, but
   **no `neg.client.log` exists** — that half of the negative control was captured in the
   run transcript only and has no surviving artifact. The server log alone is sufficient to
   establish the point (the handshake was refused, zero connections finished), and it is the
   only half cited as evidence here. PASS is therefore not silent fallback.
5. **A control pair with QudoSSL absent** (Go ↔ stock 3.6.1, both directions, three hybrids)
   to establish that the harness itself is sound independent of the system under test.

### 3.1 Known limitation of the evidence capture

Both-endpoint confirmation of the negotiated group is available only where **Go is the
server** — `openssl s_server` does not print the negotiated group in the configuration used.
Measured over the log set: exactly six server logs contain a group line, and they are
`go-qudo-*.server.log` (3) and `go-brew-*.server.log` (3).

Of those six, **only three involve QudoSSL** — the `go-brew-*` trio is the control pair of
section 4.2, in which QudoSSL is absent. So the honest count for the 18-case matrix of
section 4.1 is:

| Evidence strength | Count | Cases |
|---|---|---|
| Both endpoints name the group | 3 of 18 | cases 16–18 (`go-qudo-*`) |
| Client names the group; server confirms completion only | 15 of 18 | cases 1–15 (`qudo-brew-*`, `brew-qudo-*`, `qudo-go-*`) |

The fifteen OpenSSL-server logs contain `1 server accepts that finished`, which confirms the
server-side handshake completed but does **not** independently name the group. Note that
cases 13–15 (QudoSSL server ← Go client) fall in this group too: the Go *client* names the
group, but the QudoSSL `s_server` does not.

For those fifteen cases the group name comes from one endpoint only. The single-group
pinning in (1) is what makes that sufficient — with exactly one group offered and one
accepted, a completed TLS 1.3 handshake cannot have used any other — but it is an argument
from configuration rather than a second independent observation, and a lab may treat it as
such. A future harness revision should add `-state`/`-trace` or an equivalent so both ends
name the group. This is listed in [Open items](#open-items).

---

## 4. Result matrix

### 4.1 Handshakes involving QudoSSL — 18 cases, 18 PASS, 0 FAIL

Direction is written *server ← client*.

| # | Server | Client | Group | Result | Negotiated-group evidence |
|---|---|---|---|---|---|
| 1 | QudoSSL | stock 3.6.1 | X25519MLKEM768 | PASS | CLI `Negotiated TLS1.3 group: X25519MLKEM768`; SRV `1 server accepts that finished` |
| 2 | QudoSSL | stock 3.6.1 | SecP256r1MLKEM768 | PASS | CLI `Negotiated TLS1.3 group: SecP256r1MLKEM768`; SRV `1 server accepts that finished` |
| 3 | QudoSSL | stock 3.6.1 | SecP384r1MLKEM1024 | PASS | CLI `Negotiated TLS1.3 group: SecP384r1MLKEM1024`; SRV `1 server accepts that finished` |
| 4 | QudoSSL | stock 3.6.1 | MLKEM512 | PASS | CLI `Negotiated TLS1.3 group: MLKEM512`; SRV `1 server accepts that finished` |
| 5 | QudoSSL | stock 3.6.1 | MLKEM768 | PASS | CLI `Negotiated TLS1.3 group: MLKEM768`; SRV `1 server accepts that finished` |
| 6 | QudoSSL | stock 3.6.1 | MLKEM1024 | PASS | CLI `Negotiated TLS1.3 group: MLKEM1024`; SRV `1 server accepts that finished` |
| 7 | stock 3.6.1 | QudoSSL | X25519MLKEM768 | PASS | CLI `Negotiated TLS1.3 group: X25519MLKEM768`; SRV `1 server accepts that finished` |
| 8 | stock 3.6.1 | QudoSSL | SecP256r1MLKEM768 | PASS | CLI `Negotiated TLS1.3 group: SecP256r1MLKEM768`; SRV `1 server accepts that finished` |
| 9 | stock 3.6.1 | QudoSSL | SecP384r1MLKEM1024 | PASS | CLI `Negotiated TLS1.3 group: SecP384r1MLKEM1024`; SRV `1 server accepts that finished` |
| 10 | stock 3.6.1 | QudoSSL | MLKEM512 | PASS | CLI `Negotiated TLS1.3 group: MLKEM512`; SRV `1 server accepts that finished` |
| 11 | stock 3.6.1 | QudoSSL | MLKEM768 | PASS | CLI `Negotiated TLS1.3 group: MLKEM768`; SRV `1 server accepts that finished` |
| 12 | stock 3.6.1 | QudoSSL | MLKEM1024 | PASS | CLI `Negotiated TLS1.3 group: MLKEM1024`; SRV `1 server accepts that finished` |
| 13 | QudoSSL | Go 1.26 | X25519MLKEM768 | PASS | CLI `RESULT=PASS group=X25519MLKEM768 negotiated=X25519MLKEM768 version=TLS 1.3 cipher=TLS_AES_128_GCM_SHA256`, `APPDATA_RX="HTTP/1.0 200 ok"` |
| 14 | QudoSSL | Go 1.26 | SecP256r1MLKEM768 | PASS | CLI `RESULT=PASS … negotiated=SecP256r1MLKEM768`, `APPDATA_RX="HTTP/1.0 200 ok"` |
| 15 | QudoSSL | Go 1.26 | SecP384r1MLKEM1024 | PASS | CLI `RESULT=PASS … negotiated=SecP384r1MLKEM1024`, `APPDATA_RX="HTTP/1.0 200 ok"` |
| 16 | Go 1.26 | QudoSSL | X25519MLKEM768 | PASS | CLI `Negotiated TLS1.3 group: X25519MLKEM768`; **SRV** `SERVER_OK negotiated=X25519MLKEM768 version=TLS 1.3 cipher=TLS_AES_128_GCM_SHA256` |
| 17 | Go 1.26 | QudoSSL | SecP256r1MLKEM768 | PASS | CLI + **SRV** both name `SecP256r1MLKEM768` |
| 18 | Go 1.26 | QudoSSL | SecP384r1MLKEM1024 | PASS | CLI + **SRV** both name `SecP384r1MLKEM1024` |

Counts by pair, exactly as the logs support:

| Pair | Direction | Groups attempted | PASS | FAIL |
|---|---|---|---|---|
| 1 | QudoSSL server ← stock OpenSSL 3.6.1 client | 6 (3 hybrid + 3 pure) | 6 | 0 |
| 2 | Stock OpenSSL 3.6.1 server ← QudoSSL client | 6 (3 hybrid + 3 pure) | 6 | 0 |
| 3 | QudoSSL server ← Go 1.26 client | 3 (hybrid only) | 3 | 0 |
| 4 | Go 1.26 server ← QudoSSL client | 3 (hybrid only) | 3 | 0 |
| **Total involving QudoSSL** | | **18** | **18** | **0** |

Pairs 3 and 4 attempt three groups rather than six because Go implements no pure ML-KEM
group; that is an expected negative, characterized in section 5.2, not a failure.

Independently of the harness's own PASS logic, all 36 log files backing the 18 cases were
re-grepped for `alert`, `error`, `failure` and `no suitable`: zero matches. The 18/18 result
therefore also holds under the stricter criteria written in section 8.7.

### 4.2 Supporting runs (QudoSSL not under test)

| Pair | Direction | Cases | Result | Purpose |
|---|---|---|---|---|
| Control | stock 3.6.1 server ← Go client, and Go server ← stock 3.6.1 client | 6 (3 hybrid × 2 directions) | 6 PASS | Validates the harness independently of QudoSSL |
| Negative control | QudoSSL server (`-groups MLKEM768`) ← Go client (X25519MLKEM768 only) | 1 | Expected FAIL, observed | Proves the harness can fail |

**These six control cases are not interop evidence for QudoSSL and must not be added to the
18.** The grand total of handshakes executed was 25 (18 + 6 control + 1 negative control).

---

## 5. Group support and asymmetries

### 5.1 QudoSSL vs stock OpenSSL 3.6.1: no asymmetry

Both implementations advertise byte-identical TLS 1.3 group lists. Measured today:

```
$ DYLD_LIBRARY_PATH=<repo>/openssl OPENSSL_CONF=/dev/null <repo>/openssl/apps/openssl list -tls-groups -tls1_3
$ OPENSSL_CONF=/dev/null /opt/homebrew/opt/openssl@3/bin/openssl list -tls-groups -tls1_3
```

both print:

```
secp256r1:secp384r1:secp521r1:x25519:x448:brainpoolP256r1tls13:brainpoolP384r1tls13:
brainpoolP512r1tls13:ffdhe2048:ffdhe3072:ffdhe4096:ffdhe6144:ffdhe8192:
MLKEM512:MLKEM768:MLKEM1024:SecP256r1MLKEM768:X25519MLKEM768:SecP384r1MLKEM1024
```

(Line breaks above are editorial; the tool emits one colon-separated line. The two outputs
are identical including ordering.)

Note for anyone re-running this: the subcommand is `list -tls-groups -tls1_3`. The form
`list -tls1_3-kem-key-exchange-groups` **does not exist** in either build and exits 1 with
`list: Unknown option:`.

### 5.2 Go 1.26 — correction to the Story 3.2 premise

**Go 1.26 supports three ML-KEM hybrids, not one.** The plan's assumption that Go offers
only X25519MLKEM768 is outdated and understates coverage by two-thirds.

```
$(go env GOROOT)/src/crypto/tls/common.go:153   X25519MLKEM768     CurveID = 4588
$(go env GOROOT)/src/crypto/tls/common.go:154   SecP256r1MLKEM768  CurveID = 4587
$(go env GOROOT)/src/crypto/tls/common.go:155   SecP384r1MLKEM1024 CurveID = 4589
```

All three are **on by default**, ahead of the classical curves:

```go
// $(go env GOROOT)/src/crypto/tls/defaults.go:29-33
default:
    return []CurveID{
        X25519MLKEM768, SecP256r1MLKEM768, SecP384r1MLKEM1024,
        X25519, CurveP256, CurveP384, CurveP521,
    }
```

Go's own comment at `common.go:809-810` records the change: "From Go 1.26, the default
includes the [SecP256r1MLKEM768] and [SecP256r1MLKEM768] hybrid post-quantum key exchanges,
too." That is quoted verbatim — the comment names `SecP256r1MLKEM768` twice, an upstream Go
documentation typo; `defaults.go:29-33` above is the authoritative statement of what is
actually offered. Two environment
knobs roll the default back — `GODEBUG=tlsmlkem=0` to the pre-Go-1.24 classical set
(`defaults.go:24-25`), and `GODEBUG=tlssecpmlkem=0` to the pre-Go-1.26 set, which is
X25519MLKEM768 as the *only hybrid* alongside the four classical curves, not X25519MLKEM768
alone (`defaults.go:26-28`). A peer's Go version alone therefore does not determine its
offer set; the harness should keep pinning groups explicitly.

**Go supports no pure ML-KEM group.** The `CurveID` const block at `common.go:149-155`
contains only P-256/P-384/P-521/X25519 plus the three hybrids; there are no MLKEM512/768/1024
identifiers. Empirically the harness reports
`RESULT=FAIL reason=go-does-not-support-group group=MLKEM768` on the client side and
`SERVER_FATAL unsupported-group=MLKEM768` on the server side. **This should be recorded in
the matrix as an expected negative, not as an interop failure.**

### 5.3 The one real group asymmetry: vendor-extension hybrids

This asymmetry is a consequence of the contamination described in section 1 and is worth
documenting because customers will hit it. With its default config, the 3.6.1 peer
advertises 13 groups beyond what the same binary advertises under `OPENSSL_CONF=/dev/null`;
loading `qudoprovider` is the only difference between the two runs, so those 13 come from
it. Measured today, default-config `list -tls-groups -tls1_3` appends:

```
P256MLKEM512, P384MLKEM768, P521MLKEM1024, X25519MLKEM768Composite, X25519MLKEM512,
X448MLKEM768, X448MLKEM1024, ML-KEM-512, ML-KEM-768, ML-KEM-1024,
BrainpoolP256MLKEM512, BrainpoolP384MLKEM768, BrainpoolP512MLKEM1024
```

QudoSSL rejects these at configuration time, before any network traffic. Measured:

```
$ ... apps/openssl s_client -connect 127.0.0.1:1 -groups X25519MLKEM512
Call to SSL_CONF_cmd(-groups, X25519MLKEM512) failed
$ echo $?
1
```

This is a hard configuration error, not a graceful downgrade. QudoSSL implements exactly six
ML-KEM groups, with the codepoints it inherits unchanged from upstream OpenSSL 3.5.7:

| Group | Codepoint | Definition |
|---|---|---|
| MLKEM512 | `0x0200` (512) | `openssl/include/internal/tlsgroups.h:59` |
| MLKEM768 | `0x0201` (513) | `openssl/include/internal/tlsgroups.h:60` |
| MLKEM1024 | `0x0202` (514) | `openssl/include/internal/tlsgroups.h:61` |
| SecP256r1MLKEM768 | `0x11EB` (4587) | `openssl/include/internal/tlsgroups.h:62` |
| X25519MLKEM768 | `0x11EC` (4588) | `openssl/include/internal/tlsgroups.h:63` |
| SecP384r1MLKEM1024 | `0x11ED` (4589) | `openssl/include/internal/tlsgroups.h:64` |

They are entered in the TLS group table at
`openssl/providers/common/capabilities.c:88-93` and named at `:178-180`, `:157`,
`:192-193`. That table is upstream OpenSSL's and is untouched by QudoSSL, consistent with
the `providers/implementations/` boundary of
[ADR-0005](adr/ADR-0005-crypto-layer-delegation.md) — delegation replaces the math beneath
these groups, not the group set itself.

*Caveat on wording:* these six are the codepoints upstream OpenSSL assigns, measured from
the source above. They were **not** independently checked against the IANA TLS Supported
Groups registry as part of this report; the assertion that all six (and none of the 13
vendor-extension groups) are IANA-registered rests on upstream's assignment, not on a
registry lookup performed here. A lab-facing version of the customer note should cite the
registry directly.

A peer configured for a vendor-extension hybrid will get a startup failure rather than a
negotiated classical fallback, and that behaviour should appear in the customer-facing
interop note.

---

## 6. Certificate recipe gotcha: a SAN is mandatory

The Story 3.2 certificate recipe as written produces a **CN-only** certificate. Go rejects
it outright:

```
x509: certificate relies on legacy Common Name field, use SANs instead
```

and the server side sees `SSL alert number 42` (bad certificate). With the CN-only
certificate, **all three Go-client cases failed** — and they fail in a way that looks
exactly like a PQC interop failure while being pure X.509 policy. Go removed CommonName
fallback years ago; this has nothing to do with ML-KEM.

Adding a subject alternative name makes all three pass. The corrected recipe:

```bash
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes \
  -keyout k.pem -out c.pem -days 30 \
  -subj "/CN=localhost" \
  -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"
```

The certificate actually used in the passing runs carries, verified today:

```
subject=CN=localhost
X509v3 Subject Alternative Name:
    DNS:localhost, IP Address:127.0.0.1
Public Key Algorithm: id-ecPublicKey   NIST CURVE: P-256
Signature Algorithm: ecdsa-with-SHA256
```

The `-addext` clause must be propagated into the Story 3.2 sheet and into any interop
runbook before the next person runs the matrix.

---

## 7. Evidence that the PASSes exercised delegated code

An interop PASS is only meaningful for this project if the ML-KEM math actually came from
qudo-pqc-lib rather than OpenSSL's upstream reference implementation. Measured on the binary
used for the runs:

- `openssl version -a` shows `-DQUDO_PQC_DELEGATE` in the compiler line (section 2).
- `nm -u crypto/ml_kem/libcrypto-shlib-ml_kem.o | grep QUDO` yields `_QUDO_KEM_decaps`,
  `_QUDO_KEM_encaps_derand`, `_QUDO_KEM_free`, `_QUDO_KEM_keypair_from_seed`,
  `_QUDO_KEM_new` — i.e. the TLS-facing libcrypto object calls out to the delegated library
  for keygen, encapsulation and decapsulation.
- `nm -g libcrypto.3.dylib | grep -c QUDO_` → `123`; the delegated implementation is
  statically linked into the shared library that the harness loaded via
  `DYLD_LIBRARY_PATH`.

The source structure behind that. In `openssl/crypto/ml_kem/ml_kem.c` the three ML-KEM
operations dispatch through `#ifdef QUDO_PQC_DELEGATE`, with upstream's FIPS 203 routines
reachable only from the `#else` arm:

| Operation | Delegated call | Upstream path |
|---|---|---|
| keygen | `ml_kem.c:2390` `ret = qudo_genkey(...)` | `#else` at `:2391`, `#endif` `:2393` |
| encapsulate | `ml_kem.c:2445` `ret = qudo_encap(...)` | `#else` at `:2446`, `#endif` `:2461` |
| decapsulate | `ml_kem.c:2529` `ret = qudo_decap(...)` | `#else` at `:2530`, `#endif` `:2546` |

The three wrappers are defined at `ml_kem.c:2220` (`qudo_genkey`), `:2283` (`qudo_encap`)
and `:2318` (`qudo_decap`), inside the `#ifdef QUDO_PQC_DELEGATE` block spanning `:2159` to
`:2351`.

**Inference, not measurement.** From the above it follows that any ML-KEM operation routed
through `crypto/ml_kem/ml_kem.c` in this binary used qudo-pqc-lib, because with
`QUDO_PQC_DELEGATE` defined the `#else` arms are not compiled and the only surviving entry
points call the `QUDO_KEM_*` functions.

Two things that argument does **not** establish, stated plainly:

- It is a compile-time and link-time argument about one file. It does not by itself exclude
  ML-KEM being served by some other implementation loaded into the process. In these runs
  that possibility is closed by the provider measurement in the Scope section — the QudoSSL
  process loaded only the in-tree `default` provider — but that is a second, separate
  measurement, not a consequence of the first.
- No per-handshake instrumentation (e.g. a call counter inside qudo-pqc-lib) was used to
  confirm the delegated code executed on any specific handshake. If a lab wants a positive
  dynamic proof rather than a compile-and-link argument, that instrumentation does not yet
  exist.

---

## 8. Reproduction instructions

These are sufficient for an independent party to re-run the full matrix.

### 8.1 Prerequisites

| Item | Requirement | Check |
|---|---|---|
| QudoSSL build | Built with `QUDO_PQC_DELEGATE` | `apps/openssl version -a` must show `-DQUDO_PQC_DELEGATE` |
| Peer A | OpenSSL ≥ 3.5 with ML-KEM groups | `OPENSSL_CONF=/dev/null openssl list -tls-groups -tls1_3` must list `MLKEM768` |
| Peer B | Go ≥ 1.24 (1.26 for all three hybrids) | `go version` |
| Ports | 24100–24199 free | `lsof -ti tcp:<port>` |

### 8.2 Step 0 — mandatory contamination check (do not skip)

```bash
OPENSSL_CONF=/dev/null /opt/homebrew/opt/openssl@3/bin/openssl list -providers
```

The output must list `default` and nothing else. If any other provider appears, the peer is
not independent — stop and fix the environment. For an unattended job, make this an assertion:

```bash
provs=$(OPENSSL_CONF=/dev/null "$PEER" list -providers | grep -c '^  [a-z]')
[ "$provs" -eq 1 ] || { echo "PEER CONTAMINATED - aborting"; exit 1; }
```

Also record, for the evidence package, what the peer looks like *without* the neutralization
(`"$PEER" list -providers`), so the report shows the trap was actively avoided rather than
never encountered.

### 8.3 Step 1 — generate the certificate (with the SAN)

```bash
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes \
  -keyout k.pem -out c.pem -days 30 \
  -subj "/CN=localhost" \
  -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"
```

### 8.4 Step 2 — build the Go peer

`client/main.go` is 78 lines and `server/main.go` is 85. The essential properties to
reproduce:

- Map only the three hybrid names to `tls.CurveID`; return a distinguishable
  `go-does-not-support-group` result for anything else, so pure ML-KEM shows up as an
  expected negative rather than a crash.
- Client: `tls.Config{ RootCAs: pool, ServerName: "localhost", MinVersion: tls.VersionTLS13,
  CurvePreferences: []tls.CurveID{cid} }` — exactly one curve.
- Client prints `RESULT=PASS group=%s negotiated=%v version=%s cipher=%s` from
  `conn.ConnectionState()`, then writes `GET / HTTP/1.0\r\n\r\n` and logs the first response
  line as `APPDATA_RX` under a 3-second deadline (the OpenSSL peer runs `s_server -www`).
- Server prints `SERVER_READY` before accepting (the driver waits on that marker rather than
  consuming a connection), then `SERVER_OK negotiated=%v …` per connection.

```bash
go mod init interop && go build -o goclient ./client && go build -o goserver ./server
```

### 8.5 Step 3 — invocations

Neutralize the config on **both** OpenSSL endpoints:

```bash
QUDO=<repo>/openssl/apps/openssl
BREW=/opt/homebrew/opt/openssl@3/bin/openssl
qudo()    { DYLD_LIBRARY_PATH=<repo>/openssl OPENSSL_CONF=/dev/null "$QUDO" "$@"; }
brewssl() { OPENSSL_CONF=/dev/null "$BREW" "$@"; }
```

Server side (one group only, single-shot):

```bash
qudo s_server -accept $PORT -cert c.pem -key k.pem -groups $G -tls1_3 -www -naccept 1
./goserver 127.0.0.1:$PORT $G c.pem k.pem
```

Client side (one group only, verification enforced):

```bash
echo Q | qudo s_client -connect 127.0.0.1:$PORT -groups $G -tls1_3 \
              -CAfile c.pem -verify_return_error -servername localhost
./goclient 127.0.0.1:$PORT $G c.pem
```

### 8.6 Step 4 — the matrix to run

```
servers        = { qudo, brew, go }
clients        = { qudo, brew, go }
openssl groups = { X25519MLKEM768, SecP256r1MLKEM768, SecP384r1MLKEM1024,
                   MLKEM512, MLKEM768, MLKEM1024 }
go groups      = { X25519MLKEM768, SecP256r1MLKEM768, SecP384r1MLKEM1024 }
```

Run every (server, client) pair where at least one side is `qudo`, over the intersection of
both sides' supported groups. That yields the 18 cases of section 4.1. Add the
`(brew, go)` and `(go, brew)` pairs as the control, and the negative control of section 3
item 4.

### 8.7 Step 5 — pass criteria

- OpenSSL client: exit code 0 **and** a `Negotiated TLS1.3 group:` line naming exactly the
  requested group.
- Go client: a line beginning `RESULT=PASS` with `negotiated=` equal to the requested group.
- Go server (where used): `SERVER_OK negotiated=` equal to the requested group.
- Any `alert`, `error`, `no suitable key share`, `SERVER_HANDSHAKE_FAIL` or
  `SERVER_FATAL` line in either log is a FAIL.
- The negative control must FAIL. A run in which the negative control passes is void.

> **These are the criteria a re-run should apply, not the ones the scratch harness
> enforced.** `matrix.sh` adjudicated PASS more weakly: for a Go client, the presence of a
> `^RESULT=PASS` line; for an OpenSSL client, exit code 0 **and** a non-empty
> `Negotiated TLS1.3 group:` line — without checking that the named group equals the
> requested one, and without failing on `alert`/`error` (those are grepped for display on a
> FAIL only). Both weaknesses were closed by hand for this report: every one of the 18 cases
> was re-read and its negotiated group confirmed equal to the requested group, and all 36
> logs were confirmed free of `alert`/`error`/`failure`/`no suitable` (section 4.1). A
> harness promoted into the repository should implement section 8.7 as written rather than
> rely on that manual step.

### 8.8 Step 6 — cleanup verification

Confirm no listeners and no orphaned processes survive the run:

```bash
lsof -nP -iTCP -sTCP:LISTEN | awk '$9 ~ /:241[0-9][0-9]$/'   # must be empty
ps aux | grep -E 's_server|goserver'                          # must be empty
```

---

## 9. Measured / inferred / not done

| Statement | Status |
|---|---|
| The Homebrew 3.6.1 peer activates `qudoprovider` and sets it as the *optional* preferred provider (`?`), so it wins for every algorithm it implements — ML-KEM included — and falls back to `default` otherwise | **Measured** — `openssl.cnf:63,72,74`; `list -providers` |
| `qudoprovider` is live in the peer's TLS key-exchange path, not merely registered | **Measured** — two default-config 3.6.1 instances negotiated `X25519MLKEM512`, a qudoprovider-only group (`logs/q1.c.log`); read from the investigation log, not re-executed here |
| `OPENSSL_CONF=/dev/null` restores a single-provider stock peer | **Measured** |
| QudoSSL and stock 3.6.1 advertise identical TLS 1.3 group lists | **Measured** |
| The contaminated peer advertises 13 extra vendor groups | **Measured** |
| QudoSSL rejects vendor-extension groups at `SSL_CONF_cmd` time, exit 1 | **Measured** |
| Go 1.26 defines and defaults to three ML-KEM hybrids, and no pure ML-KEM group | **Measured** — `common.go:153-155`, `defaults.go:29-33` |
| A CN-only certificate is rejected by Go; a SAN fixes it | **Measured** |
| 18/18 QudoSSL handshakes PASS with the negotiated group named | **Measured by the harness run**; log artifacts read, not re-executed for this report |
| Both endpoints independently name the negotiated group | **Measured for 3 of the 18 cases** (cases 16–18, Go server ← QudoSSL client). The other 15 confirm server-side completion but name the group from one endpoint only. Six server logs in total carry a group line, but three of those are the QudoSSL-free control pair |
| The negotiated group equals the requested group in all 18 cases | **Measured** — every client log re-read; `matrix.sh` did not itself enforce equality (section 8.7) |
| The peer's own `libcrypto` is free of vendor code, so `OPENSSL_CONF=/dev/null` removes the contamination rather than hiding it | **Measured** — `nm -g` → 0 `QUDO` symbols, `otool -L` → no `libqudo-pqc` (section 1.3) |
| The delegated `QUDO_KEM_*` path is linked into the binary that ran the matrix | **Measured** — `nm` |
| The delegated path was the one executed during each handshake | **Inferred** from compile-time (`#ifdef`/`#else` structure, section 7), link-time (`nm`) and provider-loading evidence; no runtime instrumentation exists |
| The delegated ML-KEM interoperates under the **FIPS provider** | **Not done** — all runs used the default provider |
| The delegated **ML-DSA** signing path interoperates on the wire | **Not done** — a P-256 ECDSA certificate was used throughout |
| A third independent peer | **Not done** — none available on this machine |
| Interop runs in CI | **Not done** — no interop job exists |
| Shared secrets byte-compared across implementations | **Not done** — equality is inferred from successful record-layer traffic (`APPDATA_RX`), not from exported keying material |

---

## Open items

1. **No CI interop job exists.** The matrix has only ever been run by hand on one macOS
   arm64 workstation. Any job that is added must implement the section 8.2 provider
   assertion, or it will silently become circular. Related CI gaps are tracked in
   [../ci/README.md](../ci/README.md).
2. **Third independent peer (Story 3.2 exit gate — currently unmet).** Only stock OpenSSL
   3.6.1 and Go 1.26 qualify. Candidates for a third: **BoringSSL** (ML-KEM hybrid support in
   its TLS stack) and **AWS-LC**. **Neither is installed on this machine** — `command -v bssl
   boringssl aws-lc` returns nothing and no Homebrew Cellar directory exists for either — so
   adding one means building it from source and wiring it into the harness. Note that
   AWS-LC's FIPS build is reported not to ship an ML-KEM hybrid, so it would count as a
   non-FIPS peer only; that should be confirmed before committing effort.
3. **Contamination provenance and blast radius.** Who installed `qudoprovider.dylib`
   (root-owned, Jun 22) and `qudo_fipsinstall` (root-owned, Jun 8) into the Homebrew Cellar
   is unknown, and whether the CI runners that will produce the official evidence carry the
   same contamination has not been checked. Decide whether the interop workstation is
   rebuilt clean, and record the decision — a lab will ask why the reference peer carried
   vendor code.
4. **Cellar `qudo_pqc.h` (8,497 B) differs from the in-tree subtree copy (9,649 B)** while
   both declare `QUDO_PQC_VERSION 1.0.0`. An unversioned 1.0.0 whose contents change
   undermines version traceability for CMVP; this interacts with the pinning problem
   described in [subtree-pins.md](subtree-pins.md).
5. **FIPS-provider interop is unmeasured.** Neutralizing `OPENSSL_CONF` also disables the
   FIPS provider, so every handshake here ran through the default provider. The delegated
   code is compiled into `providers/fips.dylib` as well, but a separate matrix run under a
   FIPS-only configuration is required before this can be claimed as FIPS-mode interop
   evidence. This is the largest single gap in this document.
6. **ML-DSA certificate chains are untested on the wire.** All 18 handshakes used a P-256
   ECDSA certificate, so only the ML-KEM key-exchange path was exercised. An ML-DSA
   certificate chain interop pass should be added — noting that peer support for ML-DSA
   certificates is itself less mature than for ML-KEM groups and may constrain the peer set.
7. **Both-endpoint group capture is incomplete for 15 of the 18 cases** (section 3.1). Only
   cases 16–18 have the group named independently at both ends; the other fifteen rest on
   single-group pinning plus a client-side group line. Add `-state`/`-trace` or equivalent so
   every case names the group from both ends.
8. **Harness is not in the repository.** `matrix.sh`, `client/main.go` and `server/main.go`
   live in a scratch directory and will be lost. They should be promoted into the repo —
   keeping the negative control and the control pair — as the Story 3.2 evidence artifact.
   The promoted version should implement the section 8.7 pass criteria as written (group-name
   equality and an `alert`/`error` scan), which `matrix.sh` does not, and should capture a
   client-side log for the negative control, which it did not. The superseded `run.sh` in the
   same directory should not be promoted.
9. **The parity-baseline question is unresolved.** Story 3.2 names "stock OpenSSL 3.5" as the
   parity baseline, but only 3.6.1 is installed. Whether peering 3.5.7 against 3.6.1
   satisfies the parity intent, or whether an actual 3.5.x peer must be built, is a decision
   for the lab pre-engagement. See [ADR-0004](adr/ADR-0004-openssl-baseline-3.5.7.md) for the
   baseline rationale.
10. **Shipping the pure ML-KEM groups is an open policy question.** MLKEM512/768/1024
    interoperate OpenSSL-to-OpenSSL, but no other peer tested offers them and they provide
    no classical hedge. Whether they belong in the default group list should be settled
    deliberately.
11. **Story 3.2 planning documents need three corrections** applied: the Go peer supports
    three hybrids and zero pure groups; the certificate recipe needs
    `-addext "subjectAltName=DNS:localhost,IP:127.0.0.1"`; and the peer list must record
    that the Homebrew OpenSSL counts as independent only under `OPENSSL_CONF=/dev/null`.
