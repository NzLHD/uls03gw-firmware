/*
 * net_mqtt.c - see net_mqtt.h.
 *
 * TLS (spec §8): TLS 1.2 client, trust = the pasted CA only, MBEDTLS_SSL_VERIFY_REQUIRED.
 * Host name check against mqtt_host when mqtt_verify_host is on; mbedTLS 2.x only matches
 * DNS names, so for an IPv4 mqtt_host we check the certificate's iPAddress SAN ourselves.
 * Certificate dates: the device has no RTC. mbedTLS's own date check uses time(), which is
 * not set here, so its EXPIRED/FUTURE flags are ignored and the dates are checked against
 * the SNTP time instead - and skipped while the time is unknown (chain and name always).
 */
#include "include.h"
#include "rtos_pub.h"
#include "mem_pub.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"
#include "mbedtls/ssl.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/x509_crt.h"
#include "board.h"
#include "net_mqtt.h"
#include "mqtt_pkt.h"
#include "netproto.h"
#include <stdio.h>
#include <string.h>

#define RX_BUF   2048
#define TX_BUF   1536
#define IO_MS    5000

typedef struct {
    int fd;
    bool tls, open;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt ca;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_entropy_context entropy;
    uint8_t ip[4];
    bool host_is_ip, verify_ip_san;
    uint32_t unix_time;
    uint16_t next_id;
    uint16_t keepalive_s;
    uint32_t last_tx_ms;
    mq_msg_fn on_msg;
    void *ctx;
    uint8_t rx[RX_BUF];
    size_t rx_len;
    uint8_t tx[TX_BUF];
    char err[96];
} sess_t;

static sess_t S;     /* one session at a time; ~4 KB + mbedTLS contexts */

const char *mq_last_error(void) { return S.err; }
bool mq_is_open(void) { return S.open; }

/* ---------------------------------------------------------------- helpers */

static int parse_ipv4(const char *s, uint8_t ip[4])
{
    struct in_addr a;
    if (!inet_aton(s, &a)) return 0;
    memcpy(ip, &a.s_addr, 4);
    return 1;
}

static int resolve(const char *host, uint8_t ip[4])
{
    struct hostent *h;
    if (parse_ipv4(host, ip)) return 0;
    h = gethostbyname(host);
    if (!h || h->h_addrtype != AF_INET || !h->h_addr_list[0]) return -1;
    memcpy(ip, h->h_addr_list[0], 4);
    return 0;
}

static int tcp_connect(const uint8_t ip[4], uint16_t port, uint32_t timeout_ms)
{
    struct sockaddr_in sa;
    struct timeval tv = { (long)(timeout_ms / 1000), (long)(timeout_ms % 1000) * 1000 };
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    memcpy(&sa.sin_addr.s_addr, ip, 4);
    if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    int n = send(*(int *)ctx, buf, len, 0);
    return n >= 0 ? n : MBEDTLS_ERR_SSL_WANT_WRITE;
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    int n = recv(*(int *)ctx, buf, len, 0);
    if (n > 0) return n;
    if (n == 0) return MBEDTLS_ERR_SSL_CONN_EOF;
    return MBEDTLS_ERR_SSL_TIMEOUT;     /* SO_RCVTIMEO expired (callers bound the total time) */
}

/* days from 1970-01-01 (civil calendar) */
static int64_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static int64_t x509_unix(const mbedtls_x509_time *t)
{
    return days_from_civil(t->year, t->mon, t->day) * 86400 + t->hour * 3600 + t->min * 60 + t->sec;
}

