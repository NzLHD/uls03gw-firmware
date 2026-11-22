#include "unit.h"
#include "mqtt_pkt.h"
#include "netproto.h"

static void test_mqtt(void)
{
    uint8_t b[256];
    mqtt_msg_t m;
    size_t total;
    int n;

    /* CONNECT with user, pass and a retained will (checked byte by byte) */
    mqtt_connect_t c = { "dev1", "u", "p", 60, true, "t/s", "offline", true, 1 };
    n = mqtt_enc_connect(b, sizeof(b), &c);
    static const uint8_t want[] = {
        0x10, 36, 0, 4, 'M', 'Q', 'T', 'T', 4, 0xEE, 0, 60,
        0, 4, 'd', 'e', 'v', '1', 0, 3, 't', '/', 's', 0, 7, 'o', 'f', 'f', 'l', 'i', 'n', 'e',
        0, 1, 'u', 0, 1, 'p' };
    CHECK_EQ(n, (int)sizeof(want));
    CHECK(memcmp(b, want, sizeof(want)) == 0);
    /* minimal CONNECT: no user (pass ignored without user), no will */
    mqtt_connect_t c2 = { "x", "", "secret", 0, true, 0, 0, false, 0 };
    n = mqtt_enc_connect(b, sizeof(b), &c2);
    CHECK_EQ(n, 15);
    CHECK_EQ(b[9], 0x02);
    CHECK_EQ(mqtt_enc_connect(b, 10, &c), -1);

    /* PUBLISH QoS 1 retained, and the >127 remaining-length encoding */
    n = mqtt_enc_publish(b, sizeof(b), "a/b", "hi", 2, 1, true, 0x1234);
    CHECK_EQ(n, 11);
    CHECK_EQ(b[0], 0x33);
    CHECK_EQ(b[1], 9);
    CHECK(b[7] == 0x12 && b[8] == 0x34 && b[9] == 'h');
    CHECK_EQ(mqtt_enc_publish(b, sizeof(b), "a", "", 0, 1, false, 0), -1);      /* QoS1 needs an id */
    {
        static uint8_t big[300], pl[200];
        memset(pl, 'x', sizeof(pl));
        n = mqtt_enc_publish(big, sizeof(big), "t", pl, sizeof(pl), 0, false, 0);
        CHECK_EQ(n, 1 + 2 + 3 + 200);
        CHECK(big[1] == (0x80 | (203 % 128)) && big[2] == 203 / 128);
        CHECK_EQ(mqtt_frame(big, (size_t)n, &total), 1);
        CHECK_EQ(total, (size_t)n);
        CHECK_EQ(mqtt_frame(big, 100, &total), 0);
        CHECK_EQ(mqtt_parse(big, (size_t)n, &m), 0);
        CHECK(m.type == MQTT_PUBLISH && m.topic_len == 1 && m.payload_len == 200 && m.qos == 0);
    }

    /* SUBSCRIBE / PUBACK / PINGREQ / DISCONNECT */
    n = mqtt_enc_subscribe(b, sizeof(b), 7, "base/set", 1);
    CHECK(n == 15 && b[0] == 0x82 && b[1] == 13 && b[3] == 7 && b[14] == 1);
    CHECK_EQ(mqtt_enc_puback(b, sizeof(b), 0x0102), 4);
    CHECK(b[0] == 0x40 && b[2] == 1 && b[3] == 2);
    CHECK_EQ(mqtt_enc_pingreq(b, sizeof(b)), 2);
    CHECK_EQ(mqtt_enc_disconnect(b, sizeof(b)), 2);
    CHECK(b[0] == 0xE0 && b[1] == 0);

    /* parsing server packets */
    static const uint8_t connack[] = { 0x20, 2, 0, 5 };
    CHECK_EQ(mqtt_parse(connack, 4, &m), 0);
    CHECK(m.type == MQTT_CONNACK && m.rc == 5);
    CHECK_STR(mqtt_connack_str(m.rc), "not authorized");
    static const uint8_t puback[] = { 0x40, 2, 0x12, 0x34 };
    CHECK_EQ(mqtt_parse(puback, 4, &m), 0);
    CHECK(m.type == MQTT_PUBACK && m.pkt_id == 0x1234);
    static const uint8_t suback[] = { 0x90, 3, 0, 7, 0x80 };
    CHECK_EQ(mqtt_parse(suback, 5, &m), 0);
    CHECK(m.type == MQTT_SUBACK && m.pkt_id == 7 && m.rc == 0x80);
    static const uint8_t pub[] = { 0x33, 12, 0, 5, 'b', '/', 's', 'e', 't', 0, 9, '{', '}', '!' };
    CHECK_EQ(mqtt_frame(pub, sizeof(pub), &total), 1);
    CHECK_EQ(mqtt_parse(pub, sizeof(pub), &m), 0);
    CHECK(m.qos == 1 && m.retain && m.pkt_id == 9 && m.topic_len == 5 && !memcmp(m.topic, "b/set", 5));
    CHECK(m.payload_len == 3 && m.payload[0] == '{');
    static const uint8_t empty_retained[] = { 0x31, 7, 0, 5, 'b', '/', 's', 'e', 't' };
    CHECK_EQ(mqtt_parse(empty_retained, sizeof(empty_retained), &m), 0);
    CHECK(m.retain && m.payload_len == 0);
    static const uint8_t bad1[] = { 0x20, 3, 0, 0, 0 };
    CHECK_EQ(mqtt_parse(bad1, 5, &m), -1);
    static const uint8_t bad2[] = { 0x30, 3, 0, 9, 'x' };                     /* topic overruns */
    CHECK_EQ(mqtt_parse(bad2, 5, &m), -1);
    static const uint8_t bad3[] = { 0x30, 0xFF, 0xFF, 0xFF, 0xFF, 0x01 };     /* 5-byte length */
    CHECK_EQ(mqtt_frame(bad3, 6, &total), -1);
}

