/* SPDX-License-Identifier: Apache-2.0 AND MIT */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <openssl/evp.h>
#include <openssl/provider.h>

#include "mlkem_wrapper.h"

#define NUM_ITERATIONS 1000

static double get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

typedef struct {
    double keygen_ms;
    double encaps_ms;
    double decaps_ms;
} BenchResult;

static BenchResult benchmark_openssl(const char *alg_name, int iterations) {
    BenchResult result = {0};
    EVP_PKEY *pkey = NULL;
    EVP_PKEY_CTX *ctx = NULL;
    unsigned char *ct = NULL, *ss_enc = NULL, *ss_dec = NULL;
    size_t ct_len = 0, ss_enc_len = 0, ss_dec_len = 0;
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
    if (!ctx) goto cleanup;
    if (EVP_PKEY_encapsulate_init(ctx, NULL) <= 0) goto cleanup;
    if (EVP_PKEY_encapsulate(ctx, NULL, &ct_len, NULL, &ss_enc_len) <= 0) goto cleanup;
    ct = malloc(ct_len);
    ss_enc = malloc(ss_enc_len);

    for (i = 0; i < 5; i++) {
        size_t c = ct_len, s = ss_enc_len;
        EVP_PKEY_encapsulate(ctx, ct, &c, ss_enc, &s);
    }

    start = get_time_ms();
    for (i = 0; i < iterations; i++) {
        size_t c = ct_len, s = ss_enc_len;
        if (EVP_PKEY_encapsulate(ctx, ct, &c, ss_enc, &s) <= 0) goto cleanup;
    }
    result.encaps_ms = (get_time_ms() - start) / iterations;
    EVP_PKEY_CTX_free(ctx);
    ctx = NULL;

    ctx = EVP_PKEY_CTX_new_from_pkey(NULL, pkey, "provider=default");
    if (!ctx) goto cleanup;
    if (EVP_PKEY_decapsulate_init(ctx, NULL) <= 0) goto cleanup;
    if (EVP_PKEY_decapsulate(ctx, NULL, &ss_dec_len, ct, ct_len) <= 0) goto cleanup;
    ss_dec = malloc(ss_dec_len);

    for (i = 0; i < 5; i++) {
        size_t s = ss_dec_len;
        EVP_PKEY_decapsulate(ctx, ss_dec, &s, ct, ct_len);
    }

    start = get_time_ms();
    for (i = 0; i < iterations; i++) {
        size_t s = ss_dec_len;
        if (EVP_PKEY_decapsulate(ctx, ss_dec, &s, ct, ct_len) <= 0) goto cleanup;
    }
    result.decaps_ms = (get_time_ms() - start) / iterations;

cleanup:
    if (ctx) EVP_PKEY_CTX_free(ctx);
    if (pkey) EVP_PKEY_free(pkey);
    free(ct);
    free(ss_enc);
    free(ss_dec);

    return result;
}