static int verify_cb(void *ctx, mbedtls_x509_crt *crt, int depth, uint32_t *flags)
{
    sess_t *s = ctx;
    *flags &= ~(uint32_t)(MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE);
    if (s->unix_time) {
        int64_t now = s->unix_time;
        if (now < x509_unix(&crt->valid_from)) *flags |= MBEDTLS_X509_BADCERT_FUTURE;
        if (now > x509_unix(&crt->valid_to))   *flags |= MBEDTLS_X509_BADCERT_EXPIRED;
    }
    if (depth == 0 && s->verify_ip_san) {
        int match = 0;
        for (const mbedtls_x509_sequence *n = &crt->subject_alt_names; n && n->buf.p; n = n->next)
            if (n->buf.tag == (MBEDTLS_ASN1_CONTEXT_SPECIFIC | 7) && n->buf.len == 4 &&
                !memcmp(n->buf.p, s->ip, 4))
                match = 1;
        if (!match) *flags |= MBEDTLS_X509_BADCERT_CN_MISMATCH;
    }
    return 0;
}

static void describe_verify(uint32_t f, char *out, size_t n)
{
    snprintf(out, n, "certificate rejected:%s%s%s%s%s",
             (f & MBEDTLS_X509_BADCERT_NOT_TRUSTED) ? " not signed by the configured CA" : "",
             (f & MBEDTLS_X509_BADCERT_CN_MISMATCH) ? " name/IP does not match mqtt_host" : "",
             (f & MBEDTLS_X509_BADCERT_EXPIRED) ? " expired" : "",
             (f & MBEDTLS_X509_BADCERT_FUTURE) ? " not yet valid" : "",
             (f & ~(uint32_t)(MBEDTLS_X509_BADCERT_NOT_TRUSTED | MBEDTLS_X509_BADCERT_CN_MISMATCH |
                              MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE)) ? " (other)" : "");
}

static void tls_teardown(void)
{
    mbedtls_ssl_free(&S.ssl);
    mbedtls_ssl_config_free(&S.conf);
    mbedtls_x509_crt_free(&S.ca);
    mbedtls_ctr_drbg_free(&S.drbg);
    mbedtls_entropy_free(&S.entropy);
}

int mq_ca_check(const char *pem, char *err, size_t errlen)
{
    mbedtls_x509_crt crt;
    int rc;
    mbedtls_x509_crt_init(&crt);
    rc = mbedtls_x509_crt_parse(&crt, (const unsigned char *)pem, strlen(pem) + 1);
    mbedtls_x509_crt_free(&crt);
    if (rc != 0) {
        snprintf(err, errlen, "does not parse (mbedTLS -0x%04x)", (unsigned)-rc);
        return -1;
    }
    return 0;
}

/* ---------------------------------------------------------------- raw I/O */

static int io_write(const uint8_t *b, size_t n)
{
    while (n) {
        int w = S.tls ? mbedtls_ssl_write(&S.ssl, b, n) : send(S.fd, b, n, 0);
        if (w == MBEDTLS_ERR_SSL_WANT_WRITE || w == MBEDTLS_ERR_SSL_WANT_READ) continue;
        if (w <= 0) return -1;
        b += w;
        n -= (size_t)w;
    }
    S.last_tx_ms = board_ms();
    return 0;
}

/* Read more bytes into S.rx. 1 = got data, 0 = timeout, -1 = closed/error. */
static int io_fill(uint32_t timeout_ms)
{
    struct timeval tv = { (long)(timeout_ms / 1000), (long)(timeout_ms % 1000) * 1000 };
    int r;
    if (S.rx_len >= sizeof(S.rx)) return -1;
    if (S.tls && mbedtls_ssl_get_bytes_avail(&S.ssl) == 0) {
        fd_set rf;
        FD_ZERO(&rf);
        FD_SET(S.fd, &rf);
        if (select(S.fd + 1, &rf, NULL, NULL, &tv) <= 0) return 0;
    } else if (!S.tls) {
        fd_set rf;
        FD_ZERO(&rf);
        FD_SET(S.fd, &rf);
        if (select(S.fd + 1, &rf, NULL, NULL, &tv) <= 0) return 0;
    }
    if (S.tls) {
        r = mbedtls_ssl_read(&S.ssl, S.rx + S.rx_len, sizeof(S.rx) - S.rx_len);
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_TIMEOUT) return 0;
    } else {
        r = recv(S.fd, S.rx + S.rx_len, sizeof(S.rx) - S.rx_len, 0);
    }
    if (r <= 0) return -1;
    S.rx_len += (size_t)r;
    return 1;
}

