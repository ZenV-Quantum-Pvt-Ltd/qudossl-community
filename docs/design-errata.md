# Design errata

Consolidated delta between `design/QudoSSL_Design.docx` (v1.0, baseline frozen
14 Jul 2026) and the decisions actually in force.

**Why an errata rather than an edited document.** The design's own convention is
that *"a DECISION callout is a locked-in choice; changing it requires a written
ADR and design review."* ADRs are the change mechanism. The .docx is a frozen
baseline that goes to the lab, so it is not rewritten in place — this file is the
index of what the ADRs override.

**Precedence:** post-freeze ADR → v1.0 Decision Log (D1–D5) → document body.
`CLAUDE.md` carries the short form of this rule.

---

## 1. Superseded sections, by design reference

| Design § | Says | In force | Source |
|---|---|---|---|
| §1.1, §12.3, §22.1, §23.2 | Build qudo-pqc-lib with `QUDO_FIPS_MODULE=ON` + 5 boundary-dedupe flags | Standard build, no flags — **but see §3, this is blocked** | ADR-0009 |
| §1.1, §4.2, §4.3, §9, §12.3, §14.3, §19.2, §22.3 | PQC subtree at `external/qudo-pqc/` | `qudo-pqc-lib/`, parallel to `openssl/` | ADR-0008 |
| §1.2, §1.5, §5, §7.1, §15.1 | FIPS module renamed to `qudo-fips.so` | **No rename** — ships as `fips.so` | ADR-0006 |
| §3 Phase 0, Story 1.5 | Drop oqs-provider / pkcs11-provider at Configure time | **Retained.** Both are upstream submodule gitlinks, empty without `--recursive`, never compiled | ADR-0007 |
| §4.2 | `git subtree add … openssl-3.5` (branch) | Pinned tag `openssl-3.5.7` | ADR-0004 / D1 |
| §4.2, Story 1.8 | Sync on `main`; create long-lived `vendor/openssl-3.x` | On-demand `sync/<subtree>-<version>` branches cut from `dev`. The design contradicts itself here | `docs/branching.md` |
| §6.1 | qudo's CTR-DRBG, AES, HMAC, integrity "removed from boundary" by the dedupe flags | **Not achieved by flags.** Removal must come from link-time non-reference — currently failing, see §3 | ADR-0009 + audit A2 |
| §9, §10, §12, §20, §22.2–22.3, §23.3, App. A | Provider-level adapter layer in `providers/implementations/*` | **Crypto-layer delegation** in `crypto/ml_kem`, `crypto/ml_dsa`, `crypto/slh_dsa`. Reference-only — App. A's `ml_kem_kem.c` example is NOT the design to implement | ADR-0005 / D2 |
| §9 | Delete `crypto/ml_*` directories | Keep the entry-point shims; compile out only the math internals | ADR-0005 / D2 |
| §12.3, §22.1 | macOS reproducibility via `-Wl,-no_uuid` | **Breaks the build** — strips `LC_UUID`, macOS linker then refuses to link against `libcrypto.dylib`. Use `ZERO_AR_DATE=1` + content-derived `LC_UUID` | `docs/reproducibility.md` |
| §22.3 | The dedupe flags compile out `qudo_fips_aes/hmac/ctrdrbg/embedded_hmac/integrity` so "the .o files don't exist in the archive" | **False.** They exist in *both* build modes with real code — `qudo_pqc_post.o` alone is 31 KB / 7 exported symbols | ADR-0009 |
| §23.1 E1.2 | Asserts `providers/qudo-fips.so` | `providers/fips.so`, permanently | ADR-0006 |
| Part V Epic 2 | 7 stories, 8–12 days of `USE_HOST_*` flag work | Stories 2.1–2.5 dropped; **2.6 and 2.7 survive**. Epic 2 now re-opens with different scope — see §3 | ADR-0003, ADR-0009 |
| §15.2, D2 | SLH-DSA (FIPS 205) sourced from qudo-pqc-lib | **Not delegated.** OpenSSL's own SLH-DSA is used: qudo's is 1.6x slower and duplicates SHA-2/Keccak inside the boundary | ADR-0010 |
| Part V Epic 3 | Adapter layer, 21–32 days | Crypto-layer delegation, 15–24 days | D5 / ADR-0005 |
| Part V Epics 4–5 | Bridge qudo self-test events, extend `ST_ID_*` per algorithm, wire deferred CASTs, migrate KATs | **No subject.** OpenSSL's POST/CAST/PCT/integrity exercise qudo's math through the delegated entry points. Stories 4.2–4.4 and 5.1–5.3 removed; 5.4 survives | D2 |
| Part V Story 6.4, 6.5 | Rename build output; update `fipsinstall` default path | Deleted / no-op | ADR-0006 |
| §17.1 roll-up | 113–186 days | ~89–151 days | `docs/effort-rollup.md` |
| §18.2 open questions | OE list unconfirmed; recommends Linux-only first cert | **All four confirmed**: Linux x86_64, Linux aarch64, macOS arm64, Windows x64 | 2026-07-22 |
| Critical path (E3 → E5/E4 → E6) | Adapter work before build linkage | **Ordering defect.** `--with-qudo-pqc-archive` is Story 6.1; delegation is untestable until the archive links. Pulled forward as Sprint 2 Story 2.0 | Sprint 2 plan |

