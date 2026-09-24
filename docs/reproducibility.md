# Reproducible builds

Two clean builds of the same commit must produce byte-identical artifacts on
**every** cert OE. The reproducible-build kit lets a lab or auditor regenerate
our bytes independently (design §15.5), and it is what makes a per-CVE 1SUB
delta provable rather than asserted (§15.4).

Confirmed cert OEs: **Linux x86_64, Linux aarch64, macOS arm64, Windows x64.**

## Status

| OE | Mechanism | Verified |
|---|---|---|
| Linux x86_64 | `SOURCE_DATE_EPOCH`, `-Wl,--build-id=none`, `-ffile-prefix-map` | CI |
| Linux aarch64 | same | CI |
| macOS arm64 | `SOURCE_DATE_EPOCH`, `ZERO_AR_DATE=1`, `-ffile-prefix-map` | CI + local |
| Windows x64 | `/Brepro` | **Not verified** |

Locally verified on macOS arm64 (2026-07-22): `libqudo-pqc.a` is byte-identical
across two clean builds.

## Per-platform notes

### Linux

Standard approach. `--build-id=none` suppresses the linker-generated build ID;
`SOURCE_DATE_EPOCH` fixes embedded timestamps; `-ffile-prefix-map` keeps
absolute build paths out of the binaries. GNU `ar` is invoked deterministically
by default in modern toolchains (`D` flag).

### macOS

**The design's prescribed flag does not work.** §12.3 and §22.1 specify
`-Wl,-no_uuid` on macOS. Stripping `LC_UUID` from `libcrypto.dylib` causes the
macOS linker to refuse to link anything against it:

```
ld: missing LC_UUID load command in './libcrypto.dylib'
```

which breaks `engines/`, `providers/` and `fuzz/`. The build fails outright.

Determinism is achieved instead by:

- **`ZERO_AR_DATE=1`** — zeroes the timestamps `ar`/`libtool` embed in static
  archive members. Without this, `libqudo-pqc.a` differs between builds.
- **Content-derived `LC_UUID`** — ld64 computes the UUID from a hash of the
  linked image, so identical inputs yield an identical UUID. Nothing needs
  stripping.
- **`-ffile-prefix-map`** — as on Linux.

This supersedes §12.3's macOS guidance. Update the design text before the
Security Policy is drafted, since the Security Policy describes the build
process to the lab.

### Windows

**Not yet verified — open item.**

`build/build.ps1` passes `/Brepro`, which asks the MSVC toolchain to emit a
deterministic PE with no link timestamp. This is a starting point, not a
demonstrated result. Known additional concerns:

- MSVC does not honour `SOURCE_DATE_EPOCH`.
- PDB paths and the debug directory can embed absolute paths.
- `lib.exe` archive member ordering and timestamps need checking.

Before the Windows OE can be submitted, `verify-reproducible.sh` needs a
PowerShell equivalent and a green two-build comparison. Track this as a
prerequisite for Epic 8, not Sprint 1.

## Running the check

```sh
make -C build repro-check          # Linux / macOS
```

Or in the pinned container:

```sh
docker build -f reproducible-build/Dockerfile.reproducible -t qudossl-repro .
docker run --rm -v "$PWD:/src" -w /src qudossl-repro make -C build repro-check
```

## Scope

Sprint 1 covers `libqudo-pqc.a`, `libcrypto`, `libssl`. The FIPS module joins
the comparison once Story 6.1 links the archive into it; Story 6.7 owns the
full three-artifact check across all OEs.
