# Upstream OpenSSL 3.5.7 defects

## Summary

Sprint 3 found four defects in upstream-authored code that QudoSSL ships from its
OpenSSL 3.5.7 baseline. Three are zeroization gaps in `providers/implementations/` — the ML-KEM
text encoder leaves a full decapsulation key and the `d||z` seed uncleansed, the
ML-KEM and ML-DSA PKCS#8 encoders free a populated private-key buffer with a bare
`free()` on their error paths, and the ML-KEM `load` keymgmt entry does the same
with the unparsed PKCS#8 private key. The fourth is a constant-time test-harness
defect in `crypto/ml_kem/ml_kem.c`, where the secret-poisoning extent is
hard-coded to rank 2 and so under-covers ML-KEM-768 and ML-KEM-1024. The first
three cannot be fixed locally because [ADR-0005](adr/ADR-0005-crypto-layer-delegation.md)
keeps `providers/implementations/` byte-identical to upstream, and we have
measured that they are — every file named here is bit-for-bit the 3.5.7 subtree
import. The fourth *is* inside our seam and is therefore fixable, but it affects
only `OPENSSL_CONSTANT_TIME_VALIDATION` builds, which we do not currently produce;
it is recorded here as deferred with an explicit trigger. This document is the
tracking record and the basis for the upstream report.

## Scope

- **Baseline:** OpenSSL 3.5.7, imported as a `git subtree` at `openssl/`
  ([ADR-0004](adr/ADR-0004-openssl-baseline-3.5.7.md)). Import commit `5943b28`
  ("Merge commit `4ca9ad0b47863bb7f27e2e4f86dd49ce3a5e0630` as 'openssl'"), which
  squashes upstream `8cf17aa`. `openssl/include/openssl/opensslv.h:90` reads
  `OPENSSL_VERSION_STR "3.5.7"`.
- **Build measured:** the delegated FIPS build in the working tree
  (`openssl/configdata.pm:178` lists `QUDO_PQC_DELEGATE`), macOS arm64,
  clang, `-O3`.
- **In scope:** defects in upstream-authored code that QudoSSL ships. Line
  numbers are as they appear in *our* tree at commit `033cab7`; where our
  delegation edits have shifted a line relative to pristine upstream, both
  numbers are given. `openssl/crypto/ml_kem/ml_kem.c` line numbers in
  particular have moved twice this sprint — re-check them against the tree
  rather than trusting a copy of this document.
- **Out of scope:** defects in QudoSSL-authored delegation code (`qudo_*`
  functions in `openssl/crypto/ml_kem`, `openssl/crypto/ml_dsa`), and
  upstream-origin defects inside `openssl/crypto/ml_dsa/` that we *can* fix
  locally — two of which we already have. All are noted in "Related, not tracked
  here" below so the upstream report is complete, but their disposition is
  decided elsewhere; the full CSP analysis is in
  [zeroization-analysis.md](zeroization-analysis.md) and the constant-time
  analysis in [side-channel-analysis.md](side-channel-analysis.md).
- **SLH-DSA:** not delegated ([ADR-0010](adr/ADR-0010-slh-dsa-stays-upstream.md)).
  `openssl/crypto/slh_dsa/` was **not audited** in this pass.

### Evidence discipline

Every claim below is tagged:

- **Measured** — reproduced in this working tree by a command that is quoted.
- **Inferred** — a conclusion drawn from measured facts, with the reasoning shown.
- **Not measured** — stated as unknown. Nothing in this document is asserted as
  tested when it was not.

### Why these cannot be fixed locally

[ADR-0005](adr/ADR-0005-crypto-layer-delegation.md) lines 28-29 state the
decision verbatim:

> Leave `providers/implementations/{keymgmt,signature,kem,encode_decode}`
> **untouched**.

**Measured** — the constraint holds today:

```
$ git log --oneline -1 -- openssl/providers/implementations/
5943b28 Merge commit '4ca9ad0b47863bb7f27e2e4f86dd49ce3a5e0630' as 'openssl'

$ grep -rl "QUDO" openssl/providers/implementations/
(no output)

$ git show 4ca9ad0:providers/implementations/encode_decode/ml_kem_codecs.c \
    | diff - openssl/providers/implementations/encode_decode/ml_kem_codecs.c
(no output — identical)
```

The same `diff` returns identical for `ml_dsa_codecs.c`, `encode_key2any.c` and
`keymgmt/ml_kem_kmgmt.c`. The entire subtree's last-touching commit is the import
itself. Patching any of these files converts a zero-delta directory into a
maintained fork, which is the cost ADR-0005 was written to avoid.

---

## Defect register

| ID | Location | Class | In FIPS module? | Fixed on upstream master? | Disposition |
|---|---|---|---|---|---|
| UD-1 | `providers/implementations/encode_decode/ml_kem_codecs.c:558-619` | Zeroization | No | No | Report upstream |
| UD-2 | `.../encode_decode/ml_kem_codecs.c:551-555`, `.../ml_dsa_codecs.c:402-406` | Zeroization | No | No | Report upstream |
| UD-3 | `providers/implementations/keymgmt/ml_kem_kmgmt.c:509-553` | Zeroization | No | **Yes** | Request 3.5 backport |
| UD-4 | `crypto/ml_kem/ml_kem.c:2518` | CT test coverage | n/a (test build only) | **Yes** | Deferred locally + request 3.5 backport |