---

## 2. Reviewer comments resolved

Three comment threads in the .docx were left open. Their resolutions:

- *"i think pqc also git subtree is better"* → adopted as **D4**, refined by ADR-0008.
- *"only one state machine will be there that too openssl"* → **correct.** §10.3's RULE requiring adapters to satisfy both `ossl_prov_is_running()` and `qudo_pqc_is_running_or_selftest()` is dead under delegation; OpenSSL's state machine is the single authority.
- *"no need of this many macros"* (on the `ST_ID_*` expansion) → **correct.** Story 4.3's per-algorithm `ST_ID_*` expansion is removed with the rest of the self-test bridging.

---

## 3. Open against the current decisions

ADR-0009's build-mode decision stands, but two of its claims were disproved by
execution during the PR #2 audit. This is the largest open item in the plan.

- **ML-KEM cannot be constructed** in the configuration ADR-0009 mandates:
  `QUDO_KEM_init()` returns `-5` and `QUDO_KEM_new()` returns `NULL`, because the
  RNG self-test needs a DRBG seeded only by `qudo_pqc_init()` — the call the ADR
  forbids. Every seeded ML-KEM entry point requires a handle, so there is no
  handle-free path.
- **The FIPS scaffolding links in anyway.** Referencing only the prescribed
  seeded entry points still pulls `qudo_ctrdrbg`, `qudo_aes`, `qudo_fips_hmac`,
  `qudo_pqc_post`, `qudo_fips_rand`, `qudo_pqc_init` and `qudo_audit`.
- **Root cause:** the entanglement is gated on `QUDO_COMBINED_BUILD`, set
  unconditionally at `CMakeLists.txt:174` — not on `QUDO_FIPS_MODULE`.

**Resolution:** `QUDO_PQC_MATH_ONLY`, an additive build mode in
ZenVInnovations/qudo-pqc-lib#9. Its FIPS services are not compiled at all, and
`QUDO_COMBINED_BUILD` is left undefined, so the chain is severed at the source.
Verified: both defects close; the standalone FIPS build's exported symbol table
is byte-identical to pristine, so no certification path is affected. QudoSSL
builds with `-DBUILD_SHARED_LIBS=OFF -DQUDO_PQC_MATH_ONLY=ON`. Epic 3's ML-KEM
delegation unblocks once the subtree pin is bumped. Full detail in ADR-0009's
Correction section and `code-review/QUDOSSL_PR2_AUDIT.md`.

**This is the first configuration that actually achieves §6.1.** That table
lists exactly which qudo files must be "removed from boundary" —
`qudo_fips_ctrdrbg.c`, `qudo_fips_rand.c`, `qudo_fips_embedded_hmac.c`,
`qudo_fips_integrity.c`, `qudo_fips_aes*.c`, `qudo_fips_hmac.c`. Math-only
removes precisely those. The design's prescribed mechanism (the `USE_HOST_*`
flags) does not, per E6 above. Design intent unchanged; mechanism corrected.

---

## 4. Errata that do not change decisions

Corrections to detail, recorded so they are not rediscovered:

- **Entry-point names in ADR-0009 were wrong.** `QUDO_KEM_enc_derand` does not
  exist (it is `QUDO_KEM_encaps_derand`); `mldsa44|65|87_keypair_internal` does
  not exist (they are `mldsa_ref44_keypair_internal` and the `_neon` / `_avx2`
  dispatch variants). Verified with `nm -g`. Corrected in ADR-0009 and the
  Sprint 2 delegation map.
- **The delegation surface is 12 functions, not 81.** The headers declare 24
  `ossl_ml_kem_*`, 30 `ossl_ml_dsa_*` and 27 `ossl_slh_dsa_*`, but most are key
  lifecycle, accessors and codecs which D2 keeps as OpenSSL's.
- **§12.2's claim that algorithm names need no translation** should be verified
  rather than assumed, particularly for SLH-DSA's 12 parameter sets.
- **§12.1's indicator adapter has no subject.** It specifies "~30 LoC mirroring
  `qudo_fips_ind_t` into OpenSSL's `OSSL_ALG_PARAM_FIPS_APPROVED_INDICATOR`".
  Under math-only there is no `qudo_fips_ind_t` — OpenSSL's `fipsindicator.c`
  is the sole indicator. The adapter is removed, not reimplemented.
- **Constant-time validation no longer reaches the PQC math.** OpenSSL's
  `CONSTTIME_SECRET` / `CONSTTIME_DECLASSIFY` markers still bracket the
  delegated calls in `crypto/ml_kem/ml_kem.c`, but they now enclose a call into
  a separately-compiled archive, so OpenSSL's valgrind-based CT validation
  cannot see the lattice math. This is not a defect -- qudo-pqc carries its own
  constant-time claim -- but it means design Story 7.10's side-channel evidence
  must be produced by running dudect/ctgrind against `libqudo-pqc.a` directly,
  not by relying on OpenSSL's harness. Record this before lab engagement.
- **`BUILD_SHARED_LIBS=OFF` is mandatory**, per §12.3's RULE that the integrity
  HMAC cover the algorithm code in one `.text` region. It is also the
  configuration that exposed the MSVC `C2491` defect, since upstream only ever
  exercised the shared Windows build.
