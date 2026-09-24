# Branch model

## Branches

| Branch | Purpose | Accepts |
|---|---|---|
| `main` | Release-ready trunk | Merges from `dev` only |
| `dev` | Integration branch | PRs from `feat/*` and `fix/*` |
| `feat/<story>-<slug>` | One story or epic | Direct commits by its author |
| `fix/<slug>` | Bug fix | Direct commits by its author |
| `sync/<subtree>-<version>` | One upstream subtree sync | `git subtree pull` only |
| `release/<version>-qudo-<n>` | Frozen cert branch | **CVE cherry-picks only** |

All of these except `main` and `dev` are **short-lived**: cut when needed,
merged by PR, then deleted. There are no standing staging branches.

> **`vendor/subtrees`** is a one-off from Sprint 1. Introducing the two subtrees
> added ~6,500 files in a single commit, and GitHub caps a PR at 3,000 files —
> so the vendoring was split onto its own branch to keep the ~20 Qudo-authored
> files reviewable. Delete it once it merges into `dev`. Later subtree updates
> use `sync/` branches and are far smaller, so this should not recur.

## Flow

```
feat/sprint1-foundation ──PR──> dev ──PR──> main ──branch──> release/3.5-qudo-1.0
```

Feature branches are cut from `dev` and merged back into `dev` by PR. Team
reviews and merges; PRs are not self-merged.

## Cert release branch rules

Once a `release/<version>-qudo-<n>` branch is cut for certification it is
**frozen**:

- **No** upstream merges.
- **No** `git subtree pull` — ever.
- CVE cherry-picks only, using path translation:

  ```sh
  git cherry-pick -x --strategy=subtree -Xsubtree=openssl <upstream-sha>
  ```

- Any cherry-pick touching a file listed in
  `openssl/providers/fips.module.sources` triggers re-certification. See the
  design §15.4 for the 1SUB / 3SUB decision flow.

## Upstream sync

Never on a cert release branch. On a **short-lived `sync/` branch cut from
`dev`**, one subtree per branch, merged by PR and then deleted:

```sh
# OpenSSL
git checkout -b sync/openssl-3.5.8 dev
git fetch openssl-upstream refs/tags/openssl-3.5.8
git subtree pull --prefix=openssl openssl-upstream openssl-3.5.8 --squash
# resolve conflicts on openssl/* with normal git tooling, then PR into dev

# qudo-pqc-lib
git checkout -b sync/qudo-pqc-<release> dev
git subtree pull --prefix=qudo-pqc-lib qudo-pqc-upstream <ref> --squash
```

Update the pin table in `subtree-pins.md` in the same PR.

### Why a `sync/` branch and not a standing one

The design is inconsistent here: §4.2 says syncs happen on `qudossl/main` only,
while Story 1.8 says to create a long-lived `vendor/openssl-3.x` staging branch.
Neither is used, for two reasons.

**Syncing on `main` contradicts the branch model.** `main` accepts merges from
`dev` only, and a subtree pull can conflict heavily once Epic 3 puts Qudo edits
in `crypto/ml_kem`, `crypto/ml_dsa` and `crypto/slh_dsa`. Conflict resolution
belongs on a branch behind a PR and CI, not on the release trunk.

**A standing `vendor/openssl-3.x` has no work to do between syncs.** Syncs are
episodic — a few times a year, per upstream release. A branch that sits untouched
in between goes stale, and anyone who finds it has to work out whether it is
ahead, behind, or abandoned. On a repo where cert reproducibility means "one
repo, one SHA", an ambiguous long-lived branch is a liability.

A per-sync branch gives the same isolation and the same CI gate, records *which*
version it carried in its own name, and disappears when merged.

## Branch protection

Applied to `main` and `dev` after the first push:

- Require a pull request before merging
- Require at least one approving review
- Dismiss stale approvals on new commits
- Require CI status checks to pass
- No force pushes, no deletion

Cert release branches additionally restrict who may push.

## What Qudo owns

Everything outside the two subtrees:

```sh
git ls-files | grep -v '^openssl/' | grep -v '^qudo-pqc-lib/'
```