**Correction to the Sprint 3 investigation notes.** The investigation's rollup
summary states that the ML-KEM codec defect is "present in the FIPS-module
objects". That is **not** what we measure, and the distinction matters to a lab.
`providers/implementations/encode_decode/build.info:4-5` sets
`$ENCODER_GOAL` and `$DECODER_GOAL` to `../../libdefault.a`, so the codecs are
compiled only into the default provider:

```
$ ls openssl/providers/implementations/encode_decode/ | grep ml_kem_codecs
libdefault-lib-ml_kem_codecs.d
libdefault-lib-ml_kem_codecs.o
ml_kem_codecs.c
ml_kem_codecs.h
        # no libfips-lib-ml_kem_codecs.o exists

$ nm -a openssl/providers/fips.dylib | grep -c key_to_text
0

$ nm -a openssl/providers/libdefault.a | grep ossl_ml_kem_key_to_text
                 U _ossl_ml_kem_key_to_text
0000000000000a88 T _ossl_ml_kem_key_to_text
0000000000000f80 s l___func__.ossl_ml_kem_key_to_text
```

`grep -n "encode_decode\|codecs" openssl/providers/fips.module.sources` returns
nothing. UD-1, UD-2 and UD-3 therefore affect `libcrypto` and the **default**
provider, not the certified module boundary. They remain real product defects
and remain CSP-handling gaps that a lab will ask about in the Security Policy,
but they must not be described as defects *inside* the FIPS module.

---

## UD-1 — `ossl_ml_kem_key_to_text` leaves the decapsulation key and the `d||z` seed uncleansed

**Location:** `openssl/providers/implementations/encode_decode/ml_kem_codecs.c:558-619`

### What is wrong

The function copies two CSPs into buffers it owns and then abandons both without
cleansing.

- `ml_kem_codecs.c:560` — `uint8_t seed[ML_KEM_SEED_BYTES], *prvenc = NULL, *pubenc = NULL;`
  `ML_KEM_SEED_BYTES` is 64 (`include/crypto/ml_kem.h:48`, `ML_KEM_RANDOM_BYTES * 2`).
  This stack buffer receives the raw keygen seed `d||z`.
- `ml_kem_codecs.c:580` — `ossl_ml_kem_encode_seed(seed, sizeof(seed), key)` fills it.
- `ml_kem_codecs.c:586` — `prvenc = OPENSSL_malloc(prvlen)`, where `prvlen` is
  `key->vinfo->prvkey_bytes` (`:571`): 1632, 2400 or 3168 bytes for ML-KEM-512,
  -768, -1024 respectively (`PRVKEY_BYTES` at `crypto/ml_kem/ml_kem.c:134`).
- `ml_kem_codecs.c:588` — `ossl_ml_kem_encode_private_key(prvenc, prvlen, key)`
  fills it with the full FIPS 203 decapsulation key `dk`.
- `ml_kem_codecs.c:615-618` — the sole cleanup block is

  ```c
  end:
      OPENSSL_free(pubenc);
      OPENSSL_free(prvenc);
      return ret;
  ```

  `OPENSSL_free` reaches `CRYPTO_free` at `crypto/mem.c:347-356`, which is a bare
  `free(str)` with no cleansing. `seed` is never touched at all.

**The early `return 0` paths.** Three `return 0` statements bypass the `end:`
label entirely — `:567` (NULL arguments), `:577` (the `BIO_printf` header write
fails) and `:587` (`OPENSSL_malloc(prvlen)` returns NULL). Their significance is
narrower than it first appears, and the accurate reading is:

- **Today they change nothing.** `end:` does not cleanse anything, so bypassing
  it forfeits nothing. On the `:587` path `prvenc` is NULL (the malloc failed) and
  `pubenc` is NULL, so there is no leak and no missed free either.
- **They matter to the fix.** `:587` is reachable *after* `seed` has already been
  populated at `:580`, so a fix that only adds `OPENSSL_cleanse(seed, sizeof(seed))`
  at `end:` would still miss that path. The `:587` `return 0` must become
  `goto end` before the cleanse is added. `:577` is also reachable before any CSP
  is written, so routing it through `end:` is harmless.

**Measured**, at object level:

```
$ objdump -d -r --disassemble-symbols=_ossl_ml_kem_key_to_text \
    openssl/providers/implementations/encode_decode/libdefault-lib-ml_kem_codecs.o \
  | grep -oE "_(CRYPTO_free|CRYPTO_clear_free|OPENSSL_cleanse|CRYPTO_malloc)" | sort | uniq -c
   2 _CRYPTO_free
   2 _CRYPTO_malloc
```

Two allocations, two bare frees, zero `_CRYPTO_clear_free`, zero
`_OPENSSL_cleanse`. The compiled object confirms the source reading.

### How it is reached

**Measured** — an ordinary `openssl pkey -text` invocation. `openssl pkey` →
`encode_key2text.c:443-446` (`ml_kem_to_text` → `ossl_ml_kem_key_to_text`):

```
$ DYLD_LIBRARY_PATH=openssl OPENSSL_CONF=/dev/null \
    openssl/apps/openssl genpkey -algorithm ML-KEM-768 -out mlkem768.pem
$ DYLD_LIBRARY_PATH=openssl OPENSSL_CONF=/dev/null \
    openssl/apps/openssl pkey -in mlkem768.pem -noout -text
ML-KEM-768 Private-Key:
seed:
dk:
ek:
```

Both `seed:` and `dk:` printed, so both buffers were populated on this run: a
64-byte `d||z` seed on the stack and a 2400-byte `dk` on the heap, both abandoned
uncleansed.

### Actual impact

