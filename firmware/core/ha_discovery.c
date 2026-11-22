/* ha_discovery.c - see ha_discovery.h */
#include "ha_discovery.h"
#include "json_mini.h"
#include "abstore.h"            /* crc32_ieee */
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------ state */

const char *wake_str(uls_wake_t w)
{
    switch (w) {
    case WAKE_TIMER:    return "timer";
    case WAKE_BUTTON:   return "button";
    case WAKE_WATCHDOG: return "watchdog";
    default:            return "power_on";
    }
}

int state_json(const state_info_t *si, char *buf, size_t cap)
{
    const level_result_t *r = si->level;
    json_writer_t w;

    jw_init(&w, buf, cap);
    jw_begin(&w);
    jw_int_or_null(&w, "distance_mm", r->distance_mm, r->distance_mm == LEVEL_NULL);
    jw_int_or_null(&w, "depth_cm", r->depth_cm, r->depth_cm == LEVEL_NULL);
    jw_int_or_null(&w, "level_pct", r->level_pct, r->level_pct == LEVEL_NULL);
    jw_str(&w, "alarm", level_alarm_str(r->alarm));
    jw_int_or_null(&w, "volume_l", r->volume_l, r->volume_l == LEVEL_NULL);
    jw_bool(&w, "configured", r->configured);
    jw_bool(&w, "sensor_ok", r->sensor_ok);
    jw_str(&w, "err", level_err_str(r->err));
    jw_str(&w, "power", si->battery ? "battery" : "usb");
    jw_str(&w, "wake", wake_str(si->wake));
    jw_int_or_null(&w, "rssi", si->rssi, !si->rssi_valid);
    jw_int(&w, "reads_since_send", si->reads_since_send);
    jw_int(&w, "boot_count", si->boot_count);
    jw_str(&w, "fw", si->fw);
    jw_end(&w);
    return jw_finish(&w);
}

/* ------------------------------------------------------------ discovery */

enum { C_SENSOR, C_BINARY, C_NUMBER };
static const char *const component_name[] = { "sensor", "binary_sensor", "number" };

#define DIAG 0x01   /* entity_category diagnostic */
#define MEAS 0x02   /* state_class measurement   */
#define VOL  0x04   /* only when tank_volume_l > 0 */

typedef struct {
    uint8_t     comp;
    uint8_t     flags;
    const char *object;         /* object id, also unique_id suffix */
    const char *name;
    const char *field;          /* state: value_json key; number: config key */
    const char *unit;
    const char *device_class;
    int32_t     min, max;       /* numbers */
} entity_t;

static const entity_t entities[] = {
    { C_SENSOR, MEAS,        "level",    "Level",        "level_pct",   "%",   0,                0, 0 },
    { C_SENSOR, MEAS,        "depth",    "Depth",        "depth_cm",    "cm",  "distance",       0, 0 },
    { C_SENSOR, MEAS | DIAG, "distance", "Distance",     "distance_mm", "mm",  "distance",       0, 0 },
    { C_SENSOR, MEAS | VOL,  "volume",   "Volume",       "volume_l",    "L",   "volume_storage", 0, 0 },
    { C_SENSOR, 0,           "alarm",    "Alarm",        "alarm",       0,     "enum",           0, 0 },
    { C_BINARY, 0,           "problem",  "Problem",      "err",         0,     "problem",        0, 0 },
    { C_SENSOR, MEAS | DIAG, "rssi",     "Wi-Fi signal", "rssi",        "dBm", "signal_strength", 0, 0 },
    { C_SENSOR, DIAG,        "power",    "Power source", "power",       0,     0,                0, 0 },
    /* settable over MQTT (§7.4); state from the retained <base>/config */
    { C_NUMBER, 0, "install_height", "Install height",   "install_height_cm", "cm",  "distance", 1, 1000 },
    { C_NUMBER, 0, "full_distance",  "Full distance",    "full_distance_cm",  "cm",  "distance", 0, 999 },
    { C_NUMBER, 0, "upper_alarm",    "Upper alarm",      "upper_alarm_pct",   "%",   0,          0, 100 },
    { C_NUMBER, 0, "lower_alarm",    "Lower alarm",      "lower_alarm_pct",   "%",   0,          0, 100 },
    { C_NUMBER, 0, "heartbeat",      "Heartbeat",        "heartbeat_h",       "h",   0,          1, 24 },
    { C_NUMBER, 0, "read_interval",  "Read interval",    "read_interval_min", "min", 0,          5, 1440 },
};
#define NENT ((int)(sizeof(entities) / sizeof(entities[0])))

int ha_discovery_count(void) { return NENT; }

