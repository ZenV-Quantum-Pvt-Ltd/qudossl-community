/* SPDX-License-Identifier: Apache-2.0 AND MIT */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <openssl/evp.h>
#include <openssl/provider.h>

#include "mldsa_wrapper.h"

#define NUM_ITERATIONS 1000

static double get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
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
    const uint8_t msg[] = "Benchmark test message for ML-DSA comparison";
    size_t msg_len = sizeof(msg) - 1;
    size_t sig_len = 0;
    uint8_t *signature = NULL;
    double start;
    int i;

    for (i = 0; i < 5; i++) {
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

    for (i = 0; i < 5; i++) {
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

    for (i = 0; i < 5; i++) {
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

static BenchResult benchmark_qudo_provider(const char *alg_name, int iterations) {
    BenchResult result = {0};
    EVP_PKEY *pkey = NULL;
    EVP_PKEY_CTX *ctx = NULL;
    EVP_MD_CTX *mdctx = NULL;
    const uint8_t msg[] = "Benchmark test message for ML-DSA comparison";
    size_t msg_len = sizeof(msg) - 1;
    size_t sig_len = 0;
    uint8_t *signature = NULL;
    double start;
    int i;

    for (i = 0; i < 5; i++) {
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

    for (i = 0; i < 5; i++) {
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

    for (i = 0; i < 5; i++) {
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

static BenchResult benchmark_qudo_direct(const char *alg_name, size_t pk_bytes,
                                          size_t sk_bytes, size_t sig_bytes,
                                          int iterations) {
    BenchResult result = {0};

    QUDO_MLDSA *sig = QUDO_MLDSA_new(alg_name);
    if (!sig) {
        fprintf(stderr, "ERROR: Failed to create QUDO %s instance\n", alg_name);
        return result;
    }

    uint8_t *pk = malloc(pk_bytes);
    uint8_t *sk = malloc(sk_bytes);
    uint8_t *signature = malloc(sig_bytes);
    const uint8_t msg[] = "Benchmark test message for ML-DSA comparison";
    size_t msg_len = sizeof(msg) - 1;
    size_t sig_len;
    double start;

    for (int i = 0; i < 10; i++) {
        QUDO_MLDSA_keypair(sig, pk, sk);
    }

    start = get_time_ms();
    for (int i = 0; i < iterations; i++) {
        QUDO_MLDSA_keypair(sig, pk, sk);
    }
    result.keygen_ms = (get_time_ms() - start) / iterations;

    QUDO_MLDSA_keypair(sig, pk, sk);

    for (int i = 0; i < 10; i++) {
        QUDO_MLDSA_sign(sig, signature, &sig_len, msg, msg_len, sk);
    }

    start = get_time_ms();
    for (int i = 0; i < iterations; i++) {
        QUDO_MLDSA_sign(sig, signature, &sig_len, msg, msg_len, sk);
    }
    result.sign_ms = (get_time_ms() - start) / iterations;

    QUDO_MLDSA_sign(sig, signature, &sig_len, msg, msg_len, sk);

    for (int i = 0; i < 10; i++) {
        QUDO_MLDSA_verify(sig, signature, sig_len, msg, msg_len, pk);
    }

    start = get_time_ms();
    for (int i = 0; i < iterations; i++) {
        QUDO_MLDSA_verify(sig, signature, sig_len, msg, msg_len, pk);
    }
    result.verify_ms = (get_time_ms() - start) / iterations;

    free(pk);
    free(sk);
    free(signature);
    QUDO_MLDSA_free(sig);

    return result;
}

static void print_comparison(const char *alg_name, BenchResult openssl,
                              BenchResult qudo_prov, BenchResult qudo_dir) {
    double prov_keygen_spd = openssl.keygen_ms / qudo_prov.keygen_ms;
    double prov_sign_spd = openssl.sign_ms / qudo_prov.sign_ms;
    double prov_verify_spd = openssl.verify_ms / qudo_prov.verify_ms;

    double dir_keygen_spd = openssl.keygen_ms / qudo_dir.keygen_ms;
    double dir_sign_spd = openssl.sign_ms / qudo_dir.sign_ms;
    double dir_verify_spd = openssl.verify_ms / qudo_dir.verify_ms;

    printf("%-12s | %7.3f | %7.3f | %5.2fx | %7.3f | %5.2fx | %7.3f | %7.3f | %5.2fx | %7.3f | %5.2fx | %7.3f | %7.3f | %5.2fx | %7.3f | %5.2fx\n",
           alg_name,
           openssl.keygen_ms, qudo_prov.keygen_ms, prov_keygen_spd, qudo_dir.keygen_ms, dir_keygen_spd,
           openssl.sign_ms, qudo_prov.sign_ms, prov_sign_spd, qudo_dir.sign_ms, dir_sign_spd,
           openssl.verify_ms, qudo_prov.verify_ms, prov_verify_spd, qudo_dir.verify_ms, dir_verify_spd);
}

int main(void) {
    OSSL_PROVIDER *qudo_prov = NULL;
    OSSL_PROVIDER *default_prov = NULL;

    QUDO_MLDSA_init();

    qudo_prov = OSSL_PROVIDER_load(NULL, "qudoprovider");
    if (!qudo_prov) {
        fprintf(stderr, "Warning: Failed to load qudoprovider - skipping provider benchmarks\n");
        fprintf(stderr, "Set OPENSSL_MODULES environment variable to qudo-provider/build/lib\n");
    }

    default_prov = OSSL_PROVIDER_load(NULL, "default");

    int iterations = NUM_ITERATIONS;
    int rounds = 3;

    printf("\n");
    printf("=============================================================================================================================================================\n");
    printf("                           3-Way ML-DSA Performance Comparison: OpenSSL Native vs QUDO Provider vs QUDO Direct\n");
    printf("=============================================================================================================================================================\n\n");

    if (qudo_prov) {
        printf("  QUDO Provider loaded successfully\n\n");
    } else {
        printf("  QUDO Provider not available - showing OpenSSL vs QUDO Direct only\n\n");
    }

    printf("Iterations: %d, Rounds: %d (median)\n\n", iterations, rounds);

    printf("             |       Keygen (ms)                           |          Sign (ms)                            |         Verify (ms)\n");
    printf("Algorithm    | OpenSSL | Q-Prov  | Spd   | Q-Dir   | Spd   | OpenSSL | Q-Prov  | Spd   | Q-Dir   | Spd   | OpenSSL | Q-Prov  | Spd   | Q-Dir   | Spd\n");
    printf("-------------+---------+---------+-------+---------+-------+---------+---------+-------+---------+-------+---------+---------+-------+---------+------\n");

    struct {
        const char *name;
        size_t pk_bytes;
        size_t sk_bytes;
        size_t sig_bytes;
    } algorithms[] = {
        { "ML-DSA-44", ML_DSA_44_PUBLIC_KEY_BYTES, ML_DSA_44_SECRET_KEY_BYTES, ML_DSA_44_SIGNATURE_BYTES },
        { "ML-DSA-65", ML_DSA_65_PUBLIC_KEY_BYTES, ML_DSA_65_SECRET_KEY_BYTES, ML_DSA_65_SIGNATURE_BYTES },
        { "ML-DSA-87", ML_DSA_87_PUBLIC_KEY_BYTES, ML_DSA_87_SECRET_KEY_BYTES, ML_DSA_87_SIGNATURE_BYTES },
    };

    for (int a = 0; a < 3; a++) {
        BenchResult openssl_results[3];
        BenchResult qudo_prov_results[3];
        BenchResult qudo_dir_results[3];

        for (int r = 0; r < rounds; r++) {
            openssl_results[r] = benchmark_openssl(algorithms[a].name, iterations);
            if (qudo_prov) {
                qudo_prov_results[r] = benchmark_qudo_provider(algorithms[a].name, iterations);
            } else {
                memset(&qudo_prov_results[r], 0, sizeof(BenchResult));
            }
            qudo_dir_results[r] = benchmark_qudo_direct(algorithms[a].name,
                algorithms[a].pk_bytes, algorithms[a].sk_bytes,
                algorithms[a].sig_bytes, iterations);
        }

        for (int i = 0; i < rounds - 1; i++) {
            for (int j = i + 1; j < rounds; j++) {
                if (openssl_results[i].sign_ms > openssl_results[j].sign_ms) {
                    BenchResult tmp = openssl_results[i];
                    openssl_results[i] = openssl_results[j];
                    openssl_results[j] = tmp;
                }
                if (qudo_prov && qudo_prov_results[i].sign_ms > qudo_prov_results[j].sign_ms) {
                    BenchResult tmp = qudo_prov_results[i];
                    qudo_prov_results[i] = qudo_prov_results[j];
                    qudo_prov_results[j] = tmp;
                }
                if (qudo_dir_results[i].sign_ms > qudo_dir_results[j].sign_ms) {
                    BenchResult tmp = qudo_dir_results[i];
                    qudo_dir_results[i] = qudo_dir_results[j];
                    qudo_dir_results[j] = tmp;
                }
            }
        }

        print_comparison(algorithms[a].name,
            openssl_results[rounds / 2],
            qudo_prov_results[rounds / 2],
            qudo_dir_results[rounds / 2]);
    }

    printf("\n");
    printf("Speedup > 1.0 means QUDO is faster than OpenSSL\n");
    printf("Median of %d rounds shown for stability\n\n", rounds);

    printf("Legend:\n");
    printf("  OpenSSL = OpenSSL native ML-DSA (via EVP API)\n");
    if (qudo_prov) {
        printf("  Q-Prov  = QUDO crypto via OpenSSL provider (via EVP API)\n");
    } else {
        printf("  Q-Prov  = Not available (set OPENSSL_MODULES)\n");
    }
    printf("  Q-Dir   = QUDO crypto via direct native API (no EVP overhead)\n\n");

    printf("Analysis:\n");
    printf("  Q-Prov vs OpenSSL shows crypto implementation difference (both use EVP API)\n");
    printf("  Q-Dir vs Q-Prov shows EVP API overhead\n");
    printf("  Q-Dir vs OpenSSL shows total speedup (better crypto + no EVP overhead)\n\n");

    if (qudo_prov) OSSL_PROVIDER_unload(qudo_prov);
    if (default_prov) OSSL_PROVIDER_unload(default_prov);

    return 0;
}
