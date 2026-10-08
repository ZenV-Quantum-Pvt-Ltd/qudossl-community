# QudoSSL

A FIPS 140-3 post-quantum TLS stack.

QudoSSL is an OpenSSL fork that delegates its ML-KEM and ML-DSA math to ZENV
Quantum's `qudo-pqc-lib`, and ships a single FIPS 140-3 module — the **QudoSSL
FIPS Provider** — for CMVP certification under ZENV Quantum's name.

## Layout

```
qudossl/
├── openssl/            OpenSSL fork (git subtree, tag openssl-3.5.7)
├── qudo-pqc-lib/       PQC engine (git subtree, pinned release)
├── build/              top-level build wrapper
├── ci/                 CI configuration
├── docs/               ADRs and engineering docs
├── cert/               CMVP submission package
├── reproducible-build/ pinned-toolchain build kit
├── design/             design reference and build plan
└── sprint/             sprint plans
```

Both `openssl/` and `qudo-pqc-lib/` are **git subtrees**, not submodules. A
plain `git clone` gets everything — no `--recursive`.

## What it ships

- **`libcrypto.so` / `libssl.so`** — full OpenSSL functionality, including
  TLS 1.3 with PQ groups (X25519MLKEM768, SecP256r1MLKEM768,
  SecP384r1MLKEM1024).
- **`fips.so`** — the FIPS 140-3 provider module (the **QudoSSL FIPS Provider**).
  Classical primitives (AES, SHA-2/3, HMAC, DRBG, RSA, ECDSA, EdDSA) **and
  SLH-DSA** come from upstream OpenSSL; only **ML-KEM and ML-DSA** are delegated
  to `libqudo-pqc.a`, statically linked (crypto-layer delegation, ADR-0005;
  SLH-DSA stays upstream per ADR-0010).
- **`qudossl`** — the CLI. Functionally unchanged from upstream's `openssl`,
  and installed alongside an `openssl` symlink so existing scripts keep working
  (ADR-0013). Applications continue to fetch with property `"fips=yes"`.

## Supported platforms

The four target operational environments, all built and tested green in CI:

- Linux x86-64
- Linux aarch64
- macOS arm64 (Apple Silicon)
- Windows x64 (MSVC / VC-WIN64A)

## Prerequisites

| | Linux | macOS | Windows |
|---|---|---|---|
| Compiler | GCC ≥ 11 or Clang ≥ 14 | Apple Clang ≥ 14 (Xcode CLT) | MSVC 2022 |
| Also | GNU Make, perl ≥ 5.10, CMake ≥ 3.15, git ≥ 2.30 | same (`brew install cmake`) | NASM, Strawberry Perl |

Optional, not needed for a normal build: Python ≥ 3.8 (ACVP runners), Go ≥ 1.24
(interop harness in `ci/interop/`), Docker (reproducible-build verification).

**No existing OpenSSL is required.** QudoSSL *is* the OpenSSL — it is a
distribution, not a plugin into another one.

## Build and install

Everything below is copy-pasteable and was run end to end on macOS arm64 against
this commit.

The FIPS module's filename differs by platform, so set it once and the commands
below work unchanged everywhere:

```sh
case "$(uname -s)" in
  Darwin) export FIPS_MODULE=fips.dylib ;;
  *)      export FIPS_MODULE=fips.so    ;;
esac
```

> **Windows uses a different driver.** The `make` commands below are for Linux
> and macOS. On Windows build with `build\build.ps1` from a VS 2022 developer
> prompt; the verification and test sections still apply, with `fips.dll`.
> Note that the `qudossl` CLI wrapper is currently a shell script and is not
> installed on Windows — see ADR-0013.

### 1. Choose a prefix

```sh
export QUDOSSL_PREFIX="$HOME/qudossl"      # no sudo needed
# or, system-wide (needs sudo on the install steps):
# export QUDOSSL_PREFIX=/opt/qudossl
```

> `/opt/qudossl` belongs to QudoSSL exclusively. The free **Qudo Provider** is a
> plugin and installs into your existing OpenSSL's `ossl-modules/` directory —
> never here. See ADR-0013; the two have been confused before.

### 2. Build

Get the source. Either take the published release, which carries a SHA-256 you
can check, or clone the repository:

