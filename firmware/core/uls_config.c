/* uls_config.c - see uls_config.h and spec §4 */
#include "uls_config.h"
#include "json_mini.h"
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------ field table */

enum { F_INT, F_BOOL, F_STR, F_IP4 };
#define F_SECRET    0x01    /* write-only, never exported               */
#define F_MQTT      0x02    /* may be changed over MQTT <base>/set      */
#define F_MULTILINE 0x04    /* CR/LF allowed (PEM)                      */

typedef struct {
    const char *name;
    uint8_t     type;
    uint8_t     flags;
    uint16_t    off;
    uint16_t    size;       /* F_STR: buffer size incl. NUL */
    int32_t     min, max;   /* F_INT range                  */
} field_t;

#define FI(n, lo, hi, fl) { #n, F_INT,  (fl), offsetof(uls_config_t, n), 0, (lo), (hi) }
#define FB(n, fl)         { #n, F_BOOL, (fl), offsetof(uls_config_t, n), 0, 0, 1 }
#define FS(n, fl)         { #n, F_STR,  (fl), offsetof(uls_config_t, n), sizeof(((uls_config_t *)0)->n), 0, 0 }
#define FP(n)             { #n, F_IP4,  0,    offsetof(uls_config_t, n), 4, 0, 0 }

static const field_t fields[] = {
    FI(install_height_cm,   1,    1000,   F_MQTT),
    FI(full_distance_cm,    0,    999,    F_MQTT),
    FI(upper_alarm_pct,     0,    100,    F_MQTT),
    FI(lower_alarm_pct,     0,    100,    F_MQTT),
    FI(tank_volume_l,       0,    100000, F_MQTT),
    FI(change_threshold_cm, 1,    50,     F_MQTT),
    FI(read_interval_min,   5,    1440,   F_MQTT),
    FI(heartbeat_h,         1,    24,     F_MQTT),
    FI(mains_read_s,        2,    3600,   F_MQTT),
    FI(mains_heartbeat_min, 5,    1440,   F_MQTT),
    FI(sensor_warmup_ms,    100,  5000,   F_MQTT),
    FI(sensor_poll_ms,      450,  2000,   F_MQTT),  /* <= 400 ms never measures (spec §2) */
    FI(sensor_samples,      1,    15,     F_MQTT),
    FI(ap_timeout_min,      1,    30,     0),
    FS(ap_password,                       F_SECRET),
    FS(wifi_ssid,                         0),
    FS(wifi_pass,                         F_SECRET),
    FS(hostname,                          0),
    FB(wifi_static,                       0),
    FP(wifi_ip),
    FP(wifi_mask),
    FP(wifi_gw),
    FP(wifi_dns),
    FS(mqtt_host,                         0),
    FI(mqtt_port,           1,    65535,  0),
    FB(mqtt_tls,                          0),
    FS(mqtt_ca_pem,                       F_SECRET | F_MULTILINE),
    FB(mqtt_verify_host,                  0),
    FS(mqtt_user,                         0),
    FS(mqtt_pass,                         F_SECRET),
    FS(mqtt_client_id,                    0),
    FS(mqtt_base_topic,                   0),
    FB(ha_discovery,                      0),
    FS(ha_prefix,                         0),
};
#define NFIELDS (sizeof(fields) / sizeof(fields[0]))

static void *fptr(const uls_config_t *c, const field_t *f) { return (char *)c + f->off; }

static const field_t *find_field(const char *name)
{
    for (size_t i = 0; i < NFIELDS; i++)
        if (strcmp(fields[i].name, name) == 0)
            return &fields[i];
    return 0;
}

static void set_str(char *dst, size_t size, const char *src)
{
    size_t n = strlen(src);
    if (n >= size)
        n = size - 1;
    memset(dst, 0, size);       /* zero tail: deterministic blobs and memcmp */
    memcpy(dst, src, n);
}

#define ERR(...) do { if (err && errlen) snprintf(err, errlen, __VA_ARGS__); return -1; } while (0)
/* JSON member callback errors must be > 0 (negative values are the parser's own codes). */
#define MERR(...) do { if (err && errlen) snprintf(err, errlen, __VA_ARGS__); return 1; } while (0)

/* ------------------------------------------------------------ defaults */

