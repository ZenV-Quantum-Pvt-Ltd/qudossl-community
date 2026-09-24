# ADR-0011 — ML-KEM's superseded math is not gated; the compiler already removes it

- **Status:** Accepted
- **Date:** 2026-07-23
- **Design ref:** closes Sprint 2 Story 2.5 (ML-KEM half) and Sprint 3 Story 3.6
- **Depends on:** ADR-0005 (crypto-layer delegation), ADR-0009 (math-only build)

## Context

Story 2.5 gated ML-DSA's upstream math behind `#ifndef QUDO_PQC_DELEGATE` and
left the ML-KEM half open; Story 3.6 was to finish the job. The stated
motivation was CMVP: shrink the FIPS boundary by removing algorithm math that
delegation has superseded, so the lab is not asked to reason about two ML-KEM
implementations inside one module.

Before writing the gate we measured whether it would actually remove anything.

## Decision

**`crypto/ml_kem/ml_kem.c` is left ungated.** No `#ifndef QUDO_PQC_DELEGATE`
blocks are added around the superseded FIPS 203 math. Story 3.6 is closed as
documentation rather than code.

This deliberately does **not** mirror what was done for ML-DSA. The reason is
structural, not stylistic, and is recorded below.

## Evidence

**1. The superseded math is already absent from every shipped artifact.**
`crypto/ml_kem/ml_kem.c` compiled with the build's exact flags, with and
without the macro:

| | `__TEXT,__text` | `__TEXT,__const` | object on disk |
|---|---|---|---|
| `-DQUDO_PQC_DELEGATE` (shipped) | 6 088 B | *(absent)* | 13 192 B |
| without the macro | 42 280 B | 768 B | 53 472 B |

The recompiled object is byte-identical to the shipped one
(`md5 = fb7e067c12f234b90ac9d64b5166e2ac`, matching
`crypto/ml_kem/libfips-lib-ml_kem.o`), so this is a measurement of the real
build, not of an approximation.

**2. The NTT root tables are nowhere in the build.** Scanning for the
little-endian byte prefix of `kNTTRoots` (`ml_kem.c:254-258`) across every
`*.o` in the tree returned no matches, and across `providers/fips.dylib`,
`libcrypto.a` and `libcrypto.3.dylib` returned no match in any of the three.

**3. Gating would save exactly zero bytes.** A fully gated copy of the file
(all 35 dead functions and all 7 dead file-scope constants wrapped) compiles to
the same section sizes as the ungated file: `__text` 6 088, `__DATA,__const`
312, unwind 608, both 13 296 B on disk. The only delta is one byte of
`__cstring` — the length of the `__FILE__` string baked in by `ERR_raise_data`.

**4. Why it disappears without `--gc-sections`.** The build uses neither
`-ffunction-sections` nor `--gc-sections`/`-dead_strip`. It does not need them:
every superseded function is `static` within a single translation unit, so at
`-O3` the compiler front end proves them unreferenced and never emits code.
The linker is not involved. This is precisely why ML-KEM differs from ML-DSA —
ML-DSA's math is spread across eight files with external linkage between them,
so the compiler cannot see that it is dead and gating there does real work.

**5. Partial gating is strictly worse.** Gating only `genkey`/`encap`/`decap`
raises the `-Wunused-function` count from 3 to 6, because their callees then
become visibly unreferenced in turn. The change is all-or-nothing.

## Consequences

**The FIPS boundary is unaffected.** There is no second ML-KEM implementation
in the module to disclose. The lab sees one.

**The residual benefit is 3 compiler warnings.** With the macro defined, clang
reports `genkey` (`ml_kem.c:1684`), `encap` (`:1761`) and `decap` (`:1796`) as
unused. `-Wunused-function` is non-transitive, so the other 32 dead functions
never warn. Silencing 3 warnings does not justify ~420 lines of preprocessor
divergence in a file that must absorb upstream CVE cherry-picks
(`-Xsubtree=openssl`) for the life of the product.

**Upstream diff stays minimal**, which is the standing engineering principle in
`CLAUDE.md` and what keeps the `upstream-parity` gate meaningful.

**What we give up:** source-level clarity. A reader of `ml_kem.c` sees ~1 400
lines of FIPS 203 math that never runs. This is mitigated by a comment at the
delegation site rather than by preprocessor surgery.

## Alternatives considered

**Gate the full transitive closure (35 functions + 7 constants).** Verified to
compile cleanly both with and without the macro. Rejected: it buys 0 bytes and
permanently complicates CVE merges.

**Delete the superseded math outright.** Rejected: it makes the file
un-mergeable against upstream and forecloses reverting to OpenSSL's
implementation, which ADR-0010 shows we may need per algorithm.

## Revisit if

- The build ever adopts `-ffunction-sections` plus `--gc-sections`, or moves to
  a compiler that emits unreferenced statics.
- ML-KEM's math is split across translation units upstream, making it externally
  linked and therefore no longer removable by the compiler alone.
- A lab reviewer asks for source-level absence rather than binary-level absence.
  The measurements above answer the binary question; they do not answer a
  source-inspection question, and the gated variant is known to compile if that
  is ever required.
