/* SPDX-License-Identifier: Apache-2.0 AND MIT */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

#include <openssl/evp.h>
#include <openssl/provider.h>

#include "slhdsa_wrapper.h"

static double get_time_ms(void) {
#ifdef _WIN32
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart * 1000.0 / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
#endif
}

typedef struct {
    double keygen_ms;
    double sign_ms;
    double verify_ms;
} BenchResult;

static BenchResult benchmark_openssl(const char *alg_name, int iterations) {
    BenchResult result = {0};
    EVP_PKEY *pkey = NULL;
    EVP_PKEY_CTX *ctx = NULL;
    EVP_SIGNATURE *sig_alg = NULL;
    const uint8_t msg[] = "Benchmark test message";
    size_t msg_len = sizeof(msg) - 1;
    size_t sig_len = 0;
    uint8_t *signature = NULL;
    double start;
    int i;

    for (i = 0; i < 2; i++) {
        if (pkey) EVP_PKEY_free(pkey);
        pkey = NULL;
        ctx = EVP_PKEY_CTX_new_from_name(NULL, alg_name, "provider=default");
        if (ctx) {
            EVP_PKEY_keygen_init(ctx);
            EVP_PKEY_keygen(ctx, &pkey);
            EVP_PKEY_CTX_free(ctx);
            ctx = NULL;
        }
    }

    start = get_time_ms();
    for (i = 0; i < iterations; i++) {
        if (pkey) EVP_PKEY_free(pkey);
        pkey = NULL;

        ctx = EVP_PKEY_CTX_new_from_name(NULL, alg_name, "provider=default");
        if (!ctx) goto cleanup;

        if (EVP_PKEY_keygen_init(ctx) <= 0) goto cleanup;
        if (EVP_PKEY_keygen(ctx, &pkey) <= 0) goto cleanup;

        EVP_PKEY_CTX_free(ctx);
        ctx = NULL;
    }
    result.keygen_ms = (get_time_ms() - start) / iterations;

    ctx = EVP_PKEY_CTX_new_from_pkey(NULL, pkey, "provider=default");
    sig_alg = EVP_SIGNATURE_fetch(NULL, alg_name, "provider=default");
    if (!ctx || !sig_alg) goto cleanup;
    if (EVP_PKEY_sign_message_init(ctx, sig_alg, NULL) <= 0) goto cleanup;

    if (EVP_PKEY_sign(ctx, NULL, &sig_len, msg, msg_len) <= 0) goto cleanup;
    signature = malloc(sig_len);

    for (i = 0; i < 2; i++) {
        size_t len = sig_len;
        EVP_PKEY_sign(ctx, signature, &len, msg, msg_len);
    }

    start = get_time_ms();
    for (i = 0; i < iterations; i++) {
        size_t len = sig_len;
        if (EVP_PKEY_sign(ctx, signature, &len, msg, msg_len) <= 0) goto cleanup;
    }
    result.sign_ms = (get_time_ms() - start) / iterations;
    EVP_PKEY_CTX_free(ctx);
    EVP_SIGNATURE_free(sig_alg);
    ctx = NULL;
    sig_alg = NULL;

    ctx = EVP_PKEY_CTX_new_from_pkey(NULL, pkey, "provider=default");
    sig_alg = EVP_SIGNATURE_fetch(NULL, alg_name, "provider=default");
    if (!ctx || !sig_alg) goto cleanup;
    if (EVP_PKEY_verify_message_init(ctx, sig_alg, NULL) <= 0) goto cleanup;

    for (i = 0; i < 2; i++) {
        EVP_PKEY_verify(ctx, signature, sig_len, msg, msg_len);
    }

    start = get_time_ms();
    for (i = 0; i < iterations; i++) {
        if (EVP_PKEY_verify(ctx, signature, sig_len, msg, msg_len) <= 0) goto cleanup;
    }
    result.verify_ms = (get_time_ms() - start) / iterations;

cleanup:
    if (ctx) EVP_PKEY_CTX_free(ctx);
    if (sig_alg) EVP_SIGNATURE_free(sig_alg);
    if (pkey) EVP_PKEY_free(pkey);
    if (signature) free(signature);

    return result;
}