static BenchResult benchmark_qudo_provider(const char *alg_name, int iterations) {
    BenchResult result = {0};
    EVP_PKEY *pkey = NULL;
    EVP_PKEY_CTX *ctx = NULL;
    unsigned char *ct = NULL, *ss_enc = NULL, *ss_dec = NULL;
    size_t ct_len = 0, ss_enc_len = 0, ss_dec_len = 0;
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

    ctx = EVP_PKEY_CTX_new_from_pkey(NULL, pkey, "provider=qudoprovider");
    if (!ctx) goto cleanup;
    if (EVP_PKEY_encapsulate_init(ctx, NULL) <= 0) goto cleanup;
    if (EVP_PKEY_encapsulate(ctx, NULL, &ct_len, NULL, &ss_enc_len) <= 0) goto cleanup;
    ct = malloc(ct_len);
    ss_enc = malloc(ss_enc_len);

    for (i = 0; i < 5; i++) {
        size_t c = ct_len, s = ss_enc_len;
        EVP_PKEY_encapsulate(ctx, ct, &c, ss_enc, &s);
    }

    start = get_time_ms();
    for (i = 0; i < iterations; i++) {
        size_t c = ct_len, s = ss_enc_len;
        if (EVP_PKEY_encapsulate(ctx, ct, &c, ss_enc, &s) <= 0) goto cleanup;
    }
    result.encaps_ms = (get_time_ms() - start) / iterations;
    EVP_PKEY_CTX_free(ctx);
    ctx = NULL;

    ctx = EVP_PKEY_CTX_new_from_pkey(NULL, pkey, "provider=qudoprovider");
    if (!ctx) goto cleanup;
    if (EVP_PKEY_decapsulate_init(ctx, NULL) <= 0) goto cleanup;
    if (EVP_PKEY_decapsulate(ctx, NULL, &ss_dec_len, ct, ct_len) <= 0) goto cleanup;
    ss_dec = malloc(ss_dec_len);

    for (i = 0; i < 5; i++) {
        size_t s = ss_dec_len;
        EVP_PKEY_decapsulate(ctx, ss_dec, &s, ct, ct_len);
    }

    start = get_time_ms();
    for (i = 0; i < iterations; i++) {
        size_t s = ss_dec_len;
        if (EVP_PKEY_decapsulate(ctx, ss_dec, &s, ct, ct_len) <= 0) goto cleanup;
    }
    result.decaps_ms = (get_time_ms() - start) / iterations;

cleanup:
    if (ctx) EVP_PKEY_CTX_free(ctx);
    if (pkey) EVP_PKEY_free(pkey);
    free(ct);
    free(ss_enc);
    free(ss_dec);

    return result;
}

static BenchResult benchmark_qudo_direct(const char *alg_name, int iterations) {
    BenchResult result = {0};

    QUDO_KEM *kem = QUDO_KEM_new(alg_name);
    if (!kem) {
        fprintf(stderr, "ERROR: Failed to create QUDO %s instance\n", alg_name);
        return result;
    }

    uint8_t *pk = malloc(kem->length_public_key);
    uint8_t *sk = malloc(kem->length_secret_key);
    uint8_t *ct = malloc(kem->length_ciphertext);
    uint8_t *ss_enc = malloc(kem->length_shared_secret);
    uint8_t *ss_dec = malloc(kem->length_shared_secret);
    double start;

    for (int i = 0; i < 10; i++) {
        QUDO_KEM_keypair(kem, pk, sk);
    }

    start = get_time_ms();
    for (int i = 0; i < iterations; i++) {
        QUDO_KEM_keypair(kem, pk, sk);
    }
    result.keygen_ms = (get_time_ms() - start) / iterations;

    QUDO_KEM_keypair(kem, pk, sk);

    for (int i = 0; i < 10; i++) {
        QUDO_KEM_encaps(kem, ct, ss_enc, pk);
    }

    start = get_time_ms();
    for (int i = 0; i < iterations; i++) {
        QUDO_KEM_encaps(kem, ct, ss_enc, pk);
    }
    result.encaps_ms = (get_time_ms() - start) / iterations;

    QUDO_KEM_encaps(kem, ct, ss_enc, pk);

    for (int i = 0; i < 10; i++) {
        QUDO_KEM_decaps(kem, ss_dec, ct, sk);
    }

    start = get_time_ms();
    for (int i = 0; i < iterations; i++) {
        QUDO_KEM_decaps(kem, ss_dec, ct, sk);
    }
    result.decaps_ms = (get_time_ms() - start) / iterations;

    free(pk);
    free(sk);
    free(ct);
    free(ss_enc);
    free(ss_dec);
    QUDO_KEM_free(kem);

    return result;
}