```sh
rel=qudossl-community-1.0.0
url=https://github.com/ZenV-Quantum-Pvt-Ltd/qudossl-community/releases/download/$rel

curl -fLO "$url/$rel-src.tar.gz" -fLO "$url/$rel-src.tar.gz.sha256"
shasum -a 256 -c "$rel-src.tar.gz.sha256"    # Linux: sha256sum -c

tar xf "$rel-src.tar.gz" && cd "$rel"
```

```sh
# or from git
git clone https://github.com/ZenV-Quantum-Pvt-Ltd/qudossl-community.git
cd qudossl-community
```

Then build:

```sh
make -C build all      PREFIX="$QUDOSSL_PREFIX"      OPENSSL_EXTRA_FLAGS="-Wl,-rpath,$QUDOSSL_PREFIX/lib"
```

Two stages: CMake builds `qudo-pqc-lib/` into `libqudo-pqc.a` (math-only), then
OpenSSL is configured against that archive and built with the FIPS provider.
Roughly 3–6 minutes on 10 cores. Add `JOBS=N` to control parallelism.

> **`-Wl,-rpath` is not optional on Linux.** Distributions ship their own
> `libssl.so.3` — the *same* SONAME, because QudoSSL is ABI-compatible with
> OpenSSL 3.x. Without an RPATH the loader finds the distribution's copy first
> and the CLI dies with ``version `OPENSSL_3.5.0' not found``.

### 3. Install

```sh
make -C build install_sw PREFIX="$QUDOSSL_PREFIX"   # libs, headers, CLI
make -C openssl install_ssldirs                     # openssl.cnf, certs/, private/
make -C openssl install_fips                        # fips.so + fipsmodule.cnf
```

`install_ssldirs` installs the configuration directory itself. `install_sw` does
not create it, and `install_fips` only writes `fipsmodule.cnf` into it — so
without this step a non-FIPS install has nowhere to keep its configuration, and
`openssl req` aborts with no `openssl.cnf` to read.

`install_sw` skips the ~2000 man pages that `install` would also copy. Use
`sudo` on both if your prefix is outside your home directory.

This installs **two** CLI names:

| | |
|---|---|
| `openssl` | the real binary, behaviour identical to upstream |
| `qudossl` | a wrapper that prints the QudoSSL banner, then delegates |

### 4. Put it on your PATH

So that `qudossl` and `openssl` work as bare commands, rather than having to
give the full path every time:

```sh
export PATH="$QUDOSSL_PREFIX/bin:$PATH"
```

The rest of this document assumes you have done this.

That directory supplies **both** names, so `openssl` now resolves to QudoSSL as
well. That is the point when migrating an existing application — but it does
shadow your distribution's or Homebrew's `openssl` for everything in that shell.

Check it took effect:

```sh
which -a openssl    # $QUDOSSL_PREFIX/bin/openssl must be first
openssl version     # OpenSSL 3.5.7+qudo-1.0.0
```

To make it permanent, add it to `~/.zshrc` (or `~/.bashrc`) — together with the
two variables from step 5, once you have them:

```sh
export PATH="/opt/qudossl/bin:$PATH"
export OPENSSL_CONF="/opt/qudossl/ssl/qudossl-fips.cnf"
export OPENSSL_MODULES="/opt/qudossl/lib/ossl-modules"
```

Add all three together or none of them. `PATH` on its own gives you QudoSSL
running with the `default` provider and no FIPS — which looks like success.

Two ordering points. These lines must come **after** anything else that prepends
to `PATH` (on macOS, Homebrew's `eval "$(brew shellenv)"` runs from
`/etc/zprofile`, before `~/.zshrc`, so `~/.zshrc` wins). And an already-open
shell keeps the `PATH` it started with — `source ~/.zshrc` or open a new
terminal.

> Exporting `OPENSSL_CONF` in your shell profile applies it to **every** OpenSSL
> on the machine, not just this one. If some other tool invokes a different
> `openssl` by absolute path, it will read this config and fail to load a module
> built for a different version. Suspect this first if an unrelated tool starts
> reporting provider errors.

### 5. Activate the FIPS provider

`fipsinstall` runs the module's power-on self-tests — KATs for AES, SHA-2/3,
HMAC, the DRBG, ML-KEM, ML-DSA and SLH-DSA — and writes the integrity record.

> **Using a system prefix?** If `$QUDOSSL_PREFIX` is outside your home
> directory (e.g. `/opt/qudossl`), both commands in this step write to a
> root-owned directory and need `sudo`.
>
> Give `fipsinstall` the **full path** under `sudo`, because `sudo` resets
> `PATH` and will not find the `qudossl` you put on it in step 4:
>
> ```sh
> sudo "$QUDOSSL_PREFIX/bin/qudossl" fipsinstall \
>   -module "$QUDOSSL_PREFIX/lib/ossl-modules/$FIPS_MODULE" \
>   -out    "$QUDOSSL_PREFIX/ssl/fipsmodule.cnf" \
>   -provider_name fips
> ```
>
> And write the config file with `sudo tee` rather than `cat`:
>
> ```sh
> sudo tee "$QUDOSSL_PREFIX/ssl/qudossl-fips.cnf" > /dev/null <<EOF
> ...the same heredoc body, unchanged...
> EOF
> ```
>
> `sudo cat > file` does **not** work: the `>` redirection is performed by your
> shell, which is still unprivileged. `cat` runs as root, but creating the file
> fails with `permission denied`. `tee` receives the text on stdin and does the
> writing itself, as root.

```sh
qudossl fipsinstall \
  -module "$QUDOSSL_PREFIX/lib/ossl-modules/$FIPS_MODULE" \
  -out    "$QUDOSSL_PREFIX/ssl/fipsmodule.cnf" \
  -provider_name fips