**Inferred, with the reasoning shown.** This is a residual-memory exposure, not a
disclosure. Neither buffer is transmitted anywhere; the defect is that after the
call returns, plaintext CSP bytes remain in freed heap and in the current stack
frame until overwritten by unrelated activity. Exploiting it requires an
*independent* primitive the defect does not provide — a heap read primitive from
another bug, a core dump, a swapped-out page, or a hibernation image. Against
that, the two aggravating factors are that the seed is the strongest form of the
CSP (64 bytes that regenerate the entire key) and that the path is a routine,
documented CLI operation rather than an obscure corner.

For certification the relevant framing is different from the security framing:
FIPS 140-3 / ISO 19790 §7.9 requires that unprotected SSPs be zeroized, and a
lab reading this function will find the gap in minutes. That is why it is tracked
here even though its security severity is low.

**Not measured:** we have not attempted to recover either buffer from freed heap
after the call. No such experiment is planned; see the note on quarantining
allocators in the Sprint 3 zeroization work.

### Why we are not fixing it locally

`ml_kem_codecs.c` is byte-identical to the 3.5.7 import (verified above) and
sits in the directory ADR-0005 pins as untouched. Editing it would put the first
QudoSSL delta into `providers/implementations/`, which changes the character of
the fork and the diff a lab is asked to review. The function is also outside the
FIPS module, so the local fix would not improve the certified boundary.

### Proposed disposition

**Report upstream** as an ordinary bug with a patch. The upstream fix is:
`OPENSSL_clear_free(prvenc, prvlen)` at `:617`, `OPENSSL_cleanse(seed, sizeof(seed))`
at `end:`, and converting the `:587` `return 0` to `goto end`. Re-evaluate on the
next 3.5.x sync — if upstream has taken the fix, it arrives for free.

---

## UD-2 — PKCS#8 private-key buffer freed uncleansed on the encoder error paths

**Location:**
`openssl/providers/implementations/encode_decode/ml_kem_codecs.c:435-556` and
`openssl/providers/implementations/encode_decode/ml_dsa_codecs.c:288-407`

**Naming note.** The Sprint 3 findings refer to this as `ossl_ml_dsa_encode_p8`.
No such symbol exists in 3.5.7 — `grep -rn "encode_p8" openssl/providers openssl/crypto
openssl/include` returns nothing. The functions are `ossl_ml_kem_i2d_prvkey` and
`ossl_ml_dsa_i2d_prvkey`. The defect the findings describe is real; only the name
was wrong.

### What is wrong

Both functions build the complete PKCS#8 private-key body in a single heap
buffer and, on failure, release it with a bare free.

ML-DSA (`ml_dsa_codecs.c`):

- `:341` — `if ((pos = buf = OPENSSL_malloc((size_t)len)) == NULL) goto end;`
- `:371` — `memcpy(pos, seed, ML_DSA_SEED_BYTES);` (32 bytes, `include/crypto/ml_dsa.h:21`)
- `:382` — `memcpy(pos, sk, params->sk_len);` (2560 / 4032 / 4896 bytes,
  `include/crypto/ml_dsa.h:26,31,36`)
- `:402-406` —

  ```c
  end:
      OPENSSL_free(fmt_slots);
      if (ret == 0)
          OPENSSL_free(buf);
      return ret;
  ```

ML-KEM (`ml_kem_codecs.c`) is structurally identical: `:486` allocates, `:513`
writes the seed via `ossl_ml_kem_encode_seed`, `:525` writes `dk` via
`ossl_ml_kem_encode_private_key`, and `:551-554` frees with `OPENSSL_free(buf)`
when `ret == 0`.

**Measured**, at object level — neither function references a cleansing call:

```
$ objdump -d -r --disassemble-symbols=_ossl_ml_kem_i2d_prvkey \
    .../libdefault-lib-ml_kem_codecs.o | grep -oE "_(CRYPTO_free|CRYPTO_clear_free|OPENSSL_cleanse|CRYPTO_malloc)" | sort | uniq -c
   4 _CRYPTO_free
   1 _CRYPTO_malloc

$ objdump -d -r --disassemble-symbols=_ossl_ml_dsa_i2d_prvkey \
    .../libdefault-lib-ml_dsa_codecs.o | grep -oE "_(CRYPTO_free|CRYPTO_clear_free|OPENSSL_cleanse|CRYPTO_malloc)" | sort | uniq -c
   4 _CRYPTO_free
   1 _CRYPTO_malloc
```

### How it is reached

**Inferred from source reading; not exercised at runtime.** The `ret == 0`
branch is taken on every exit that reaches `end:` without having set `ret`.
Three further `return 0` statements at `:304`, `:310` and `:318` bypass `end:`
altogether, but all three are before the `malloc` at `:341`, so no CSP exists
yet on those paths and they are not part of this defect. Tracing which of the
exits that *do* reach `end:` leave CSP bytes in `buf` (ML-DSA line numbers):

| Exit | `buf` state | CSP present? |
|---|---|---|
| `:332` no matching format | not yet allocated (NULL) | No |
| `:342` malloc failure | NULL | No |
| `:356` bad `p8_shift` | allocated, nothing written | No |
| `:369` seed-offset mismatch | header magic only | No |
| `:380` priv-offset mismatch | **seed already copied at `:371`** | Yes |
| `:391` pub-offset mismatch | **seed + `sk` already copied** | Yes |
| `:397` `pos != buf + len` falls through with `ret == 0` | **fully populated** | Yes |

