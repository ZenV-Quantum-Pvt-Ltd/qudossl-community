# QudoSSL Cryptographic Module — Crypto Officer and User Guide

**Document status: DRAFT v0.1 — for internal review (Sprint 4, Story 4.4).**
Administrative facts that cannot be derived from the source tree are marked
**[TO BE CONFIRMED]** and must be supplied before submission.

This guide is the FIPS 140-3 administrator (Crypto Officer) and user guidance for
the **QudoSSL Cryptographic Module** (the "module"). It is a companion to
[`docs/security-policy.md`](security-policy.md) and is governed by the same ADRs.
Where this guide and the Security Policy differ, the Security Policy governs.

> **Supersedes the vendor guide.** The `qudo-pqc-lib` repository ships a
> `CRYPTO_OFFICER_GUIDANCE.md` that predates crypto-layer delegation (ADR-0005)
> and describes qudo-pqc-lib as a standalone FIPS module. That document does
> **not** govern this module. In QudoSSL, qudo-pqc-lib supplies ML-KEM/ML-DSA
> *math only*; every FIPS service is OpenSSL's. This guide governs.

---

## 1. Scope and audience

- **Crypto Officer (CO):** installs and configures the module, runs
  `fipsinstall`, loads the module into an application, invokes on-demand
  self-tests, queries status and the approval indicator, and performs
  zeroization. Role definitions are in Security Policy §4.
- **User:** invokes the module's approved cryptographic services through the
  application.

At the assumed overall Security Level 1 **[TO BE CONFIRMED: overall level]**,
neither role is authenticated; roles are assumed implicitly by the service
invoked (Security Policy §4.3).

## 2. Module identification

| Field | Value |
|---|---|
| Module name | **QudoSSL FIPS Provider** — as reported by `qudossl list -providers` (ADR-0013) |
| Version | **3.5.7+qudo-1.0.0** — the provider's `build info` field. `qudo-pqc-lib` is pinned to tag `v1.0.0`; the branch-head pin that blocked this is resolved (ADR-0013, `docs/subtree-pins.md`). |
| FIPS module file | `fips.so` (Linux) · `fips.dylib` (macOS) · `fips.dll` (Windows) — **not** renamed to `qudo-fips.so` (ADR-0006) |
| Tested OEs | Linux x86-64, Linux aarch64, macOS arm64, Windows x64 |

> **Only the FIPS provider is the validated module.** Every provider QudoSSL
> ships is now QudoSSL-branded, so `qudossl list -providers` shows several
> entries beginning "QudoSSL". That is product branding, **not** a statement of
> validation scope:
>
> | Provider | In the cryptographic boundary? |
> |---|---|
> | **QudoSSL FIPS Provider** (`fips.so`) | **Yes — this is the validated module** |
> | QudoSSL Base Provider | No — encoders/decoders, built into `libcrypto` |
> | QudoSSL Default Provider | No — non-approved algorithms; must not be active in an approved deployment |
> | QudoSSL Legacy Provider | No |
> | QudoSSL Null Provider | No |
>
> Approved operation requires the `fips` and `base` providers with
> `default_properties = fips=yes`, and the default provider **not** activated.
> See ADR-0013.
| Base | OpenSSL 3.5.7 FIPS provider, tag `openssl-3.5.7` |

The module is only the module described here when it is the **delegated** build.
A build that does not define `QUDO_PQC_DELEGATE` is upstream OpenSSL, not
QudoSSL (see §4.4 verification).

## 3. Secure acquisition and delivery

Obtain the module only from **[TO BE CONFIRMED: authorized distribution
channel]**. On receipt, verify integrity by **[TO BE CONFIRMED: signature /
checksum mechanism]**. The build is reproducible (Security Policy §11.4): a
recipient who builds from the pinned source SHAs
([`docs/subtree-pins.md`](subtree-pins.md)) must obtain byte-identical
artifacts, which lets a CO independently confirm the delivered bytes.

## 4. Installation and approved-mode configuration

### 4.1 Build and install

The module is produced by the two-stage build in `build/` and installed under
`PREFIX` (default `build/out`):

```
make -C build all           # stage 1: qudo-pqc-lib (math-only) → stage 2: OpenSSL FIPS
make -C build install        # installs to $(PREFIX); FIPS module → $(PREFIX)/lib/ossl-modules/fips.<ext>
```

Because the FIPS module keeps its upstream filename `fips.<ext>` (ADR-0006), its
installed path `<libdir>/ossl-modules/fips.<ext>` is **filename-identical to a
stock OpenSSL FIPS provider**. The CO **must** confirm the install does not
overwrite, and is not shadowed by, a system OpenSSL FIPS provider on the host.

### 4.2 Generate the module configuration (`fipsinstall`)

Approved mode requires the module's integrity value and self-test status to be
recorded in a configuration file. Generate it once per installed module image,
on each OE:

```
openssl fipsinstall \
    -module <libdir>/ossl-modules/fips.<ext> \
    -out    <configdir>/fipsmodule.cnf \
    -provider_name fips
```

`fipsinstall` runs the module's pre-operational self-tests and, on success,
writes `fipsmodule.cnf` containing the `module-mac` (an HMAC-SHA-256 over the
module image) and the self-test status. This `module-mac` is **build- and
OE-specific** — it is regenerated for each image and is not a fixed
certification constant (Security Policy §5.2).

