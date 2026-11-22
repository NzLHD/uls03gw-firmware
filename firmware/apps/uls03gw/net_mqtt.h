/* net_mqtt.h - MQTT 3.1.1 session over TCP or TLS (mbedTLS), blocking with timeouts. */
#ifndef ULS_NET_MQTT_H
#define ULS_NET_MQTT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "uls_config.h"

/* Incoming PUBLISH handler (topic not NUL-terminated). */
typedef void (*mq_msg_fn)(void *ctx, const char *topic, size_t tlen, const uint8_t *payload, size_t plen,
                          bool retained);

typedef struct {
    const uls_config_t *cfg;
    const char *client_id;
    const char *will_topic;     /* mains mode: <base>/status, NULL in battery mode */
    uint32_t    unix_time;      /* 0 = unknown: certificate dates are not checked */
    uint16_t    keepalive_s;
    mq_msg_fn   on_msg;
    void       *ctx;
} mq_opts_t;

/* Connect: DNS, TCP, TLS (if mqtt_tls), CONNECT/CONNACK. 0 = ok, else err filled. */
int  mq_open(const mq_opts_t *o, char *err, size_t errlen);
bool mq_is_open(void);
/* QoS 0, or QoS 1 waiting for PUBACK (dispatches other incoming PUBLISHes meanwhile). */
int  mq_publish(const char *topic, const void *payload, size_t len, uint8_t qos, bool retain);
int  mq_subscribe(const char *topic, uint8_t qos);   /* waits for SUBACK */
/* Process incoming packets for up to timeout_ms (keepalive ping included). <0 = connection lost. */
int  mq_poll(uint32_t timeout_ms);
void mq_close(bool send_disconnect);
const char *mq_last_error(void);

/* Check a PEM CA the way the connection will use it (config_validate hook). */
int  mq_ca_check(const char *pem, char *err, size_t errlen);

/* One SNTP query (UDP 123) to host. Returns unix time or 0. Needs a station connection. */
uint32_t net_sntp(const char *host, uint32_t timeout_ms);

#endif