So three of the seven error exits that reach `end:` free a buffer containing
real key material. Two of the three (`:380`, `:391`) raise
`ERR_R_INTERNAL_ERROR`; the third is the `:397` fall-through, which raises
*nothing at all* and returns 0 silently. All three correspond to an
inconsistency between the format table and the computed offsets — they are not
expected to occur in normal operation, and we have not constructed an input that
triggers one.

**Not measured:** we did not force any of these branches. Reachability from
attacker-controlled input is therefore **unknown**; the honest position is that
these look like internal-consistency assertions rather than input-driven paths.

The success path is different and is *not* claimed to be a defect here: at `:397-400`
ownership of `buf` transfers to the caller via `*out = buf`. The immediate caller
`key_to_p8info` (`encode_key2any.c:85-107`) hands it to `PKCS8_pkey_set0`, and
frees it with `OPENSSL_free(der)` at `:102` only when that fails. That is the same
pattern one level up, in a file we also ship unmodified. We have **not** audited
the full DER/PEM output chain for zeroization, so no claim is made about it beyond
noting the pattern.

### Actual impact

**Inferred.** Lower than UD-1: the exposure requires an internal-error path that
should not occur, and the exposure itself is again residual freed memory with no
disclosure mechanism. It is included because the fix is the same one-line change,
because a lab auditing CSP handling will enumerate every free of a private-key
buffer, and because leaving it means the CSP register carries footnoted
exceptions.

### Why we are not fixing it locally

Same reason as UD-1: both files are byte-identical to the 3.5.7 import, both are
inside the ADR-0005 no-touch directory, and neither is in the FIPS module.

### Proposed disposition

**Report upstream** with UD-1, in the same issue or PR series. Fix is
`OPENSSL_clear_free(buf, (size_t)len)` in both functions. **Accept locally** in
the interim and record it in the CSP register as an upstream-tracked exception on
a non-module path.

---

## UD-3 — `ml_kem_load` frees the unparsed PKCS#8 private key uncleansed

**Location:** `openssl/providers/implementations/keymgmt/ml_kem_kmgmt.c:509-553`

### What is wrong

- `:513-514` — `uint8_t *encoded_dk = NULL; uint8_t seed[ML_KEM_SEED_BYTES];`
- `:519-520` — `encoded_dk = key->encoded_dk; key->encoded_dk = NULL;` — the
  function takes ownership of the unparsed PKCS#8 private key body
  (`include/crypto/ml_kem.h:194`, "Unparsed P8 private key"), allocated at
  `ml_kem_codecs.c:388` with size `p8fmt->priv_length` = `prvkey_bytes` =
  1632 / 2400 / 3168.
- `:524` — `ossl_ml_kem_encode_seed(seed, sizeof(seed), key)` fills the 64-byte
  stack seed with `d||z`.
- `:545` (success) and `:550` (error) — `OPENSSL_free(encoded_dk);` with no
  cleanse. `seed` is never cleansed on any path.

This is a straightforward inconsistency inside upstream's own codebase: the
crypto-layer owner of the same buffer does cleanse it —
`crypto/ml_kem/ml_kem.c:2030-2031` reads
`OPENSSL_cleanse(key->encoded_dk, key->vinfo->prvkey_bytes); OPENSSL_free(key->encoded_dk);`
— but the keymgmt layer, which detaches and frees it itself, does not.

**Measured:**

```
$ objdump -d -r --disassemble-symbols=_ml_kem_load \
    openssl/providers/implementations/keymgmt/libdefault-lib-ml_kem_kmgmt.o \
  | grep -oE "_(CRYPTO_free|CRYPTO_clear_free|OPENSSL_cleanse|CRYPTO_secure_clear_free)" | sort | uniq -c
   2 _CRYPTO_free
```

### How it is reached

**Inferred from source.** `ml_kem_load` is the keymgmt `load` operation, invoked
when a decoded ML-KEM private key reference is turned into a key object — i.e. the
normal `d2i` / PEM-read path, not an error path. This makes it more routinely
reached than UD-2.

**Not measured:** we did not instrument a load to confirm the free executes. The
call is unconditional in both exits, so the inference is strong, but it is an
inference.

### Actual impact

**Measured constraint that bounds it:** the whole function is inside
`#ifndef FIPS_MODULE` (`:509` and `:554`), so it does not exist in the FIPS
provider at all:

```
$ nm -a openssl/providers/implementations/keymgmt/libfips-lib-ml_kem_kmgmt.o | grep -c ml_kem_load
0
$ nm -a openssl/providers/implementations/keymgmt/libdefault-lib-ml_kem_kmgmt.o | grep ml_kem_load
00000000000008bc t _ml_kem_load
0000000000001744 s l___func__.ml_kem_load
```

Impact is therefore confined to the default provider. **Correction to an
over-broad reading:** it is not "every load". Both buffers are populated only
when the decoded key carried an unparsed PKCS#8 body — `encoded_dk` is by
definition NULL otherwise, and `seed` is filled only inside the
`encoded_dk != NULL &&` short-circuit at `:523-524`, so a seed-only load leaves
`seed` unwritten and frees a NULL pointer. On the loads that *do* carry a P8
body, up to 3168 bytes of `dk` plus 64 bytes of `d||z` are left in freed heap and
on the stack. Same residual-memory class as UD-1, and that path is still
ordinary key loading rather than an error path.

### Why we are not fixing it locally

`ml_kem_kmgmt.c` is byte-identical to the import (`diff` against
`4ca9ad0:providers/implementations/keymgmt/ml_kem_kmgmt.c` returns nothing) and is
inside the ADR-0005 no-touch set.

### Proposed disposition

