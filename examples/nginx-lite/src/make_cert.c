#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>
#include <mbedtls/x509_crt.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 4) { fprintf(stderr, "Usage: nginx-lite-cert <domain> <cert.pem> <key.pem>\n"); return 2; }
    mbedtls_entropy_context entropy; mbedtls_ctr_drbg_context rng; mbedtls_pk_context key;
    mbedtls_x509write_cert crt; mbedtls_entropy_init(&entropy); mbedtls_ctr_drbg_init(&rng);
    mbedtls_pk_init(&key); mbedtls_x509write_crt_init(&crt);
    const char *pers = "nginx-lite-cert";
    int rc = mbedtls_ctr_drbg_seed(&rng, mbedtls_entropy_func, &entropy, (const unsigned char *)pers, strlen(pers));
    if (!rc) rc = mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA));
    if (!rc) rc = mbedtls_rsa_gen_key(mbedtls_pk_rsa(key), mbedtls_ctr_drbg_random, &rng, 2048, 65537);
    if (!rc) {
        mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
        mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
        mbedtls_x509write_crt_set_subject_key(&crt, &key); mbedtls_x509write_crt_set_issuer_key(&crt, &key);
        char subject[300];
        if (strlen(argv[1]) > 240) rc = MBEDTLS_ERR_X509_BAD_INPUT_DATA;
        else { snprintf(subject, sizeof(subject), "CN=%s,O=Steward local", argv[1]); rc = mbedtls_x509write_crt_set_subject_name(&crt, subject); }
    }
    if (!rc) { char issuer[300]; snprintf(issuer, sizeof(issuer), "CN=%s,O=Steward local", argv[1]); rc = mbedtls_x509write_crt_set_issuer_name(&crt, issuer); }
    if (!rc) rc = mbedtls_x509write_crt_set_validity(&crt, "20250101000000", "20350101000000");
    unsigned char serial[16]; if (!rc) rc = mbedtls_ctr_drbg_random(&rng, serial, sizeof(serial));
    serial[0] &= 0x7f; serial[0] |= 1;
    if (!rc) rc = mbedtls_x509write_crt_set_serial_raw(&crt, serial, sizeof(serial));
    if (!rc) rc = mbedtls_x509write_crt_set_basic_constraints(&crt, 0, -1);
    if (!rc) rc = mbedtls_x509write_crt_set_key_usage(&crt, MBEDTLS_X509_KU_DIGITAL_SIGNATURE | MBEDTLS_X509_KU_KEY_ENCIPHERMENT);
    mbedtls_x509_san_list san = {0}; san.node.type = MBEDTLS_X509_SAN_DNS_NAME;
    san.node.san.unstructured_name.tag = MBEDTLS_ASN1_CONTEXT_SPECIFIC | MBEDTLS_X509_SAN_DNS_NAME;
    san.node.san.unstructured_name.p = (unsigned char *)argv[1]; san.node.san.unstructured_name.len = strlen(argv[1]);
    if (!rc) rc = mbedtls_x509write_crt_set_subject_alternative_name(&crt, &san);
    unsigned char cert_pem[8192], key_pem[4096];
    if (!rc) rc = mbedtls_x509write_crt_pem(&crt, cert_pem, sizeof(cert_pem), mbedtls_ctr_drbg_random, &rng);
    if (!rc) rc = mbedtls_pk_write_key_pem(&key, key_pem, sizeof(key_pem));
    if (!rc) {
        FILE *f = fopen(argv[2], "wb"); if (!f || fwrite(cert_pem, 1, strlen((char *)cert_pem), f) != strlen((char *)cert_pem)) rc = -1; if (f) fclose(f);
        f = fopen(argv[3], "wb"); if (!f || fwrite(key_pem, 1, strlen((char *)key_pem), f) != strlen((char *)key_pem)) rc = -1; if (f) fclose(f);
    }
    if (rc) fprintf(stderr, "Certificate generation failed: -0x%04x\n", (unsigned)-rc);
    mbedtls_x509write_crt_free(&crt); mbedtls_pk_free(&key); mbedtls_ctr_drbg_free(&rng); mbedtls_entropy_free(&entropy);
    return rc ? 1 : 0;
}
