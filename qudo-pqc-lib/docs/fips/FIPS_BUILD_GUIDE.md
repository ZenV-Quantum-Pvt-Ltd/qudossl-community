# QUDO PQC FIPS 140-3 Build Guide

## Overview

Building QUDO PQC as a FIPS 140-3 cryptographic module activates Power-On
Self-Tests, Pairwise Consistency Tests, the cryptographic state machine,
the per-operation FIPS indicator, in-memory module-integrity verification,
the audit log, and security-level enforcement. This document specifies the
build flags, link-time constraints, post-build steps, and per-platform
enforcement that a certified build depends on.

Runtime behavior — module states, indicators, self-test flow — is
documented in [FIPS.md](FIPS.md). The formal Security Policy lives in
[FIPS_SECURITY_POLICY.md](FIPS_SECURITY_POLICY.md). Operator-side
deployment requirements (file permissions, environment variables, key
storage) are in [OPERATING_ENVIRONMENT.md](OPERATING_ENVIRONMENT.md).

## Prerequisites

| Component        | Minimum                                    |
| ---------------- | ------------------------------------------ |
| CMake            | 3.15+                                      |
| Linux compiler   | GCC 10+ or Clang 14+                       |
| macOS compiler   | Apple Clang 14+                            |
| Windows compiler | MSVC 2019+, or MinGW-w64 GCC 10+           |
| Build tool       | GNU Make, Ninja, or MSBuild                |