**Already fixed upstream — request a 3.5 backport.** Upstream `master` now reads
`OPENSSL_secure_clear_free(encoded_dk, key->vinfo->prvkey_bytes);` at both exits
(fetched from `raw.githubusercontent.com/openssl/openssl/master/...` on
2026-07-23). The stack `seed` still appears uncleansed on master, so that half of
the defect is worth reporting separately. **Fix on next sync** once a 3.5.x point
release carries the change.

---

## UD-4 — ML-KEM decap constant-time poisoning under-covers rank 3 and rank 4

**Location:** `openssl/crypto/ml_kem/ml_kem.c:2518`
(pristine upstream 3.5.7 line 2312 — our delegation edits shifted it by 206
lines). The same defect is analysed in depth in
[side-channel-analysis.md](side-channel-analysis.md) §4, which uses the same
line numbers.

**This one is inside our seam.** `crypto/ml_kem/ml_kem.c` is a file QudoSSL
already modifies (`git log --oneline -1 -- openssl/crypto/ml_kem/ml_kem.c` →
`033cab7 Record why ML-KEM's dead math is not gated`; the delegation edits
themselves are `7171d40` and `a765197`). The defective line is nevertheless
unmodified upstream code — `git blame` attributes it to the import `4ca9ad0`.
We *could* fix it in one line. We are deliberately not doing so yet; the trigger
for doing so is stated below.

### What is wrong

```c
2513  #if defined(OPENSSL_CONSTANT_TIME_VALIDATION)
2514      /*
2515       * Data derived from |s| and |z| defaults secret, and to avoid side-channel
2516       * leaks should not influence control flow.
2517       */
2518      classify_bytes = 2 * sizeof(scalar) + ML_KEM_RANDOM_BYTES;
2519      CONSTTIME_SECRET(key->s, classify_bytes);
2520  #endif
```

The rank is hard-coded as `2`. Constants:

- `sizeof(scalar)` = 512 — `scalar` is `uint16_t c[ML_KEM_DEGREE]`
  (`ml_kem.c:94-97`) with `ML_KEM_DEGREE` 256 (`include/crypto/ml_kem.h:19`).
- `ML_KEM_RANDOM_BYTES` = 32 (`include/crypto/ml_kem.h:47`).
- So `classify_bytes` = 2 × 512 + 32 = **1056 bytes**, for every parameter set.

The region that is actually secret starts at `key->s` and is laid out by
`add_storage` (`ml_kem.c:1878`): `key->z = (uint8_t *)(rank + (key->s = key->m + rank * rank));`
— i.e. `rank` scalars of private vector `s`, immediately followed by the 64-byte
`zbuf` declared at `ml_kem.c:112` as `uint8_t zbuf[2 * ML_KEM_RANDOM_BYTES]`.
Within `zbuf`, `z` is first and `d` second (`key->d = key->z + ML_KEM_RANDOM_BYTES`,
`ml_kem.c:2248`, and likewise `:1733`, `:2092`). `rank` is 2 / 3 / 4
(`include/crypto/ml_kem.h:95,104,113`).

**Upstream's own code in the same file states the correct extent twice.**
`ossl_ml_kem_key_reset` cleanses exactly the right region at `ml_kem.c:1897-1899`:

```c
if (ossl_ml_kem_have_prvkey(key))
    OPENSSL_cleanse(key->s,
        key->vinfo->rank * sizeof(scalar) + 2 * ML_KEM_RANDOM_BYTES);
```

and `ossl_ml_kem_genkey` declassifies with the right extent at `ml_kem.c:2396-2397`:

```c
CONSTTIME_DECLASSIFY(key->s, vinfo->rank * sizeof(scalar));
CONSTTIME_DECLASSIFY(key->z, 2 * ML_KEM_RANDOM_BYTES);
```

Only line 2508 uses the literal `2`. That makes it unambiguously an oversight
rather than a deliberate narrowing.

**Measured** — the shortfall, byte by byte:

| Parameter set | rank | `s` bytes | `zbuf` bytes | Secret region | Poisoned | Left unpoisoned |
|---|---|---|---|---|---|---|
| ML-KEM-512 | 2 | 1024 | 64 | 1088 | 1056 | 32 — the `d` half of `zbuf` only. `z` **is** covered. |
| ML-KEM-768 | 3 | 1536 | 64 | 1600 | 1056 | 544 — the last 480 bytes of `s`, **plus all of `z` and `d`** |
| ML-KEM-1024 | 4 | 2048 | 64 | 2112 | 1056 | 1056 — the last 992 bytes of `s`, **plus all of `z` and `d`** |

The consequential part is that for ML-KEM-768 and ML-KEM-1024 the implicit-rejection
secret `z` is entirely unpoisoned. A branch or a secret-dependent memory index on
`z` — precisely the Fujisaki-Okamoto failure mode a constant-time run exists to
rule out — would not be reported by valgrind at those parameter sets. ML-KEM-512
does cover `z`, so a run limited to 512 would look clean and prove less than it
appears to.

The matching `CONSTTIME_DECLASSIFY(key->s, classify_bytes)` at `:2539` uses the
same short value, so the poison and the declassify are symmetric — there is no
secondary "declassified more than was poisoned" problem.

### How it is reached

Only in a build that defines `OPENSSL_CONSTANT_TIME_VALIDATION`.
`CONSTTIME_SECRET` is `VALGRIND_MAKE_MEM_UNDEFINED` under that macro
(`ml_kem.c:146-161`) and expands to nothing otherwise (`:163-168`).

**Measured** — that build does not exist here:

