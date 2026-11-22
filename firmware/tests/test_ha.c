#include "unit.h"
#include "ha_discovery.h"

/* Minimal strict JSON validator (full grammar, nesting allowed), for discovery payloads. */
static const char *jv_value(const char *p);
static const char *jv_ws(const char *p) { while (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r') p++; return p; }
static const char *jv_string(const char *p)
{
    if (*p++ != '"') return 0;
    while (*p && *p != '"') {
        if ((unsigned char)*p < 0x20) return 0;
        if (*p == '\\') { p++; if (!*p) return 0; }
        p++;
    }
    return *p == '"' ? p + 1 : 0;
}
static const char *jv_value(const char *p)
{
    p = jv_ws(p);
    if (*p == '{' || *p == '[') {
        char close = *p == '{' ? '}' : ']';
        int obj = *p == '{';
        p = jv_ws(p + 1);
        if (*p == close) return p + 1;
        for (;;) {
            if (obj) { p = jv_string(jv_ws(p)); if (!p) return 0; p = jv_ws(p); if (*p++ != ':') return 0; }
            p = jv_value(p); if (!p) return 0;
            p = jv_ws(p);
            if (*p == ',') { p++; continue; }
            return *p == close ? p + 1 : 0;
        }
    }
    if (*p == '"') return jv_string(p);
    if (!strncmp(p, "true", 4)) return p + 4;
    if (!strncmp(p, "false", 5)) return p + 5;
    if (!strncmp(p, "null", 4)) return p + 4;
    if (*p == '-' || (*p >= '0' && *p <= '9')) { p++; while ((*p >= '0' && *p <= '9') || *p == '.') p++; return p; }
    return 0;
}
static int json_ok(const char *s) { const char *e = jv_value(s); return e && *jv_ws(e) == 0; }

typedef struct { int n, bad_json; char topics[20][96]; char payloads[20][1024]; } sink_t;

static int collect(void *ctx, const char *topic, const char *payload)
{
    sink_t *k = ctx;
    if (k->n >= 20) return 5;
    snprintf(k->topics[k->n], 96, "%s", topic);
    snprintf(k->payloads[k->n], 1024, "%s", payload);
    if (payload[0] && !json_ok(payload)) { k->bad_json++; printf("bad json: %s\n", payload); }
    k->n++;
    return 0;
}

static const char *find(const sink_t *k, const char *topic_part)
{
    for (int i = 0; i < k->n; i++) if (strstr(k->topics[i], topic_part)) return k->payloads[i];
    return 0;
}

static const uint8_t MAC[6] = { 0x10, 0x5a, 0x17, 0xab, 0xcd, 0xef };

void test_ha(void)
{
    static uls_config_t cfg;
    static sink_t k;
    char topic[128], payload[1024], buf[512];
    uint32_t h1, h2, h3;
    const char *p;

    config_defaults(&cfg, MAC);
    ha_info_t hi = { &cfg, MAC, "1.0.0", true };

    /* battery mode, no volume */
    memset(&k, 0, sizeof(k));
    CHECK_EQ(ha_discovery_build(&hi, topic, sizeof(topic), payload, sizeof(payload), collect, &k, &h1), 0);
    CHECK_EQ(k.n, ha_discovery_count());
    CHECK_EQ(k.n, 14);
    CHECK_EQ(k.bad_json, 0);
    CHECK_STR(k.topics[0], "homeassistant/sensor/uls03gw_abcdef/level/config");
    p = find(&k, "/sensor/uls03gw_abcdef/level/");
    CHECK(p && strstr(p, "\"unique_id\":\"uls03gw_abcdef_level\""));
    CHECK(p && strstr(p, "\"state_topic\":\"uls03gw/abcdef/state\""));
    CHECK(p && strstr(p, "\"value_template\":\"{{ value_json.level_pct }}\""));
    CHECK(p && strstr(p, "\"expire_after\":43800"));          /* 2 x 6 h + 10 min */
    CHECK(p && !strstr(p, "availability_topic"));
    CHECK(p && strstr(p, "\"identifiers\":[\"uls03gw_abcdef\"]"));
    CHECK(p && strstr(p, "\"sw_version\":\"1.0.0\""));
    p = find(&k, "/volume/");
    CHECK(p && p[0] == 0);                                    /* removed */
    p = find(&k, "binary_sensor/uls03gw_abcdef/problem/");
    CHECK(p && strstr(p, "{{ 'ON' if value_json.err else 'OFF' }}"));
    CHECK(p && strstr(p, "\"device_class\":\"problem\""));
    p = find(&k, "/alarm/");
    CHECK(p && strstr(p, "\"options\":[\"normal\",\"lower\",\"upper\"]"));
    p = find(&k, "/rssi/");
    CHECK(p && strstr(p, "\"entity_category\":\"diagnostic\"") && strstr(p, "\"unit_of_measurement\":\"dBm\""));
    p = find(&k, "number/uls03gw_abcdef/install_height/");
    CHECK(p && strstr(p, "\"command_topic\":\"uls03gw/abcdef/set\""));
    CHECK(p && strstr(p, "\"command_template\":\"{\\\"install_height_cm\\\": {{ value }}}\""));
    CHECK(p && strstr(p, "\"state_topic\":\"uls03gw/abcdef/config\""));
    CHECK(p && strstr(p, "\"retain\":true") && strstr(p, "\"min\":1") && strstr(p, "\"max\":1000"));
    CHECK(p && !strstr(p, "expire_after"));                   /* numbers read retained config */

    /* hashing: stable, independent of emit, sensitive to content */
    CHECK_EQ(ha_discovery_build(&hi, topic, sizeof(topic), payload, sizeof(payload), 0, 0, &h2), 0);
    CHECK(h1 == h2);
    cfg.tank_volume_l = 1000;
    memset(&k, 0, sizeof(k));
    CHECK_EQ(ha_discovery_build(&hi, topic, sizeof(topic), payload, sizeof(payload), collect, &k, &h3), 0);
    CHECK(h3 != h1);
    p = find(&k, "/volume/");
    CHECK(p && strstr(p, "\"device_class\":\"volume_storage\"") && strstr(p, "\"unit_of_measurement\":\"L\""));
    cfg.tank_volume_l = 0;
    cfg.heartbeat_h = 3;                                      /* expire_after changes */
    CHECK_EQ(ha_discovery_build(&hi, topic, sizeof(topic), payload, sizeof(payload), 0, 0, &h3), 0);
    CHECK(h3 != h1);
    cfg.heartbeat_h = 6;

    /* mains mode: availability, no expiry */
    hi.battery = false;
    memset(&k, 0, sizeof(k));
    CHECK_EQ(ha_discovery_build(&hi, topic, sizeof(topic), payload, sizeof(payload), collect, &k, &h3), 0);
    p = find(&k, "/depth/");
    CHECK(p && strstr(p, "\"availability_topic\":\"uls03gw/abcdef/status\"") && !strstr(p, "expire_after"));
    CHECK(h3 != h1);

    /* custom prefix / base topic, small buffers, emit abort */
    snprintf(cfg.ha_prefix, sizeof(cfg.ha_prefix), "ha");
    snprintf(cfg.mqtt_base_topic, sizeof(cfg.mqtt_base_topic), "home/tank");
    memset(&k, 0, sizeof(k));
    CHECK_EQ(ha_discovery_build(&hi, topic, sizeof(topic), payload, sizeof(payload), collect, &k, 0), 0);
    CHECK_STR(k.topics[0], "ha/sensor/uls03gw_abcdef/level/config");
    CHECK(strstr(k.payloads[0], "\"state_topic\":\"home/tank/state\"") != 0);
    CHECK_EQ(ha_discovery_build(&hi, topic, sizeof(topic), payload, 100, 0, 0, 0), -1);
    CHECK_EQ(ha_discovery_build(&hi, topic, 20, payload, sizeof(payload), 0, 0, 0), -1);
    {
        int max = 0;
        for (int i = 0; i < k.n; i++) if ((int)strlen(k.payloads[i]) > max) max = (int)strlen(k.payloads[i]);
        CHECK(max < 700);                                     /* fits one TLS record easily */
    }

    /* state JSON (spec §7.3 example shape) */
    level_cfg_t lc = { 300, 10, 94, 12, 2000 };
    level_result_t r;
    level_compute(&lc, true, 1234, &r);                       /* depth 177, 61 %, 1220 L */
    state_info_t si = { &r, true, WAKE_TIMER, -62, true, 5, 412, "1.0.0" };
    CHECK(state_json(&si, buf, sizeof(buf)) > 0);
    CHECK_STR(buf, "{\"distance_mm\":1234,\"depth_cm\":177,\"level_pct\":61,\"alarm\":\"normal\","
                   "\"volume_l\":1220,\"configured\":true,\"sensor_ok\":true,\"err\":null,"
                   "\"power\":\"battery\",\"wake\":\"timer\",\"rssi\":-62,\"reads_since_send\":5,"
                   "\"boot_count\":412,\"fw\":\"1.0.0\"}");
    level_compute(&lc, false, 0, &r);
    si.rssi_valid = false; si.wake = WAKE_BUTTON; si.battery = false;
    CHECK(state_json(&si, buf, sizeof(buf)) > 0);
    CHECK(json_ok(buf));
    CHECK(strstr(buf, "\"distance_mm\":null,\"depth_cm\":null,\"level_pct\":null,\"alarm\":null,\"volume_l\":null") != 0);
    CHECK(strstr(buf, "\"err\":\"sensor_timeout\"") && strstr(buf, "\"rssi\":null") &&
          strstr(buf, "\"wake\":\"button\"") && strstr(buf, "\"power\":\"usb\""));
    CHECK_EQ(state_json(&si, buf, 32), -1);
}