```

Expect `INSTALL PASSED`.

Then write the configuration that activates the module and sets the
post-quantum groups process-wide.

> **Leave the heredoc delimiter unquoted.** It is `<<EOF`, not `<<'EOF'`, so that
> `$QUDOSSL_PREFIX` on the `.include` line is expanded by the shell **as the file
> is written**. The finished file must contain an absolute path.
>
> OpenSSL's config parser does not expand shell variables. If `QUDOSSL_PREFIX`
> was unset, or you quoted the delimiter, the file keeps a literal
> `$QUDOSSL_PREFIX`, the include resolves to nothing, and **the FIPS provider is
> never activated** — with no error. `list -providers` then shows only
> `default`, which is easily mistaken for a working install. The `grep` below
> catches this.

```sh
cat > "$QUDOSSL_PREFIX/ssl/qudossl-fips.cnf" <<EOF
openssl_conf = openssl_init

.include $QUDOSSL_PREFIX/ssl/fipsmodule.cnf

[openssl_init]
providers   = provider_sect
alg_section = algorithm_sect
ssl_conf    = ssl_sect

[provider_sect]
fips = fips_sect
base = base_sect

# base supplies encoders/decoders. Without it, key and certificate
# files cannot be read.
[base_sect]
activate = 1

[algorithm_sect]
default_properties = fips=yes

# Applies to every SSL_CTX in the process, so applications need no
# TLS-group configuration of their own.
[ssl_sect]
system_default = system_default_sect

[system_default_sect]
Groups = X25519MLKEM768:SecP256r1MLKEM768:SecP384r1MLKEM1024:secp256r1:secp384r1
MinProtocol = TLSv1.2
EOF
```

Confirm the include expanded before going any further:

```sh
grep include "$QUDOSSL_PREFIX/ssl/qudossl-fips.cnf"
```

This must print an absolute path — `.include /opt/qudossl/ssl/fipsmodule.cnf` — and
**not** a literal `$QUDOSSL_PREFIX`. If it shows the variable name, edit that one
line by hand to the real path before continuing; everything downstream will
otherwise appear to work while running without FIPS.

Only then point the environment at it:

```sh
export OPENSSL_CONF="$QUDOSSL_PREFIX/ssl/qudossl-fips.cnf"
export OPENSSL_MODULES="$QUDOSSL_PREFIX/lib/ossl-modules"
```

> Under `fips=yes`, standalone `x25519`/`x448` are **not** offered as TLS groups
> — they are not FIPS-approved. Use the ML-KEM hybrids plus the NIST curves, as
> above. A bare `x25519` in that list makes the whole list fail to parse.

## Verify the install

```sh
qudossl version
```
```
QudoSSL 1.0.0 (OpenSSL 3.5.7+qudo-1.0.0 base)
Copyright (c) ZENV Quantum Private Limited. Apache License 2.0.
Post-quantum TLS 1.3: ML-KEM (FIPS 203) hybrid key exchange.
FIPS module: QudoSSL FIPS Provider  ·  Homepage: https://zenv.ai/qudo
OpenSSL 3.5.7+qudo-1.0.0 9 Jun 2026 (Library: OpenSSL 3.5.7+qudo-1.0.0 9 Jun 2026)
```

```sh
qudossl list -providers
```
```
Providers:
  base
    name: QudoSSL Base Provider
    version: 3.5.7
    status: active
  fips
    name: QudoSSL FIPS Provider
    version: 3.5.7
    status: active
