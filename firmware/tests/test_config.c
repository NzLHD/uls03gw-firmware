#include "unit.h"
#include "uls_config.h"
#include <stdlib.h>

static const uint8_t MAC[6] = { 0x10, 0x5a, 0x17, 0xab, 0xcd, 0xef };

/* A self-made test certificate frame; the PC tests only check PEM framing. */
static const char *CA =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIBfTCCASOgAwIBAgIUTESTONLYTESTONLYTESTONLYTESTONLYwCgYIKoZIzj0E\n"
    "-----END CERTIFICATE-----\n";

static uls_config_t cfg, tmp;
static char scratch[CFG_CA_PEM_MAX + 256];
static char err[160];

static int apply(const char *json, cfg_source_t src, bool *changed, cfg_cmd_t *cmd)
{
    return config_apply_json(&cfg, &tmp, json, strlen(json), src, 0, scratch, sizeof(scratch),
                             changed, cmd, err, sizeof(err));
}

static int reject_ca(const char *pem, char *e, size_t n)
{
    (void)pem;
    snprintf(e, n, "X509 - parse error");
    return -1;
}

void test_config(void)
{
    bool changed;
    cfg_cmd_t cmd;
    char out[2048];

    /* defaults */
    config_defaults(&cfg, MAC);
    CHECK_EQ(config_validate(&cfg, 0, err, sizeof(err)), 0);
    CHECK_STR(cfg.hostname, "uls03gw-abcdef");
    CHECK_STR(cfg.mqtt_client_id, "uls03gw-abcdef");
    CHECK_STR(cfg.mqtt_base_topic, "uls03gw/abcdef");
    CHECK_STR(cfg.ap_password, "uls03gw-setup");
    CHECK_EQ(cfg.install_height_cm, 10);
    CHECK_EQ(cfg.full_distance_cm, 10);
    CHECK_EQ(cfg.mqtt_port, 8883);
    CHECK(cfg.mqtt_tls && cfg.mqtt_verify_host && cfg.ha_discovery);
    CHECK_EQ(cfg.sensor_warmup_ms, 500);
    CHECK_EQ(cfg.sensor_poll_ms, 500);
    CHECK_EQ(cfg.sensor_samples, 1);
    CHECK(apply("{\"sensor_poll_ms\":400}", CFG_SRC_MQTT, &changed, &cmd) != 0);   /* never measures */
    CHECK_EQ(apply("{\"sensor_poll_ms\":450}", CFG_SRC_MQTT, &changed, &cmd), 0);
    CHECK_EQ(apply("{\"sensor_poll_ms\":500}", CFG_SRC_MQTT, &changed, &cmd), 0);
    CHECK(!config_wifi_ok(&cfg) && !config_mqtt_enabled(&cfg));

    /* web update */
    CHECK_EQ(apply("{\"wifi_ssid\":\"Home\",\"wifi_pass\":\"secret123\",\"install_height_cm\":180}",
                   CFG_SRC_WEB, &changed, &cmd), 0);
    CHECK(changed);
    CHECK_STR(cfg.wifi_ssid, "Home");
    CHECK_STR(cfg.wifi_pass, "secret123");
    CHECK_EQ(cfg.install_height_cm, 180);
    CHECK_EQ(apply("{\"install_height_cm\":180}", CFG_SRC_WEB, &changed, &cmd), 0);
    CHECK(!changed);

    /* secrets: "" keeps, null clears */
    CHECK_EQ(apply("{\"wifi_pass\":\"\"}", CFG_SRC_WEB, &changed, &cmd), 0);
    CHECK_STR(cfg.wifi_pass, "secret123");
    CHECK(!changed);
    CHECK_EQ(apply("{\"wifi_pass\":null}", CFG_SRC_WEB, &changed, &cmd), 0);
    CHECK_STR(cfg.wifi_pass, "");
    CHECK(changed);
    CHECK(apply("{\"wifi_ssid\":null}", CFG_SRC_WEB, &changed, &cmd) != 0);    /* not a secret */

    /* atomic: one bad value rejects everything */
    CHECK(apply("{\"install_height_cm\":200,\"sensor_samples\":99}", CFG_SRC_WEB, &changed, &cmd) != 0);
    CHECK_EQ(cfg.install_height_cm, 180);
    CHECK(strstr(err, "sensor_samples") != 0);
    CHECK(apply("{\"bogus\":1}", CFG_SRC_WEB, &changed, &cmd) != 0);
    CHECK(strstr(err, "unknown setting") != 0);
    CHECK(apply("{\"install_height_cm\":\"180\"}", CFG_SRC_WEB, &changed, &cmd) != 0);
    CHECK(apply("{\"install_height_cm\":1.5}", CFG_SRC_WEB, &changed, &cmd) != 0);
    CHECK(apply("not json", CFG_SRC_WEB, &changed, &cmd) != 0);
    CHECK_STR(err, "malformed JSON");

    /* passwords */
    CHECK(apply("{\"wifi_pass\":\"short\"}", CFG_SRC_WEB, &changed, &cmd) != 0);
    CHECK_EQ(apply("{\"wifi_pass\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}",
                   CFG_SRC_WEB, &changed, &cmd), 0);
    CHECK(apply("{\"ap_password\":\"1234567\"}", CFG_SRC_WEB, &changed, &cmd) != 0);
    CHECK_EQ(apply("{\"ap_password\":null}", CFG_SRC_WEB, &changed, &cmd), 0);  /* open AP */
    CHECK_STR(cfg.ap_password, "");

    /* alarms */
    CHECK(apply("{\"upper_alarm_pct\":50,\"lower_alarm_pct\":50}", CFG_SRC_WEB, &changed, &cmd) != 0);
    CHECK_EQ(apply("{\"upper_alarm_pct\":90,\"lower_alarm_pct\":10}", CFG_SRC_WEB, &changed, &cmd), 0);

    /* MQTT source: tank/schedule/sensor only, plus cmd */
    CHECK_EQ(apply("{\"install_height_cm\":150,\"heartbeat_h\":3}", CFG_SRC_MQTT, &changed, &cmd), 0);
    CHECK(changed && cmd == CFG_CMD_NONE);
    CHECK_EQ(cfg.heartbeat_h, 3);
    CHECK(apply("{\"wifi_ssid\":\"evil\"}", CFG_SRC_MQTT, &changed, &cmd) != 0);
    CHECK(strstr(err, "setup page") != 0);
    CHECK(apply("{\"mqtt_host\":\"evil\"}", CFG_SRC_MQTT, &changed, &cmd) != 0);
    CHECK(apply("{\"ap_password\":null}", CFG_SRC_MQTT, &changed, &cmd) != 0);
    CHECK_STR(cfg.wifi_ssid, "Home");
    CHECK_EQ(apply("{\"cmd\":\"report\"}", CFG_SRC_MQTT, &changed, &cmd), 0);
    CHECK(cmd == CFG_CMD_REPORT && !changed);
    CHECK_EQ(apply("{\"cmd\":\"reboot\",\"read_interval_min\":60}", CFG_SRC_MQTT, &changed, &cmd), 0);
    CHECK(cmd == CFG_CMD_REBOOT && changed);
    CHECK(apply("{\"cmd\":\"format\"}", CFG_SRC_MQTT, &changed, &cmd) != 0);
    CHECK(apply("{\"cmd\":\"report\"}", CFG_SRC_WEB, &changed, &cmd) != 0);
    CHECK_EQ(apply("{\"install_height_cm\":200.0}", CFG_SRC_MQTT, &changed, &cmd), 0);  /* HA float */
    CHECK_EQ(cfg.install_height_cm, 200);

    /* MQTT broker settings: TLS needs a CA */
    CHECK(apply("{\"mqtt_host\":\"mqtt.home.lan\"}", CFG_SRC_WEB, &changed, &cmd) != 0);
    CHECK(strstr(err, "mqtt_ca_pem") != 0);
    CHECK(apply("{\"mqtt_host\":\"mqtt.home.lan\",\"mqtt_ca_pem\":\"hello\"}", CFG_SRC_WEB, &changed, &cmd) != 0);
    {
        char body[1024];
        snprintf(body, sizeof(body), "{\"mqtt_host\":\"mqtt.home.lan\",\"mqtt_ca_pem\":\"%s\"}",
                 "-----BEGIN CERTIFICATE-----\\nMIIBfTCCASOg\\n-----END CERTIFICATE-----\\n");
        CHECK_EQ(apply(body, CFG_SRC_WEB, &changed, &cmd), 0);
        CHECK(strstr(cfg.mqtt_ca_pem, "\nMIIBfTCCASOg\n") != 0);
        /* the device-side parser hook can still refuse it */
        CHECK(config_apply_json(&cfg, &tmp, "{\"mqtt_ca_pem\":\"\"}", 18, CFG_SRC_WEB, reject_ca,
                                scratch, sizeof(scratch), &changed, &cmd, err, sizeof(err)) != 0);
        CHECK(strstr(err, "X509") != 0);
    }
    CHECK(apply("{\"mqtt_host\":\"bad host\"}", CFG_SRC_WEB, &changed, &cmd) != 0);
    CHECK(apply("{\"mqtt_host\":\"a..b\"}", CFG_SRC_WEB, &changed, &cmd) != 0);
    CHECK_EQ(apply("{\"mqtt_host\":\"192.168.1.10\"}", CFG_SRC_WEB, &changed, &cmd), 0);

    /* TLS off moves the default port, explicit port wins */
    CHECK_EQ(apply("{\"mqtt_tls\":false}", CFG_SRC_WEB, &changed, &cmd), 0);
    CHECK_EQ(cfg.mqtt_port, 1883);
    CHECK_EQ(apply("{\"mqtt_tls\":true}", CFG_SRC_WEB, &changed, &cmd), 0);
    CHECK_EQ(cfg.mqtt_port, 8883);
    CHECK_EQ(apply("{\"mqtt_tls\":false,\"mqtt_port\":8884}", CFG_SRC_WEB, &changed, &cmd), 0);
    CHECK_EQ(cfg.mqtt_port, 8884);
    CHECK(apply("{\"mqtt_port\":0}", CFG_SRC_WEB, &changed, &cmd) != 0);

    /* topics / ids / hostname */
    CHECK(apply("{\"mqtt_base_topic\":\"a/+/b\"}", CFG_SRC_WEB, &changed, &cmd) != 0);
    CHECK(apply("{\"mqtt_base_topic\":\"/x\"}", CFG_SRC_WEB, &changed, &cmd) != 0);
    CHECK(apply("{\"mqtt_base_topic\":\"\"}", CFG_SRC_WEB, &changed, &cmd) != 0);
    CHECK(apply("{\"mqtt_client_id\":\"a b\"}", CFG_SRC_WEB, &changed, &cmd) != 0);
    CHECK(apply("{\"hostname\":\"-x\"}", CFG_SRC_WEB, &changed, &cmd) != 0);
    CHECK(apply("{\"hostname\":\"tank_1\"}", CFG_SRC_WEB, &changed, &cmd) != 0);
    CHECK_EQ(apply("{\"hostname\":\"tank-1\",\"mqtt_base_topic\":\"home/tank\"}", CFG_SRC_WEB, &changed, &cmd), 0);

    /* static IP */
    CHECK(apply("{\"wifi_static\":true}", CFG_SRC_WEB, &changed, &cmd) != 0);
    CHECK(apply("{\"wifi_static\":true,\"wifi_ip\":\"192.168.1.50\",\"wifi_mask\":\"255.0.255.0\","
                "\"wifi_gw\":\"192.168.1.1\"}", CFG_SRC_WEB, &changed, &cmd) != 0);
    CHECK(apply("{\"wifi_static\":true,\"wifi_ip\":\"192.168.1.50\",\"wifi_mask\":\"255.255.255.0\","
                "\"wifi_gw\":\"192.168.2.1\"}", CFG_SRC_WEB, &changed, &cmd) != 0);
    CHECK(apply("{\"wifi_ip\":\"192.168.1.256\"}", CFG_SRC_WEB, &changed, &cmd) != 0);
    CHECK_EQ(apply("{\"wifi_static\":true,\"wifi_ip\":\"192.168.1.50\",\"wifi_mask\":\"255.255.255.0\","
                   "\"wifi_gw\":\"192.168.1.1\",\"wifi_dns\":\"\"}", CFG_SRC_WEB, &changed, &cmd), 0);
    CHECK(cfg.wifi_ip[3] == 50 && cfg.wifi_mask[2] == 255 && cfg.wifi_dns[0] == 0);

    /* export: no secret values, *_set flags instead */
    CHECK_EQ(apply("{\"wifi_pass\":\"supersecret!\",\"mqtt_pass\":\"brokerpw\"}", CFG_SRC_WEB, &changed, &cmd), 0);
    CHECK(config_to_json(&cfg, false, out, sizeof(out)) > 0);
    CHECK(strstr(out, "supersecret") == 0);
    CHECK(strstr(out, "brokerpw") == 0);
    CHECK(strstr(out, "BEGIN CERTIFICATE") == 0);
    CHECK(strstr(out, "\"wifi_pass_set\":true") != 0);
    CHECK(strstr(out, "\"mqtt_ca_pem_set\":true") != 0);
    CHECK(strstr(out, "\"ap_password_set\":false") != 0);
    CHECK(strstr(out, "\"wifi_ip\":\"192.168.1.50\"") != 0);
    CHECK(strstr(out, "\"wifi_ssid\":\"Home\"") != 0);
    CHECK(config_to_json(&cfg, true, out, sizeof(out)) > 0);
    CHECK(strstr(out, "wifi") == 0 && strstr(out, "mqtt") == 0);
    CHECK(strstr(out, "\"install_height_cm\":200") != 0);
    CHECK_EQ(config_to_json(&cfg, false, out, 64), -1);

    /* export -> apply round trip over the web keeps everything */
    {
        uls_config_t before = cfg;
        CHECK(config_to_json(&cfg, false, out, sizeof(out)) > 0);
        /* *_set keys are not settings: strip them the way the page would */
        char *p;
        while ((p = strstr(out, "_set\":")) != 0) {
            char *k = p; while (k > out && *k != ',' && *k != '{') k--;
            char *e = strchr(p, ','); if (!e) e = strchr(p, '}');
            if (*k == '{') { memmove(k + 1, e + (*e == ',' ? 1 : 0), strlen(e + (*e == ',' ? 1 : 0)) + 1); }
            else memmove(k, e, strlen(e) + 1);
        }
        CHECK_EQ(apply(out, CFG_SRC_WEB, &changed, &cmd), 0);
        CHECK(!changed);
        CHECK(memcmp(&before, &cfg, sizeof(cfg)) == 0);
    }

    /* load: shorter (older) blob gets defaults for the tail; bad fields repaired */
    {
        uls_config_t stored = cfg, loaded;
        CHECK_EQ(config_load_blob(&loaded, &stored, sizeof(stored), MAC), 0);
        CHECK(memcmp(&loaded, &stored, sizeof(stored)) == 0);

        size_t cut = offsetof(uls_config_t, mqtt_host);
        CHECK_EQ(config_load_blob(&loaded, &stored, cut, MAC), 0);
        CHECK_STR(loaded.wifi_ssid, "Home");
        CHECK_STR(loaded.mqtt_host, "");
        CHECK_EQ(loaded.mqtt_port, 8883);

        stored.sensor_samples = 0;                  /* out of range */
        memset(stored.hostname, 'x', sizeof(stored.hostname));     /* unterminated */
        stored.mqtt_tls = 7;
        CHECK_EQ(config_load_blob(&loaded, &stored, sizeof(stored), MAC), 3);
        CHECK_EQ(loaded.sensor_samples, 1);
        CHECK_STR(loaded.hostname, "uls03gw-abcdef");
        CHECK_EQ(loaded.mqtt_tls, 1);
        CHECK_STR(loaded.wifi_ssid, "Home");        /* Wi-Fi survives */
    }

    /* MQTT updates still work if a setup-page field is invalid under newer rules */
    {
        cfg.mqtt_tls = 1;
        memset(cfg.mqtt_ca_pem, 0, sizeof(cfg.mqtt_ca_pem));  /* TLS on, CA missing */
        CHECK(config_validate(&cfg, 0, err, sizeof(err)) != 0);
        CHECK_EQ(apply("{\"tank_volume_l\":500}", CFG_SRC_MQTT, &changed, &cmd), 0);
        CHECK_EQ(cfg.tank_volume_l, 500);
        CHECK(apply("{\"lower_alarm_pct\":95}", CFG_SRC_MQTT, &changed, &cmd) != 0);
    }

    (void)CA;
}
