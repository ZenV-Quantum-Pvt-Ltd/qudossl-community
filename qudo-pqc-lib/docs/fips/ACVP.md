# ACVP Test Runner

NIST Automated Cryptographic Validation Protocol (ACVP) test harness for ML-KEM (FIPS 203), ML-DSA (FIPS 204), and SLH-DSA (FIPS 205). Required for CAVP algorithm validation certificates, which are prerequisites for FIPS 140-3 CMVP module certification.

## Quick Start

From the repository root:

```bash
# Build
./build.sh --acvp           # Standard build + ACVP runners (uses build/)
./build.sh --acvp -f        # Clean rebuild

# Run all ACVP tests (downloads NIST vectors on first run)
bash acvp/run_acvp.sh

# Run a single algorithm
LD_LIBRARY_PATH=build/lib python3 acvp/acvp_client_mlkem.py
LD_LIBRARY_PATH=build/lib python3 acvp/acvp_client_mldsa.py
LD_LIBRARY_PATH=build/lib python3 acvp/acvp_client_slhdsa.py

# Skip SLH-DSA (slow: ~10 min for sigGen)
bash acvp/run_acvp.sh --skip-slhdsa

# Specify custom build directory
bash acvp/run_acvp.sh --build-dir /path/to/build

# Use specific ACVP Server version
bash acvp/run_acvp.sh --version v1.1.0.41

# Use local vector files
python3 acvp/acvp_client_mlkem.py -p prompt.json -e expected.json
```

## Architecture

```
NIST ACVP Server (usnistgov/ACVP-Server)
    ↓ JSON test vectors (prompt.json, expectedResults.json)
Python ACVP Clients (acvp_client_*.py)
    ↓ CLI args (hex-encoded)
C ACVP Binaries (qudo_acvp_mlkem, qudo_acvp_mldsa, qudo_acvp_slhdsa)
    ↓ calls through
libqudo-pqc.so (FIPS module boundary)
    ↓ stdout
Python ACVP Clients
    ↓ compare
Expected Results
```

All cryptographic operations go through `libqudo-pqc.so` — the FIPS 140-3 module boundary. ACVP binaries link against `libqudo-pqc` and call the Object API directly. This validates the exact code path that production applications use.

## C Binaries

### qudo_acvp_mlkem — ML-KEM (FIPS 203)

| Mode | CLI | Output |
|------|-----|--------|
| keyGen AFT | `keyGen AFT level=512\|768\|1024 d=HEX z=HEX` | `ek=HEX dk=HEX` |
| encaps AFT | `encapDecap AFT encapsulation level=... ek=HEX m=HEX` | `c=HEX k=HEX` |
| decaps VAL | `encapDecap VAL decapsulation level=... dk=HEX c=HEX` | `k=HEX` |

**API calls**: `QUDO_KEM_keypair_from_seed()`, `QUDO_KEM_encaps_derand()`, `QUDO_KEM_decaps()`

### qudo_acvp_mldsa — ML-DSA (FIPS 204)

| Mode | CLI | Output |
|------|-----|--------|
| keyGen | `keyGen level=44\|65\|87 seed=HEX` | `pk=HEX sk=HEX` |
| sigGen | `sigGen level=... message=HEX sk=HEX context=HEX rnd=HEX` | `signature=HEX` |
| sigGenDeterministic | `sigGenDeterministic level=... message=HEX sk=HEX context=HEX` | `signature=HEX` |
| sigGenInternal | `sigGenInternal level=... message=HEX sk=HEX externalMu=0\|1 rnd=HEX` | `signature=HEX` |
| sigGenInternalDeterministic | `sigGenInternalDeterministic level=... message=HEX sk=HEX externalMu=0\|1` | `signature=HEX` |
| sigGenPreHash | `sigGenPreHash level=... ph=HEX sk=HEX rnd=HEX hashAlg=STRING [context=HEX]` | `signature=HEX` |
| sigGenPreHashDeterministic | `sigGenPreHashDeterministic level=... ph=HEX sk=HEX hashAlg=STRING [context=HEX]` | `signature=HEX` |
| sigGenPreHashShake256 | `sigGenPreHashShake256 level=... message=HEX sk=HEX [context=HEX] rnd=HEX` | `signature=HEX` |
| sigGenPreHashShake256Deterministic | `sigGenPreHashShake256Deterministic level=... message=HEX sk=HEX [context=HEX]` | `signature=HEX` |
| sigVer | `sigVer level=... message=HEX pk=HEX signature=HEX [context=HEX]` | `testPassed=0\|1` |
| sigVerInternal | `sigVerInternal level=... message=HEX pk=HEX signature=HEX externalMu=0\|1` | `testPassed=0\|1` |
| sigVerPreHash | `sigVerPreHash level=... ph=HEX pk=HEX signature=HEX hashAlg=STRING [context=HEX]` | `testPassed=0\|1` |
| sigVerPreHashShake256 | `sigVerPreHashShake256 level=... message=HEX pk=HEX signature=HEX [context=HEX]` | `testPassed=0\|1` |

