# Post-Validation Maintenance Plan

CMVP Implementation Guidance — Module maintenance after FIPS 140-3 validation.

---

## 1. Re-Validation Scenarios

CMVP defines three submission types for changes to a validated module:

### 1.1 Type 1-SUB (Security-Relevant Changes)

**Requires full re-testing by CST lab.**

Triggers:
- Changes to any cryptographic algorithm implementation
- Changes to self-test logic (POST, PCT, CAST, integrity)
- Changes to state machine transitions
- Changes to key management or zeroization
- Changes to the DRBG or entropy handling
- Changes to the module boundary (adding/removing files)
- Compiler version change (different code generation)

Process:
1. Implement changes in a separate branch
2. Run full test suite including FIPS tests
3. Submit to CST lab for re-testing
4. Receive updated CMVP certificate

### 1.2 Type 2-SUB (Non-Security Changes)

**Limited re-testing by CST lab.**

Triggers:
- Documentation updates only
- Build system changes that do not affect output binary
- Test suite improvements
- Bug fixes in non-cryptographic code (e.g., error messages)
- Adding new platform support (same binary, different OS version)

Process:
1. Document changes and rationale
2. Verify no security-relevant code changed
3. Submit to CST lab with change description
4. Lab verifies limited scope, updates certificate

### 1.3 Type 3-SUB (New Operational Environment)

**Adding a new platform to the certificate.**

Triggers:
- Supporting a new OS version (e.g., Ubuntu 26.04)
- Supporting a new CPU architecture
- Supporting a new compiler version

Process:
1. Build and test on new platform
2. Verify all FIPS tests pass
3. Submit platform test results to CST lab
4. Lab adds platform to certificate

## 2. Validated Branch Management

### 2.1 Branch Strategy

| Branch | Purpose |
|--------|---------|
| `main` | Active development (may include non-validated changes) |
| `fips-validated-v1.0` | Exact source code matching CMVP certificate |
| `fips-maintenance-v1.x` | Security patches for validated version |

### 2.2 Rules for Validated Branch

- No changes without CST lab approval (Type 1-SUB or 2-SUB)
- Tagged releases match CMVP certificate version
- Build instructions frozen (exact compiler version, flags)
- Binary reproducibility verified before each release

## 3. Vulnerability Response

### 3.1 Monitoring

- Subscribe to NIST NVD alerts for ML-KEM, ML-DSA, SLH-DSA
- Monitor the upstream projects `mlkem-native`, `mldsa-native`, and `slhdsa-c` (vendored locally as `qudo-slhdsa/slhdsa-native/`) for security commits — see [UPSTREAM_MAINTENANCE.md](UPSTREAM_MAINTENANCE.md)
- Monitor OpenSSL security advisories *only if* the deployment uses the optional `qudoprovider.so` (which lives in the separate `qudo-provider` repository and is outside this module's boundary)
- Track CMVP Implementation Guidance updates on the [NIST CMVP IG page](https://csrc.nist.gov/projects/cryptographic-module-validation-program/implementation-guidance)

### 3.2 Response Process

| Severity | Response Time | Action |
|----------|--------------|--------|
| Critical (active exploit) | 24 hours | Emergency patch, Type 1-SUB |
| High (feasible attack) | 1 week | Patch development, Type 1-SUB |
| Medium (theoretical) | 30 days | Assessment, patch if needed |
| Low (informational) | Next release | Document and track |

### 3.3 Disclosure

1. Assess vulnerability impact on FIPS-validated module
2. Develop and test patch
3. Notify CST lab of security-relevant change
4. Coordinate disclosure with CMVP
5. Release patched module with updated integrity HMAC
6. Publish security advisory

## 4. Algorithm Transition Planning

### 4.1 Current Status

All three algorithm families (ML-KEM, ML-DSA, SLH-DSA) are the final NIST
PQC standards (published 2024). No algorithm transitions are anticipated in
the near term.

### 4.2 Future Considerations

| Scenario | Impact | Action |
|----------|--------|--------|
| NIST parameter update | Type 1-SUB | Update implementation, re-validate |
| New PQC standard added | Type 1-SUB | Add to module, expand boundary |
| Algorithm deprecation | Type 1-SUB | Remove from module, re-validate |
| Key size guidance change | Documentation update | Type 2-SUB |

## 5. Upstream Dependency Management

### 5.1 Tracked Upstream Projects

| Project | Purpose | Update Frequency |
|---------|---------|-----------------|
| mlkem-native | ML-KEM reference + optimized | Per NIST errata |
| mldsa-native | ML-DSA reference + optimized | Per NIST errata |
| slhdsa-native | SLH-DSA reference + optimized | Per NIST errata |

### 5.2 Upstream Update Process

1. Review upstream changelog for security-relevant changes
2. Apply changes to development branch
3. Run full FIPS test suite
4. If security-relevant: Type 1-SUB re-validation
5. If non-security: Type 2-SUB or defer to next validation

## 6. Annual Review

Conduct an annual review of:

1. CMVP certificate status (active, revoked, historical)
2. Algorithm security status (any NIST advisories)
3. Platform support (OS versions still supported)
4. Vulnerability history (any CVEs in reporting period)
5. Upstream dependency status (any breaking changes)
6. CST lab relationship (contract renewal, lab availability)
