/* netproto.c - see netproto.h */
#include "netproto.h"
#include <string.h>
#include <stdlib.h>

/* ------------------------------------------------------------ DNS */

int dns_captive_reply(const uint8_t *q, size_t qlen, const uint8_t ip[4], uint8_t *out, size_t cap)
{
    size_t p = 12, qend;
    uint16_t qtype;

    if (qlen < 12 || (q[2] & 0x80) || (q[2] & 0x78))   /* QR=1 or opcode != QUERY */
        return -1;
    if (q[4] != 0 || q[5] < 1)                          /* QDCOUNT >= 1 */
        return -1;
    /* walk the first question's name */
    while (p < qlen && q[p]) {
        if (q[p] & 0xC0) return -1;                     /* no compression in queries */
        p += (size_t)q[p] + 1;
    }
    if (p + 5 > qlen) return -1;
    qend = p + 5;                                       /* zero byte + type + class */
    qtype = (uint16_t)(q[p + 1] << 8 | q[p + 2]);

    int answer = qtype == 1 || qtype == 255;
    size_t need = qend + (answer ? 16 : 0);
    if (need > cap) return -1;

    memcpy(out, q, qend);
    out[2] = 0x84 | (q[2] & 0x01);                      /* QR, AA, keep RD */
    out[3] = 0x80;                                      /* RA, NOERROR */
    out[4] = 0; out[5] = 1;                             /* one question */
    out[6] = 0; out[7] = answer ? 1 : 0;
    out[8] = out[9] = out[10] = out[11] = 0;
    if (answer) {
        uint8_t *a = out + qend;
        a[0] = 0xC0; a[1] = 0x0C;                       /* name: pointer to the question */
        a[2] = 0; a[3] = 1;                             /* type A */
        a[4] = 0; a[5] = 1;                             /* class IN */
        a[6] = 0; a[7] = 0; a[8] = 0; a[9] = 10;        /* TTL 10 s */
        a[10] = 0; a[11] = 4;
        memcpy(a + 12, ip, 4);
    }
    return (int)need;
}

/* ------------------------------------------------------------ HTTP */

static int ci_prefix(const char *s, const char *end, const char *pfx)
{
    while (*pfx) {
        if (s >= end) return 0;
        char a = *s++, b = *pfx++;
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (a != b) return 0;
    }
    return 1;
}

int http_parse_head(const char *buf, size_t len, http_req_t *r)
{
    const char *end = buf + len, *eoh = 0, *p, *sp1, *sp2;

    for (p = buf; p + 3 < end; p++)
        if (p[0] == '\r' && p[1] == '\n' && p[2] == '\r' && p[3] == '\n') { eoh = p + 4; break; }
    if (!eoh)
        return 0;

    memset(r, 0, sizeof(*r));
    r->content_length = -1;
    r->head_len = (size_t)(eoh - buf);

    sp1 = memchr(buf, ' ', (size_t)(eoh - buf));
    if (!sp1) return -1;
    sp2 = memchr(sp1 + 1, ' ', (size_t)(eoh - sp1 - 1));
    if (!sp2) return -1;
    if (sp1 - buf == 3 && !memcmp(buf, "GET", 3))       r->method = HTTP_GET;
    else if (sp1 - buf == 4 && !memcmp(buf, "POST", 4)) r->method = HTTP_POST;
    else                                                r->method = HTTP_OTHER;

    {
        const char *u = sp1 + 1, *q = memchr(u, '?', (size_t)(sp2 - u));
        size_t n = (size_t)((q ? q : sp2) - u);
        if (n == 0 || n >= sizeof(r->path) || u[0] != '/') return -1;
        memcpy(r->path, u, n);
        r->path[n] = 0;
    }

    for (p = memchr(sp2, '\n', (size_t)(eoh - sp2)); p && p + 1 < eoh; p = memchr(p, '\n', (size_t)(eoh - p))) {
        p++;
        if (ci_prefix(p, eoh, "content-length:")) {
            char *e;
            long v = strtol(p + 15, &e, 10);
            if (v < 0) return -1;
            r->content_length = v;
        }
    }
    return 1;
}

int http_is_captive_probe(const char *path)
{
    static const char *const probes[] = {
        "/generate_204", "/gen_204", "/hotspot-detect.html", "/library/test/success.html",
        "/connecttest.txt", "/ncsi.txt", "/redirect", "/canonical.html", "/success.txt",
    };
    for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); i++)
        if (!strcmp(path, probes[i])) return 1;
    return 0;
}

/* ------------------------------------------------------------ SNTP */

#define NTP_UNIX_OFFSET 2208988800u     /* 1900 -> 1970 */

void sntp_build_request(uint8_t out[SNTP_PACKET_LEN])
{
    memset(out, 0, SNTP_PACKET_LEN);
    out[0] = 0x23;                      /* LI 0, version 4, mode 3 (client) */
}

uint32_t sntp_parse_reply(const uint8_t *in, size_t len)
{
    uint32_t secs;
    if (len < SNTP_PACKET_LEN) return 0;
    if ((in[0] & 0x07) != 4 && (in[0] & 0x07) != 5) return 0;   /* server / broadcast */
    if ((in[0] >> 6) == 3) return 0;                            /* LI = alarm (unsynchronised) */
    if (in[1] == 0 || in[1] > 15) return 0;                     /* stratum (0 = kiss-o'-death) */
    uint32_t unix_t;
    secs = (uint32_t)in[40] << 24 | (uint32_t)in[41] << 16 | (uint32_t)in[42] << 8 | in[43];
    if (secs & 0x80000000u)
        unix_t = secs - NTP_UNIX_OFFSET;                        /* NTP era 0 (until 2036) */
    else
        unix_t = secs + (0xFFFFFFFFu - NTP_UNIX_OFFSET + 1u);   /* era 1 (2036-2106) */
    return unix_t >= 1700000000u ? unix_t : 0;                  /* reject anything before 2023 */
}