No third-party cryptographic dependency. AES-256-CTR-DRBG, HMAC-SHA-256,
SHA-2, and SHA-3 are compiled from in-tree sources. OpenSSL is never
linked into `libqudo-pqc` in any configuration — see
[§4](#4-no-openssl-in-the-cryptographic-core).

## What `QUDO_FIPS_MODULE=ON` enables

`-DQUDO_FIPS_MODULE=ON` is the single switch that turns the standard
build into a FIPS module. It:

- Compiles in the POST suite: KAT coverage per FIPS 140-3 IG 10.3.A §14
  Note22 (ML-KEM), §15 Note26 (ML-DSA) and §16 Note29/Note30 (SLH-DSA) —
  ML-KEM keygen KATs for all three sets (512/768/1024) with the
  encaps/decaps KAT on ML-KEM-512, ML-DSA-65 keygen+sign+verify, and one
  SHA2 + one SHAKE SLH-DSA representative (SLH-DSA-SHA2-128f,
  SLH-DSA-SHAKE-128f) — plus KATs for the primitives (SHA-256,
  HMAC-SHA-256, AES-256 CTR_DRBG). Full per-parameter-set functional
  coverage for the sets not exercised by POST lives outside POST in
  `tests/` and `acvp/`.
- Activates the pairwise-consistency test on every generated and
  imported keypair, with fail-zeroize on PCT failure.
- Enables the state machine
  (`INIT → SELFTEST → RUNNING` / `ERROR`) and gates every cryptographic
  entry point behind it.
- Emits the boundary sentinels and arranges link order so the loaded
  image contains a contiguous integrity-covered region from
  `qudo_fips_module_start` to `qudo_fips_module_end`.
- Adds `-fno-function-sections -fno-data-sections` globally on GCC/Clang/Apple
  Clang/MinGW, fails configure if `-ffunction-sections` is in
  `CMAKE_C_FLAGS`, and fails configure if LTO
  (`CMAKE_INTERPROCEDURAL_OPTIMIZATION`) is on.
- Applies the supplemental linker script `cmake/fips_module.ld` on
  Linux GCC/Clang to pin boundary objects (Apple ld64 and MSVC link.exe
  do not consume GNU linker scripts).
- Runs `cmake/check_fips_boundary.sh` as a POST_BUILD step on Linux to
  verify every FIPS-core symbol resolves inside the integrity region.
- Builds `tools/qudo_fipsinstall` for post-build integrity HMAC
  patching and verification.

It does not toggle OpenSSL. The cryptographic core never uses OpenSSL
in any build configuration.

## Quick Start

### Linux / macOS

```bash
cmake -S . -B build-fips \
      -DQUDO_FIPS_MODULE=ON \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build-fips -j

# Embed the integrity HMAC into the binary (recommended on Linux/macOS):
./build-fips/tools/qudo_fipsinstall -embed \
    -module build-fips/lib/libqudo-pqc.so      # .dylib on macOS

ctest --test-dir build-fips --output-on-failure
```

### Windows

`build_windows.bat --fips` runs the equivalent flow and additionally
generates `build-fips\qudofipsmodule.cnf` automatically — Windows uses
the external-config integrity path rather than embedded HMAC (see
[§5.1](#51-choosing-between-embedded-hmac-and-external-config)).

```bat
build_windows.bat --fips --test
```

## Build Constraints

### 1. Contiguous .text region

In-memory integrity hashes the bytes from `qudo_fips_module_start` to
`qudo_fips_module_end`. This requires every FIPS-covered function to
land inside a single contiguous `.text` span and stay in source-file
order.

**Enforced by:**

| Platform                  | Mechanism                                                                                                                                                                                       |
| ------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Linux GCC/Clang           | `-fno-function-sections` per target + supplemental linker script `cmake/fips_module.ld` + POST_BUILD coverage check `cmake/check_fips_boundary.sh`.                                             |
| Apple Clang (macOS)       | `-fno-function-sections` per target. ld64 does not accept GNU linker scripts, so ordering relies on the boundary-source-file convention plus the embedded-HMAC mode's pre-deployment recompute. |
| MinGW-w64 (Windows GCC)   | `-fno-function-sections` per target. The Linux linker script and POST_BUILD coverage check do not apply.                                                                                        |
| MSVC (Windows)            | No `-fno-function-sections` equivalent. MSVC's `/OPT:ICF` folding and stripped COFF symbol tables make in-binary HMAC patching unreliable, so the external `qudofipsmodule.cnf` holds the authoritative integrity HMAC. See [§5](#5-post-build-integrity-qudo_fipsinstall). |

**Hard configure-time failures (all platforms):**

- `-ffunction-sections` present in `CMAKE_C_FLAGS` → `FATAL_ERROR`.
  This is the most common silent breakage in downstream builds; it must
  be removed.
- `CMAKE_INTERPROCEDURAL_OPTIMIZATION` enabled → `FATAL_ERROR`. See
  [§3](#3-link-time-optimization-is-forbidden).

**`-fdata-sections`:** the FIPS build adds **both** `-fno-function-sections`
and `-fno-data-sections` globally on GCC/Clang (`CMakeLists.txt:167-168`), so
neither functions nor data are split into per-symbol sections that the linker
could reorder out of the measured `.text` region.

**Manual verification:**

```bash
# Linux / macOS
nm -n build-fips/lib/libqudo-pqc.so | grep qudo_fips_module_
```

`qudo_fips_module_start` must appear before `qudo_fips_module_end`, and
both must be in the text segment.

### 2. Boundary source-file ordering

`src/qudo_fips_boundary_start.c`
must be the FIRST translation unit and
`src/qudo_fips_boundary_end.c` must be
the LAST in the library link line. The top-level `CMakeLists.txt`
constructs the source list this way — do not reorder it.

On Linux, [cmake/fips_module.ld](../../cmake/fips_module.ld) additionally
pins these objects via `KEEP(...)` sections inserted before and after
`.text`, which survives `--gc-sections` and link-time folding. On
macOS and Windows the source-file order is the sole guarantor and the
POST_BUILD coverage check is not available, so any change to library
sources must be followed by `qudo_fipsinstall -verify-embed` (or
`-verify` against the external cnf) before release.

### 3. Link-Time Optimization is forbidden

LTO may inline, merge, or reorder functions across translation units,
breaking the boundary. The configure step exits with `FATAL_ERROR` if
`CMAKE_INTERPROCEDURAL_OPTIMIZATION` is on while
`-DQUDO_FIPS_MODULE=ON`. Parent projects that enable LTO globally must
turn it off (or scope it to non-FIPS targets) when consuming
`qudo-pqc`.

### 4. No OpenSSL in the cryptographic core

`libqudo-pqc` is built against in-tree AES-256-CTR-DRBG, HMAC-SHA-256,
SHA-2, and SHA-3 in all configurations. There is no OpenSSL dependency
in the FIPS build or the standard build.

The CMake variable `QUDO_PQC_USE_OPENSSL` is set to `OFF`
unconditionally and is propagated to every sub-library
(`MLKEM_USE_OPENSSL`, `MLDSA_USE_OPENSSL`, `SLHDSA_USE_OPENSSL`). The
OpenSSL provider that exposes QUDO algorithms through the EVP API is a
*separate* component in the `qudo-provider` repository; it links
against the module's public API only.

## 5. Post-Build Integrity (`qudo_fipsinstall`)

Ordering rules — they differ by integrity mode:

- `strip` (or any tool that rewrites the binary layout) must run
  **before** the integrity step on every platform.
- **External config** (`-out qudofipsmodule.cnf`): generate the config
  **after** all post-processing, including Authenticode signing on
  Windows — the config hashes the file as shipped, so any later
  mutation invalidates it.
- **Embedded HMAC on macOS** (`-embed`): run `-embed` first, then
  re-sign as the **last** step:
  `codesign --remove-signature <lib> 2>/dev/null || true && codesign -s - -f <lib>`.
  The Mach-O signature lives in `LC_CODE_SIGNATURE`/`__LINKEDIT`,
  outside the HMAC-covered code region, so signing does not disturb
  the embedded HMAC — whereas embedding into an already-signed binary
  breaks the signature, and current macOS refuses to load the library.
  For distributed artifacts, use a real signing identity with
  `--timestamp --options runtime` instead of the ad-hoc `-s -`.

### 5.1 Choosing between embedded HMAC and external config

| Mode                          | Use when                                                                                                                                                                                  | Tool invocation                                                       |
| ----------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------- |
| **Embedded HMAC** (`-embed`)  | Linux, macOS, or embedded firmware. The HMAC is patched into a reserved symbol in the binary; integrity verification is self-contained at load time. Supports ELF and Mach-O 64-bit.       | `qudo_fipsinstall -embed -module <library>`                           |
| **External config** (`-out`)  | Windows MSVC builds (default in `build_windows.bat`), or any deployment that mandates a separate signed integrity manifest. The HMAC lives in `qudofipsmodule.cnf` and is loaded at init. | `qudo_fipsinstall -module <library> -out qudofipsmodule.cnf`          |

Both modes compute HMAC-SHA-256 over the same integrity region. Only
the storage form differs.

### 5.2 Runtime pickup of the external config

When the library is deployed without an embedded HMAC, the application
is responsible for locating `qudofipsmodule.cnf`, reading its contents,
and populating `qudo_pqc_config_t` fields before calling
`qudo_pqc_init()`:

- `module_path` — absolute path of the loaded library file
- `module_checksum_hex` — the HMAC hex string read from the cnf
- Optionally `io_open` / `io_read` / `io_close` — callbacks the
  application supplies if it wants the integrity layer to perform the
  read itself instead of providing the checksum pre-parsed

There is no environment-variable auto-discovery inside `libqudo-pqc`.
The CTest harness in this repository passes the cnf path to each test
binary via a `--fips-cnf <path>` CLI argument; each test's `main()` reads
the file and populates the config. Production deployments must do the
same — typically via a thin wrapper that knows the deployment's file
layout. The `QUDO_PQC_FIPS_CONF` env var seen in some test fixtures is
checked by tests to *prove the library ignores it*
(`tests/test_integrity_path_pinning.c`)
— it is not a library-level mechanism.

The config file MUST have permissions no laxer than `0444` and SHOULD be
co-located with the library and signed alongside it — see
[OPERATING_ENVIRONMENT.md](OPERATING_ENVIRONMENT.md).

### 5.3 `qudo_fipsinstall` command reference

```
Generate config:
  qudo_fipsinstall -module <module> [-library <lib>] -out <config.cnf>

Verify config:
  qudo_fipsinstall -verify -module <module> [-library <lib>] -in <config.cnf>

Embed HMAC (in-memory verification):
  qudo_fipsinstall -embed -module <library>

Verify embedded HMAC:
  qudo_fipsinstall -verify-embed -module <library>

Options:
  -section_name <name>  Config section name (default: qudoprov_sect)
  -quiet                Suppress informational output
```

`-library` covers the case where the integrity HMAC must span both the
module and an additional consumer (for example, the OpenSSL provider).
Omit it for standalone `libqudo-pqc` deployments.

## 6. Code Signing

### 6.1 macOS (`codesign`)

`codesign` modifies the Mach-O image by appending a code-signature load
command and signature blob. Run it BEFORE `qudo_fipsinstall -embed`:

```bash
codesign --sign "Developer ID Application: ..." \
         --timestamp --options runtime \
         build-fips/lib/libqudo-pqc.dylib

./build-fips/tools/qudo_fipsinstall -embed \
    -module build-fips/lib/libqudo-pqc.dylib

./build-fips/tools/qudo_fipsinstall -verify-embed \
    -module build-fips/lib/libqudo-pqc.dylib
```

Re-signing requires a re-`-embed`.

### 6.2 Windows (Authenticode) — deployer responsibility

If your organization's policy requires Authenticode-signed DLLs, sign
the module BEFORE running `qudo_fipsinstall`. This step is performed by
the organization deploying the module, not by the library distributor.

**Requirements:**

- Extended Validation (EV) code-signing certificate from a trusted CA.
- Windows SDK `signtool.exe` or an equivalent CMS signing tool.
- An RFC 3161 timestamp authority URL for long-term signature validity.
- SHA-256 (or stronger) digest algorithm. SHA-1 is forbidden.
- Signing key in an HSM (mandatory for EV certificates).

**Signing procedure:**

```bat
REM 1. Build the FIPS module.
build_windows.bat --fips

REM 2. Sign the DLL.
signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 ^
    /a build-fips\lib\qudo-pqc.dll

REM 3. Verify the signature.
signtool verify /pa /v build-fips\lib\qudo-pqc.dll

REM 4. Recompute the FIPS integrity config AFTER signing.
build-fips\tools\qudo_fipsinstall.exe ^
    -module build-fips\lib\qudo-pqc.dll ^
    -out build-fips\qudofipsmodule.cnf

REM 5. Verify the config.
build-fips\tools\qudo_fipsinstall.exe -verify ^
    -module build-fips\lib\qudo-pqc.dll ^
    -in build-fips\qudofipsmodule.cnf
```

The ordering `build → sign → fipsinstall` is mandatory. Any re-sign
requires a re-`fipsinstall`.

## 7. Platform Enforcement Matrix

| Platform                       | Compile flag      | Linker script | POST_BUILD coverage check | Integrity mechanism                      |
| ------------------------------ | :---------------: | :-----------: | :-----------------------: | ---------------------------------------- |
| Linux x86_64 (GCC 10+)         | applied           | applied       | applied                   | embedded HMAC (ELF)                      |
| Linux x86_64 (Clang 14+)       | applied           | applied       | applied                   | embedded HMAC (ELF)                      |
| Linux aarch64 (GCC 10+)        | applied           | applied       | applied                   | embedded HMAC (ELF)                      |
| macOS x86_64 (Apple Clang 14+) | applied           | n/a (ld64)    | n/a                       | embedded HMAC (Mach-O)                   |
| macOS arm64 (Apple Clang 14+)  | applied           | n/a (ld64)    | n/a                       | embedded HMAC (Mach-O)                   |
| Windows x64 (MSVC 2019+)       | n/a (no flag)     | n/a           | n/a                       | external `qudofipsmodule.cnf` (PE/COFF)  |
| Windows x64 (MinGW-w64)        | applied           | n/a           | n/a                       | external `qudofipsmodule.cnf` (PE/COFF)  |
| Windows ARM64 (MSVC 2022+)     | n/a (no flag)     | n/a           | n/a                       | external `qudofipsmodule.cnf` (PE/COFF)  |

"n/a" never means a requirement is unmet — it means the constraint is
satisfied by a different platform-appropriate mechanism. The integrity
HMAC covers the same byte range on every platform; only the storage
form differs.

## 8. Verification Checklist

Before promoting a FIPS build to release:

- [ ] CMake configure log shows `FIPS module: ON` and the build
      directory is dedicated (e.g. `build-fips/`).
- [ ] Compiler flags include `-fno-function-sections` on GCC, Clang,
      Apple Clang, and MinGW. Verify with
      `cmake --build build-fips --verbose | grep function-sections`.
- [ ] No LTO: no `-flto` or `-fwhole-program` in `CMAKE_C_FLAGS`, and
      `CMAKE_INTERPROCEDURAL_OPTIMIZATION` is off.
- [ ] Boundary sentinels present and ordered:
      `nm -n build-fips/lib/libqudo-pqc.so | grep qudo_fips_module_`.
- [ ] (Linux only) POST_BUILD coverage check passed without any
      "symbol outside region" diagnostic.
- [ ] Integrity HMAC verifies after every binary mutation:
      `qudo_fipsinstall -verify-embed -module <lib>` (embedded) or
      `qudo_fipsinstall -verify -module <lib> -in <cnf>` (external)
      exits 0.
- [ ] `ctest --test-dir build-fips --output-on-failure` passes,
      including the POST, PCT, integrity, and indicator test groups.
- [ ] On Windows (and any external-config deployment),
      `qudofipsmodule.cnf` permissions are 0444 and the file is
      deployed alongside the library. The application wrapping
      `qudo_pqc_init()` reads the cnf and populates `module_path` +
      `module_checksum_hex` before calling init.
- [ ] Code signature (codesign / Authenticode) verifies, and the
      integrity HMAC was computed AFTER signing.

## 9. Troubleshooting

| Symptom                                                                                              | Likely cause                                                                                                                                            |
| ---------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Configure fails: "`-ffunction-sections` in `CMAKE_C_FLAGS` breaks the in-memory integrity boundary"  | A parent project, toolchain file, or env var added `-ffunction-sections`. Remove it from `CMAKE_C_FLAGS`.                                               |
| Configure fails: "LTO ... MUST be disabled"                                                          | `CMAKE_INTERPROCEDURAL_OPTIMIZATION` is on. Turn it off (or scope it to non-FIPS targets) for the FIPS build.                                           |
| Configure fails: "linker script ... missing"                                                         | The repository is incomplete. `cmake/fips_module.ld` is required for FIPS builds on Linux.                                                              |
| POST_BUILD reports "symbol outside the integrity region"                                             | A new FIPS-core source file isn't in the right link position, or a fresh symbol prefix needs to be added to `cmake/check_fips_boundary.sh`.             |
| `qudo_fipsinstall -verify-embed` reports an HMAC mismatch                                            | The binary's code region was modified after `-embed` ran (stripped, patched). Re-run `-embed` (and on macOS re-sign afterwards).                        |
| `qudo_pqc_init()` returns an integrity error on Windows                                              | The application did not populate `module_path` + `module_checksum_hex` from `qudofipsmodule.cnf` before calling init, or the checksum does not match the loaded library. Re-read the cnf and re-populate the config. |
| The process is killed (SIGKILL) loading the module on macOS                                          | strip / `-embed` invalidated the Mach-O code signature. Re-sign as the last step: `codesign --remove-signature <lib> 2>/dev/null \|\| true` then `codesign -s - -f <lib>`. |