```

```sh
qudossl list -tls-groups | tr ':' '\n' | grep -i mlkem
```
```
MLKEM512
MLKEM768
MLKEM1024
SecP256r1MLKEM768
X25519MLKEM768
SecP384r1MLKEM1024
```

The PQC math really is delegated to `qudo-pqc-lib`. **Expect a non-zero count**
— the exact number is platform-dependent (126 on macOS arm64, 123 on Linux
aarch64), because the backends differ, so treat "not zero" as the pass
condition. Use plain `nm`, **not** `nm -g`: OpenSSL links with a version script
that localises non-public symbols, so an exported-only view reports 0 even on a
correct build.

```sh
nm -a "$QUDOSSL_PREFIX/lib/ossl-modules/$FIPS_MODULE" \
  | awk '{print $NF}' | sed 's/^_//' | grep -cE '^QUDO_(KEM|MLDSA)_'
```

## Test

### A live post-quantum TLS 1.3 handshake

```sh
cd "$(mktemp -d)"
qudossl req -x509 -new -noenc -newkey rsa:3072 \
    -keyout s.key -out s.crt -days 2 -subj "/CN=localhost" 2>/dev/null

openssl s_server -cert s.crt -key s.key -accept 4433 -www \
    -groups X25519MLKEM768:SecP256r1MLKEM768:SecP384r1MLKEM1024:secp256r1 &
sleep 2

qudossl s_client -connect 127.0.0.1:4433 \
    -groups X25519MLKEM768 -tls1_3 -brief </dev/null
```
```
CONNECTION ESTABLISHED
Protocol version: TLSv1.3
Ciphersuite: TLS_AES_256_GCM_SHA384
Signature type: rsa_pss_rsae_sha256          <- ordinary RSA certificate
Negotiated TLS1.3 group: X25519MLKEM768      <- post-quantum key exchange
```

Those last two lines are the point: an **ordinary RSA certificate** securing a
session whose **key exchange is post-quantum**. Nothing was reissued.

Stop the server with `kill %1`.

### The boundary gates

```sh
ci/check-boundary-symbols.sh      # no qudo FIPS infrastructure in the module
ci/check-delegation-linked.sh openssl   # the delegated math really is linked
ci/check-seeded-entrypoints.sh    # every delegated call takes caller randomness
```

### The full OpenSSL test suite

```sh
env -u OPENSSL_CONF -u OPENSSL_MODULES make -C build test JOBS=10
```

Expect `4855 tests`, `Result: PASS`.

> **Unset `OPENSSL_CONF` for this**, as above. The suite builds its own
> configuration; a stray `OPENSSL_CONF` pointing at a FIPS-only file makes ~68
> tests fail across `pkcs8`, `pkcs12`, `x509` and `store` — every one a
> password-based or legacy-algorithm path that FIPS correctly refuses. The
> failures look like real defects and are not.

## Troubleshooting

| Symptom | Cause and fix |
|---|---|
| `qudossl: command not found`, or `which -a qudossl` prints nothing | `$QUDOSSL_PREFIX/bin` is not on `PATH`. Either the export in step 4 was never run, or this shell was started before you added it to `~/.zshrc` — an open shell keeps the `PATH` it launched with. Re-run the export, or `source ~/.zshrc`, or open a new terminal. |
| `openssl version` reports the wrong OpenSSL — 3.6.x, or `LibreSSL` on macOS | Another `openssl` is earlier on `PATH`. Run `which -a openssl`: `$QUDOSSL_PREFIX/bin/openssl` must be the **first** line. If Homebrew's `/opt/homebrew/bin/openssl` or `/usr/bin/openssl` is above it, your `PATH` line runs too early — it must come after Homebrew's `eval "$(brew shellenv)"`. |
| `sudo qudossl …` → `command not found`, or it runs the wrong binary | `sudo` resets `PATH`, so it does not see step 4's export. Give the full path: `sudo "$QUDOSSL_PREFIX/bin/qudossl" …`. Note `sudo` also drops `OPENSSL_CONF`, so a sudo'd command runs **without** the FIPS provider. |
| ``version `OPENSSL_3.5.0' not found`` | The loader picked the distribution's `libssl.so.3`. Rebuild with the `-Wl,-rpath` flag in step 2. |
| Installed to the wrong place after changing `PREFIX` | `PREFIX` is baked into `configdata.pm` by Configure. `make` now detects a change and reconfigures; if you are on an older checkout, `rm openssl/configdata.pm` and rebuild. |
| `make test` fails in `pkcs12` / `store` / `uitest` | A `OPENSSL_CONF` is set in your environment. Use the `env -u` form above. |
| `list -providers` shows only `default` | Three causes, in order of likelihood: `OPENSSL_CONF` / `OPENSSL_MODULES` are not exported in *this* shell; the config's `.include` kept a literal `$QUDOSSL_PREFIX` (run the `grep` in step 4); or you ran the command under `sudo`, which does not inherit your environment. |
| `SSL_CONF_cmd("Groups", …) failed` | A group in the list is not offered under `fips=yes` — usually a bare `x25519`. |
| `permission denied` writing `qudossl-fips.cnf` | System prefix plus `sudo cat > file` — the `>` is done by your unprivileged shell. Use the `sudo tee` form in step 4. |
| `fipsinstall` fails a self-test | The module changed after install, or the wrong `fips.*` was referenced. Rebuild and re-run step 4. |
| Handshake fails, `no shared group` | Client and server share no group. Both need TLS 1.3 and one of the hybrids — ML-KEM does not exist in TLS 1.2. |

