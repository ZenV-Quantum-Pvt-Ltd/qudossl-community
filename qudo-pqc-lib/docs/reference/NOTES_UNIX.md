# Notes for Unix-like Platforms

Library-side build, install, and FIPS-deployment notes for Linux and macOS. For
generic build instructions see [../INSTALL.md](../../INSTALL.md); for the FIPS
build see [FIPS_BUILD_GUIDE.md](../fips/FIPS_BUILD_GUIDE.md).

This document covers `libqudo-pqc`. The integrity-config sections below apply to
any consumer of the library.

---

## Linux

### Distribution packages

The library has **no external cryptographic dependency** — the package
sets below cover only compiler, CMake, and (optional) Git.

**Ubuntu / Debian (22.04+, 24.04+):**

```bash
sudo apt-get install build-essential cmake git
```

**RHEL / Fedora / Rocky Linux 9+:**

```bash
sudo dnf install gcc cmake git
```

**Arch Linux:**

```bash
sudo pacman -S base-devel cmake git
```

**Alpine Linux (musl libc):**

```bash
apk add --no-cache build-base cmake git
```

The module is primarily tested with glibc ≥ 2.25 (required for the
`getentropy(3)` primary entropy path — see
[OPERATING_ENVIRONMENT.md §4.2](../fips/OPERATING_ENVIRONMENT.md#42-platform-entropy)).
Alpine / musl builds compile and pass tests but should be exercised in
your deployment environment: the standard (non-FIPS) build falls back to
reading `/dev/urandom` where glibc would use `getentropy`. (The FIPS build
adds a `getrandom(2)` syscall step before any `/dev/urandom` fallback.)

### CPU dispatch

On x86_64 Linux, AVX2 and AES-NI variants are runtime-detected:

```bash
grep -oE 'avx2|aes' /proc/cpuinfo | sort -u
```

On ARM64 Linux, NEON is used when available. No configuration is
required. In VMs and containers, ensure the hypervisor or container
runtime exposes the feature flags.

### Shared library paths

After `cmake --install build --prefix /usr/local`, refresh the dynamic
linker cache:

```bash
sudo ldconfig
```

For non-standard install prefixes:

```bash
echo "/opt/qudo/lib" | sudo tee /etc/ld.so.conf.d/qudo.conf
sudo ldconfig
```

Or set per-process:

```bash
export LD_LIBRARY_PATH=/opt/qudo/lib:$LD_LIBRARY_PATH
```

### FIPS integrity setup

The FIPS build verifies module integrity at every load using HMAC-SHA-256
over the integrity-covered `.text` region. Two deployment modes are
supported — pick one per OE.

#### Embedded HMAC (recommended for Linux/macOS)

`qudo_fipsinstall -embed` patches the HMAC into a reserved slot inside
the library binary. The application calls `qudo_pqc_init()` with no
integrity config — the module reads its own HMAC at startup.

```bash
# After build, AFTER any strip / codesign / package mutation step:
./build-fips/tools/qudo_fipsinstall -embed \
    -module build-fips/lib/libqudo-pqc.so

# Verify
./build-fips/tools/qudo_fipsinstall -verify-embed \
    -module build-fips/lib/libqudo-pqc.so
```

#### External `qudofipsmodule.cnf`

Use this mode when policy requires the integrity HMAC to live in a
separately-signed manifest.

```bash
sudo qudo_fipsinstall \
    -module /usr/local/lib/libqudo-pqc.so \
    -out    /etc/ssl/qudofipsmodule.cnf
```

`libqudo-pqc` does NOT auto-discover the cnf file. In external-config
deployments the application wrapping `qudo_pqc_init()` is responsible
for reading the cnf and populating `module_path` + `module_checksum_hex`
in `qudo_pqc_config_t` — see
[FIPS_BUILD_GUIDE.md §5.2](../fips/FIPS_BUILD_GUIDE.md#52-runtime-pickup-of-the-external-config).

Verify:

```bash
qudo_fipsinstall -verify \
    -module /usr/local/lib/libqudo-pqc.so \
    -in     /etc/ssl/qudofipsmodule.cnf
```

A successful run exits 0. Re-run `qudo_fipsinstall` after every binary
mutation (rebuild, package update, codesign, strip).

### Security hardening

The library applies these hardening flags automatically on Linux/Apple
Clang:

```
-fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE
-Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack
```

Symbol visibility is restricted by the linker version script
`config/libqudo-pqc.map`; only the documented public API is exported.

### SELinux

On RHEL / Fedora with SELinux enforcing, set the correct context after
install:

```bash
sudo chcon -t lib_t /usr/local/lib/libqudo-pqc.so
sudo restorecon -v /usr/local/lib/libqudo-pqc.so
```

### Cross-compilation

Toolchain files in `cmake/toolchains/`:

| Target          | Toolchain file                    | Required host package    |
| --------------- | --------------------------------- | ------------------------ |
| ARM64 (aarch64) | `aarch64-linux-gnu.cmake`         | `gcc-aarch64-linux-gnu`  |
| ARM32 (armhf)   | `arm-linux-gnueabihf.cmake`       | `gcc-arm-linux-gnueabihf` |
| x86 32-bit      | `x86-linux-gnu.cmake`             | `gcc-multilib`           |
| RISC-V 64-bit   | `riscv64-linux-gnu.cmake`         | `gcc-riscv64-linux-gnu`  |

```bash
cmake -S . -B build-arm \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-linux-gnu.cmake \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build-arm -j
```

Test cross-compiled binaries with QEMU:

```bash
sudo apt-get install qemu-user-static
qemu-aarch64-static build-arm/tests/test_pqc_post
```

---

## macOS

### Prerequisites

The library has **no OpenSSL dependency**. On macOS only CMake and the
Xcode command-line tools are required:

```bash
xcode-select --install        # Apple Clang + system libc
brew install cmake            # or download cmake from cmake.org
```

(LibreSSL ships with macOS; you do not need to install OpenSSL to build
or use `libqudo-pqc`.)

### Building

From the repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

For the FIPS build, see [FIPS_BUILD_GUIDE.md](../fips/FIPS_BUILD_GUIDE.md).

### Library paths

macOS uses `DYLD_LIBRARY_PATH` instead of `LD_LIBRARY_PATH`:

```bash
export DYLD_LIBRARY_PATH=/usr/local/lib:$DYLD_LIBRARY_PATH
```

**System Integrity Protection (SIP)** strips `DYLD_*` environment
variables from SIP-protected binaries (system tools). Run your own
binaries, or set the install name with `install_name_tool` /
`-rpath` at build time, instead of relying on the env var.

### Codesign and FIPS embedded-HMAC ordering

`codesign` modifies the Mach-O binary by appending a signature load
command. The FIPS integrity HMAC MUST be embedded **after** codesign:

```bash
codesign --sign "Developer ID Application: ..." \
         --timestamp --options runtime \
         build-fips/lib/libqudo-pqc.dylib

./build-fips/tools/qudo_fipsinstall -embed \
    -module build-fips/lib/libqudo-pqc.dylib

./build-fips/tools/qudo_fipsinstall -verify-embed \
    -module build-fips/lib/libqudo-pqc.dylib
```

Re-signing after `-embed` invalidates the HMAC. Re-run `-embed` whenever
the binary is mutated.

### CPU dispatch

Apple Silicon: NEON is always present and used. Intel: AVX2 / AES-NI
are runtime-detected.

### Universal binaries

`libqudo-pqc` does not currently build as a universal (fat) binary.
Build separately for `x86_64` and `arm64` if both are required.

---

## Common to Both Platforms

### Valgrind

```bash
# Install
sudo apt-get install valgrind     # Linux
brew install valgrind             # macOS Intel only — not supported on Apple Silicon

# The per-algorithm Valgrind harnesses live inside each sub-library:
bash qudo-mlkem/tests/test_valgrind.sh
bash qudo-mldsa/tests/test_valgrind.sh
bash qudo-slhdsa/tests/test_valgrind.sh
```

### Sanitizers

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build build-asan -j
ctest --test-dir build-asan --output-on-failure
```

`ENABLE_MSAN` (Clang only) and `ENABLE_TSAN` are also available; each is
mutually exclusive with the others.

### Ninja

For faster builds, install Ninja and pass it as the generator:

```bash
sudo apt-get install ninja-build   # Linux
brew install ninja                  # macOS

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build
```
