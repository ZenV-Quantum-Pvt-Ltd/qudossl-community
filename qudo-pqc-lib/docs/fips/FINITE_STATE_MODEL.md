# Finite State Model

This is the standalone Finite State Model (FSM) for the `libqudo-pqc`
cryptographic module — the CMVP artifact describing the module's states,
transitions, and data-output inhibition. It consolidates and is consistent with
[FIPS_SECURITY_POLICY.md §4](FIPS_SECURITY_POLICY.md) and
[LIFE_CYCLE.md §1](LIFE_CYCLE.md). State values are from
`include/qudo_pqc.h:44-47`; transition logic is enforced in
`src/qudo_pqc_init.c`.

## States

| Value | State | Meaning |
|-------|-------|---------|
| 0 | `INIT` | Module loaded/uninitialised (or reset by `qudo_pqc_fini()`). No crypto. |
| 1 | `SELFTEST` | Running pre-operational self-tests (integrity + POST KATs) or an on-demand self-test. No crypto output. |
| 2 | `RUNNING` | All self-tests passed; approved cryptographic services available. |
| 3 | `ERROR` | A self-test, integrity, or (hard) conditional test failed. Latched; all crypto rejected. |

`qudo_pqc_get_state()` returns the current value.

## State transition diagram

```text
                 qudo_pqc_init()
        INIT ───────────────────────────►  SELFTEST
         ▲                                  │   │
         │                       pass ◄─────┘   └─────► fail
         │                        │                     │
         │                        ▼                     ▼
         │                     RUNNING ───────────►  ERROR  (latched)
         │                      │   ▲   hard fail /     │
         │   qudo_pqc_self_test()│  │   integrity        │
         │      (re-verify+POST) ▼  │ pass               │
         │                     SELFTEST                  │
         │                                               │
         └───────────────────────────────────────────────
                         qudo_pqc_fini()  (from any state → INIT)
```

## Transition table

| # | From | To | Trigger | Condition |
|---|------|----|---------|-----------|
| T1 | INIT | SELFTEST | `qudo_pqc_init()` | Configuration accepted |
| T2 | SELFTEST | RUNNING | self-tests complete | Integrity **and** all POST KATs (primitive + per-family) **and** DRBG seed succeed |
| T3 | SELFTEST | ERROR | self-tests complete | Any integrity / POST KAT / DRBG-seed failure |
| T4 | RUNNING | SELFTEST | `qudo_pqc_self_test()` | On-demand re-test (re-verifies integrity, re-runs POST) |
| T5 | SELFTEST | RUNNING | on-demand re-test passes | Integrity + POST pass |
| T6 | SELFTEST | ERROR | on-demand re-test fails | Integrity or POST failure |
| T7 | RUNNING | RUNNING | service request | Approved operation; module stays running |
| T8 | RUNNING | ERROR | conditional / hard failure | A hard failure, or a conditional (PCT/CAST) failure when `conditional_errors = 1` |
| T9 | any | INIT | `qudo_pqc_fini()` | Resets state to INIT (the only exit from ERROR) |

Notes:
- `ERROR` is **latched**: re-calling `qudo_pqc_init()` while in ERROR returns
  failure (0). The only recovery is `qudo_pqc_fini()` → `INIT`, then re-init, or
  process restart.
- With the default `conditional_errors = 0`, a conditional test failure (e.g. a
  keygen PCT) is reported and the affected key discarded, but the module remains
  `RUNNING` (it does not take transition T8).

## Data-output inhibition

No data output (no key, ciphertext, signature, or DRBG output) is produced
except in the `RUNNING` state:

- In `INIT` and `SELFTEST`, services are gated and produce no cryptographic
  output — guaranteeing **no output before the power-on self-tests pass**.
- In `ERROR`, every service is rejected at the FIPS gate; no output is produced.

This is enforced by the per-operation FIPS gate (state + DRBG-ready +
indicator checks) wrapping every cryptographic entry point.

## Related

- [FIPS_SECURITY_POLICY.md §4](FIPS_SECURITY_POLICY.md) — FSM within the Security Policy.
- [LIFE_CYCLE.md §1](LIFE_CYCLE.md) — life-cycle view of the same states.
- [Troubleshooting](../development/TROUBLESHOOTING.md) — diagnosing the ERROR state.
