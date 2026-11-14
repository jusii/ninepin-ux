/**
 * this file is part of ninepin-ux; released under the terms of the Eclipse
 * Public License 2.0 (EPL-2.0), https://spdx.org/licenses/EPL-2.0
 *
 * mbed tls, as little of it as the firmware download (net_update.c) needs: a
 * tls 1.2 client that talks to github's servers, and ecdsa p-256 to check our
 * own signature on what came. the servers' certificates are parsed but not
 * verified: the signature is what the download is trusted by, so there are no
 * root certificates here and no clock.
 */

#ifndef _MBEDTLS_CONFIG_H
#define _MBEDTLS_CONFIG_H

// entropy from the chip (pico_mbedtls.c: mbedtls_hardware_poll -> pico_rand)
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_CTR_DRBG_C

// memory from lwip's heap (altcp_tls_mbedtls_mem.c): everything runs in the
// cyw43 background context, where malloc must not be called
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY

// altcp_tls_mbedtls.c stamps its sessions (time(); nothing here depends on
// the clock being right, and no certificate date is checked)
#define MBEDTLS_HAVE_TIME
#define MBEDTLS_PLATFORM_MS_TIME_ALT     // mbedtls_ms_time() in net_update.c

// altcp_tls_mbedtls.c reaches into the library's structures
#define MBEDTLS_ALLOW_PRIVATE_ACCESS

// tls 1.2 client, ecdhe with aes-gcm, rsa or ecdsa server certificates
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_SERVER_NAME_INDICATION
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
// a server may send records of up to 16K whatever we ask for; we send little
#define MBEDTLS_SSL_IN_CONTENT_LEN  16384
#define MBEDTLS_SSL_OUT_CONTENT_LEN 2048

#define MBEDTLS_AES_C
#define MBEDTLS_AES_FEWER_TABLES
#define MBEDTLS_GCM_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_MD_C
#define MBEDTLS_SHA1_C
#define MBEDTLS_SHA224_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA256_SMALLER
#define MBEDTLS_SHA384_C
#define MBEDTLS_SHA512_C

#define MBEDTLS_BIGNUM_C
#define MBEDTLS_ECP_C
// the nist curves' fast reduction: the handshake's arithmetic keeps the main
// loop waiting whilst it runs
#define MBEDTLS_ECP_NIST_OPTIM
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PKCS1_V21

#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_OID_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C

#endif // _MBEDTLS_CONFIG_H
