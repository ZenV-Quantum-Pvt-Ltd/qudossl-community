# CI

GitHub Actions reads workflows from `.github/workflows/`, so the active
definitions live there — see `.github/workflows/ci.yml`.

This directory holds CI support material that is not a workflow file:
helper scripts, pinned tool manifests, and interop peer setup added in Epic 7.

## Sprint 1 matrix

| Job | Purpose |
|---|---|
| `linux-x86_64-fips` | build + OpenSSL test suite, `--enable-fips` |
| `linux-aarch64-fips` | same, on arm64 |
| `macos-arm64-fips` | same, on macOS arm64 |
| `windows-x64-fips` | same, via MSVC / nmake (`build/build.ps1`) |
| `linux-x86_64-default` | build + test suite, no FIPS |
| `qudo-pqc-lib standalone tests` | ctest in boundary mode |
| `upstream-parity` | `openssl/` tree matches the pinned `openssl-3.5.7` |
| `reproducible-build` | two clean builds, byte-compare — Linux **and** macOS |
| `abi-compat` | capture exported symbol baseline |

All four cert OEs are confirmed: Linux x86_64, Linux aarch64, macOS arm64 and
Windows x64. Every job above is active — nothing is gated any more.

Windows uses a different toolchain entirely (MSVC + nmake, not GNU make), so it
is driven by `build/build.ps1` rather than `build/Makefile`.

## Reproducibility per OE

Reproducibility must hold on **each** cert OE, so `reproducible-build` runs as a
matrix. Current status and the per-platform mechanisms are in
`docs/reproducibility.md`. Summary: Linux and macOS are covered; **Windows is
not yet verified** and is tracked as an open item.