```
$ grep -c OPENSSL_CONSTANT_TIME_VALIDATION openssl/configdata.pm
0
$ grep -rn OPENSSL_CONSTANT_TIME_VALIDATION openssl/Configure openssl/INSTALL.md
(no output — Configure exposes no option for it)
$ grep -rlniE "valgrind|constant.time|ctgrind" ci/ .github/
(no output — no CI job runs one)
```

There is no Configure switch, no CI job, and no local build with the macro
defined. **Shipped code is unaffected**, in every configuration we produce.

### Actual impact

**Inferred.** Zero on any binary QudoSSL ships. The impact is on *evidence
quality*: the first constant-time run performed against ML-KEM-768 or -1024 would
under-report, and a clean result would carry materially less assurance than it
appears to. Since we have run no such test, the current impact is nil and the
future impact is a false-confidence risk.

### Why it is deferred rather than fixed

- It cannot affect the shipped module; the entire block is inside
  `#if defined(OPENSSL_CONSTANT_TIME_VALIDATION)`.
- It is already fixed on upstream `master` (below), so a local patch would be a
  temporary divergence we would have to unwind at the next sync.
- Fixing it now with no harness to run means claiming a correction we cannot
  demonstrate. Fixing it as the first step of the first CT run means the fix and
  its verification land together.

### Trigger condition

> **Fix `crypto/ml_kem/ml_kem.c:2508` before the first
> `OPENSSL_CONSTANT_TIME_VALIDATION` build is run, and before any constant-time
> result for ML-KEM is recorded as evidence.**

The fix, matching what `key_reset` and `genkey` already use in the same file:

```c
classify_bytes = key->vinfo->rank * sizeof(scalar) + 2 * ML_KEM_RANDOM_BYTES;
```

`vinfo` is already dereferenced at `:2494`. Note this is one term wider than
upstream master's fix, which stops at `+ ML_KEM_RANDOM_BYTES` and so still leaves
the `d` half of `zbuf` unpoisoned. `d` is not an input to decap, so covering it
costs nothing and removes a question; matching master exactly would minimise the
delta. Either is defensible — decide when the fix is made, and record which was
chosen.

### Proposed disposition

**Deferred locally** under the trigger above, and **request a 3.5 backport**
upstream. Upstream `master` reads
`classify_bytes = vinfo->rank * sizeof(scalar) + ML_KEM_RANDOM_BYTES;`, while the
`openssl-3.5` branch head still reads `2 * sizeof(scalar) + ML_KEM_RANDOM_BYTES`
(both fetched from `raw.githubusercontent.com` on 2026-07-23). The right upstream
action is a backport request, not a new bug report.

---

## Related, not tracked here

Listed so the upstream report is complete and so nothing is lost, but excluded
from the register above because their disposition belongs elsewhere — chiefly
because they sit in `openssl/crypto/`, inside the seam ADR-0005 already opens, so
we *can* fix them locally and in two cases already have.

- **`openssl/crypto/ml_dsa/ml_dsa_key.c` — already fixed locally.** Upstream 3.5.7
  freed a full ML-DSA private-key encoding (2560 / 4032 / 4896 bytes) with a bare
  `OPENSSL_free(sk)` on the **success** path of `ossl_ml_dsa_generate_key`, and
  freed a memdup'd private-key encoding plus a memdup'd seed with bare
  `OPENSSL_free` on the error path of `ossl_ml_dsa_set_prekey`.
  **Measured** — upstream's version, via
  `git show 4ca9ad0:crypto/ml_dsa/ml_dsa_key.c`, ends `set_prekey` with
  `OPENSSL_free(key->priv_encoding); OPENSSL_free(key->seed);`. Our tree no longer
  does: commit `dd192a5` ("Zeroize ML-DSA private key material on free") changed
  them to `OPENSSL_clear_free` at `ml_dsa_key.c:68,70` and `ml_dsa_key.c:645`.
  Confirmed in the built FIPS object:

  ```
  $ objdump -d -r --disassemble-symbols=_ossl_ml_dsa_set_prekey \
      openssl/crypto/ml_dsa/libfips-lib-ml_dsa_key.o | grep -oE "_(CRYPTO_free|CRYPTO_clear_free)" | sort | uniq -c
     2 _CRYPTO_clear_free
  $ objdump -d -r --disassemble-symbols=_ossl_ml_dsa_generate_key \
      openssl/crypto/ml_dsa/libfips-lib-ml_dsa_key.o | grep -oE "_(CRYPTO_free|CRYPTO_clear_free)" | sort | uniq -c
     1 _CRYPTO_clear_free
     1 _CRYPTO_free
  ```

  The remaining bare `_CRYPTO_free` in `generate_key` is `ml_dsa_key.c:620`, which
  releases `out->seed` after `RAND_priv_bytes_ex` *failed* to fill it — no CSP is
  present, so it is correct as written.

  These are still defects in upstream 3.5.7 and in the `openssl-3.5` branch and
  **should still be reported**. Upstream `master` has fixed both, using
  `OPENSSL_secure_clear_free` rather than the `OPENSSL_clear_free` we chose;
  that divergence needs reconciling at the next sync (see Open items).
- **`openssl/crypto/ml_dsa/ml_dsa_vector.h:49-53` — upstream defect, still
  unfixed locally.** `vector_zero()` is `memset(va->poly, 0, ...)` immediately
  before `vector_free()` frees the same block, which the compiler is permitted to
  elide as a dead store. Upstream's own ML-KEM path uses `OPENSSL_cleanse` for the
  identical job (`ml_kem.c:1898`). **Measured** — the file is byte-identical to
  the import (`git show 4ca9ad0:crypto/ml_dsa/ml_dsa_vector.h | diff -` returns
  nothing) and its last-touching commit is `5943b28`. Inside our seam, so
  locally fixable; the decision belongs to the Sprint 3 zeroization work, not
  here.
