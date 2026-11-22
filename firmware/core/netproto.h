/*
 * netproto.h - small protocol helpers for the setup AP and time sync (no I/O). Pure C99.
 *   - captive DNS: answer every A query with our address
 *   - HTTP/1.1 request head parsing (method, path, Content-Length)
 *   - SNTP request/response
 */
#ifndef ULS_NETPROTO_H
#define ULS_NETPROTO_H

#include <stdint.h>
#include <stddef.h>

/* Build the reply to DNS query q (qlen bytes) in out. A/ANY queries get one A record with
 * ip (4 bytes, network order); other types get an empty NOERROR answer.
 * Returns the reply length, or -1 to ignore the packet (not a query / malformed). */
int dns_captive_reply(const uint8_t *q, size_t qlen, const uint8_t ip[4], uint8_t *out, size_t cap);

typedef enum { HTTP_GET, HTTP_POST, HTTP_OTHER } http_method_t;

typedef struct {
    http_method_t method;
    char          path[64];         /* without the query string, NUL-terminated */
    long          content_length;   /* -1 if absent */
    size_t        head_len;         /* bytes up to and including the blank line */
} http_req_t;

/* Parse the request head in buf. Returns 1 when the head is complete (r filled),
 * 0 if more bytes are needed, -1 if malformed or too long (head > cap of the caller). */
int http_parse_head(const char *buf, size_t len, http_req_t *r);

/* Captive-portal probe paths that must redirect to "/". */
int http_is_captive_probe(const char *path);

#define SNTP_PACKET_LEN 48
void     sntp_build_request(uint8_t out[SNTP_PACKET_LEN]);
/* Unix time from a server reply, or 0 if it is not a valid server response. */
uint32_t sntp_parse_reply(const uint8_t *in, size_t len);

#endif