static BenchResult benchmark_qudo_provider(const char *alg_name, int iterations, OSSL_PROVIDER *qudo_prov) {
    BenchResult result = {0};
    EVP_PKEY *pkey = NULL;
    EVP_PKEY_CTX *ctx = NULL;
    EVP_MD_CTX *mdctx = NULL;
    const uint8_t msg[] = "Benchmark test message";
    size_t msg_len = sizeof(msg) - 1;
    size_t sig_len = 0;
    uint8_t *signature = NULL;
    double start;
    int i;

    for (i = 0; i < 2; i++) {
        if (pkey) EVP_PKEY_free(pkey);
        pkey = NULL;
        ctx = EVP_PKEY_CTX_new_from_name(NULL, alg_name, "provider=qudoprovider");
        if (ctx) {
            EVP_PKEY_keygen_init(ctx);
            EVP_PKEY_keygen(ctx, &pkey);
            EVP_PKEY_CTX_free(ctx);
            ctx = NULL;
        }
    }

    start = get_time_ms();
    for (i = 0; i < iterations; i++) {
        if (pkey) EVP_PKEY_free(pkey);
        pkey = NULL;

        ctx = EVP_PKEY_CTX_new_from_name(NULL, alg_name, "provider=qudoprovider");
        if (!ctx) goto cleanup;

        if (EVP_PKEY_keygen_init(ctx) <= 0) goto cleanup;
        if (EVP_PKEY_keygen(ctx, &pkey) <= 0) goto cleanup;

        EVP_PKEY_CTX_free(ctx);
        ctx = NULL;
    }
    result.keygen_ms = (get_time_ms() - start) / iterations;

    mdctx = EVP_MD_CTX_new();
    if (!mdctx) goto cleanup;
    if (EVP_DigestSignInit(mdctx, NULL, NULL, NULL, pkey) <= 0) goto cleanup;

    if (EVP_DigestSign(mdctx, NULL, &sig_len, msg, msg_len) <= 0) goto cleanup;
    signature = malloc(sig_len);

    for (i = 0; i < 2; i++) {
        size_t len = sig_len;
        EVP_MD_CTX_reset(mdctx);
        EVP_DigestSignInit(mdctx, NULL, NULL, NULL, pkey);
        EVP_DigestSign(mdctx, signature, &len, msg, msg_len);
    }

    start = get_time_ms();
    for (i = 0; i < iterations; i++) {
        size_t len = sig_len;
        EVP_MD_CTX_reset(mdctx);
        if (EVP_DigestSignInit(mdctx, NULL, NULL, NULL, pkey) <= 0) goto cleanup;
        if (EVP_DigestSign(mdctx, signature, &len, msg, msg_len) <= 0) goto cleanup;
    }
    result.sign_ms = (get_time_ms() - start) / iterations;
    EVP_MD_CTX_free(mdctx);
    mdctx = NULL;

    mdctx = EVP_MD_CTX_new();
    if (!mdctx) goto cleanup;
    if (EVP_DigestVerifyInit(mdctx, NULL, NULL, NULL, pkey) <= 0) goto cleanup;

    for (i = 0; i < 2; i++) {
        EVP_MD_CTX_reset(mdctx);
        EVP_DigestVerifyInit(mdctx, NULL, NULL, NULL, pkey);
        EVP_DigestVerify(mdctx, signature, sig_len, msg, msg_len);
    }

    start = get_time_ms();
    for (i = 0; i < iterations; i++) {
        EVP_MD_CTX_reset(mdctx);
        if (EVP_DigestVerifyInit(mdctx, NULL, NULL, NULL, pkey) <= 0) goto cleanup;
        if (EVP_DigestVerify(mdctx, signature, sig_len, msg, msg_len) <= 0) goto cleanup;
    }
    result.verify_ms = (get_time_ms() - start) / iterations;

cleanup:
    if (mdctx) EVP_MD_CTX_free(mdctx);
    if (ctx) EVP_PKEY_CTX_free(ctx);
    if (pkey) EVP_PKEY_free(pkey);
    if (signature) free(signature);

    return result;
}

