/*
 * uls_mbedtls_user.h - additions to the SDK's mbedTLS config (mbedtls-port/inc/tls_config.h),
 * included through MBEDTLS_USER_CONFIG_FILE just before check_config.h.
 *
 * The SDK's active config has the TLS protocol switched off (SSL_TLS_C, SSL_CLI_C,
 * PROTO_TLS1_2 commented out). The spec (§8) needs a TLS 1.2 client with
 * ECDHE-ECDSA/ECDHE-RSA + AES-GCM and certificate verification against a pasted CA.
 * MBEDTLS_HAVE_TIME stays off: certificate dates are checked in net_mqtt.c against SNTP time.
 * scripts/build.sh rebuilds libmbedtls whenever this file changes.
 */
#ifndef ULS_MBEDTLS_USER_H
#define ULS_MBEDTLS_USER_H

#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_SERVER_NAME_INDICATION

#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ASN1_WRITE_C            /* required by ECDSA */
#define MBEDTLS_GCM_C
#define MBEDTLS_SHA512_C                /* SHA-384 signatures (e.g. P-384 CAs) */

/* Record buffers: a 16 KB input record must be accepted (no max_fragment_length
 * negotiation assumed); our own records are small. */
#define MBEDTLS_SSL_IN_CONTENT_LEN      16384
#define MBEDTLS_SSL_OUT_CONTENT_LEN     2048

#endif