static void print_comparison(const char *alg_name, BenchResult openssl,
                              BenchResult qudo_prov, BenchResult qudo_dir) {
    double prov_keygen_spd = openssl.keygen_ms / qudo_prov.keygen_ms;
    double prov_encaps_spd = openssl.encaps_ms / qudo_prov.encaps_ms;
    double prov_decaps_spd = openssl.decaps_ms / qudo_prov.decaps_ms;

    double dir_keygen_spd = openssl.keygen_ms / qudo_dir.keygen_ms;
    double dir_encaps_spd = openssl.encaps_ms / qudo_dir.encaps_ms;
    double dir_decaps_spd = openssl.decaps_ms / qudo_dir.decaps_ms;

    printf("%-12s | %7.4f | %7.4f | %5.2fx | %7.4f | %5.2fx | %7.4f | %7.4f | %5.2fx | %7.4f | %5.2fx | %7.4f | %7.4f | %5.2fx | %7.4f | %5.2fx\n",
           alg_name,
           openssl.keygen_ms, qudo_prov.keygen_ms, prov_keygen_spd, qudo_dir.keygen_ms, dir_keygen_spd,
           openssl.encaps_ms, qudo_prov.encaps_ms, prov_encaps_spd, qudo_dir.encaps_ms, dir_encaps_spd,
           openssl.decaps_ms, qudo_prov.decaps_ms, prov_decaps_spd, qudo_dir.decaps_ms, dir_decaps_spd);
}

int main(void) {
    OSSL_PROVIDER *qudo_prov = NULL;
    OSSL_PROVIDER *default_prov = NULL;

    QUDO_KEM_init();

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
    printf("                           3-Way ML-KEM Performance Comparison: OpenSSL Native vs QUDO Provider vs QUDO Direct\n");
    printf("=============================================================================================================================================================\n\n");

    if (qudo_prov) {
        printf("  QUDO Provider loaded successfully\n\n");
    } else {
        printf("  QUDO Provider not available - showing OpenSSL vs QUDO Direct only\n\n");
    }

    printf("Iterations: %d, Rounds: %d (median)\n\n", iterations, rounds);

    printf("             |       Keygen (ms)                           |         Encaps (ms)                           |        Decaps (ms)\n");
    printf("Algorithm    | OpenSSL | Q-Prov  | Spd   | Q-Dir   | Spd   | OpenSSL | Q-Prov  | Spd   | Q-Dir   | Spd   | OpenSSL | Q-Prov  | Spd   | Q-Dir   | Spd\n");
    printf("-------------+---------+---------+-------+---------+-------+---------+---------+-------+---------+-------+---------+---------+-------+---------+------\n");

    const char *algorithms[] = { "ML-KEM-512", "ML-KEM-768", "ML-KEM-1024" };

    for (int a = 0; a < 3; a++) {
        BenchResult openssl_results[3];
        BenchResult qudo_prov_results[3];
        BenchResult qudo_dir_results[3];

        for (int r = 0; r < rounds; r++) {
            openssl_results[r] = benchmark_openssl(algorithms[a], iterations);
            if (qudo_prov) {
                qudo_prov_results[r] = benchmark_qudo_provider(algorithms[a], iterations);
            } else {
                memset(&qudo_prov_results[r], 0, sizeof(BenchResult));
            }
            qudo_dir_results[r] = benchmark_qudo_direct(algorithms[a], iterations);
        }

        for (int i = 0; i < rounds - 1; i++) {
            for (int j = i + 1; j < rounds; j++) {
                if (openssl_results[i].encaps_ms > openssl_results[j].encaps_ms) {
                    BenchResult tmp = openssl_results[i];
                    openssl_results[i] = openssl_results[j];
                    openssl_results[j] = tmp;
                }
                if (qudo_prov && qudo_prov_results[i].encaps_ms > qudo_prov_results[j].encaps_ms) {
                    BenchResult tmp = qudo_prov_results[i];
                    qudo_prov_results[i] = qudo_prov_results[j];
                    qudo_prov_results[j] = tmp;
                }
                if (qudo_dir_results[i].encaps_ms > qudo_dir_results[j].encaps_ms) {
                    BenchResult tmp = qudo_dir_results[i];
                    qudo_dir_results[i] = qudo_dir_results[j];
                    qudo_dir_results[j] = tmp;
                }
            }
        }

        print_comparison(algorithms[a],
            openssl_results[rounds / 2],
            qudo_prov_results[rounds / 2],
            qudo_dir_results[rounds / 2]);
    }

    printf("\n");
    printf("Speedup > 1.0 means QUDO is faster than OpenSSL\n");
    printf("Median of %d rounds shown for stability\n\n", rounds);

    printf("Legend:\n");
    printf("  OpenSSL = OpenSSL native ML-KEM (via EVP API)\n");
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
