# ADR-0013 — QudoSSL product identity: CLI name, version, FIPS provider name

- **Status:** Accepted
- **Date:** 2026-08-05
- **Design ref:** completes §7.1/§15.1 identity handling left open by ADR-0006
- **Amends:** ADR-0006's statement that "the rebrand list inside `openssl/` is empty"

## Context

ADR-0006 cancelled the `qudo-fips.so` rename and recorded the consequence:

> **Cert identity moves entirely to the paperwork.** The design deliberately
> kept the filename as the one carrier of Qudo identity. With the rename
> cancelled, the Security Policy, module version and repo SHA carry it instead.

That left the shipped software with no self-identification at all. Three
symptoms:

1. `openssl list -providers` reported **`OpenSSL FIPS Provider`**, while the
   module is submitted to CMVP as **QudoSSL FIPS Provider**. A module whose
   reported name differs from its certificate is a question a lab will ask.
2. There was no QudoSSL version anywhere in the build. `docs/security-policy.md`
   and `docs/crypto-officer-guide.md` both carried `[TO BE CONFIRMED]` for the
   module version, blocked on a `qudo-pqc-lib` release tag that has since been
   cut (`v1.0.0`).
3. The product is called QudoSSL but its CLI was only installed as `openssl`.

## Decision

Adopt the vendor-identity mechanism upstream provides for exactly this case.
`openssl/README-FIPS.md` documents it:

> Some Vendors choose to patch/modify/build their own FIPS provider, test it
> with a Security Laboratory and submit it under their own CMVP certificate…
> Setting `PRE_RELEASE_TAG`, `BUILD_METADATA` and `FIPS_VENDOR` allow to control
> reported FIPS provider name and build version as required for CMVP submission.

### 1. `openssl/VERSION.dat`

```
BUILD_METADATA=qudo-1.0.0
FIPS_VENDOR=QudoSSL
```

Produces:

| Field | Value |
|---|---|
| `OPENSSL_VERSION_STR` | `3.5.7` |
| `OPENSSL_FULL_VERSION_STR` | `3.5.7+qudo-1.0.0` |
| FIPS provider `name` | `QudoSSL FIPS Provider` |
| FIPS provider `build info` | `3.5.7+qudo-1.0.0` |

### 2. `openssl/Configure` — one line

Upstream builds a vendor name as `"$FIPS_VENDOR $provider_string for OpenSSL"`,
which would report `QudoSSL FIPS Provider for OpenSSL`. The certificate names
the module **QudoSSL FIPS Provider**, and a module must report the name it is
certified under, so the ` for OpenSSL` suffix is dropped.

Only the vendor branch is touched. With `FIPS_VENDOR` unset the expression still
yields upstream's `OpenSSL FIPS Provider` byte-for-byte, so the change is inert
for any non-vendor build.

### 3. All shipped providers are QudoSSL-branded

The four non-FIPS providers report their names from hardcoded strings, with no
configuration hook equivalent to `FIPS_VENDOR`. Each is a one-line change:

| File | Now reports |
|---|---|
| `providers/baseprov.c` | `QudoSSL Base Provider` |
| `providers/defltprov.c` | `QudoSSL Default Provider` |
| `providers/legacyprov.c` | `QudoSSL Legacy Provider` |
| `providers/nullprov.c` | `QudoSSL Null Provider` |

All four are **outside** the FIPS boundary — zero entries in
`providers/fips.module.sources` — so this carries no certification impact for
the module itself. No test or documentation in the tree asserts these strings
(verified: 0 hits under `openssl/test/`).

**The risk this creates, and how it is handled.** With every provider named
"QudoSSL", `list -providers` no longer makes the validated boundary obvious at a
glance; previously the contrast between "OpenSSL Base Provider" and "QudoSSL
FIPS Provider" did that implicitly. A reader could infer that the default
provider is also validated, which would be wrong and, in a regulated deployment,
harmful. `docs/security-policy.md` and `docs/crypto-officer-guide.md` therefore
carry an explicit table stating that **only the FIPS provider is the validated
module**, and that approved operation requires `fips` + `base` with
`default_properties = fips=yes` and the default provider not activated.

