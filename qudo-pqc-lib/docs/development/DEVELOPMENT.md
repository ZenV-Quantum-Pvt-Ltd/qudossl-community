# Development Guide

Developer-workflow notes for working on the QUDO PQC **library** (`libqudo-pqc`)
in this repository. For the contribution process (PRs, review, sign-off) and
code style see the root [`CONTRIBUTING.md`](../../CONTRIBUTING.md); this guide
covers the build/test/debug loop and the constraints specific to a FIPS module.

## Where the authoritative docs are

This guide does not duplicate the reference docs — go to the source of truth:

| Topic | Document |
|-------|----------|
| Architecture & design | [design/ARCHITECTURE.md](../design/ARCHITECTURE.md) |
| C API | [reference/API.md](../reference/API.md) |
| Algorithms & parameter sets | [reference/ALGORITHMS.md](../reference/ALGORITHMS.md) |
| Building (incl. FIPS) | [fips/FIPS_BUILD_GUIDE.md](../fips/FIPS_BUILD_GUIDE.md), [INSTALL.md](../../INSTALL.md) |
| Test suite & ACVP | [TESTING.md](TESTING.md) |
| Debugging | [DEBUGGING.md](DEBUGGING.md) |
| Updating vendored `*-native` | [maintenance/UPSTREAM_MAINTENANCE.md](../maintenance/UPSTREAM_MAINTENANCE.md) |
| Versioning & release | [maintenance/VERSIONING.md](../maintenance/VERSIONING.md), [maintenance/MAINTENANCE.md](../maintenance/MAINTENANCE.md) |

## Build / test loop

```bash
# Standard build + tests
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
ctest --test-dir build --output-on-failure

# FIPS module build (adds POST / CAST / integrity / state machine + gated tests)
cmake -S . -B build-fips -DQUDO_FIPS_MODULE=ON && cmake --build build-fips -j
ctest --test-dir build-fips --output-on-failure
```

`build.sh` wraps these (`--fips`, `-t`, `-s`/`-m`/`--tsan`, `--acvp`); see the
[FIPS Build Guide](../fips/FIPS_BUILD_GUIDE.md).

## FIPS-module constraints (read before changing the build)

The integrity self-test computes an HMAC-SHA-256 over a **contiguous `.text`
region** bounded by sentinel symbols. Several otherwise-normal toolchain options
**break** that and must not be enabled for a FIPS build:

- **No LTO** (`-flto`) — it reorders/merges code across the boundary.
- **No `-ffunction-sections` / `-fdata-sections`** and no `--gc-sections` — the
  algorithm objects are aggregated **between** the boundary sentinels with
  `-fno-function-sections -fno-data-sections` so nothing is reordered out of the
  measured region (see the parent `CMakeLists.txt`).
- **Boundary ordering matters** — `boundary_start` must link first and
  `boundary_end` last among the module objects.
- After any change that affects the binary, **re-finalise the integrity HMAC**
  (`qudo_fipsinstall -embed`, then re-codesign on macOS). A stale HMAC makes the
  module fail POST and refuse to load.

The three sub-libraries are pulled in as CMake **OBJECT** libraries and merged
into a single `libqudo-pqc`, so all crypto code lands inside the boundary.

## Vendored upstreams (`*-native`)

The lattice/hash mathematics lives in vendored, formally-verified upstreams
(`qudo-mlkem/mlkem-native/`, etc.). **Do not edit upstream sources** — QUDO
additions go in a `qudo_runtime_dispatch/` subdirectory, and version bumps follow
[UPSTREAM_MAINTENANCE.md](../maintenance/UPSTREAM_MAINTENANCE.md). The algorithm
set is fixed by FIPS 203/204/205; there is no routine "add an algorithm" workflow.

## Coding standards

- `clang-format` (`.clang-format`) and `clang-tidy` (`.clang-tidy`) are enforced
  in CI (`lint.yml`); run them locally before pushing.
- Every source file carries an SPDX licence header.
- Public symbols are restricted by `config/libqudo-pqc.map`; new internal symbols
  stay hidden (`local: *`).
- Sensitive material must be zeroized (`qudo_cleanse` / `qudo_secure_clear`)
  before free; see [Side-Channel Analysis](../security/SIDE_CHANNEL_ANALYSIS.md).

## Debugging

Sanitizers, Valgrind, coverage, fuzzing, error inspection and FIPS-state
diagnosis are covered in [DEBUGGING.md](DEBUGGING.md).