static void test_dns(void)
{
    /* query for "a.b" type A, id 0xBEEF, RD set */
    static const uint8_t q[] = { 0xBE, 0xEF, 0x01, 0x00, 0, 1, 0, 0, 0, 0, 0, 0,
                                 1, 'a', 1, 'b', 0, 0, 1, 0, 1 };
    const uint8_t ip[4] = { 192, 168, 4, 1 };
    uint8_t out[64];
    int n = dns_captive_reply(q, sizeof(q), ip, out, sizeof(out));
    CHECK_EQ(n, (int)sizeof(q) + 16);
    CHECK(out[0] == 0xBE && out[1] == 0xEF);
    CHECK(out[2] == 0x85 && out[3] == 0x80);                /* QR AA RD, RA NOERROR */
    CHECK(out[7] == 1);                                     /* one answer */
    CHECK(out[21] == 0xC0 && out[22] == 0x0C);
    CHECK(!memcmp(out + n - 4, ip, 4));
    /* AAAA query: empty answer */
    uint8_t q6[sizeof(q)];
    memcpy(q6, q, sizeof(q));
    q6[18] = 28;
    n = dns_captive_reply(q6, sizeof(q6), ip, out, sizeof(out));
    CHECK(n == (int)sizeof(q6) && out[7] == 0);
    /* responses and truncated packets are ignored */
    uint8_t r[sizeof(q)];
    memcpy(r, q, sizeof(q));
    r[2] |= 0x80;
    CHECK_EQ(dns_captive_reply(r, sizeof(r), ip, out, sizeof(out)), -1);
    CHECK_EQ(dns_captive_reply(q, 15, ip, out, sizeof(out)), -1);
    CHECK_EQ(dns_captive_reply(q, sizeof(q), ip, out, 20), -1);
}

static void test_http(void)
{
    http_req_t r;
    const char *g = "GET /api/status?x=1 HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n";
    CHECK_EQ(http_parse_head(g, strlen(g), &r), 1);
    CHECK(r.method == HTTP_GET && !strcmp(r.path, "/api/status") && r.content_length == -1);
    CHECK_EQ(r.head_len, strlen(g));
    const char *p = "POST /api/config HTTP/1.1\r\nHost: x\r\ncontent-LENGTH: 42\r\n\r\n{\"a\":1}";
    CHECK_EQ(http_parse_head(p, strlen(p), &r), 1);
    CHECK(r.method == HTTP_POST && !strcmp(r.path, "/api/config") && r.content_length == 42);
    CHECK_EQ(http_parse_head(p, 30, &r), 0);                /* head incomplete */
    CHECK_EQ(http_parse_head("GET\r\n\r\n", 7, &r), -1);
    CHECK_EQ(http_parse_head("GET x HTTP/1.1\r\n\r\n", 18, &r), -1);
    CHECK(http_is_captive_probe("/generate_204") && http_is_captive_probe("/hotspot-detect.html"));
    CHECK(!http_is_captive_probe("/"));
}

static void test_sntp(void)
{
    uint8_t pkt[SNTP_PACKET_LEN];
    sntp_build_request(pkt);
    CHECK(pkt[0] == 0x23 && pkt[40] == 0);
    /* server reply, transmit time = 2026-11-22 00:00:00 UTC = unix 1795305600 */
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 0x24; pkt[1] = 2;
    uint32_t ntp = 1795305600u + 2208988800u;
    pkt[40] = (uint8_t)(ntp >> 24); pkt[41] = (uint8_t)(ntp >> 16); pkt[42] = (uint8_t)(ntp >> 8); pkt[43] = (uint8_t)ntp;
    CHECK_EQ(sntp_parse_reply(pkt, sizeof(pkt)), 1795305600u);
    pkt[1] = 0;                                             /* kiss-o'-death */
    CHECK_EQ(sntp_parse_reply(pkt, sizeof(pkt)), 0);
    pkt[1] = 2; pkt[0] = 0x23;                              /* client mode */
    CHECK_EQ(sntp_parse_reply(pkt, sizeof(pkt)), 0);
    CHECK_EQ(sntp_parse_reply(pkt, 47), 0);
}

void test_net(void)
{
    test_mqtt();
    test_dns();
    test_http();
    test_sntp();
}
