# QudoSSL Cryptographic Module — Patch-Management and Maintenance Policy

**Document status: DRAFT v0.1 — for internal review (Sprint 4, Story 4.5).**

This policy defines how the QudoSSL Cryptographic Module is maintained after
validation: how security fixes are applied, how the certified configuration is
protected, and which changes require CMVP re-validation. It is a companion to
[`docs/security-policy.md`](security-policy.md) and the branch model in
[`docs/branching.md`](branching.md).

---

## 1. Principles

1. **The certified module is a specific set of bytes.** It is identified by the
   module version and pinned source SHAs
   ([`docs/subtree-pins.md`](subtree-pins.md)) and is reproducible
   (Security Policy §11.4). Any change to those bytes produces a different module
   that is no longer covered by the original validation until re-assessed.
2. **Do not modify an installed module image.** A patched image fails the
   integrity check (Security Policy §5.3) and is outside the validated
   configuration. Fixes are delivered as new validated builds, not in-place edits.
3. **Minimize divergence from upstream.** ML-KEM/ML-DSA delegation is confined to
   the crypto-layer seam (ADR-0005); the `upstream-parity` CI gate enforces that
   the OpenSSL subtree differs from `openssl-3.5.7` only in that seam, so security
   fixes cherry-pick cleanly.

## 2. Sources of change

| Source | Example | Path |
|---|---|---|
| OpenSSL CVE / security fix | a `libcrypto`/FIPS-provider CVE | cherry-pick onto the cert branch (§3) |
| OpenSSL maintenance release | `openssl-3.5.8` | subtree sync on `dev` (§4) |
| qudo-pqc-lib fix (ML-KEM/ML-DSA math) | a correctness or side-channel fix | new qudo-pqc-lib release + subtree re-pin (§4) |
| QudoSSL-owned code | build, CI, delegation seam | ordinary PR on `dev` |

## 3. Security fixes on a frozen cert branch

The cert release branch (`release/<version>-qudo-<n>`) is frozen and accepts
**CVE cherry-picks only** — never a full `git subtree pull` (`docs/branching.md`):

```
git cherry-pick -x --strategy=subtree -Xsubtree=openssl <upstream-sha>
```

- Each cherry-pick is minimal and traceable (`-x` records the origin SHA).
- A cherry-pick that touches any file inside the FIPS module boundary source
  manifest is treated as **module-affecting** and triggers the re-validation
  assessment in §5.
- Every fixed build is re-run through the module's self-tests and the CI gates,
  and `fipsinstall` is re-run to regenerate `fipsmodule.cnf` for the new image.

## 4. Upstream syncs (non-frozen)

Maintenance syncs happen on `dev`, one subtree per branch, by PR:

```
git subtree pull --prefix=openssl       openssl-upstream    openssl-3.5.8 --squash
git subtree pull --prefix=qudo-pqc-lib  qudo-pqc-upstream   <tag>         --squash
```

The pin table in [`docs/subtree-pins.md`](subtree-pins.md) is updated in the same
PR. **qudo-pqc-lib must be pinned to a release tag, not a branch head**, for any
build intended for validation (this is the open item behind the current
version `[TO BE CONFIRMED]`; see Story 4.0).

## 5. Re-validation assessment

Before shipping any changed build as a validated module, classify the change:

| Change class | Examples | CMVP action |
|---|---|---|
| No security-relevant change | docs, CI, test-only | none |
| Non-security maintenance inside the boundary | refactor with identical behaviour, rebuild on a new toolchain | assess under CMVP maintenance / re-brand guidance **[TO BE CONFIRMED with lab]** |
| Security-relevant change inside the boundary | CVE fix in `libcrypto`/FIPS provider, ML-KEM/ML-DSA math change | re-validation or the applicable CMVP update scenario **[TO BE CONFIRMED with lab]** |
| New or changed OE | a new platform, new compiler baseline | operational-testing / re-validation on that OE |

The exact CMVP change-scenario mapping (e.g. the 3SUB / update scenarios) is
confirmed with the testing laboratory at pre-engagement (Story 4.7) and recorded
here **[TO BE CONFIRMED]**.

## 6. Version and identity discipline

- Every validated build has a distinct module version tied to exact subtree SHAs.
- qudo-pqc-lib is pinned to a **release tag**; the tag, not a branch head, is
  recorded in `subtree-pins.md` for cert builds.
- The reproducible-build property is used to prove that a rebuilt or
  independently built image is byte-identical to the validated one
  (Security Policy §11.4).

## 7. Roles and records

- The **Crypto Officer** applies updates only by installing a new validated
  build (never by patching an installed image) — see
  [`docs/crypto-officer-guide.md`](crypto-officer-guide.md) §10.
- Change records — the CVE SHA, the affected files, the re-validation
  classification, and the resulting module version — are retained by the vendor
  **[TO BE CONFIRMED: record-keeping owner and location]**.

## 8. References

- [`docs/security-policy.md`](security-policy.md), [`docs/branching.md`](branching.md),
  [`docs/subtree-pins.md`](subtree-pins.md), [`docs/reproducibility.md`](reproducibility.md)
- [`docs/crypto-officer-guide.md`](crypto-officer-guide.md)
- ADR-0004 (baseline pin), ADR-0005 (delegation seam), ADR-0008 (subtree layout)
- CMVP management manual / re-validation guidance **[TO BE CONFIRMED: reference]**