- **`openssl/providers/implementations/encode_decode/encode_key2any.c:102` and `:165`** —
  the DER buffer produced by the `i2d` callbacks is freed with plain `OPENSSL_free`
  on the failure paths of `key_to_p8info` and `key_to_pubkey`. This is the caller
  side of UD-2 and the same pattern. **We have not audited the full
  encode/PEM/DER chain**, so this is recorded as an observation, not as a
  characterised defect.
- **`ossl_ml_dsa_key_to_text`** (`ml_dsa_codecs.c:409-451`) has **no** equivalent
  of UD-1. **Measured by reading the function**: it prints directly from
  key-owned buffers via `ossl_ml_dsa_key_get_seed` / `_get_priv` / `_get_pub`
  (`:419-421`) and makes no copy, so there is nothing for it to cleanse. The
  asymmetry with the ML-KEM version is real and is worth mentioning in the
  upstream report as supporting evidence that UD-1 is an oversight.
- **`openssl/crypto/slh_dsa/`** — not audited. SLH-DSA is not delegated
  ([ADR-0010](adr/ADR-0010-slh-dsa-stays-upstream.md)) but its private keys are
  still module CSPs.

---

## Upstream status

Checked on 2026-07-23 against `raw.githubusercontent.com/openssl/openssl/<ref>/...`.
This is a point-in-time read of two branch tips and should be re-confirmed
immediately before anything is filed.

| ID | 3.5.7 (ours) | `openssl-3.5` branch head | `master` |
|---|---|---|---|
| UD-1 `ossl_ml_kem_key_to_text` | Defect present | Not checked | **Still present** — same bare `OPENSSL_free(pubenc)/(prvenc)` at `end:`, same early `return 0` after the `prvenc` malloc, `seed` still uncleansed |
| UD-2 `ossl_ml_{kem,dsa}_i2d_prvkey` | Defect present | Not checked | **Still present** — both end with `if (ret == 0) OPENSSL_free(buf);` |
| UD-3 `ml_kem_load` | Defect present | Not checked | **Fixed** — `OPENSSL_secure_clear_free(encoded_dk, key->vinfo->prvkey_bytes)` at both exits. Stack `seed` still uncleansed. |
| UD-4 `classify_bytes` | Defect present | **Still present** (`2 * sizeof(scalar) + ML_KEM_RANDOM_BYTES`) | **Fixed** (`vinfo->rank * sizeof(scalar) + ML_KEM_RANDOM_BYTES`) |
| (related) `ossl_ml_dsa_set_prekey` | Defect present; **fixed in our tree** by `dd192a5` | Not checked | **Fixed** — `OPENSSL_secure_clear_free(key->priv_encoding, sk_len)` / `(key->seed, seed_len)` |
| (related) `ossl_ml_dsa_generate_key` | Defect present; **fixed in our tree** by `dd192a5` | Not checked | **Fixed** — `OPENSSL_secure_clear_free(sk, out->params->sk_len)` |
| (related) `vector_zero` | Defect present, unfixed | Not checked | Not checked |

Two caveats on this table. First, the fetches were single reads of raw files and
have not been cross-checked against the OpenSSL git history, so we cannot say
*which* commit fixed UD-3 or UD-4 or whether a backport is already queued.
Second, "still present on master" is a negative claim derived from reading the
current file; before filing, confirm there is no open PR covering it.

---

## How to report

### Security issue or ordinary bug?

The question has to be answered per defect, not by category. OpenSSL's severity
policy turns on whether a practical attack exists, not on whether sensitive data
is involved, so "it touches a private key" is not by itself sufficient to warrant
private disclosure.

**UD-1, UD-2, UD-3 — ordinary bugs, public disclosure.** The reasoning:

1. None of them creates a disclosure channel. The CSP bytes stay inside the
   process. Reaching them requires a *separate*, pre-existing primitive — an
   arbitrary heap read from some other vulnerability, a core dump, a page written
   to swap, or physical acquisition of memory. A defect that is only exploitable
   given an existing memory-disclosure primitive is hardening, not a vulnerability.
2. None of them is remotely triggerable in a way that yields anything to the
   attacker. UD-1 is a local diagnostic path (`openssl pkey -text`) invoked
   deliberately by the key's own owner. UD-2 is an internal-error path we could
   not show is input-reachable at all. UD-3 is on the local key-load path.
3. The affected code is not in the FIPS module (measured above), so there is no
   argument that a certified boundary is compromised.
4. There is no cryptographic weakness: no key is derived incorrectly, no output
   is wrong, no comparison is variable-time.

The counter-argument, stated fairly: UD-1 abandons the *seed*, which is the
strongest possible form of the CSP, and it does so on a path that runs by default
whenever a user inspects a key. If OpenSSL's security team disagrees with our
reading, the cost of having filed publicly is that the fix is visible before it
ships. Given points 1-4 we judge that cost acceptable, but the judgement is ours
and it is recorded here rather than assumed. **If in doubt on UD-1, ask the
security team first** — a one-line private query costs nothing and a wrong public
filing cannot be undone.

