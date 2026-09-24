# Porting & Embedded Guide

`libqudo-pqc` is portable C99 with no external cryptographic dependency, which
makes it straightforward to bring up on new targets. This guide covers
cross-compilation, embedded/firmware builds, and the one thing you almost always
have to supply yourself on a bare-metal target: **platform entropy**.

For desktop/server platforms see [Notes — Unix](../reference/NOTES_UNIX.md) /
[Windows](../reference/NOTES_WINDOWS.md); for the FIPS build see
[FIPS Build Guide](../fips/FIPS_BUILD_GUIDE.md).

## Cross-compilation

Toolchain files live in `cmake/toolchains/` (aarch64, armhf, riscv64, x86,
arm64-windows-msvc):

```bash
cmake -S . -B build-arm \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-linux-gnu.cmake \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build-arm -j
qemu-aarch64-static build-arm/tests/test_pqc_post   # test under emulation
```

Runtime CPU dispatch (AVX2/NEON for the lattice families) is detected at run
time, so a single cross-built binary runs across micro-architectures of the same
ISA. SLH-DSA is portable C (no SIMD dispatch; SHA-NI used only where present at
build time on x86-64).

## Embedded / firmware builds

Two build options target constrained environments:

| Option | Effect |
|--------|--------|
| `MLKEM_EMBEDDED_BUILD` (per sub-library) | Builds for systems without a full stdlib; drops `-fPIE`/hardening flags that don't apply to firmware (`qudo-mlkem/CMakeLists.txt:35`) |
| `QUDO_BUILD_EMBEDDED_OBJ` (top level, FIPS only) | Produces a static archive plus a partial-linked `qudo_fips_module.o` for linking the FIPS module into firmware (`CMakeLists.txt:26,426-492`) |

```bash
# FIPS module as a partial-link object for firmware integration
cmake -S . -B build-emb -DQUDO_FIPS_MODULE=ON -DQUDO_BUILD_EMBEDDED_OBJ=ON
cmake --build build-emb -j
# -> static archive + qudo_fips_module.o  (see CMakeLists.txt "Usage (embedded firmware)")
```

Guidance:
- **No OpenSSL.** The combined module links no external crypto; DER works, PEM is
  unavailable in that configuration (returns `*_ERROR_NOT_IMPL`).
- **Prefer the Direct API** (zero-allocation, caller-provided buffers) on targets
  with constrained or no heap; the Object API needs a heap allocator.
- **Stack usage.** Signature operations (especially SLH-DSA, and ML-DSA rejection
  sampling) use non-trivial stack; size task/thread stacks accordingly and test
  with your toolchain's stack-usage reporting.

## Platform entropy (the critical port)

The module seeds its SP 800-90A CTR_DRBG from the OS CSPRNG via
`raw_platform_entropy()` (`src/fips/qudo_fips_rand.c`), which uses
`getentropy`/`getrandom` (Linux/macOS) or `BCryptGenRandom` (Windows). **On a
bare-metal/RTOS target none of these exist**, so you must provide a
cryptographically secure entropy source for that path; without it `qudo_pqc_init()`
fails (`QUDO_ERR_DRBG_SEED_FAIL`) and the module stays in `ERROR`.

Requirements for a port's entropy source:
- Full-entropy output (the module assumes H = 8 bits/byte — see
  [Entropy Source Assessment](../fips/ENTROPY_ASSESSMENT.md)).
- Available before the first cryptographic call (the startup health test draws
  1,024 samples).
- For FIPS use, an approved/assessed source — vendor-affirmed OS RNG or an ESV
  noise source; an unconditioned hardware TRNG must be assessed under SP 800-90B.

The SP 800-90B health tests (RCT/APT) still run on whatever source you supply, so
a stuck or biased source is caught (`QUDO_ERR_DRBG_HEALTH_FAIL`).

## Thread safety

The module state machine, DRBG, and self-test paths are synchronised; wrapper
error state is thread-local. On an RTOS, ensure the locking primitives the
platform layer expects are available, or build single-threaded.

## Verify the port

```bash
ctest --test-dir <build> --output-on-failure      # full suite incl. POST/PCT
bash acvp/run_acvp.sh --build-dir <build-acvp>     # NIST vectors, if reachable
```

A correct port passes POST at init, runs the PCT on keygen, and matches the ACVP
known-answer vectors bit-for-bit. See [Testing](TESTING.md) and
[Troubleshooting](TROUBLESHOOTING.md).
