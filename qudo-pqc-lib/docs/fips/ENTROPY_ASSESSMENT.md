# Entropy Source Assessment (SP 800-90B)

This document consolidates the entropy story for `libqudo-pqc` into a single
SP 800-90B-oriented deliverable: the noise source, the entropy claim and ESV
strategy, the SP 800-90A DRBG it feeds, the continuous health tests, and the
operational limits. It is consistent with — and the authoritative source for —
the entropy material summarised in
[FIPS_SECURITY_POLICY.md §7.6](FIPS_SECURITY_POLICY.md),
[OPERATING_ENVIRONMENT.md §4](OPERATING_ENVIRONMENT.md), and
[VENDOR_EVIDENCE.md §5.1](VENDOR_EVIDENCE.md). All values are taken from
`src/fips/qudo_fips_rand.c` and `src/fips/qudo_fips_ctrdrbg.{h,c}`.

## 1. Architecture

```text
  OS CSPRNG (noise source)          [outside the module — platform-assured]
        |  raw bytes
        v
  SP 800-90B continuous health tests  (RCT + APT, per draw)   src/fips/qudo_fips_rand.c
        |  health-checked seed material (48 bytes)
        v
  AES-256 CTR_DRBG (no derivation function)  SP 800-90A §10.2  src/fips/qudo_fips_ctrdrbg.c
        |  output
        v
  qudo_pqc_rand_bytes()  ──>  ML-KEM / ML-DSA / SLH-DSA keygen, encaps, hedged signing
```

The module does **not** implement its own physical noise source or entropy
conditioning. It treats the operating system's validated CSPRNG as the noise
source and consumes its output directly as DRBG seed material. The approved DRBG
is the in-tree AES-256 CTR_DRBG; no external DRBG is used inside the boundary.

## 2. Noise sources (per platform)

| Platform | Noise-source API (`raw_platform_entropy`, `qudo_fips_rand.c:73`) | Backing CSPRNG |
|----------|------------------------------------------------------------------|----------------|
| Windows  | `BCryptGenRandom(BCRYPT_USE_SYSTEM_PREFERRED_RNG)` | CNG RNG (CMVP-validated) |
| macOS    | `getentropy()` (`<sys/random.h>`) | Kernel CRNG (Apple Silicon mixes SEP-supplied entropy) |
| Linux (glibc ≥ 2.25) | `getentropy()` → `getrandom(flags=0)` | Kernel CRNG (ChaCha20-based) |
| Linux (fallback) | direct `getrandom(2)` syscall | Same kernel CRNG |
| Linux (last-resort) | `/dev/urandom` — **non-approved configuration** | Same kernel CRNG |

Approved-mode operation requires one of the primary sources; the `/dev/urandom`
last-resort path is only reached when neither `getentropy()` nor the `getrandom`
syscall is available and is **outside** the approved configuration.

## 3. Entropy claim & ESV strategy

**Claim: H = 8 bits of min-entropy per sample byte** (full-entropy output).

The claim is made via the **SP 800-90B §3.1.5.1.1 already-validated-source**
(vendor-affirmation) pathway: each listed OS CSPRNG is a validated or
platform-vendor-assured source that produces full-entropy output after its own
initialisation (SP 800-90B §3.1.2), and the module applies the §4.4 continuous
health tests (Section 5) to the consumed output.

> **Residual risk / recommendation.** Vendor affirmation of OS entropy is
> increasingly constrained under current CMVP guidance. The strongest posture
> for validation is an **ESV certificate** — either referencing the operating
> system's own ESV/entropy certificate, or an SP 800-90B entropy assessment of
> the deployment's noise source — rather than affirmation alone. This document
> states the affirmation basis; the ESV evidence package is finalised per the
> target operational environment(s).

## 4. DRBG construction (SP 800-90A)

