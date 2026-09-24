# Contributing

Engineering contribution guide for the QUDO PQC library (`libqudo-pqc`). This
repository is **not currently public**; the conventions below govern internal
development and also keep the project ready for an eventual public release.

Because the module is built for **FIPS 140-3 certification**, changes inside the
cryptographic boundary carry obligations beyond ordinary code review — see
[FIPS discipline](#fips-discipline) below.

## Development workflow

1. Install the git pre-commit hook once: `./scripts/install-hooks.sh`
2. Create a feature branch (`git checkout -b feature/my-change`)
3. Make your changes
4. Run the suite (`ctest --output-on-failure`, and the FIPS build — see below)
5. Open a merge/pull request for review

The pre-commit hook runs `clang-format --dry-run --Werror` on staged C/C++ files.
CI (`lint.yml`) is the authoritative gate; the hook is a local convenience.

For the full developer workflow — build/test loop, FIPS-module build constraints,
debugging, and updating the vendored `*-native` upstreams — see the
[Development Guide](docs/development/DEVELOPMENT.md).

## Requirements

- All code must be C99 compliant
- Cross-platform: Linux, macOS, Windows (MSVC + MinGW)
- No VLAs (Variable Length Arrays) — MSVC does not support them
- All cryptographic functions must have self-tests
- All sensitive data must be zeroized after use (`qudo_secure_clear` / `qudo_cleanse`)
- No POSIX-only or MSVC-only functions without platform guards

## Code Style

- Public API: `QUDO_KEM_*` / `QUDO_MLDSA_*` / `QUDO_SLHDSA_*` (uppercase)
- Internal functions: `qudo_*` (lowercase)
- Constants: `QUDO_KEM_SUCCESS`, `QUDO_ERR_*`
- New internal symbols stay hidden (not added to `config/libqudo-pqc.map`)
- Comments reference FIPS/NIST standards only (FIPS 203, FIPS 204, FIPS 205)
- Every source file carries an SPDX license header

## Testing

Every change must:

- Build on all platforms (check CI results)
- Pass the full CTest suite in both standard and FIPS builds
  (`-DQUDO_FIPS_MODULE=ON`)
- Include tests for new functionality
- For algorithm changes, pass the NIST ACVP vectors (`bash acvp/run_acvp.sh`)

See the [Testing Guide](docs/development/TESTING.md).

## FIPS discipline

The module is structured for CMVP validation; treat the cryptographic boundary as
controlled:

- **Do not** enable LTO or `-ffunction-sections`/`-fdata-sections` for FIPS
  builds — they break the contiguous-`.text` integrity region (the build enforces
  this; see [FIPS Build Guide](docs/fips/FIPS_BUILD_GUIDE.md)).
- Any change that alters the module binary requires **re-finalising the integrity
  HMAC** (`qudo_fipsinstall -embed`, then re-codesign on macOS).
- Changes must keep **POST, CAST, and PCT** passing; never weaken or bypass a
  self-test to make a build green.
- Adding/removing an approved algorithm or parameter set is a **re-validation
  event** — coordinate with the [Maintenance Plan](docs/maintenance/MAINTENANCE.md)
  before starting (see also the [Development Guide](docs/development/DEVELOPMENT.md)).
- Vendored `*-native` upstreams are not edited in-tree; updates follow
  [Upstream Maintenance](docs/maintenance/UPSTREAM_MAINTENANCE.md).

## Security

Report security vulnerabilities through the channel in [SECURITY.md](SECURITY.md)
— do not file them in the normal issue tracker.

## License

By contributing, you agree that your contributions are licensed under the same
terms as the project (Apache-2.0 AND MIT), and that any third-party code is
recorded in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
