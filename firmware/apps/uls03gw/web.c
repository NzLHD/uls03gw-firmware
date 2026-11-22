/* web.c - see web.h */
#include "include.h"
#include "rtos_pub.h"
#include "lwip/sockets.h"
#include "board.h"
#include "wifi.h"
#include "web.h"
#include "app.h"
#include "netproto.h"
#include "json_mini.h"
#include "web_page.h"           /* generated: WEB_PAGE_GZ */
#include <stdio.h>
#include <string.h>

#define REQ_MAX   9216          /* head + body (config with a 4 KB CA, JSON-escaped) */
#define RESP_MAX  6144

static char req[REQ_MAX];
static char resp[RESP_MAX];
static wifi_ap_t aps[24];
static int n_aps;
static const uint8_t ap_ip[4] = { 192, 168, 4, 1 };

/* ---------------------------------------------------------------- DNS */

/* The SDK's DHCP server owns UDP 53 on the AP and calls this for every query
 * (sdk-patches/0004): answer every A query with our address -> captive portal. */
int uls_dns_reply(const uint8_t *q, size_t qlen, uint8_t *out, size_t cap)
{
    return dns_captive_reply(q, qlen, ap_ip, out, cap);
}

/* ---------------------------------------------------------------- HTTP */

static int send_all(int fd, const void *b, size_t n)
{
    const char *p = b;
    while (n) {
        int w = send(fd, p, n, 0);
        if (w <= 0) return -1;
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

static void reply(int fd, const char *status, const char *ctype, const void *body, size_t len, int gz)
{
    char h[256];
    int n = snprintf(h, sizeof(h),
                     "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n%s"
                     "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
                     status, ctype, (unsigned)len, gz ? "Content-Encoding: gzip\r\n" : "");
    send_all(fd, h, (size_t)n);
    if (len) send_all(fd, body, len);
}

static void reply_json(int fd, int len)
{
    if (len < 0) {
        static const char e[] = "{\"ok\":false,\"error\":\"response too large\"}";
        reply(fd, "500 Internal Server Error", "application/json", e, sizeof(e) - 1, 0);
        return;
    }
    reply(fd, "200 OK", "application/json", resp, (size_t)len, 0);
}

static void redirect(int fd)
{
    static const char h[] = "HTTP/1.1 302 Found\r\nLocation: http://" WIFI_AP_IP "/\r\n"
                            "Content-Length: 0\r\nConnection: close\r\n\r\n";
    send_all(fd, h, sizeof(h) - 1);
}

static int scan_json(void)
{
    size_t n = (size_t)snprintf(resp, sizeof(resp), "{\"aps\":[");
    for (int i = 0; i < n_aps; i++) {
        json_writer_t o;
        if (n + 8 >= sizeof(resp)) return -1;
        if (i) resp[n++] = ',';
        jw_init(&o, resp + n, sizeof(resp) - n - 3);
        jw_begin(&o);
        jw_str(&o, "ssid", aps[i].ssid);
        jw_int(&o, "rssi", aps[i].rssi);
        jw_int(&o, "ch", aps[i].ch);
        jw_int(&o, "auth", aps[i].auth);
        jw_end(&o);
        if (jw_finish(&o) < 0) return -1;
        n += o.len;
    }
    resp[n++] = ']';
    resp[n++] = '}';
    resp[n] = 0;
    return (int)n;
}

/* Returns 1 if the setup loop should end with a reboot. */
static int handle(int fd)
{
    http_req_t r;
    size_t len = 0;
    int ph = 0;
    uint32_t t0 = board_ms();

    while (board_ms() - t0 < 3000) {
        int n = recv(fd, req + len, REQ_MAX - 1 - len, 0);
        if (n <= 0) break;
        len += (size_t)n;
        req[len] = 0;
        ph = http_parse_head(req, len, &r);
        if (ph != 0) break;
        if (len >= REQ_MAX - 1) { ph = -1; break; }
    }
    if (ph != 1) {
        reply(fd, "400 Bad Request", "text/plain", "bad request", 11, 0);
        return 0;
    }
    if (r.content_length > 0) {
        size_t want = r.head_len + (size_t)r.content_length;
        if (want >= REQ_MAX) {
            static const char e[] = "{\"ok\":false,\"error\":\"request too large\"}";
            reply(fd, "413 Payload Too Large", "application/json", e, sizeof(e) - 1, 0);
            return 0;
        }
        while (len < want && board_ms() - t0 < 5000) {
            int n = recv(fd, req + len, want - len, 0);
            if (n <= 0) break;
            len += (size_t)n;
        }
        req[len] = 0;
    }
    const char *body = req + r.head_len;
    size_t blen = len - r.head_len;
    LOG("http: %s %s (%u bytes)\r\n", r.method == HTTP_POST ? "POST" : "GET", r.path, (unsigned)blen);

    if (r.method == HTTP_GET && !strcmp(r.path, "/")) {
        reply(fd, "200 OK", "text/html; charset=utf-8", WEB_PAGE_GZ, WEB_PAGE_GZ_LEN, 1);
    } else if (r.method == HTTP_GET && !strcmp(r.path, "/api/status")) {
        reply_json(fd, app_status_json(resp, sizeof(resp)));
    } else if (r.method == HTTP_GET && !strcmp(r.path, "/api/scan")) {
        int n = wifi_scan(aps, (int)(sizeof(aps) / sizeof(aps[0])));
        if (n > 0) n_aps = n;       /* keep the pre-AP scan if a rescan comes back empty */
        reply_json(fd, scan_json());
    } else if (r.method == HTTP_GET && !strcmp(r.path, "/api/read")) {
        reply_json(fd, app_read_json(resp, sizeof(resp)));
    } else if (r.method == HTTP_POST && !strcmp(r.path, "/api/config")) {
        reply_json(fd, app_config_post(body, blen, resp, sizeof(resp)));
    } else if (r.method == HTTP_POST && !strcmp(r.path, "/api/test")) {
        reply_json(fd, app_test_post(body, blen, resp, sizeof(resp)));
    } else if (r.method == HTTP_POST && !strcmp(r.path, "/api/reset")) {
        static const char ok[] = "{\"ok\":true}";
        reply(fd, "200 OK", "application/json", ok, sizeof(ok) - 1, 0);
        app_factory_reset();
        return 1;
    } else if (r.method == HTTP_POST && !strcmp(r.path, "/api/reboot")) {
        static const char ok[] = "{\"ok\":true}";
        reply(fd, "200 OK", "application/json", ok, sizeof(ok) - 1, 0);
        return 1;
    } else if (r.method == HTTP_GET) {
        redirect(fd);               /* captive-portal probes and anything else */
    } else {
        reply(fd, "404 Not Found", "text/plain", "not found", 9, 0);
    }
    return 0;
}

setup_exit_t web_setup_run(const char *ssid, const char *pass, uint32_t idle_timeout_ms)
{
    struct sockaddr_in sa;
    int lfd, on = 1;
    uint32_t last = board_ms();
    bool was_down = board_button_down();
    setup_exit_t why = SETUP_EXIT_TIMEOUT;

    n_aps = wifi_scan(aps, (int)(sizeof(aps) / sizeof(aps[0])));   /* before the AP starts */
    if (wifi_ap_start(ssid, pass) != 0)
        LOG("setup: AP start failed\r\n");
    led_set(LED_AP);
    board_delay(500);

    lfd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(80);
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    if (lfd < 0 || bind(lfd, (struct sockaddr *)&sa, sizeof(sa)) != 0 || listen(lfd, 2) != 0)
        LOG("setup: HTTP listen failed\r\n");
    LOG("setup: serving http://" WIFI_AP_IP "/ for up to %u min idle\r\n", (unsigned)(idle_timeout_ms / 60000));

    for (;;) {
        fd_set rf;
        struct timeval tv = { 0, 200000 };
        board_watchdog_feed();          /* only armed when setup was entered from USB mode */
        bool down = board_button_down();
        if (was_down && !down) { why = SETUP_EXIT_BUTTON; break; }  /* press-and-release */
        was_down = down;
        if (board_ms() - last > idle_timeout_ms) { why = SETUP_EXIT_TIMEOUT; break; }

        FD_ZERO(&rf);
        FD_SET(lfd, &rf);
        if (select(lfd + 1, &rf, NULL, NULL, &tv) <= 0) continue;
        int cfd = accept(lfd, NULL, NULL);
        if (cfd < 0) continue;
        struct timeval to = { 3, 0 };
        setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to));
        setsockopt(cfd, SOL_SOCKET, SO_SNDTIMEO, &to, sizeof(to));
        int rb = handle(cfd);
        close(cfd);
        last = board_ms();
        if (rb) { why = SETUP_EXIT_REBOOT; break; }
    }

    close(lfd);
    board_delay(400);
    wifi_ap_stop();
    led_set(LED_OFF);
    LOG("setup: exit (%s)\r\n", why == SETUP_EXIT_BUTTON ? "button" : why == SETUP_EXIT_REBOOT ? "reboot" : "timeout");
    return why;
}