| Property | Value | Reference |
|----------|-------|-----------|
| Mechanism | AES-256 CTR_DRBG, **no derivation function** | SP 800-90A §10.2.1 |
| Seed length | **48 bytes** (key 32 + V 16), supplied as a full block | `FIPS_RAND_SEEDLEN`, `qudo_fips_rand.c:39` |
| Reseed interval | **2²⁰** generate calls | `RESEED_INTERVAL`, `qudo_fips_rand.c:36,269` |
| Max request size | **65,536 bytes** per generate | `QUDO_CTRDRBG_MAX_REQUEST`, `qudo_fips_ctrdrbg.h:33` |
| Prediction resistance / additional input | Supported by the primitive; **not used** on the standard generate path | — |
| Public entry point | `qudo_pqc_rand_bytes()` → `qudo_fips_rand.c` | — |

On reaching the reseed interval the module draws fresh, health-checked seed
material and reseeds before producing further output (`qudo_fips_rand.c:269`).

## 5. SP 800-90B health tests

Continuous health tests run on **raw noise-source output before** it is used to
seed or reseed the DRBG (`qudo_fips_rand.c`). On any failure the module emits
`QUDO_ERR_DRBG_HEALTH_FAIL` (0x0502), aborts the seed/reseed attempt, and
produces **no** DRBG output from the failed draw.

| Test | Parameters (at H = 8) | Implementation |
|------|------------------------|----------------|
| **Startup test** | 1,024 samples exercised through RCT/APT before first output | `FIPS_ENTROPY_STARTUP_SAMPLES`, `startup_health_test()` (`qudo_fips_rand.c:53,206`) |
| **Repetition Count Test (RCT)** | cutoff C = 4 (general C = 1 + ⌈−log₂α / H⌉, α = 2⁻²⁰) | `rct_critical[]` (`qudo_fips_rand.c:47`) |
| **Adaptive Proportion Test (APT)** | window W = 512, cutoff C = 13 | `FIPS_APT_WINDOW`, `apt_critical[]` (`qudo_fips_rand.c:49-50`) |

The `rct_critical[]` / `apt_critical[]` tables are indexed by the assumed
per-byte entropy `H`, so the cutoffs adjust automatically if the entropy
assumption changes. RCT detects a stuck source; APT detects bias / low-entropy
windows.

## 6. Failure behaviour

- A **health-test failure during init** seeding leaves the module in `ERROR`
  (init returns 0) — no cryptographic operations are permitted.
- A **health-test failure at reseed** aborts the reseed; the failed draw yields
  no output and the event is audited (`QUDO_AUDIT_COMP_DRBG`).
- The startup test must pass once per process before any DRBG output is produced.

See [Troubleshooting](../development/TROUBLESHOOTING.md) for operator-facing
symptoms and [CAST Mapping](CAST_MAPPING.md) for the DRBG KAT (POST) coverage.

## 7. Per-operation DRBG consumption

| Operation | Bytes drawn |
|-----------|-------------|
| ML-KEM keygen | 64 (`d ‖ z`) |
| ML-KEM encaps | 32 (`m`) |
| ML-DSA keygen | 32 (`ξ`) |
| ML-DSA hedged sign | 32 (`rnd`) |
| SLH-DSA keygen | 3 × n (`SK.seed`, `SK.prf`, `PK.seed`; n = 16/24/32) |
| SLH-DSA hedged sign | n (`addrnd`) |

All randomness is obtained through `qudo_pqc_rand_bytes()`; deterministic
(derandomised) API variants consume caller-supplied seeds instead.

## 8. Operational limits (summary)

| Limit | Value |
|-------|-------|
| Min-entropy claim | 8 bits / byte (full entropy) |
| DRBG seed length | 48 bytes |
| Reseed interval | 2²⁰ generates |
| Max bytes per request | 65,536 |
| APT window | 512 |
| Startup test | 1,024 samples |
| Health-test significance (α) | 2⁻²⁰ |

## 9. Related documents

- [FIPS_SECURITY_POLICY.md §7.6](FIPS_SECURITY_POLICY.md) — entropy/DRBG summary in the Security Policy.
- [OPERATING_ENVIRONMENT.md §4](OPERATING_ENVIRONMENT.md) — platform entropy requirements.
- [VENDOR_EVIDENCE.md §5.1](VENDOR_EVIDENCE.md) — ESV vendor-affirmation evidence.
- [CAST_MAPPING.md](CAST_MAPPING.md) — DRBG known-answer (POST) coverage.