## Integrating a server

Point the application's build at QudoSSL instead of the system OpenSSL. Working,
verified examples for nginx, HAProxy and Tomcat live in
the QudoSSL demo suite (available from ZENV on request).

```sh
# nginx
./configure --with-http_ssl_module \
    --with-cc-opt="-I$QUDOSSL_PREFIX/include" \
    --with-ld-opt="-L$QUDOSSL_PREFIX/lib -lssl -lcrypto -Wl,-rpath,$QUDOSSL_PREFIX/lib"

# HAProxy
make TARGET=linux-glibc USE_OPENSSL=1 \
     SSL_INC="$QUDOSSL_PREFIX/include" SSL_LIB="$QUDOSSL_PREFIX/lib" \
     LDFLAGS="-Wl,-rpath,$QUDOSSL_PREFIX/lib"
```

## Documentation

- `docs/qudossl-current-state.md` — **living project status** (headline, PQC
  delegation state, sprint status, cert/lab engagement). Start here for a
  snapshot.
- `design/QudoSSL_Design.docx` — architecture and build plan. Read the v1.0
  Decision Log first; it overrides the body. See `CLAUDE.md` for precedence.
- `docs/design-errata.md` — the authoritative section-by-section delta between
  the frozen design and the decisions in force. Read before implementing from
  any design section.
- `docs/adr/` — architecture decision records
- `docs/branching.md` — branch model and cert-branch rules
- `docs/reproducibility.md`, `docs/subtree-pins.md` — reproducible-build notes
  and the pinned subtree SHAs
- `sprint/` — sprint plans and change logs

Certification evidence (Sprint 3):

- `docs/interop-report.md` — cross-implementation PQC TLS interop results.
  Read §1 before running any interop yourself: the obvious reference peer on a
  developer machine may have our own provider active, which makes the evidence
  circular.
- `docs/zeroization-analysis.md` — CSP inventory, zeroization triggers, and the
  defects found and fixed
- `docs/side-channel-analysis.md` — constant-time posture. States plainly that
  no measured evidence exists yet.
- `docs/acvp-status.md` — what ACVP coverage already exists and what does not
- `docs/boundary-duplicate-hash-disclosure.md` — formal disclosure of the
  duplicate SHA-2/Keccak implementations inside the boundary
- `docs/upstream-defects.md` — upstream OpenSSL defects we deliberately do not
  fix locally, tracked for upstream report

Certification submission (Sprint 4, in progress):

- `docs/security-policy.md` — FIPS 140-3 non-proprietary Security Policy (DRAFT v0.1)
- `docs/crypto-officer-guide.md` — Crypto Officer / User Guide (DRAFT v0.1)
- `docs/patch-management-policy.md` — patch-management and maintenance policy (DRAFT v0.1)
- `docs/qudo-pqc-lib-release-tag-proposal.md` — proposal to tag qudo-pqc-lib for the cert freeze

## Licence

Apache-2.0. See `LICENSE`. QudoSSL derives from the OpenSSL project; upstream
attribution in `openssl/LICENSE.txt` and `openssl/ACKNOWLEDGEMENTS.md` is
retained verbatim.
