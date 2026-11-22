/*
 * mqtt_pkt.h - MQTT 3.1.1 packet encoding/decoding (no I/O). Pure C99.
 * Only what this device needs: CONNECT (with optional will), PUBLISH QoS 0/1, PUBACK,
 * SUBSCRIBE, PINGREQ, DISCONNECT; parse CONNACK, PUBLISH, PUBACK, SUBACK, PINGRESP.
 * Encoders return the packet length, or -1 if the buffer is too small / input invalid.
 */
#ifndef ULS_MQTT_PKT_H
#define ULS_MQTT_PKT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

enum {
    MQTT_CONNECT = 1, MQTT_CONNACK = 2, MQTT_PUBLISH = 3, MQTT_PUBACK = 4,
    MQTT_SUBSCRIBE = 8, MQTT_SUBACK = 9, MQTT_PINGREQ = 12, MQTT_PINGRESP = 13,
    MQTT_DISCONNECT = 14,
};

typedef struct {
    const char *client_id;
    const char *user;           /* NULL or "" = none */
    const char *pass;           /* NULL or "" = none (only sent with a user) */
    uint16_t    keepalive_s;
    bool        clean_session;
    const char *will_topic;     /* NULL = no will */
    const char *will_msg;
    bool        will_retain;
    uint8_t     will_qos;
} mqtt_connect_t;

int mqtt_enc_connect(uint8_t *b, size_t cap, const mqtt_connect_t *c);
int mqtt_enc_publish(uint8_t *b, size_t cap, const char *topic, const void *payload, size_t plen,
                     uint8_t qos, bool retain, uint16_t pkt_id);
int mqtt_enc_subscribe(uint8_t *b, size_t cap, uint16_t pkt_id, const char *topic, uint8_t qos);
int mqtt_enc_puback(uint8_t *b, size_t cap, uint16_t pkt_id);
int mqtt_enc_pingreq(uint8_t *b, size_t cap);
int mqtt_enc_disconnect(uint8_t *b, size_t cap);

/* Is a complete packet at b? Returns 1 (total length in *total), 0 = need more, -1 = malformed. */
int mqtt_frame(const uint8_t *b, size_t len, size_t *total);

typedef struct {
    uint8_t        type;
    uint8_t        flags;       /* low nibble of the fixed header */
    uint16_t       pkt_id;      /* PUBLISH (QoS>0), PUBACK, SUBACK */
    uint8_t        rc;          /* CONNACK return code, SUBACK granted QoS (0x80 = failure) */
    bool           session_present;
    uint8_t        qos;         /* PUBLISH */
    bool           retain;      /* PUBLISH */
    const char    *topic;       /* PUBLISH, NOT NUL-terminated */
    size_t         topic_len;
    const uint8_t *payload;     /* PUBLISH */
    size_t         payload_len;
} mqtt_msg_t;

/* Parse one complete packet of exactly len bytes (from mqtt_frame). 0 = ok, -1 = malformed. */
int mqtt_parse(const uint8_t *b, size_t len, mqtt_msg_t *m);

const char *mqtt_connack_str(uint8_t rc);

#endif