### 4. CLI: `openssl` stays the real binary, `qudossl` is a banner wrapper

`build/Makefile` installs `build/qudossl`, a small wrapper, alongside the
upstream-built `openssl`:

| Name | What it is | `version` output |
|---|---|---|
| `openssl` | the real binary, unmodified | upstream-identical, one line |
| `qudossl` | wrapper: banner, then `exec` | QudoSSL banner + the same line |

**Why a wrapper rather than renaming the binary.** The version string comes from
`OPENSSL_VERSION_TEXT` in `include/openssl/opensslv.h.in`, which **is** listed in
`providers/fips.module.sources`; editing it enters the re-certification flow.
Printing the banner from `apps/version.c` was tried and rejected: `opt_init()`
re-runs `opt_progname(argv[0])` and overwrites the invoked name with the
sub-command, so recovering "qudossl" needs a global threaded through `apps.h`,
`openssl.c` and `version.c` — three more upstream files patched for a cosmetic
string. The wrapper gets the same result with **zero** upstream changes.

It also scopes the banner correctly for free: `openssl version` is the untouched
binary, so its output stays byte-identical to upstream and anything parsing it
(`openssl version | cut -d' ' -f2`, autoconf probes, packaging scripts) is
unaffected.

The wrapper deliberately refuses to fall back to a system `openssl` — running a
distribution build under a QudoSSL banner would silently misidentify which
library is in use. This mirrors the earlier provider-based distribution's
`qudossl` wrapper (Subtask 1.4.4), which solved the same problem the same way.

`QUDO_PRODUCT_VERSION` in the wrapper must track `BUILD_METADATA` in
`VERSION.dat`. Two sources of truth for one number is a real drift risk; a CI
check comparing them would be a reasonable follow-up.

### 5. `make` reconfigures when PREFIX changes

Not branding, but found while verifying it. `PREFIX` is baked into
`configdata.pm` by Configure, and changing it on a later `make` command line did
**not** reconfigure — the build silently kept installing to the *old* prefix.
That produced three wrong-prefix installs during this work, and it broke the new
CLI install step, which derives its path from `$(PREFIX)`.

`build/Makefile` now compares `$(PREFIX)` against `configdata.pm` and forces a
reconfigure on mismatch, and `install-cli-name` fails loudly rather than
silently skipping if the binary is not where it expects.

## What was deliberately NOT changed

- **The module version stays `3.5.7`.** It is not a provider-specific field —
  `OPENSSL_VERSION_STR` is `MAJOR.MINOR.PATCH` from `VERSION.dat`, and those also
  synthesise `OPENSSL_VERSION_NUMBER` (layout `0xMNN00PP0L`). Setting them to
  `1.0.0` would yield `0x10000000`, and every consumer guarded on
  `OPENSSL_VERSION_NUMBER >= 0x30000000L` — nginx, HAProxy, curl, Python,
  `qudo-jni-crypto` — would select OpenSSL 1.0.x code paths against a 3.5.7
  library. The QudoSSL release is carried by `build info` instead, which is what
  upstream intends `BUILD_METADATA` for.
- **`OPENSSL_VERSION_TEXT` still begins "OpenSSL".** `qudossl version` prints
  `OpenSSL 3.5.7+qudo-1.0.0`. The literal lives in `include/openssl/opensslv.h.in`,
  which **is** listed in `providers/fips.module.sources`; editing it would enter
  the re-certification flow in `docs/branching.md` for a cosmetic gain. The
  statement is also true — the library *is* OpenSSL 3.5.7 derived.
- **`fips.so` is still not renamed.** ADR-0006 stands.

## Consequences

- **ADR-0006's "rebrand list inside `openssl/` is empty" no longer holds.** Six
  files now differ from upstream for identity reasons: `VERSION.dat`, one line of
  `Configure`, and a one-line string in each of the four non-FIPS provider
  sources. ADR-0006's stronger claim that *no source line differs* no longer
  holds either — this is the deliberate, recorded exception. The underlying
  concern, CVE cherry-pick cleanliness (§15.4), is preserved in practice: all six
  are single-line changes in files upstream rarely touches, and none is inside
  `fips.module.sources`.