/* Wait for the next complete packet (copied to S.tx scratch is not needed: parsed in place).
 * Returns packet length at S.rx[0..len), 0 on timeout, -1 on error. */
static int next_packet(uint32_t timeout_ms, mqtt_msg_t *m)
{
    uint32_t t0 = board_ms();
    for (;;) {
        size_t total;
        int f = mqtt_frame(S.rx, S.rx_len, &total);
        if (f < 0) return -1;
        if (f == 1) {
            if (mqtt_parse(S.rx, total, m) != 0) return -1;
            return (int)total;
        }
        uint32_t el = board_ms() - t0;
        if (el >= timeout_ms) return 0;
        int r = io_fill(timeout_ms - el);
        if (r < 0) return -1;
        if (r == 0) return 0;
    }
}

static void consume(size_t n)
{
    memmove(S.rx, S.rx + n, S.rx_len - n);
    S.rx_len -= n;
}

/* Handle a packet that isn't the one being waited for. */
static void dispatch(const mqtt_msg_t *m)
{
    if (m->type == MQTT_PUBLISH) {
        if (m->qos == 1) {
            uint8_t ack[4];
            int n = mqtt_enc_puback(ack, sizeof(ack), m->pkt_id);
            if (n > 0) io_write(ack, (size_t)n);
        }
        if (S.on_msg)
            S.on_msg(S.ctx, m->topic, m->topic_len, m->payload, m->payload_len, m->retain);
    }
}

/* Wait for a packet of `type` (and pkt_id if nonzero), dispatching others. */
static int wait_for(uint8_t type, uint16_t id, uint32_t timeout_ms, mqtt_msg_t *out)
{
    uint32_t t0 = board_ms();
    for (;;) {
        mqtt_msg_t m;
        uint32_t el = board_ms() - t0;
        if (el >= timeout_ms) return 0;
        int n = next_packet(timeout_ms - el, &m);
        if (n <= 0) return n;
        bool hit = m.type == type && (!id || m.pkt_id == id);
        if (!hit) dispatch(&m);
        if (hit && out) *out = m;       /* note: pointers into S.rx valid until consume */
        consume((size_t)n);
        if (hit) return 1;
    }
}

/* ---------------------------------------------------------------- API */

