# QudoSSL — component inventory (SBOM preview)

Hand-authored inventory of the vendored components, so the bill of materials is
visible without waiting for a CI run. The authoritative machine-readable SBOM
(CycloneDX + SPDX) is produced by the `security-scan` workflow (Trivy + Syft)
and attached to each run as the `sca-reports` / `sca-grype-reports` artifacts.

## Top-level components

| Component | Version / pin | Source | License |
|---|---|---|---|
| **OpenSSL** | 3.5.7 (rel. 9 Jun 2026), tag `openssl-3.5.7` @ `8cf17aae` | github.com/openssl/openssl | Apache-2.0 |
| **qudo-pqc-lib** | `main` @ `73e5499b` | ZenVInnovations/qudo-pqc-lib | Apache-2.0 AND MIT |

## qudo-pqc-lib vendored upstreams (the "native" libraries)

| Component | Role | Formal proofs | License |
|---|---|---|---|
| **mlkem-native** | ML-KEM (FIPS 203) math, own SHAKE/Keccak (`mlk_`) | CBMC + HOL-Light + Isabelle | Apache-2.0 AND MIT |
| **mldsa-native** | ML-DSA (FIPS 204) math, own SHAKE/Keccak (`mld_`) | CBMC + HOL-Light (partial) | Apache-2.0 AND MIT |
| **slhdsa-native** | SLH-DSA (FIPS 205) math + SHA-2/Keccak (`qudo_SHA3`) | KAT/ACVP/valgrind-CT | Apache-2.0 AND MIT |

## Notes for the SBOM / license review

- **All components are Apache-2.0 (OpenSSL) or Apache-2.0 AND MIT (qudo side).**
  Both are permissive; obligations are attribution + license text retention. No
  copyleft. A `NOTICE`/attribution file should aggregate these for distribution.
- **qudo-pqc-lib is pinned to a branch head (`73e5499b`), not a release tag.**
  A branch head is not a stable cert artifact — cut a release tag and re-pin
  before the cert freeze (tracked in `docs/qudo-pqc-lib-release-tag-proposal.md`).
- **Under QudoSSL only ML-KEM and ML-DSA are delegated to qudo;** SLH-DSA is
  served by OpenSSL (ADR-0010). slhdsa-native is present in the source tree but
  not linked into `fips.so`.
- The CI SBOM (Syft/Trivy) will additionally enumerate build-time tooling and
  any transitive OS packages in the runner image; this table is the crypto
  bill of materials that matters for the cert.

## The authoritative machine-readable SBOM — and why it is hand-authored

`sbom/qudossl.cdx.json` (CycloneDX, checked in) is the **source of truth** and the
input to the CI CVE gate. It is hand-authored on purpose:

- Trivy/Grype filesystem SCA is **manifest-based** — it finds components by reading
  package manifests (`go.mod`, `package-lock.json`, OS package DBs). The shipped
  crypto (OpenSSL, qudo-pqc-lib, the natives) is **vendored C source with no
  manifest**, so a filesystem scan does **not** see it. A tree scan instead
  catalogues the CI tooling and flags OpenSSL's test `*.pem` fixtures as secrets —
  green but hollow.
- The pinned SBOM carries the exact versions **and CPEs** (`cpe:2.3:a:openssl:openssl:3.5.7`),
  so the scanners match real CVEs against the shipped versions. This is what would
  actually catch a vulnerable OpenSSL.

The `security-scan` workflow scans `sbom/qudossl.cdx.json` with **Grype**, which
CPE-matches components against its DB (Trivy's `trivy sbom` mode only matches
ecosystem purls, not raw CPEs, so it cannot see OpenSSL and is used only for
secrets + manifest deps). The Grype job gates on **CRITICAL**.

**Self-test / anti-false-pass:** a clean result is only trustworthy if the CPE
matcher actually works, so each run first scans `sbom/_selftest_vuln.cdx.json` — a
deliberately old, known-vulnerable OpenSSL pin (3.0.0). If that canary finds **0**
CVEs the matcher is broken and the pipeline **fails** rather than reporting a false
all-clear.

Keep `sbom/qudossl.cdx.json` in lockstep with `openssl/VERSION.dat` and the
qudo-pqc-lib subtree SHA — a stale version here silently under-reports CVEs. nginx
and other Sprint-9 integration consumers are **not** product components and are
intentionally absent.

## Where the machine-readable SBOMs land in CI

`Actions` tab → a `security-scan` run → **Artifacts**:
- `sca-reports/` → `qudossl.cdx.json` (the pinned SBOM) + `trivy-results.sarif` (CVE findings)
- `sca-grype-reports/` → `qudossl-inventory.spdx.json` (Syft full-tree inventory) + Grype SARIF
