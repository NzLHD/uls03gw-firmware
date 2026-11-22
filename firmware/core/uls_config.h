/*
 * uls_config.h - settings (spec §4): struct, defaults, validation, JSON in/out. Pure C99.
 *
 * JSON rules (POST /api/config and MQTT <base>/set):
 *   - Any subset of keys. Unknown keys and out-of-range values reject the whole update;
 *     nothing is applied unless everything validates (atomic).
 *   - Secrets (wifi_pass, mqtt_pass, mqtt_ca_pem, ap_password) are write-only:
 *     ""  or absent = keep the old value, null = clear it.
 *   - Over MQTT only the tank, schedule and sensor keys are accepted, plus
 *     {"cmd":"report"} / {"cmd":"reboot"}. Wi-Fi, MQTT, CA and AP keys are refused.
 *   - If mqtt_tls changes and mqtt_port is not given, a default port follows it
 *     (8883 <-> 1883).
 *
 * Storage: the struct is saved as-is by config_store (A/B records with CRC). New fields
 * must only be appended; config_load_blob() fills missing tail fields with defaults.
 */
#ifndef ULS_CONFIG_H
#define ULS_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define CFG_VERSION         1

#define CFG_SSID_MAX        32
#define CFG_WIFI_PASS_MAX   64      /* 8..63 passphrase or 64 hex digits */
#define CFG_HOSTNAME_MAX    32
#define CFG_MQTT_HOST_MAX   128
#define CFG_MQTT_USER_MAX   64
#define CFG_MQTT_PASS_MAX   64
#define CFG_CLIENT_ID_MAX   64
#define CFG_TOPIC_MAX       64
#define CFG_HA_PREFIX_MAX   32
#define CFG_AP_PASS_MAX     63
#define CFG_CA_PEM_MAX      4096

typedef struct {
    /* tank (MQTT-settable) */
    int32_t  install_height_cm;
    int32_t  full_distance_cm;
    int32_t  upper_alarm_pct;
    int32_t  lower_alarm_pct;
    int32_t  tank_volume_l;
    /* schedule (MQTT-settable) */
    int32_t  change_threshold_cm;
    int32_t  read_interval_min;
    int32_t  heartbeat_h;
    int32_t  mains_read_s;
    int32_t  mains_heartbeat_min;
    /* sensor (MQTT-settable) */
    int32_t  sensor_warmup_ms;
    int32_t  sensor_poll_ms;
    int32_t  sensor_samples;
    /* device / AP */
    int32_t  ap_timeout_min;
    char     ap_password[CFG_AP_PASS_MAX + 1];
    /* Wi-Fi */
    char     wifi_ssid[CFG_SSID_MAX + 1];
    char     wifi_pass[CFG_WIFI_PASS_MAX + 1];
    char     hostname[CFG_HOSTNAME_MAX + 1];
    uint8_t  wifi_static;
    uint8_t  wifi_ip[4], wifi_mask[4], wifi_gw[4], wifi_dns[4];
    /* MQTT */
    char     mqtt_host[CFG_MQTT_HOST_MAX + 1];
    int32_t  mqtt_port;
    uint8_t  mqtt_tls;
    uint8_t  mqtt_verify_host;
    uint8_t  ha_discovery;
    char     mqtt_user[CFG_MQTT_USER_MAX + 1];
    char     mqtt_pass[CFG_MQTT_PASS_MAX + 1];
    char     mqtt_client_id[CFG_CLIENT_ID_MAX + 1];
    char     mqtt_base_topic[CFG_TOPIC_MAX + 1];
    char     ha_prefix[CFG_HA_PREFIX_MAX + 1];
    char     mqtt_ca_pem[CFG_CA_PEM_MAX + 1];
} uls_config_t;

typedef enum { CFG_SRC_WEB, CFG_SRC_MQTT } cfg_source_t;

typedef enum { CFG_CMD_NONE = 0, CFG_CMD_REPORT, CFG_CMD_REBOOT } cfg_cmd_t;

/* Optional deeper CA check (on the device: mbedtls_x509_crt_parse). Return 0 if OK,
 * else write a short reason into err. NULL = only the PEM framing is checked. */
typedef int (*cfg_ca_check_fn)(const char *pem, char *err, size_t errlen);

/* Fill with defaults. mac = station MAC (for the XXXXXX suffix), may be NULL. */
void config_defaults(uls_config_t *cfg, const uint8_t mac[6]);

/* Validate everything (ranges, string formats, cross-field rules).
 * Returns 0 if valid, else -1 with a human-readable message in err. */
int  config_validate(const uls_config_t *cfg, cfg_ca_check_fn ca_check, char *err, size_t errlen);

/* Apply a JSON update atomically. On success *cfg is updated, *changed says whether any
 * value differs, *cmd gets an MQTT command (if any). Returns 0, or -1 with err set.
 * scratch: work buffer for JSON strings, at least CFG_CA_PEM_MAX + 256 bytes when the
 * CA can be posted (web), ~256 bytes for MQTT. tmp: a second uls_config_t to build in. */
int  config_apply_json(uls_config_t *cfg, uls_config_t *tmp, const char *json, size_t len,
                       cfg_source_t src, cfg_ca_check_fn ca_check,
                       char *scratch, size_t scratch_len,
                       bool *changed, cfg_cmd_t *cmd, char *err, size_t errlen);

/* Export settings as JSON with secrets omitted; for each secret "<key>_set": bool.
 * mqtt_only = only the MQTT-settable keys (for <base>/config). Returns length or -1. */
int  config_to_json(const uls_config_t *cfg, bool mqtt_only, char *buf, size_t cap);

/* Load a stored blob of `len` bytes (any older, shorter version): defaults first, then
 * the stored prefix. Fields that are out of range or malformed are reset to their
 * default one by one, so a single bad value never costs the Wi-Fi settings.
 * Returns the number of fields reset (0 = clean). */
int  config_load_blob(uls_config_t *cfg, const void *blob, size_t len, const uint8_t mac[6]);

/* Wi-Fi configured (SSID set) -> can leave AP mode. MQTT enabled = host set. */
static inline bool config_wifi_ok(const uls_config_t *c) { return c->wifi_ssid[0] != 0; }
static inline bool config_mqtt_enabled(const uls_config_t *c) { return c->mqtt_host[0] != 0; }

#endif
