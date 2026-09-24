# Subtree pins

QudoSSL vendors two upstream trees as git subtrees. Cert reproducibility depends
on one repo resolving to one SHA, so both pins are recorded here explicitly.

| Subtree | Upstream | Pinned to | Commit |
|---|---|---|---|
| `openssl/` | github.com/openssl/openssl | tag `openssl-3.5.7` | `8cf17aaeb4599f8af87fefd810b5b5fee90fe69e` |
| `qudo-pqc-lib/` | ZenVInnovations/qudo-pqc-lib | tag `v1.0.0` | `505326e1...` |

Verify what is actually in the tree:

```sh
git log --oneline -1 -- openssl
git log --oneline -1 -- qudo-pqc-lib
```

## Why these pins

**`openssl-3.5.7`** — a released tag, not the `openssl-3.5` branch the design's
§4.2 example uses. A branch is a moving target and cannot support a reproducible
cert SHA. See ADR-0004.

**qudo-pqc-lib `v1.0.0`** (`505326e`) — a released tag, not a branch head, so
it can support a reproducible cert SHA. It carries:

- **`QUDO_PQC_MATH_ONLY`** (qudo-pqc-lib#9) — keeps qudo's own POST, PCT,
  integrity check, CTR-DRBG, AES and HMAC out of the archive. Without it those
  are linked into `fips.so` alongside OpenSSL's and ML-KEM cannot be
  constructed at all. See ADR-0009.
- **Audit K2** (qudo-pqc-lib#10) — no platform entropy source is compiled in
  under math-only, so `getentropy` / `BCryptGenRandom` / `arc4random` cannot
  enter the FIPS boundary. Measured before the fix: `fips.so` on Linux carried
  `U getentropy@GLIBC_2.25`; after, zero. `ci/check-boundary-symbols.sh`
  asserts this.
- The standalone test suite is skipped in math-only builds, which previously
  failed to link 21 of 35 targets.

> **Never hand-edit files under `qudo-pqc-lib/`.** They are vendored. A local
> edit is silently reverted by the next `git subtree pull`, and the divergence
> is invisible in a normal diff. Fix upstream, cut a tag, then re-pin here —
> the route this pin took.

## Updating a pin

Only on `main` — never on a cert release branch (see `branching.md`).

```sh
# OpenSSL
git fetch openssl-upstream refs/tags/openssl-3.5.8
git subtree pull --prefix=openssl openssl-upstream openssl-3.5.8 --squash

# qudo-pqc-lib
git fetch qudo-pqc-upstream --tags
git subtree pull --prefix=qudo-pqc-lib qudo-pqc-upstream v1.0.1 --squash
```

Update the table above in the same commit, and file an ADR if the pin moves for
any reason other than a routine sync.

## Remotes

These are not stored in the repo; add them locally once:

```sh
git remote add openssl-upstream  https://github.com/openssl/openssl.git
git remote add qudo-pqc-upstream https://github.com/ZenVInnovations/qudo-pqc-lib.git
```
