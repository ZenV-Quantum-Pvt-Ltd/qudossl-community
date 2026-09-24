# Build and Install

## Prerequisites

- **CMake** 3.15+
- **C99 compiler**: GCC 10+, Clang 14+, or MSVC 2019+
- **Platform libraries**: pthreads (Unix), bcrypt (Windows)

No external dependencies. The library and all tools use internal
AES-256-CTR-DRBG and HMAC-SHA-256. No OpenSSL required.

## Standard Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

This produces (under `build/`):

- `lib/libqudo-pqc.{so,dylib,dll}` — shared library (default; controlled by `BUILD_SHARED_LIBS`)
- `lib/libqudo-pqc.a` / `qudo-pqc.lib` — also built when `QUDO_BUILD_EMBEDDED_OBJ=ON` for the static archive used by the embedded partial-link step

## FIPS Build

```bash
cmake -S . -B build-fips -DCMAKE_BUILD_TYPE=Release -DQUDO_FIPS_MODULE=ON
cmake --build build-fips -j
```

The FIPS build adds power-on self-tests (POST), module-integrity verification, the state machine, pairwise consistency tests, conditional algorithm self-tests, and the per-operation FIPS indicator. See [FIPS compliance overview](docs/fips/FIPS.md) for the full picture.

After building, compute the integrity HMAC. Two modes are supported:

```bash
# Embedded HMAC — recommended for Linux/macOS. Patches the HMAC into the binary
# so qudo_pqc_init() needs no external config.
./build-fips/tools/qudo_fipsinstall -embed -module build-fips/lib/libqudo-pqc.so

# External config — required for Windows (and any deployment that wants a
# separate signed integrity manifest). The application is responsible for
# reading the cnf and passing module_path + module_checksum_hex into
# qudo_pqc_config_t before calling qudo_pqc_init() — the library does NOT
# auto-discover the cnf.
./build-fips/tools/qudo_fipsinstall -module build-fips/lib/libqudo-pqc.so \
                                    -out  build-fips/qudofipsmodule.cnf
```

`build_windows.bat --fips` runs the external-config flow automatically. See [FIPS Build Guide](docs/fips/FIPS_BUILD_GUIDE.md) for the full enforcement matrix and verification checklist.

### Build wrapper script

`build.sh` at the repository root wraps common cmake invocations:

```bash
./build.sh --fips             # FIPS build into build-fips/
./build.sh --fips -t          # FIPS build + run tests (always clean)
./build.sh --acvp             # Standard build with ACVP test runners
./build.sh -s                 # Standard build with ASan + UBSan
./build.sh --coverage         # Standard build with gcov/lcov coverage
./build.sh -c                 # Remove all build directories (clean, no rebuild)
./build.sh --help             # Full option list
```

### Version-aware install (`-i`)

`./build.sh -i` compares the freshly built library against the one already
installed at the resolved prefix (the only copy consumers of that prefix
load) before touching anything:

| Installed state at the prefix       | Action                                   |
| ----------------------------------- | ---------------------------------------- |
| Not installed                       | Install                                  |
| Same version, identical checksum    | Skip — the installed module is untouched |
| Same version, different contents    | Reinstall ("refreshing")                 |
| Older than this build               | Reinstall ("upgrading X -> Y")           |
| Newer than this build               | Stop with an error — update your sources first, or pass `--force-lib-downgrade` |

Every install ends with a verdict line stating the version, location, and
checksum of what is now installed. The same logic runs when
`qudo-provider/build.sh -i` delegates the library install to this script.

## Build Options

| Option                      | Default | Description                                                                    |
| --------------------------- | ------- | ------------------------------------------------------------------------------ |
| `QUDO_FIPS_MODULE`          | OFF     | Build as FIPS 140-3 module (POST, KATs, integrity, state machine, PCT, CAST)   |
| `QUDO_PQC_BUILD_TESTS`      | ON      | Build the library test suite                                                   |
| `BUILD_ACVP`                | OFF     | Build the ACVP runners under `acvp/` for NIST algorithm validation             |
| `QUDO_BUILD_EMBEDDED_OBJ`   | OFF     | Build the partial-link `qudo_fips_module.o` for embedded / firmware (FIPS only) |
| `BUILD_SHARED_LIBS`         | ON      | Build a shared library; set OFF for static-only                                |
| `ENABLE_SANITIZERS`         | OFF     | Enable ASan + UBSan (Debug builds)                                             |
| `ENABLE_MSAN`               | OFF     | Enable MemorySanitizer (Clang only; mutually exclusive with ASan/UBSan/TSan)    |
| `ENABLE_TSAN`               | OFF     | Enable ThreadSanitizer (mutually exclusive with ASan/UBSan/MSan)                |
| `ENABLE_COVERAGE`           | OFF     | gcov/lcov instrumentation; report via `coverage.sh`                            |

## Running Tests

```bash
cd build
ctest --output-on-failure
```

See [Testing](docs/development/TESTING.md) for details on the test suite.

## Installation

```bash
cmake --install build --prefix /usr/local
```

This installs:
- Libraries to `lib/`
- Headers to `include/qudo-pqc/`
- CMake config to `lib/cmake/qudo-pqc/`

## Using from Another CMake Project

After installation:

```cmake
find_package(qudo-pqc 1.0 REQUIRED)
target_link_libraries(myapp PRIVATE qudo::qudo-pqc)
```

Or as a subdirectory:

```cmake
add_subdirectory(qudo-pqc)
target_link_libraries(myapp PRIVATE qudo-pqc)
```

## Platform Notes

### Linux
```bash
sudo apt-get install cmake gcc   # Debian/Ubuntu
```

### macOS
```bash
brew install cmake
```

### Windows (MSVC)
```bat
mkdir build && cd build
cmake -G "Visual Studio 17 2022" -A x64 ..
cmake --build . --config Release
```

### Windows (MinGW)
```bash
mkdir build && cd build
cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release ..
mingw32-make -j4
```

### Cross-compilation

Toolchain files are provided in `cmake/toolchains/`:

```bash
cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-linux-gnu.cmake ..
```

For deployment-time platform specifics — FIPS integrity install, OS entropy
source, packaging — see [docs/reference/NOTES_UNIX.md](docs/reference/NOTES_UNIX.md)
and [docs/reference/NOTES_WINDOWS.md](docs/reference/NOTES_WINDOWS.md).
