/*
 * QudoSSL PQC demo -- exercises qudo-pqc's delegated ML-KEM and ML-DSA through
 * OpenSSL's EVP API. Build against the qudossl tree, run with the qudossl libs.
 */
#include <openssl/evp.h>
#include <openssl/params.h>
#include <string.h>
#include <stdio.h>

static EVP_PKEY *keygen(const char *alg){
    EVP_PKEY *k=NULL; EVP_PKEY_CTX*c=EVP_PKEY_CTX_new_from_name(NULL,alg,NULL);
    if(c&&EVP_PKEY_keygen_init(c)>0) EVP_PKEY_keygen(c,&k);
    EVP_PKEY_CTX_free(c); return k;
}

static int mlkem_demo(void){
    printf("[ML-KEM-768] "); EVP_PKEY *k=keygen("ML-KEM-768"); if(!k){printf("keygen FAIL\n");return 1;}
    unsigned char ct[2048],sa[32],sb[32]; size_t cl=sizeof ct,al=32,bl=32;
    EVP_PKEY_CTX*e=EVP_PKEY_CTX_new_from_pkey(NULL,k,NULL);
    EVP_PKEY_encapsulate_init(e,NULL); EVP_PKEY_encapsulate(e,ct,&cl,sa,&al);
    EVP_PKEY_CTX*d=EVP_PKEY_CTX_new_from_pkey(NULL,k,NULL);
    EVP_PKEY_decapsulate_init(d,NULL); EVP_PKEY_decapsulate(d,sb,&bl,ct,cl);
    int ok=(al==bl&&memcmp(sa,sb,al)==0);
    printf("keygen+encaps+decaps: shared secret %s", ok?"MATCHES":"MISMATCH");
    EVP_PKEY *dup=EVP_PKEY_dup(k);           /* K1 fix */
    printf("  | EVP_PKEY_dup: %s\n", dup?"OK":"NULL(bug)");
    EVP_PKEY_free(k);EVP_PKEY_free(dup);EVP_PKEY_CTX_free(e);EVP_PKEY_CTX_free(d);
    return ok&&dup?0:1;
}

static int mldsa_demo(void){
    printf("[ML-DSA-65] "); EVP_PKEY *k=keygen("ML-DSA-65"); if(!k){printf("keygen FAIL\n");return 1;}
    const char *msg="qudossl post-quantum signature demo";
    unsigned char sig[5000]; size_t sl=sizeof sig;
    EVP_MD_CTX*sc=EVP_MD_CTX_new();
    EVP_DigestSignInit_ex(sc,NULL,NULL,NULL,NULL,k,NULL);
    EVP_DigestSign(sc,sig,&sl,(const unsigned char*)msg,strlen(msg));
    EVP_MD_CTX*vc=EVP_MD_CTX_new();
    EVP_DigestVerifyInit_ex(vc,NULL,NULL,NULL,NULL,k,NULL);
    int v=EVP_DigestVerify(vc,sig,sl,(const unsigned char*)msg,strlen(msg));
    printf("sign+verify: %s (sig %zu bytes)", v==1?"VALID":"INVALID", sl);
    EVP_PKEY *dup=EVP_PKEY_dup(k);
    printf("  | EVP_PKEY_dup: %s\n", dup?"OK":"NULL");
    EVP_MD_CTX_free(sc);EVP_MD_CTX_free(vc);EVP_PKEY_free(k);EVP_PKEY_free(dup);
    return v==1?0:1;
}

int main(void){
    printf("=== QudoSSL PQC demo (%s) ===\n", OpenSSL_version(OPENSSL_VERSION));
    int rc = mlkem_demo() | mldsa_demo();
    printf("=== %s ===\n", rc==0?"ALL DEMOS PASSED":"DEMO FAILED");
    return rc;
}