**API calls**: `QUDO_MLDSA_keypair_internal()`, `QUDO_MLDSA_sign_internal()`, `QUDO_MLDSA_verify_internal()`, `QUDO_MLDSA_sign_pre_hash_internal()`, `QUDO_MLDSA_verify_pre_hash_internal()`, `QUDO_MLDSA_shake256()`

### qudo_acvp_slhdsa — SLH-DSA (FIPS 205)

Uses flag-based CLI (`-flag value` pairs):

| Mode | CLI | Output |
|------|-----|--------|
| keyGen | `-parameterSet NAME -skSeed HEX -skPrf HEX -pkSeed HEX keyGen` | `pk=HEX sk=HEX` |
| sigGen | `-parameterSet NAME -message HEX -sk HEX [-context HEX] [-deterministic 1] [-additionalRandomness HEX] sigGen` | `signature=HEX` |
| sigVer | `-parameterSet NAME -message HEX -signature HEX -pk HEX [-context HEX] sigVer` | `testPassed=0\|1` |

**Parameter sets**: All 12 FIPS 205 variants — `SLH-DSA-SHA2-128s`, `SLH-DSA-SHA2-128f`, `SLH-DSA-SHA2-192s`, `SLH-DSA-SHA2-192f`, `SLH-DSA-SHA2-256s`, `SLH-DSA-SHA2-256f`, `SLH-DSA-SHAKE-128s`, `SLH-DSA-SHAKE-128f`, `SLH-DSA-SHAKE-192s`, `SLH-DSA-SHAKE-192f`, `SLH-DSA-SHAKE-256s`, `SLH-DSA-SHAKE-256f`

**API calls**: `QUDO_SLHDSA_keypair_internal()`, `QUDO_SLHDSA_sign_ex()`, `QUDO_SLHDSA_verify()`, `QUDO_SLHDSA_verify_with_ctx_str()`

## Python Clients

Each Python client handles:

1. **Download** — Fetches NIST ACVP test vectors from `usnistgov/ACVP-Server` GitHub repository. Cached locally in `acvp/.acvp-data/{version}/files/`.
2. **Parse** — Reads prompt JSON and expected results JSON. Handles both raw JSON and ACVTS array format (`[acvVersion, vectorSet]`).
3. **Execute** — Invokes the C binary per test case with hex-encoded arguments. Parses `key=value` output from stdout.
4. **Validate** — Compares binary output against expected results. Reports pass/fail/skip per test case.

### Options

| Flag | Description |
|------|-------------|
| `--binary PATH` | Path to ACVP binary (auto-detected from build dir) |
| `--version VER` | ACVP Server version (default: `v1.1.0.41`) |
| `-p FILE` | Local prompt JSON (skips download) |
| `-e FILE` | Local expected results JSON |
| `-o FILE` | Write results JSON to file |
| `--workers N` | Parallel workers for SLH-DSA sigGen (SLH-DSA only) |

### Environment Variables

| Variable | Description |
|----------|-------------|
| `LD_LIBRARY_PATH` | Must include path to `libqudo-pqc.so` |
| `EXEC_WRAPPER` | Optional command prefix for cross-compilation (e.g., `qemu-arm`) |

## Test Vector Sources