void config_defaults(uls_config_t *c, const uint8_t mac[6])
{
    char sfx[8];
    snprintf(sfx, sizeof(sfx), "%02x%02x%02x",
             mac ? mac[3] : 0, mac ? mac[4] : 0, mac ? mac[5] : 0);

    memset(c, 0, sizeof(*c));
    c->install_height_cm   = 10;    /* stock DP19 default */
    c->full_distance_cm    = 10;    /* stock DP21 default -> configured = false */
    c->upper_alarm_pct     = 0;
    c->lower_alarm_pct     = 0;
    c->tank_volume_l       = 0;
    c->change_threshold_cm = 2;
    c->read_interval_min   = 30;
    c->heartbeat_h         = 6;
    c->mains_read_s        = 10;
    c->mains_heartbeat_min = 30;
    c->sensor_warmup_ms    = 500;   /* before the trigger request; measured (spec §2), stock 2000 */
    c->sensor_poll_ms      = 500;
    c->sensor_samples      = 1;     /* the head filters internally; stock 5 */
    c->ap_timeout_min      = 5;
    set_str(c->ap_password, sizeof(c->ap_password), "uls03gw-setup");
    snprintf(c->hostname, sizeof(c->hostname), "uls03gw-%s", sfx);
    c->mqtt_port           = 8883;
    c->mqtt_tls            = 1;
    c->mqtt_verify_host    = 1;
    c->ha_discovery        = 1;
    snprintf(c->mqtt_client_id, sizeof(c->mqtt_client_id), "uls03gw-%s", sfx);
    snprintf(c->mqtt_base_topic, sizeof(c->mqtt_base_topic), "uls03gw/%s", sfx);
    set_str(c->ha_prefix, sizeof(c->ha_prefix), "homeassistant");
}

/* ------------------------------------------------------------ validation helpers */

static bool str_terminated(const char *s, size_t size) { return memchr(s, 0, size) != 0; }

static bool chars_ok(const char *s, bool multiline)
{
    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch == 0x7F) return false;
        if (ch < 0x20 && !(multiline && (ch == '\n' || ch == '\r' || ch == '\t'))) return false;
    }
    return true;
}

static bool ascii_printable(const char *s)
{
    for (; *s; s++)
        if ((unsigned char)*s < 0x20 || (unsigned char)*s > 0x7E) return false;
    return true;
}