- **No FIPS boundary source changed.** `VERSION.dat` and `Configure` have zero
  entries in `providers/fips.module.sources`; `fipsprov.c` and `fipskey.h.in`
  are byte-identical to upstream and merely receive a different substituted
  value. The module binary changes, so the integrity HMAC changes — expected and
  required for a vendor submission.
- **Timing is load-bearing.** Module name and version appear on the CMVP
  certificate. Setting them before the cert-branch freeze (Story 4.0) is free;
  changing them after validation is a re-validation. This lands before the
  freeze.
- **CVE cherry-pick cleanliness is essentially preserved.** Upstream rarely
  touches these two files, and a conflict in `VERSION.dat` is trivial to resolve.
  §15.4's argument is unaffected.
- **`[TO BE CONFIRMED]` module name and version are now answerable.**
  `docs/security-policy.md` and `docs/crypto-officer-guide.md` are updated in the
  same change: name **QudoSSL FIPS Provider**, version **3.5.7+qudo-1.0.0**.
- **Both CLI names work, and `openssl` is bit-for-bit upstream behaviour.**
  Existing scripts, Makefiles and `--with-openssl` builds are unaffected,
  including anything parsing `openssl version`.
- **Windows CLI branding is not yet handled.** The wrapper is a bash script;
  Windows needs a `.cmd` equivalent, and `install-cli-name` does not run there.
  Windows is a claimed OE, so this must be resolved before the cert freeze.

## `/opt/qudossl` belongs to QudoSSL

The install prefix is part of the product identity, and it has already caused a
real mix-up: an earlier distribution — built 24 Apr 2026, when the name "QudoSSL"
still meant the *provider bundle* — installed itself at `/opt/qudossl` with a
`/usr/local/bin/qudossl` symlink. After the name was reassigned to this fork,
typing `qudossl` on a developer machine silently ran **OpenSSL 3.5.2 with the
provider plugin** instead of this build. It reported a plausible-looking
`QudoSSL 1.0.0` banner, so nothing looked wrong.

The rule, from here:

| Product | Install prefix |
|---|---|
| **QudoSSL** (this fork) | `/opt/qudossl` — exclusively |
| **Qudo Provider** (the free plugin) | into the *host* OpenSSL's `ossl-modules/` directory. It is a plugin, not a distribution, and must not own a `/opt/qudossl` tree |

The current documentation already follows this — the portal's provider guide
installs to `$MODULES` and never mentions `/opt/qudossl`; the QudoSSL guide and
`qudossl-demos` use it exclusively. The rule is recorded here so a future
provider installer does not reintroduce the collision.

**Diagnosing a suspected mix-up:** `qudossl version` on this build reports
`QudoSSL <ver> (OpenSSL 3.5.7+qudo-… base)`. Anything reporting an OpenSSL base
other than the pinned one, or a FIPS provider named other than `QudoSSL FIPS
Provider`, is not this product.

## Verification

Built on macOS arm64 with `FIPS=yes`:

```
qudossl version                 QudoSSL 1.0.0 (OpenSSL 3.5.7+qudo-1.0.0 base)
                                + the upstream line, unchanged
openssl version                 upstream-identical, single line
qudossl list -providers         fips -> name: QudoSSL FIPS Provider
                                       version: 3.5.7
                                       build info: 3.5.7+qudo-1.0.0
fipsinstall                     INSTALL PASSED
check-boundary-symbols.sh       139 qudo symbols · 0 violations · 0 OS entropy refs
check-delegation-linked.sh      libcrypto 126 · fips.dylib 126
check-seeded-entrypoints.sh     PASSED
make -C build test              4855 tests, 344 files — all pass
```

Linux x86-64, Linux aarch64 and Windows x64 remain to be confirmed by CI.