### 4.3 Activate the FIPS provider

Reference `fipsmodule.cnf` from the application's `openssl.cnf` and activate the
`fips` (and `base`) providers so that approved-mode operations resolve to the
module. A minimal configuration:

```
config_diagnostics = 1
openssl_conf = openssl_init

.include <configdir>/fipsmodule.cnf

[openssl_init]
providers = provider_sect
alg_section = algorithm_sect

[provider_sect]
fips  = fips_sect
base  = base_sect

[base_sect]
activate = 1

[algorithm_sect]
default_properties = fips=yes
```

`default_properties = fips=yes` makes the application select approved
implementations by default. See §5 for per-operation selection.

### 4.4 Verify the module is the delegated QudoSSL build

Before relying on the module, confirm delegation is actually linked — a build
without it is upstream OpenSSL, not QudoSSL:

```
# configuration recorded the delegation macro
grep -q QUDO_PQC_DELEGATE <builddir>/configdata.pm

# and the math is actually in the module's libcrypto
nm -g <libdir>/libcrypto.<ext> | grep -c 'QUDO_\(KEM\|MLDSA\)_'   # must be > 0
```

The repository ships `ci/check-delegation-linked.sh`, which performs exactly
this check and is run in every CI build job.

## 5. Approved use

### 5.1 Entering and confirming approved mode

The module is in the approved mode when the application has loaded the `fips`
provider from `fipsmodule.cnf` and selects implementations with the `fips=yes`
property. Confirm the provider is active:

```
openssl list -providers            # 'fips' must be listed and active
```

### 5.2 Selecting approved algorithms

Approved-mode operations must resolve to the `fips` provider. Either set
`default_properties = fips=yes` globally (§4.3) or pass an explicit property
query per operation, e.g. `-propquery 'fips=yes'` / `provider=fips`. The
approved algorithm list — including the delegated **ML-KEM (FIPS 203)** and
**ML-DSA (FIPS 204)**, OpenSSL-native **SLH-DSA (FIPS 205)**, and all approved
classical algorithms — is in Security Policy §2A.

### 5.3 The approval indicator

Each approved service exposes OpenSSL's FIPS approval indicator as a status
output (Security Policy §3.5). The CO/User should treat an operation as approved
only when the indicator confirms it. Selecting an algorithm or parameter set not
approved for the module places that operation in the **non-approved mode**.

### 5.4 Non-approved mode

Any use of a non-FIPS provider (`default`, `legacy`) or of an algorithm the
approval indicator does not confirm is outside the approved mode. To stay in the
approved mode, do not load or select those providers for approved operations.

## 6. On-demand self-tests

The module runs its pre-operational self-tests (integrity HMAC) and conditional
algorithm self-tests automatically at load. The CO can re-trigger self-tests
on demand by reloading the module / re-invoking the provider self-test
mechanism (Security Policy §10.6). A self-test failure puts the module into an
error state in which no approved service is available (§7).

## 7. Handling self-test and integrity failures

If the module fails its integrity check or any self-test:

1. The module enters an error state and returns no cryptographic output
   (Security Policy §5.3, §10.7).
2. The CO must not attempt to bypass the failure. Re-verify the installed module
   image against the delivered bytes (§3) and re-run `fipsinstall` (§4.2).
3. If the failure persists, the installed module image is not trustworthy;
   re-acquire per §3.

Never edit `fipsmodule.cnf` by hand to change the `module-mac` — doing so
defeats the integrity binding and takes the module out of the validated
configuration.

## 8. Key and SSP management

- Approved key generation uses the module's approved DRBG (SP 800-90A); do not
  supply keys from a non-approved RNG for approved operations.
- CSPs (ML-KEM/ML-DSA private keys and seeds, shared secrets, DRBG state) are
  held in volatile memory and zeroized by the module on free (Security Policy
  §9.7). Applications should free key objects promptly after use so zeroization
  occurs.
- The module performs no persistent CSP storage; persistence, if any, is the
  application's responsibility and is outside the boundary.

Full SSP handling, zeroization methods and triggers are in Security Policy §9.

## 9. Operational environment guidance

The module is validated on the OEs in §2 (modifiable operational environment).
Operation on an untested but compatible environment is under
vendor-affirmation only (Security Policy §6.5); such operation is the operator's
responsibility and is not covered by the tested-configuration results.

## 10. Update and end-of-life

Module updates, CVE handling and the conditions that require re-validation are
governed by the patch-management policy
([`docs/patch-management-policy.md`](patch-management-policy.md)). Do not apply
ad-hoc patches to an installed module image; a modified image fails the
integrity check and is outside the validated configuration.

## 11. References

- [`docs/security-policy.md`](security-policy.md) — the governing Security Policy
- [`docs/patch-management-policy.md`](patch-management-policy.md)
- [`docs/subtree-pins.md`](subtree-pins.md), [`docs/reproducibility.md`](reproducibility.md)
- ADR-0005 (crypto-layer delegation), ADR-0006 (fips.so, no rename),
  ADR-0009 (math-only build), ADR-0010 (SLH-DSA upstream) — `docs/adr/`
- FIPS 140-3 / ISO/IEC 19790:2012; SP 800-90A; FIPS 203/204/205