int mq_open(const mq_opts_t *o, char *err, size_t errlen)
{
    const uls_config_t *c = o->cfg;
    mqtt_msg_t m;
    int rc, n;

    mq_close(false);
    memset(&S, 0, offsetof(sess_t, rx));
    S.fd = -1;
    S.unix_time = o->unix_time;
    S.on_msg = o->on_msg;
    S.ctx = o->ctx;
    S.keepalive_s = o->keepalive_s;
    S.next_id = 1;

    if (resolve(c->mqtt_host, S.ip) != 0) {
        snprintf(err, errlen, "dns: cannot resolve %s", c->mqtt_host);
        return -1;
    }
    S.host_is_ip = parse_ipv4(c->mqtt_host, S.ip);
    S.fd = tcp_connect(S.ip, (uint16_t)c->mqtt_port, IO_MS);
    if (S.fd < 0) {
        snprintf(err, errlen, "tcp: cannot connect to %u.%u.%u.%u:%d", S.ip[0], S.ip[1], S.ip[2], S.ip[3],
                 (int)c->mqtt_port);
        return -1;
    }

    if (c->mqtt_tls) {
        const char *pers = "uls03gw";
        uint32_t flags;
        S.tls = true;
        mbedtls_ssl_init(&S.ssl);
        mbedtls_ssl_config_init(&S.conf);
        mbedtls_x509_crt_init(&S.ca);
        mbedtls_ctr_drbg_init(&S.drbg);
        mbedtls_entropy_init(&S.entropy);
        rc = mbedtls_ctr_drbg_seed(&S.drbg, mbedtls_entropy_func, &S.entropy, (const unsigned char *)pers,
                                   strlen(pers));
        if (rc == 0)
            rc = mbedtls_x509_crt_parse(&S.ca, (const unsigned char *)c->mqtt_ca_pem, strlen(c->mqtt_ca_pem) + 1);
        if (rc != 0) {
            snprintf(err, errlen, "tls: CA certificate does not parse (-0x%04x)", (unsigned)-rc);
            goto fail;
        }
        mbedtls_ssl_config_defaults(&S.conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT);
        mbedtls_ssl_conf_min_version(&S.conf, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3);
        mbedtls_ssl_conf_authmode(&S.conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&S.conf, &S.ca, NULL);
        mbedtls_ssl_conf_rng(&S.conf, mbedtls_ctr_drbg_random, &S.drbg);
        mbedtls_ssl_conf_verify(&S.conf, verify_cb, &S);
        rc = mbedtls_ssl_setup(&S.ssl, &S.conf);
        if (rc == 0) {
            S.verify_ip_san = c->mqtt_verify_host && S.host_is_ip;
            /* DNS names: SNI + mbedTLS name check. IP / verify off: no hostname (no CN check). */
            rc = mbedtls_ssl_set_hostname(&S.ssl, (c->mqtt_verify_host && !S.host_is_ip) ? c->mqtt_host : NULL);
        }
        if (rc != 0) {
            snprintf(err, errlen, "tls: setup failed (-0x%04x, out of memory?)", (unsigned)-rc);
            goto fail;
        }
        mbedtls_ssl_set_bio(&S.ssl, &S.fd, bio_send, bio_recv, NULL);
        uint32_t t0 = board_ms();
        while ((rc = mbedtls_ssl_handshake(&S.ssl)) != 0) {
            if ((rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE ||
                 rc == MBEDTLS_ERR_SSL_TIMEOUT) && board_ms() - t0 < 15000)
                continue;
            flags = mbedtls_ssl_get_verify_result(&S.ssl);
            if (rc == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED && flags)
                describe_verify(flags, err, errlen);
            else
                snprintf(err, errlen, "tls: handshake failed (-0x%04x)", (unsigned)-rc);
            goto fail;
        }
        LOG("mqtt: TLS %s, %s, %u ms\r\n", mbedtls_ssl_get_version(&S.ssl), mbedtls_ssl_get_ciphersuite(&S.ssl),
            (unsigned)(board_ms() - t0));
    }

    {
        mqtt_connect_t cp = { o->client_id, c->mqtt_user, c->mqtt_pass, o->keepalive_s, true,
                              o->will_topic, "offline", true, 1 };
        n = mqtt_enc_connect(S.tx, sizeof(S.tx), &cp);
        if (n < 0 || io_write(S.tx, (size_t)n) != 0) {
            snprintf(err, errlen, "mqtt: send CONNECT failed");
            goto fail;
        }
    }
    rc = wait_for(MQTT_CONNACK, 0, IO_MS, &m);
    if (rc <= 0) {
        snprintf(err, errlen, rc == 0 ? "mqtt: no CONNACK (timeout)" : "mqtt: connection closed by broker");
        goto fail;
    }
    if (m.rc != 0) {
        snprintf(err, errlen, "mqtt: broker refused: %s", mqtt_connack_str(m.rc));
        goto fail;
    }
    S.open = true;
    LOG("mqtt: connected\r\n");
    return 0;

fail:
    snprintf(S.err, sizeof(S.err), "%s", err);
    LOG("%s\r\n", err);
    mq_close(false);
    return -1;
}