static bool is_hex(char ch)
{
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

static bool alnum(char ch)
{
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

static bool wpa_pass_ok(const char *p)
{
    size_t n = strlen(p);
    if (n == 0) return true;                        /* open network */
    if (n == 64) {
        for (size_t i = 0; i < 64; i++) if (!is_hex(p[i])) return false;
        return true;                                /* raw PSK */
    }
    return n >= 8 && n <= 63 && ascii_printable(p);
}

static bool hostname_ok(const char *h)              /* one RFC 1123 label */
{
    size_t n = strlen(h);
    if (n == 0 || n > 32 || h[0] == '-' || h[n - 1] == '-') return false;
    for (size_t i = 0; i < n; i++) if (!alnum(h[i]) && h[i] != '-') return false;
    return true;
}

static bool host_ok(const char *h)                  /* DNS name or dotted IPv4 */
{
    size_t n = strlen(h);
    if (n == 0 || h[0] == '.' || h[0] == '-' || h[n - 1] == '.' || h[n - 1] == '-') return false;
    for (size_t i = 0; i < n; i++)
        if (!alnum(h[i]) && h[i] != '-' && h[i] != '.') return false;
    return strstr(h, "..") == 0;
}

static bool topic_ok(const char *t)                 /* publish topic without wildcards */
{
    size_t n = strlen(t);
    if (n == 0 || t[0] == '/' || t[n - 1] == '/') return false;
    for (size_t i = 0; i < n; i++)
        if (t[i] == '+' || t[i] == '#' || t[i] == ' ') return false;
    return chars_ok(t, false);
}

static bool client_id_ok(const char *s)
{
    if (!*s) return false;
    for (; *s; s++) if ((unsigned char)*s <= 0x20 || (unsigned char)*s > 0x7E) return false;
    return true;
}

static bool ip_zero(const uint8_t ip[4]) { return !(ip[0] | ip[1] | ip[2] | ip[3]); }

static bool mask_ok(const uint8_t m[4])
{
    uint32_t v = (uint32_t)m[0] << 24 | (uint32_t)m[1] << 16 | (uint32_t)m[2] << 8 | m[3];
    return v != 0 && ((~v + 1) & ~v) == 0;          /* contiguous ones from the top */
}

static int parse_ip4(const char *s, uint8_t out[4])
{
    if (!*s) { memset(out, 0, 4); return 0; }
    for (int i = 0; i < 4; i++) {
        int v = 0, d = 0;
        while (*s >= '0' && *s <= '9') {
            v = v * 10 + (*s++ - '0');
            if (++d > 3 || v > 255) return -1;
        }
        if (!d) return -1;
        out[i] = (uint8_t)v;
        if (i < 3 && *s++ != '.') return -1;
    }
    return *s ? -1 : 0;
}

static bool pem_framed(const char *pem)
{
    const char *b = strstr(pem, "-----BEGIN CERTIFICATE-----");
    return b && strstr(b, "-----END CERTIFICATE-----");
}

/* ------------------------------------------------------------ validate */

int config_validate(const uls_config_t *c, cfg_ca_check_fn ca_check, char *err, size_t errlen)
{
    for (size_t i = 0; i < NFIELDS; i++) {
        const field_t *f = &fields[i];
        if (f->type == F_INT) {
            int32_t v = *(const int32_t *)fptr(c, f);
            if (v < f->min || v > f->max)
                ERR("%s must be %ld..%ld", f->name, (long)f->min, (long)f->max);
        } else if (f->type == F_BOOL) {
            if (*(const uint8_t *)fptr(c, f) > 1)
                ERR("%s must be true or false", f->name);
        } else if (f->type == F_STR) {
            const char *s = fptr(c, f);
            if (!str_terminated(s, f->size))
                ERR("%s is too long", f->name);
            if (!chars_ok(s, f->flags & F_MULTILINE))
                ERR("%s contains control characters", f->name);
        }
    }

    if (c->lower_alarm_pct > 0 && c->upper_alarm_pct > 0 && c->lower_alarm_pct >= c->upper_alarm_pct)
        ERR("lower_alarm_pct must be below upper_alarm_pct");

    if (!wpa_pass_ok(c->wifi_pass))
        ERR("wifi_pass must be empty, 8-63 characters, or 64 hex digits");
    if (c->wifi_pass[0] && !c->wifi_ssid[0])
        ERR("wifi_ssid is required when wifi_pass is set");
    if (c->ap_password[0] && (strlen(c->ap_password) < 8 || !ascii_printable(c->ap_password)))
        ERR("ap_password must be empty (open AP) or 8-63 printable ASCII characters");
    if (!hostname_ok(c->hostname))
        ERR("hostname must be 1-32 letters, digits or '-'");

    if (c->wifi_static) {
        if (ip_zero(c->wifi_ip) || ip_zero(c->wifi_gw))
            ERR("static IP needs wifi_ip and wifi_gw");
        if (!mask_ok(c->wifi_mask))
            ERR("wifi_mask is not a valid netmask");
        for (int i = 0; i < 4; i++)
            if ((c->wifi_ip[i] & c->wifi_mask[i]) != (c->wifi_gw[i] & c->wifi_mask[i]))
                ERR("wifi_gw is not in the wifi_ip subnet");
    }

    if (!client_id_ok(c->mqtt_client_id))
        ERR("mqtt_client_id must be 1-64 printable characters without spaces");
    if (!topic_ok(c->mqtt_base_topic))
        ERR("mqtt_base_topic must be non-empty, without + # spaces or leading/trailing /");
    if (!topic_ok(c->ha_prefix))
        ERR("ha_prefix must be non-empty, without + # spaces or leading/trailing /");

    if (c->mqtt_host[0]) {
        if (!host_ok(c->mqtt_host))
            ERR("mqtt_host must be a host name or IPv4 address");
        if (c->mqtt_tls) {
            if (!c->mqtt_ca_pem[0])
                ERR("mqtt_ca_pem is required when TLS is on");
            if (!pem_framed(c->mqtt_ca_pem))
                ERR("mqtt_ca_pem is not a PEM certificate (BEGIN/END CERTIFICATE)");
            if (ca_check) {
                char why[96] = "";
                if (ca_check(c->mqtt_ca_pem, why, sizeof(why)) != 0)
                    ERR("mqtt_ca_pem: %s", why[0] ? why : "certificate does not parse");
            }
        }
    }
    return 0;
}

/* ------------------------------------------------------------ JSON apply */

typedef struct {
    uls_config_t *c;
    cfg_source_t  src;
    bool          port_given, tls_given;
    cfg_cmd_t     cmd;
    char         *err;
    size_t        errlen;
} apply_ctx_t;

static int apply_member(void *vctx, const char *key, const json_value_t *v)
{
    apply_ctx_t *x = vctx;
    char *err = x->err;
    size_t errlen = x->errlen;

    if (strcmp(key, "cmd") == 0) {
        if (x->src != CFG_SRC_MQTT || v->type != JV_STRING)
            MERR("cmd is only accepted over MQTT, as a string");
        if (strcmp(v->str, "report") == 0)      x->cmd = CFG_CMD_REPORT;
        else if (strcmp(v->str, "reboot") == 0) x->cmd = CFG_CMD_REBOOT;
        else MERR("unknown cmd \"%.16s\"", v->str);
        return 0;
    }

    const field_t *f = find_field(key);
    if (!f)
        MERR("unknown setting \"%.32s\"", key);
    if (x->src == CFG_SRC_MQTT && !(f->flags & F_MQTT))
        MERR("%s can only be changed on the setup page", f->name);

    void *p = fptr(x->c, f);
    switch (f->type) {
    case F_INT:
        if (v->type != JV_INT)
            MERR("%s must be a number", f->name);
        if (v->i < f->min || v->i > f->max)
            MERR("%s must be %ld..%ld", f->name, (long)f->min, (long)f->max);
        *(int32_t *)p = (int32_t)v->i;
        if (f->off == offsetof(uls_config_t, mqtt_port)) x->port_given = true;
        break;
    case F_BOOL:
        if (v->type == JV_BOOL)                         *(uint8_t *)p = v->b;
        else if (v->type == JV_INT && (v->i == 0 || v->i == 1)) *(uint8_t *)p = (uint8_t)v->i;
        else MERR("%s must be true or false", f->name);
        if (f->off == offsetof(uls_config_t, mqtt_tls)) x->tls_given = true;
        break;
    case F_STR:
        if (v->type == JV_NULL) {
            if (!(f->flags & F_SECRET))
                MERR("%s cannot be null", f->name);
            memset(p, 0, f->size);                      /* clear a secret */
            break;
        }
        if (v->type != JV_STRING)
            MERR("%s must be a string", f->name);
        if ((f->flags & F_SECRET) && v->str_len == 0)
            break;                                      /* "" keeps the old secret */
        if (v->str_len >= f->size)
            MERR("%s is too long (max %u)", f->name, (unsigned)(f->size - 1));
        set_str(p, f->size, v->str);
        break;
    case F_IP4:
        if (v->type != JV_STRING || parse_ip4(v->str, p) != 0)
            MERR("%s must be an IPv4 address like 192.168.1.10", f->name);
        break;
    }
    return 0;
}

static const char *json_err_str(int rc)
{
    switch (rc) {
    case JSON_ERR_NESTED:  return "nested objects/arrays are not allowed";
    case JSON_ERR_NUMBER:  return "numbers must be integers";
    case JSON_ERR_SCRATCH: return "a value is too long";
    default:               return "malformed JSON";
    }
}

int config_apply_json(uls_config_t *cfg, uls_config_t *tmp, const char *json, size_t len,
                      cfg_source_t src, cfg_ca_check_fn ca_check,
                      char *scratch, size_t scratch_len,
                      bool *changed, cfg_cmd_t *cmd, char *err, size_t errlen)
{
    apply_ctx_t x = { tmp, src, false, false, CFG_CMD_NONE, err, errlen };
    int rc;

    if (changed) *changed = false;
    if (cmd) *cmd = CFG_CMD_NONE;
    if (err && errlen) err[0] = 0;

    memcpy(tmp, cfg, sizeof(*tmp));
    rc = json_parse_flat(json, len, scratch, scratch_len, apply_member, &x);
    if (rc < 0)
        ERR("%s", json_err_str(rc));
    if (rc > 0)
        return -1;                                      /* member error, err already set */

    /* TLS toggled without an explicit port: move a default port along with it. */
    if (x.tls_given && !x.port_given && tmp->mqtt_tls != cfg->mqtt_tls) {
        if (tmp->mqtt_tls && tmp->mqtt_port == 1883)  tmp->mqtt_port = 8883;
        if (!tmp->mqtt_tls && tmp->mqtt_port == 8883) tmp->mqtt_port = 1883;
    }

    /* Over MQTT only re-check what MQTT can change, so a stored setup-page value that a
     * newer firmware considers invalid can't block tank/schedule updates. */
    if (config_validate(tmp, src == CFG_SRC_WEB ? ca_check : 0, err, errlen) != 0) {
        if (src == CFG_SRC_WEB || config_validate(cfg, 0, 0, 0) == 0)
            return -1;
        /* The stored config was already invalid in a setup-page field: accept the
         * MQTT change if the MQTT-settable part is consistent (ranges were checked
         * per member above; this is the only cross-field rule among them). */
        if (tmp->lower_alarm_pct > 0 && tmp->upper_alarm_pct > 0 &&
            tmp->lower_alarm_pct >= tmp->upper_alarm_pct)
            ERR("lower_alarm_pct must be below upper_alarm_pct");
        if (err && errlen) err[0] = 0;
    }

    if (changed) *changed = memcmp(tmp, cfg, sizeof(*tmp)) != 0;
    if (cmd) *cmd = x.cmd;
    memcpy(cfg, tmp, sizeof(*cfg));
    return 0;
}

/* ------------------------------------------------------------ JSON export */

int config_to_json(const uls_config_t *c, bool mqtt_only, char *buf, size_t cap)
{
    json_writer_t w;
    char key[48], ip[16];

    jw_init(&w, buf, cap);
    jw_begin(&w);
    for (size_t i = 0; i < NFIELDS; i++) {
        const field_t *f = &fields[i];
        const void *p = fptr(c, f);
        if (mqtt_only && !(f->flags & F_MQTT))
            continue;
        if (f->flags & F_SECRET) {
            snprintf(key, sizeof(key), "%s_set", f->name);
            jw_bool(&w, key, ((const char *)p)[0] != 0);
            continue;
        }
        switch (f->type) {
        case F_INT:  jw_int(&w, f->name, *(const int32_t *)p); break;
        case F_BOOL: jw_bool(&w, f->name, *(const uint8_t *)p != 0); break;
        case F_STR:  jw_str(&w, f->name, (const char *)p); break;
        case F_IP4: {
            const uint8_t *a = p;
            if (ip_zero(a)) ip[0] = 0;
            else snprintf(ip, sizeof(ip), "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
            jw_str(&w, f->name, ip);
            break;
        }
        }
    }
    jw_end(&w);
    return jw_finish(&w);
}

/* ------------------------------------------------------------ load */

int config_load_blob(uls_config_t *c, const void *blob, size_t len, const uint8_t mac[6])
{
    uls_config_t d;
    int repaired = 0;

    config_defaults(&d, mac);
    config_defaults(c, mac);
    if (len > sizeof(*c))
        len = sizeof(*c);
    memcpy(c, blob, len);

    /* Repair field by field so one bad value can't cost the Wi-Fi settings. */
    for (size_t i = 0; i < NFIELDS; i++) {
        const field_t *f = &fields[i];
        void *p = fptr(c, f);
        const void *dp = fptr(&d, f);
        bool bad = false;
        if (f->type == F_INT) {
            int32_t v = *(int32_t *)p;
            bad = v < f->min || v > f->max;
            if (bad) *(int32_t *)p = *(const int32_t *)dp;
        } else if (f->type == F_BOOL) {
            bad = *(uint8_t *)p > 1;
            if (bad) *(uint8_t *)p = *(const uint8_t *)dp;
        } else if (f->type == F_STR) {
            char *s = p;
            bad = !str_terminated(s, f->size) || !chars_ok(s, f->flags & F_MULTILINE);
            if (bad) memcpy(s, dp, f->size);
        }
        repaired += bad;
    }
    if (!hostname_ok(c->hostname))       { memcpy(c->hostname, d.hostname, sizeof(d.hostname)); repaired++; }
    if (!client_id_ok(c->mqtt_client_id)) { memcpy(c->mqtt_client_id, d.mqtt_client_id, sizeof(d.mqtt_client_id)); repaired++; }
    if (!topic_ok(c->mqtt_base_topic))   { memcpy(c->mqtt_base_topic, d.mqtt_base_topic, sizeof(d.mqtt_base_topic)); repaired++; }
    if (!topic_ok(c->ha_prefix))         { memcpy(c->ha_prefix, d.ha_prefix, sizeof(d.ha_prefix)); repaired++; }
    return repaired;
}