int ha_discovery_build(const ha_info_t *hi, char *topic, size_t tcap, char *payload, size_t pcap,
                       ha_emit_fn emit, void *ctx, uint32_t *hash)
{
    const uls_config_t *c = hi->cfg;
    char node[16], uid[48], tmpl[96], st_topic[CFG_TOPIC_MAX + 16], cfg_topic[CFG_TOPIC_MAX + 16],
         set_topic[CFG_TOPIC_MAX + 16], avail[CFG_TOPIC_MAX + 16];
    uint32_t h = 0;

    snprintf(node, sizeof(node), "uls03gw_%02x%02x%02x", hi->mac[3], hi->mac[4], hi->mac[5]);
    snprintf(st_topic, sizeof(st_topic), "%s/state", c->mqtt_base_topic);
    snprintf(cfg_topic, sizeof(cfg_topic), "%s/config", c->mqtt_base_topic);
    snprintf(set_topic, sizeof(set_topic), "%s/set", c->mqtt_base_topic);
    snprintf(avail, sizeof(avail), "%s/status", c->mqtt_base_topic);

    for (int i = 0; i < NENT; i++) {
        const entity_t *e = &entities[i];
        json_writer_t w;
        int n = snprintf(topic, tcap, "%s/%s/%s/%s/config",
                         c->ha_prefix, component_name[e->comp], node, e->object);
        if (n < 0 || (size_t)n >= tcap)
            return -1;

        if ((e->flags & VOL) && c->tank_volume_l <= 0) {
            payload[0] = 0;                         /* remove the entity */
        } else {
            jw_init(&w, payload, pcap);
            jw_begin(&w);
            jw_str(&w, "name", e->name);
            snprintf(uid, sizeof(uid), "%s_%s", node, e->object);
            jw_str(&w, "unique_id", uid);

            if (e->comp == C_NUMBER) {
                jw_str(&w, "state_topic", cfg_topic);
                snprintf(tmpl, sizeof(tmpl), "{{ value_json.%s }}", e->field);
                jw_str(&w, "value_template", tmpl);
                jw_str(&w, "command_topic", set_topic);
                snprintf(tmpl, sizeof(tmpl), "{\"%s\": {{ value }}}", e->field);
                jw_str(&w, "command_template", tmpl);
                jw_bool(&w, "retain", true);        /* a sleeping device reads it later */
                jw_int(&w, "min", e->min);
                jw_int(&w, "max", e->max);
                jw_int(&w, "step", 1);
                jw_str(&w, "mode", "box");
                jw_str(&w, "entity_category", "config");
            } else {
                jw_str(&w, "state_topic", st_topic);
                if (e->comp == C_BINARY)
                    snprintf(tmpl, sizeof(tmpl), "{{ 'ON' if value_json.%s else 'OFF' }}", e->field);
                else
                    snprintf(tmpl, sizeof(tmpl), "{{ value_json.%s }}", e->field);
                jw_str(&w, "value_template", tmpl);
                if (e->flags & MEAS)
                    jw_str(&w, "state_class", "measurement");
                if (e->flags & DIAG)
                    jw_str(&w, "entity_category", "diagnostic");
                if (e->device_class && strcmp(e->device_class, "enum") == 0)
                    jw_raw(&w, "options", "[\"normal\",\"lower\",\"upper\"]");
                if (hi->battery)                    /* 2 x heartbeat + 10 min */
                    jw_int(&w, "expire_after", (int64_t)c->heartbeat_h * 7200 + 600);
            }
            if (e->unit)
                jw_str(&w, "unit_of_measurement", e->unit);
            if (e->device_class)
                jw_str(&w, "device_class", e->device_class);
            if (!hi->battery)
                jw_str(&w, "availability_topic", avail);

            jw_key(&w, "device");
            jw_begin(&w);
            {
                char ids[24];                       /* ["uls03gw_xxxxxx"] */
                snprintf(ids, sizeof(ids), "[\"%s\"]", node);
                jw_raw(&w, "identifiers", ids);
            }
            jw_str(&w, "name", "Tank level");
            jw_str(&w, "model", "ULS-03GW");
            jw_str(&w, "manufacturer", "custom");
            jw_str(&w, "sw_version", hi->fw);
            jw_end(&w);
            jw_end(&w);
            if (jw_finish(&w) < 0)
                return -1;
        }

        h = crc32_ieee(h, topic, strlen(topic) + 1);
        h = crc32_ieee(h, payload, strlen(payload) + 1);
        if (emit) {
            int rc = emit(ctx, topic, payload);
            if (rc)
                return rc;
        }
    }
    if (hash)
        *hash = h;
    return 0;
}