int mq_publish(const char *topic, const void *payload, size_t len, uint8_t qos, bool retain)
{
    uint16_t id = qos ? S.next_id++ : 0;
    int n;
    if (!S.open) return -1;
    if (!S.next_id) S.next_id = 1;
    n = mqtt_enc_publish(S.tx, sizeof(S.tx), topic, payload, len, qos, retain, id);
    if (n < 0) { snprintf(S.err, sizeof(S.err), "mqtt: message too large for %s", topic); return -1; }
    if (io_write(S.tx, (size_t)n) != 0) { snprintf(S.err, sizeof(S.err), "mqtt: send failed"); S.open = false; return -1; }
    if (!qos) return 0;
    if (wait_for(MQTT_PUBACK, id, IO_MS, NULL) != 1) {
        snprintf(S.err, sizeof(S.err), "mqtt: no PUBACK for %s", topic);
        return -1;
    }
    return 0;
}

int mq_subscribe(const char *topic, uint8_t qos)
{
    mqtt_msg_t m;
    uint16_t id = S.next_id++;
    int n;
    if (!S.open) return -1;
    if (!S.next_id) S.next_id = 1;
    n = mqtt_enc_subscribe(S.tx, sizeof(S.tx), id, topic, qos);
    if (n < 0 || io_write(S.tx, (size_t)n) != 0) return -1;
    if (wait_for(MQTT_SUBACK, id, IO_MS, &m) != 1 || m.rc == 0x80) {
        snprintf(S.err, sizeof(S.err), "mqtt: subscribe to %s refused", topic);
        return -1;
    }
    return 0;
}

int mq_poll(uint32_t timeout_ms)
{
    uint32_t t0 = board_ms();
    if (!S.open) return -1;
    for (;;) {
        mqtt_msg_t m;
        uint32_t el = board_ms() - t0;
        if (S.keepalive_s && board_ms() - S.last_tx_ms > (uint32_t)S.keepalive_s * 500u) {
            int n = mqtt_enc_pingreq(S.tx, sizeof(S.tx));
            if (io_write(S.tx, (size_t)n) != 0) { S.open = false; return -1; }
        }
        if (el >= timeout_ms) return 0;
        int n = next_packet(timeout_ms - el, &m);
        if (n < 0) { S.open = false; snprintf(S.err, sizeof(S.err), "mqtt: connection lost"); return -1; }
        if (n == 0) return 0;
        dispatch(&m);
        consume((size_t)n);
    }
}

void mq_close(bool send_disconnect)
{
    if (S.open && send_disconnect) {
        int n = mqtt_enc_disconnect(S.tx, sizeof(S.tx));
        io_write(S.tx, (size_t)n);
    }
    if (S.tls) {
        if (S.open) mbedtls_ssl_close_notify(&S.ssl);
        tls_teardown();
        S.tls = false;
    }
    if (S.fd >= 0) {
        close(S.fd);
        S.fd = -1;
    }
    S.open = false;
    S.rx_len = 0;
}

uint32_t net_sntp(const char *host, uint32_t timeout_ms)
{
    uint8_t ip[4], pkt[SNTP_PACKET_LEN];
    struct sockaddr_in sa;
    struct timeval tv = { (long)(timeout_ms / 1000), (long)(timeout_ms % 1000) * 1000 };
    uint32_t t = 0;
    int fd;

    if (resolve(host, ip) != 0) return 0;
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(123);
    memcpy(&sa.sin_addr.s_addr, ip, 4);
    sntp_build_request(pkt);
    if (sendto(fd, pkt, sizeof(pkt), 0, (struct sockaddr *)&sa, sizeof(sa)) == (int)sizeof(pkt)) {
        int n = recv(fd, pkt, sizeof(pkt), 0);
        if (n > 0) t = sntp_parse_reply(pkt, (size_t)n);
    }
    close(fd);
    LOG("sntp: %s -> %u\r\n", host, (unsigned)t);
    return t;
}