static BenchResult benchmark_qudo(const char *alg_name, int iterations) {
    BenchResult result = {0};
    QUDO_SLHDSA *sig = QUDO_SLHDSA_new(alg_name);
    if (!sig) return result;

    uint8_t *pk = malloc(sig->length_public_key);
    uint8_t *sk = malloc(sig->length_secret_key);
    uint8_t *signature = malloc(sig->length_signature);
    const uint8_t msg[] = "Benchmark test message";
    size_t msg_len = sizeof(msg) - 1;
    size_t sig_len;
    double start;

    for (int i = 0; i < 2; i++) {
        QUDO_SLHDSA_keypair(sig, pk, sk);
    }

    start = get_time_ms();
    for (int i = 0; i < iterations; i++) {
        QUDO_SLHDSA_keypair(sig, pk, sk);
    }
    result.keygen_ms = (get_time_ms() - start) / iterations;

    for (int i = 0; i < 2; i++) {
        sig_len = sig->length_signature;
        QUDO_SLHDSA_sign(sig, signature, &sig_len, msg, msg_len, sk);
    }

    start = get_time_ms();
    for (int i = 0; i < iterations; i++) {
        sig_len = sig->length_signature;
        QUDO_SLHDSA_sign(sig, signature, &sig_len, msg, msg_len, sk);
    }
    result.sign_ms = (get_time_ms() - start) / iterations;

    sig_len = sig->length_signature;
    QUDO_SLHDSA_sign(sig, signature, &sig_len, msg, msg_len, sk);

    for (int i = 0; i < 2; i++) {
        QUDO_SLHDSA_verify(sig, msg, msg_len, signature, sig_len, pk);
    }

    start = get_time_ms();
    for (int i = 0; i < iterations; i++) {
        QUDO_SLHDSA_verify(sig, msg, msg_len, signature, sig_len, pk);
    }
    result.verify_ms = (get_time_ms() - start) / iterations;

    free(pk);
    free(sk);
    free(signature);
    QUDO_SLHDSA_free(sig);

    return result;
}

static void print_comparison(const char *alg_name, BenchResult openssl, BenchResult qudo_prov, BenchResult qudo_dir) {
    double prov_keygen_speedup = openssl.keygen_ms / qudo_prov.keygen_ms;
    double prov_sign_speedup = openssl.sign_ms / qudo_prov.sign_ms;
    double prov_verify_speedup = openssl.verify_ms / qudo_prov.verify_ms;

    double dir_keygen_speedup = openssl.keygen_ms / qudo_dir.keygen_ms;
    double dir_sign_speedup = openssl.sign_ms / qudo_dir.sign_ms;
    double dir_verify_speedup = openssl.verify_ms / qudo_dir.verify_ms;

    printf("%-20s | %6.2f | %6.2f | %5.2fx | %6.2f | %5.2fx | %7.2f | %7.2f | %5.2fx | %7.2f | %5.2fx | %6.2f | %6.2f | %5.2fx | %6.2f | %5.2fx\n",
           alg_name,
           openssl.keygen_ms, qudo_prov.keygen_ms, prov_keygen_speedup, qudo_dir.keygen_ms, dir_keygen_speedup,
           openssl.sign_ms, qudo_prov.sign_ms, prov_sign_speedup, qudo_dir.sign_ms, dir_sign_speedup,
           openssl.verify_ms, qudo_prov.verify_ms, prov_verify_speedup, qudo_dir.verify_ms, dir_verify_speedup);
}