Vectors are downloaded from the [NIST ACVP Server](https://github.com/usnistgov/ACVP-Server) repository:

| Algorithm | ACVP Test Sets |
|-----------|---------------|
| ML-KEM | `ML-KEM-keyGen-FIPS203`, `ML-KEM-encapDecap-FIPS203` |
| ML-DSA | `ML-DSA-keyGen-FIPS204`, `ML-DSA-sigGen-FIPS204`, `ML-DSA-sigVer-FIPS204` |
| SLH-DSA | `SLH-DSA-keyGen-FIPS205`, `SLH-DSA-sigGen-FIPS205`, `SLH-DSA-sigVer-FIPS205` |

## Validation Status

Results against NIST ACVP Server `v1.1.0.41` vectors:

| Algorithm | Test | Result | Notes |
|-----------|------|--------|-------|
| ML-KEM | keyGen (3 levels x 25 tests) | **75/75 PASS** | |
| ML-KEM | encapDecap: encaps + decaps | **105/105 PASS** | |
| ML-KEM | encapDecap: key checks (30 ekCheck + 30 dkCheck) | **60/60 PASS** | Length validation + modulus check |
| ML-DSA | keyGen (3 levels x 25 tests) | **75/75 PASS** | |
| ML-DSA | sigGen (pure + internal + pre-hash, all 12 hash algs) | **360/360 PASS** | |
| ML-DSA | sigVer (pure + internal + pre-hash) | **180/180 PASS** | |
| SLH-DSA | keyGen (12 param sets x 10 tests) | **120/120 PASS** | |
| SLH-DSA | sigVer (pure + pre-hash + internal) | **504/504 PASS** | |
| SLH-DSA | sigGen (pure + pre-hash + internal) | **624/624 PASS** | |

**Total: 2103/2103 PASS (100%)**

### ML-DSA Pre-Hash Algorithm Support

All 12 FIPS 204 pre-hash algorithm constants are defined, with OID mappings and domain-separator construction implemented in mldsa-native. Pre-hash signing is exercised end-to-end through the raw-message `QUDO_MLDSA_sign_pre_hash` / `QUDO_MLDSA_verify_pre_hash` path (the ACVP runner hashes the raw message before calling the API), and all pre-hash vectors pass.

| Hash Algorithm | ACVP Name | Constant | Status |
|---------------|-----------|----------|--------|
| SHA-224 | `SHA2-224` | `QUDO_PREHASH_SHA2_224` | PASS |
| SHA-256 | `SHA2-256` | `QUDO_PREHASH_SHA2_256` | PASS |
| SHA-384 | `SHA2-384` | `QUDO_PREHASH_SHA2_384` | PASS |
| SHA-512 | `SHA2-512` | `QUDO_PREHASH_SHA2_512` | PASS |
| SHA-512/224 | `SHA2-512/224` | `QUDO_PREHASH_SHA2_512_224` | PASS |
| SHA-512/256 | `SHA2-512/256` | `QUDO_PREHASH_SHA2_512_256` | PASS |
| SHA3-224 | `SHA3-224` | `QUDO_PREHASH_SHA3_224` | PASS |
| SHA3-256 | `SHA3-256` | `QUDO_PREHASH_SHA3_256` | PASS |
| SHA3-384 | `SHA3-384` | `QUDO_PREHASH_SHA3_384` | PASS |
| SHA3-512 | `SHA3-512` | `QUDO_PREHASH_SHA3_512` | PASS |
| SHAKE-128 | `SHAKE-128` | `QUDO_PREHASH_SHAKE_128` | PASS |
| SHAKE-256 | `SHAKE-256` | `QUDO_PREHASH_SHAKE_256` | PASS |

## Known Gaps

All gaps resolved. No remaining ACVP test failures.

### Resolved Gaps

| # | Gap | Resolution |
|---|-----|-----------|
| A | ML-KEM key checks not exposed | Added `QUDO_KEM_check_pk()`/`QUDO_KEM_check_sk()` + dispatch — **60/60 PASS** |
| B | ML-KEM check_pk false positives | Added key length validation before modulus check — **all 15 fixed** |
| C | ML-DSA missing pre-hash constants | Added all 12 `QUDO_PREHASH_*` constants matching mldsa-native |
| D | ML-DSA pre-hash raw message | Added `QUDO_MLDSA_sign/verify_pre_hash()` using internal SHA-2/SHA-3/SHAKE — **615/615 PASS** |
| E | SLH-DSA pre-hash wrapper | Added `QUDO_SLHDSA_sign/verify_pre_hash()` wrapping native `hash_slh_sign/verify` |
| F | SLH-DSA internal interface | Added `QUDO_SLHDSA_sign/verify_internal()` for Algorithm 19/20 — **504/504 PASS** |

## File Layout

```
acvp/
├── CMakeLists.txt              — Build 3 ACVP binaries
├── acvp_common.{h,c}           — Shared hex / arg utilities
├── acvp_mlkem.c                — ML-KEM ACVP binary
├── acvp_mldsa.c                — ML-DSA ACVP binary (13 modes)
├── acvp_slhdsa.c               — SLH-DSA ACVP binary (flag-based CLI)
├── acvp_client_mlkem.py        — Python client for ML-KEM
├── acvp_client_mldsa.py        — Python client for ML-DSA
├── acvp_client_slhdsa.py       — Python client for SLH-DSA
├── run_acvp.sh                 — Run all ACVP tests
└── .acvp-data/                 — Cached NIST vectors (gitignored)
```

## CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_ACVP` | OFF | Build ACVP test runner binaries |

Enable via cmake: `-DBUILD_ACVP=ON` or via build script: `./build.sh --acvp`

## Cross-Compilation

For testing on non-native architectures (e.g., ARM on x86):

```bash
# Build with cross-compiler
cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-linux-gnu.cmake \
      -DBUILD_ACVP=ON -S . -B build-arm

# Run with QEMU wrapper
EXEC_WRAPPER="qemu-aarch64 -L /usr/aarch64-linux-gnu" \
  python3 acvp/acvp_client_mlkem.py --binary build-arm/acvp/qudo_acvp_mlkem
```