**UD-4 — not a security issue by construction.** The defective statement is
inside `#if defined(OPENSSL_CONSTANT_TIME_VALIDATION)` and therefore does not
exist in any shipped binary. It is a test-instrumentation coverage bug. It also
happens to be already fixed on master, so the filing is a backport request rather
than a report.

**A note on FIPS 140-3 framing.** All four are certification-relevant under
ISO 19790 §7.9 (zeroization of unprotected SSPs) and IG D.E (constant-time
assurance). Certification relevance is not the same as security severity, and the
upstream report should keep the two arguments separate — leading with "this
breaks our FIPS submission" invites the reply that upstream is not responsible for
our submission, whereas "this frees a private key without cleansing, here is the
one-line patch" is uncontroversial.

### Procedure

1. **Re-confirm current upstream state** for each defect against `master` and
   `openssl-3.5`, from git rather than from a raw-file fetch, and search open
   issues and PRs for an existing report. The table above is a 2026-07-23
   snapshot and will go stale.
2. **UD-1 and UD-2:** one public GitHub issue on `openssl/openssl` covering both
   (they are the same class in the same directory), with a PR against `master`.
   Include the `objdump` relocation evidence — it is short, reproducible from a
   stock build, and forestalls "the compiler probably handles it". Include the
   `ossl_ml_dsa_key_to_text` contrast as evidence of oversight. Request a 3.5
   backport in the same issue.
3. **UD-3:** a backport request against `openssl-3.5`, referencing the master
   commit that introduced `OPENSSL_secure_clear_free`. File the uncleansed stack
   `seed` separately if it is still present on master when checked.
4. **UD-4:** a backport request against `openssl-3.5`, referencing the master
   fix. If we choose the wider `2 * ML_KEM_RANDOM_BYTES` variant locally, raise
   that as a separate small PR against master with the `key_reset` /
   `genkey` consistency argument.
5. **The two `crypto/ml_dsa/ml_dsa_key.c` defects we already fixed locally**
   still need an `openssl-3.5` backport request, referencing the master commit
   that introduced `OPENSSL_secure_clear_free`. Filing them costs nothing extra
   and it is what lets us drop our local delta at a future sync. Our patch is not
   offered upstream — master's is already better.
6. **If any is reclassified as a security issue** during step 1, route it
   privately per OpenSSL's published security policy (currently
   `openssl-security@openssl.org`; re-read the policy page before sending, as the
   address and process have changed historically) and do not open a public issue
   until they respond.
7. **Record the outcome here** — issue and PR numbers, upstream severity
   assessment, and the release in which each fix lands. That record is what turns
   "we knew and chose not to patch" into a defensible position for the lab.

---

## Open items

1. **UD-4 has no owner and no date.** Its trigger ("before the first
   `OPENSSL_CONSTANT_TIME_VALIDATION` build") is currently unbounded, because no
   constant-time run is scheduled. If Sprint 3 or 4 schedules one, the trigger
   fires; if it does not, this item must be re-raised rather than allowed to
   lapse silently.
2. **Nothing has been filed upstream yet.** No issue, no PR, no private query.
   Everything in "How to report" is a plan.
3. **The `openssl-3.5` branch was checked only for UD-4.** UD-1, UD-2 and UD-3
   were compared against `master` only. The 3.5 branch is the one that matters
   for our next sync.
4. **The upstream-status table rests on raw-file fetches summarised by a tool,
   not on a git clone.** The UD-3 and UD-4 "fixed on master" claims are strong
   (both quote a specific changed expression) but neither has been tied to a
   commit. Verify from git before citing upstream in any lab-facing document.
5. **Runtime reachability is measured for UD-1 only.** UD-2 and UD-3 reachability
   is inferred from source reading. UD-2's error branches have not been shown to
   be reachable from any input at all.
6. **No residual-memory experiment has been run** for any of these. We assert the
   buffers are not cleansed (measured, at source and object level); we do not
   assert that the bytes have been recovered from freed memory, and no such
   experiment is planned.
7. **`openssl/crypto/slh_dsa/` has not been audited** for the same defect class.
   It is outside delegation ([ADR-0010](adr/ADR-0010-slh-dsa-stays-upstream.md))
   but inside the module.
8. **The full encode/DER/PEM output chain has not been audited.**
   `encode_key2any.c:102` and `:165` show the same pattern one level above UD-2;
   whether it recurs elsewhere is unknown.
9. **The `providers/implementations/` zero-delta property has no CI guard.**
   It is the premise of this entire document and is currently verified only by
   hand (`git log --oneline -1 -- openssl/providers/implementations/`). A
   two-line CI assertion would make the premise durable; none exists in `ci/`
   today.
10. **Decision needed on the CSP register.** UD-1 through UD-3 will each need a
    row, or a documented exclusion, in the Security Policy's SSP table. Whether
    non-module paths are enumerated there at all is a question for lab
    pre-engagement.
11. **Our local ML-DSA zeroization fix diverges from upstream's.** Commit
    `dd192a5` used `OPENSSL_clear_free`; upstream master uses
    `OPENSSL_secure_clear_free`, which additionally routes through the secure
    heap. The ML-KEM provider path already uses the secure-heap allocators
    (`ml_kem_kmgmt.c`) while the ML-DSA path does not, and that asymmetry will be
    questioned. Decide whether to match master before the next sync, so the local
    delta disappears rather than persisting as a near-miss.
12. **`vector_zero` is still a plain `memset`.** It is inside our seam and
    therefore fixable, and it is the one item in this document where the
    zeroization may be silently removed by an optimiser. It survived on this
    arm64 `-O3` build, but "our compiler happens to keep it" is not an answer to
    a lab. No owner assigned.