int main(void) {
    OSSL_PROVIDER *qudo_prov = NULL;
    OSSL_PROVIDER *default_prov = NULL;

    qudo_prov = OSSL_PROVIDER_load(NULL, "qudoprovider");
    if (!qudo_prov) {
        fprintf(stderr, "Warning: Failed to load qudoprovider - skipping provider benchmarks\n");
        fprintf(stderr, "Set OPENSSL_MODULES environment variable to qudo-provider/build/lib\n");
    }

    default_prov = OSSL_PROVIDER_load(NULL, "default");

    const char *fast_variants[] = {
        "SLH-DSA-SHA2-128f",
        "SLH-DSA-SHAKE-128f",
        "SLH-DSA-SHA2-192f",
        "SLH-DSA-SHAKE-192f",
        "SLH-DSA-SHA2-256f",
        "SLH-DSA-SHAKE-256f",
    };

    const char *small_variants[] = {
        "SLH-DSA-SHA2-128s",
        "SLH-DSA-SHAKE-128s",
        "SLH-DSA-SHA2-192s",
        "SLH-DSA-SHAKE-192s",
        "SLH-DSA-SHA2-256s",
        "SLH-DSA-SHAKE-256s",
    };

    int fast_iterations = 20;
    int small_iterations = 3;
#define BENCH_ROUNDS 3
    int rounds = BENCH_ROUNDS;

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════╗\n");
    printf("║                                    3-Way SLH-DSA Performance Comparison: OpenSSL Native vs QUDO Provider vs QUDO Direct                                                ║\n");
    printf("╚════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════╝\n\n");

    if (qudo_prov) {
        printf("✓ QUDO Provider loaded successfully\n\n");
    } else {
        printf("✗ QUDO Provider not available - showing OpenSSL vs QUDO Direct only\n\n");
    }

    printf("Fast Variants (f) - Larger signatures, faster signing:\n");
    printf("                     │      Keygen (ms)                   │            Sign (ms)                      │          Verify (ms)                  \n");
    printf("Algorithm            │ OpenSSL│ Q-Prov │ Spd │ Q-Dir  │ Spd │ OpenSSL │ Q-Prov  │ Spd │ Q-Dir   │ Spd │ OpenSSL│ Q-Prov │ Spd │ Q-Dir  │ Spd\n");
    printf("─────────────────────┼────────┼────────┼─────┼────────┼─────┼─────────┼─────────┼─────┼─────────┼─────┼────────┼────────┼─────┼────────┼─────\n");

    for (int i = 0; i < 6; i++) {

        BenchResult openssl_results[BENCH_ROUNDS];
        BenchResult qudo_prov_results[BENCH_ROUNDS];
        BenchResult qudo_dir_results[BENCH_ROUNDS];

        for (int r = 0; r < rounds; r++) {
            openssl_results[r] = benchmark_openssl(fast_variants[i], fast_iterations);
            if (qudo_prov) {
                qudo_prov_results[r] = benchmark_qudo_provider(fast_variants[i], fast_iterations, qudo_prov);
            } else {
                memset(&qudo_prov_results[r], 0, sizeof(BenchResult));
            }
            qudo_dir_results[r] = benchmark_qudo(fast_variants[i], fast_iterations);
        }

        for (int a = 0; a < rounds - 1; a++) {
            for (int b = a + 1; b < rounds; b++) {
                if (openssl_results[a].sign_ms > openssl_results[b].sign_ms) {
                    BenchResult tmp = openssl_results[a];
                    openssl_results[a] = openssl_results[b];
                    openssl_results[b] = tmp;
                }
                if (qudo_prov && qudo_prov_results[a].sign_ms > qudo_prov_results[b].sign_ms) {
                    BenchResult tmp = qudo_prov_results[a];
                    qudo_prov_results[a] = qudo_prov_results[b];
                    qudo_prov_results[b] = tmp;
                }
                if (qudo_dir_results[a].sign_ms > qudo_dir_results[b].sign_ms) {
                    BenchResult tmp = qudo_dir_results[a];
                    qudo_dir_results[a] = qudo_dir_results[b];
                    qudo_dir_results[b] = tmp;
                }
            }
        }

        BenchResult openssl = openssl_results[rounds / 2];
        BenchResult qudo_prov_res = qudo_prov_results[rounds / 2];
        BenchResult qudo_dir = qudo_dir_results[rounds / 2];
        print_comparison(fast_variants[i], openssl, qudo_prov_res, qudo_dir);
    }

    printf("\n");
    printf("Small Variants (s) - Smaller signatures, slower signing:\n");
    printf("                     │      Keygen (ms)                   │            Sign (ms)                      │          Verify (ms)                  \n");
    printf("Algorithm            │ OpenSSL│ Q-Prov │ Spd │ Q-Dir  │ Spd │ OpenSSL │ Q-Prov  │ Spd │ Q-Dir   │ Spd │ OpenSSL│ Q-Prov │ Spd │ Q-Dir  │ Spd\n");
    printf("─────────────────────┼────────┼────────┼─────┼────────┼─────┼─────────┼─────────┼─────┼─────────┼─────┼────────┼────────┼─────┼────────┼─────\n");

    for (int i = 0; i < 6; i++) {

        BenchResult openssl_results[BENCH_ROUNDS];
        BenchResult qudo_prov_results[BENCH_ROUNDS];
        BenchResult qudo_dir_results[BENCH_ROUNDS];

        for (int r = 0; r < rounds; r++) {
            openssl_results[r] = benchmark_openssl(small_variants[i], small_iterations);
            if (qudo_prov) {
                qudo_prov_results[r] = benchmark_qudo_provider(small_variants[i], small_iterations, qudo_prov);
            } else {
                memset(&qudo_prov_results[r], 0, sizeof(BenchResult));
            }
            qudo_dir_results[r] = benchmark_qudo(small_variants[i], small_iterations);
        }

        for (int a = 0; a < rounds - 1; a++) {
            for (int b = a + 1; b < rounds; b++) {
                if (openssl_results[a].sign_ms > openssl_results[b].sign_ms) {
                    BenchResult tmp = openssl_results[a];
                    openssl_results[a] = openssl_results[b];
                    openssl_results[b] = tmp;
                }
                if (qudo_prov && qudo_prov_results[a].sign_ms > qudo_prov_results[b].sign_ms) {
                    BenchResult tmp = qudo_prov_results[a];
                    qudo_prov_results[a] = qudo_prov_results[b];
                    qudo_prov_results[b] = tmp;
                }
                if (qudo_dir_results[a].sign_ms > qudo_dir_results[b].sign_ms) {
                    BenchResult tmp = qudo_dir_results[a];
                    qudo_dir_results[a] = qudo_dir_results[b];
                    qudo_dir_results[b] = tmp;
                }
            }
        }

        BenchResult openssl = openssl_results[rounds / 2];
        BenchResult qudo_prov_res = qudo_prov_results[rounds / 2];
        BenchResult qudo_dir = qudo_dir_results[rounds / 2];
        print_comparison(small_variants[i], openssl, qudo_prov_res, qudo_dir);
    }

    printf("\n");
    printf("Speedup > 1.0 means QUDO is faster than OpenSSL\n");
    printf("Median of %d rounds shown for stability\n", rounds);
    printf("Note: 's' variants use %d iterations (slower), 'f' variants use %d iterations\n\n", small_iterations, fast_iterations);

    printf("Legend:\n");
    printf("  OpenSSL = OpenSSL 3.6.1 native SLH-DSA (via EVP API)\n");
    if (qudo_prov) {
        printf("  Q-Prov  = QUDO crypto via OpenSSL provider (via EVP API)\n");
    } else {
        printf("  Q-Prov  = Not available (set OPENSSL_MODULES)\n");
    }
    printf("  Q-Dir   = QUDO crypto via direct native API (no EVP overhead)\n\n");

    printf("QUDO Optimizations:\n");
    printf("  • SHA2:  Intel SHA-NI intrinsics (sha2_256_shani.c)\n");
    printf("  • SHAKE: OpenSSL keccak1600-x86_64.s assembly\n");
    printf("  • Build: -O3 Release optimization\n\n");

    if (qudo_prov) {
        printf("Analysis:\n");
        printf("  • Q-Prov vs OpenSSL shows crypto implementation difference (both use EVP API)\n");
        printf("  • Q-Dir vs Q-Prov shows EVP API overhead\n");
        printf("  • Q-Dir vs OpenSSL shows total speedup (better crypto + no EVP overhead)\n\n");
    }

    if (qudo_prov) OSSL_PROVIDER_unload(qudo_prov);
    if (default_prov) OSSL_PROVIDER_unload(default_prov);

    return 0;
}
