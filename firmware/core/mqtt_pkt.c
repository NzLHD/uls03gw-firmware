/* mqtt_pkt.c - see mqtt_pkt.h */
#include "mqtt_pkt.h"
#include <string.h>

typedef struct { uint8_t *b; size_t cap, len; int bad; } w_t;

static void put8(w_t *w, uint8_t v) { if (w->len < w->cap) w->b[w->len] = v; else w->bad = 1; w->len++; }
static void put16(w_t *w, uint16_t v) { put8(w, (uint8_t)(v >> 8)); put8(w, (uint8_t)v); }
static void putn(w_t *w, const void *p, size_t n)
{
    if (w->len + n > w->cap) { w->bad = 1; w->len += n; return; }
    memcpy(w->b + w->len, p, n);
    w->len += n;
}
static void putstr(w_t *w, const char *s)
{
    size_t n = s ? strlen(s) : 0;
    if (n > 65535) { w->bad = 1; return; }
    put16(w, (uint16_t)n);
    putn(w, s, n);
}


/* Write fixed header + remaining length, returns 0 or -1 */
static int header(w_t *w, uint8_t first, size_t rem)
{
    if (rem > 268435455u) return -1;
    put8(w, first);
    do {
        uint8_t d = rem % 128;
        rem /= 128;
        if (rem) d |= 0x80;
        put8(w, d);
    } while (rem);
    return 0;
}

static int finish(w_t *w) { return w->bad || w->len > w->cap ? -1 : (int)w->len; }

int mqtt_enc_connect(uint8_t *b, size_t cap, const mqtt_connect_t *c)
{
    w_t w = { b, cap, 0, 0 };
    bool user = c->user && c->user[0];
    bool pass = user && c->pass && c->pass[0];
    bool will = c->will_topic && c->will_topic[0];
    size_t rem = 10 + 2 + strlen(c->client_id);
    uint8_t flags = 0;

    if (will) rem += 2 + strlen(c->will_topic) + 2 + (c->will_msg ? strlen(c->will_msg) : 0);
    if (user) rem += 2 + strlen(c->user);
    if (pass) rem += 2 + strlen(c->pass);
    if (c->clean_session) flags |= 0x02;
    if (will) flags |= (uint8_t)(0x04 | ((c->will_qos & 3) << 3) | (c->will_retain ? 0x20 : 0));
    if (pass) flags |= 0x40;
    if (user) flags |= 0x80;

    if (header(&w, MQTT_CONNECT << 4, rem)) return -1;
    putstr(&w, "MQTT");
    put8(&w, 4);                    /* protocol level 3.1.1 */
    put8(&w, flags);
    put16(&w, c->keepalive_s);
    putstr(&w, c->client_id);
    if (will) { putstr(&w, c->will_topic); putstr(&w, c->will_msg ? c->will_msg : ""); }
    if (user) putstr(&w, c->user);
    if (pass) putstr(&w, c->pass);
    return finish(&w);
}

int mqtt_enc_publish(uint8_t *b, size_t cap, const char *topic, const void *payload, size_t plen,
                     uint8_t qos, bool retain, uint16_t pkt_id)
{
    w_t w = { b, cap, 0, 0 };
    size_t rem = 2 + strlen(topic) + (qos ? 2 : 0) + plen;
    if (qos > 1 || (qos && !pkt_id) || !topic[0]) return -1;
    if (header(&w, (uint8_t)(MQTT_PUBLISH << 4 | qos << 1 | (retain ? 1 : 0)), rem)) return -1;
    putstr(&w, topic);
    if (qos) put16(&w, pkt_id);
    if (plen) putn(&w, payload, plen);
    return finish(&w);
}

int mqtt_enc_subscribe(uint8_t *b, size_t cap, uint16_t pkt_id, const char *topic, uint8_t qos)
{
    w_t w = { b, cap, 0, 0 };
    if (!pkt_id || !topic[0] || qos > 2) return -1;
    if (header(&w, MQTT_SUBSCRIBE << 4 | 0x02, 2 + 2 + strlen(topic) + 1)) return -1;
    put16(&w, pkt_id);
    putstr(&w, topic);
    put8(&w, qos);
    return finish(&w);
}

int mqtt_enc_puback(uint8_t *b, size_t cap, uint16_t pkt_id)
{
    w_t w = { b, cap, 0, 0 };
    header(&w, MQTT_PUBACK << 4, 2);
    put16(&w, pkt_id);
    return finish(&w);
}

int mqtt_enc_pingreq(uint8_t *b, size_t cap)
{
    w_t w = { b, cap, 0, 0 };
    header(&w, MQTT_PINGREQ << 4, 0);
    return finish(&w);
}

int mqtt_enc_disconnect(uint8_t *b, size_t cap)
{
    w_t w = { b, cap, 0, 0 };
    header(&w, MQTT_DISCONNECT << 4, 0);
    return finish(&w);
}

int mqtt_frame(const uint8_t *b, size_t len, size_t *total)
{
    size_t rem = 0, mult = 1, i;
    if (len < 2) return 0;
    for (i = 1; i <= 4; i++) {
        if (i >= len) return 0;
        rem += (size_t)(b[i] & 0x7F) * mult;
        if (!(b[i] & 0x80)) break;
        mult *= 128;
        if (i == 4) return -1;
    }
    *total = 1 + i + rem;
    return len >= *total ? 1 : 0;
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

int mqtt_parse(const uint8_t *b, size_t len, mqtt_msg_t *m)
{
    size_t total, hl;
    const uint8_t *p, *end;
    if (mqtt_frame(b, len, &total) != 1 || total != len) return -1;
    for (hl = 1; hl < 5 && (b[hl] & 0x80); hl++)   /* skip the remaining-length bytes */
        ;
    hl++;
    p = b + hl;
    end = b + len;
    memset(m, 0, sizeof(*m));
    m->type = b[0] >> 4;
    m->flags = b[0] & 0x0F;

    switch (m->type) {
    case MQTT_CONNACK:
        if (end - p != 2) return -1;
        m->session_present = p[0] & 1;
        m->rc = p[1];
        return 0;
    case MQTT_PUBACK:
        if (end - p != 2) return -1;
        m->pkt_id = rd16(p);
        return 0;
    case MQTT_SUBACK:
        if (end - p < 3) return -1;
        m->pkt_id = rd16(p);
        m->rc = p[2];
        return 0;
    case MQTT_PINGRESP:
        return end == p ? 0 : -1;
    case MQTT_PUBLISH:
        m->qos = (m->flags >> 1) & 3;
        m->retain = m->flags & 1;
        if (m->qos > 2 || end - p < 2) return -1;
        m->topic_len = rd16(p);
        p += 2;
        if ((size_t)(end - p) < m->topic_len) return -1;
        m->topic = (const char *)p;
        p += m->topic_len;
        if (m->qos) {
            if (end - p < 2) return -1;
            m->pkt_id = rd16(p);
            p += 2;
        }
        m->payload = p;
        m->payload_len = (size_t)(end - p);
        return 0;
    default:
        return 0;                   /* other types: header only */
    }
}

const char *mqtt_connack_str(uint8_t rc)
{
    switch (rc) {
    case 0: return "accepted";
    case 1: return "unacceptable protocol version";
    case 2: return "client id rejected";
    case 3: return "server unavailable";
    case 4: return "bad user name or password";
    case 5: return "not authorized";
    default: return "unknown CONNACK code";
    }
}
