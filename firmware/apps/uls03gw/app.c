/*
 * app.c - ULS-03GW clean firmware: boot, battery mode, mains mode, setup mode (spec §5-§7).
 */
#include "include.h"
#include "rtos_pub.h"
#include "mem_pub.h"
#include "board.h"
#include "sensor.h"
#include "storage.h"
#include "wifi.h"
#include "net_mqtt.h"
#include "web.h"
#include "app.h"
#include "uls_config.h"
#include "level.h"
#include "uls_policy.h"
#include "runstate.h"
#include "ha_discovery.h"
#include "json_mini.h"
#include <stdio.h>
#include <string.h>

#define AWAKE_CAP_S       45
#define WIFI_TIMEOUT_MS   15000
#define SNTP_HOST         "pool.ntp.org"
#define SET_MAX           1024

static uls_config_t g_cfg, g_tmp, g_test;       /* 4.8 KB each */
static char         g_scratch[CFG_CA_PEM_MAX + 256];
static runstate_t   g_rs;
static uint8_t      g_mac[6];
static char         g_client_id[CFG_CLIENT_ID_MAX + 1];
static bool         g_battery;
static uls_wake_t   g_wake;
static level_result_t   g_level;
static sensor_reading_t g_sr;
static bool         g_have_reading;
static int          g_rssi;
static char         g_last_err[96];
static char         g_set_buf[SET_MAX + 1];
static volatile size_t g_set_len;
static volatile bool g_set_pending;
static bool         g_reboot_after;
static uint32_t     g_boot_now_min;

/* lwIP DHCP host name (ethernetif.c asks the app; overrides the weak glue stub) */
const char *CFG_GetOpenBekenHostName(void) { return g_cfg.hostname; }

/* ---------------------------------------------------------------- helpers */

static void topic(char *out, size_t cap, const char *suffix)
{
    snprintf(out, cap, "%s/%s", g_cfg.mqtt_base_topic, suffix);
}

static void level_cfg(level_cfg_t *lc, const uls_config_t *c)
{
    lc->install_height_cm = c->install_height_cm;
    lc->full_distance_cm  = c->full_distance_cm;
    lc->upper_alarm_pct   = c->upper_alarm_pct;
    lc->lower_alarm_pct   = c->lower_alarm_pct;
    lc->tank_volume_l     = c->tank_volume_l;
}

static void take_reading(void)
{
    level_cfg_t lc;
    sensor_read(&g_cfg, &g_sr);
    level_cfg(&lc, &g_cfg);
    level_compute(&lc, g_sr.ok, g_sr.distance_mm, &g_level);
    g_have_reading = true;
    if (g_sr.ok)
        g_rs.last_distance_mm = (uint16_t)(g_sr.distance_mm > 65535 ? 65535 : g_sr.distance_mm);
}

static uint32_t now_min(void)
{
    /* USB mode: the clock advances with uptime (battery mode adds the sleep length on timer wakes) */
    if (!g_battery)
        g_rs.now_min = g_boot_now_min + board_ms() / 60000u;
    return g_rs.now_min;
}

static void on_msg(void *ctx, const char *t, size_t tlen, const uint8_t *p, size_t plen, bool retained)
{
    char set[CFG_TOPIC_MAX + 8];
    (void)ctx; (void)retained;
    topic(set, sizeof(set), "set");
    if (tlen != strlen(set) || memcmp(t, set, tlen) != 0 || plen == 0)
        return;                                 /* empty = our own clear */
    if (plen > SET_MAX) {
        LOG("set: message too long, ignored\r\n");
        return;
    }
    memcpy(g_set_buf, p, plen);
    g_set_buf[plen] = 0;
    g_set_len = plen;
    g_set_pending = true;
}

/* The link status needs a moment after GOT_IP before it reports the signal strength. */
static int read_rssi(void)
{
    for (int i = 0; i < 10; i++) {
        int r = wifi_rssi();
        if (r) return r;
        board_delay(50);
    }
    return 0;
}

static int emit_pub(void *ctx, const char *t, const char *payload)
{
    (void)ctx;
    return mq_publish(t, payload, strlen(payload), 1, true);
}

static int publish_config(void)
{
    char t[CFG_TOPIC_MAX + 8], buf[1024];
    int n = config_to_json(&g_cfg, true, buf, sizeof(buf));
    topic(t, sizeof(t), "config");
    if (n < 0 || mq_publish(t, buf, (size_t)n, 1, true) != 0) return -1;
    g_rs.flags |= RS_FLAG_CONFIG_PUBLISHED;
    return 0;
}

static int publish_discovery(bool force)
{
    static char t[160], p[1100];
    ha_info_t hi = { &g_cfg, g_mac, FW_VERSION, g_battery };
    uint32_t h;
    if (!g_cfg.ha_discovery) return 0;
    if (ha_discovery_build(&hi, t, sizeof(t), p, sizeof(p), 0, 0, &h) != 0) return -1;
    if (!force && h == g_rs.discovery_hash && now_min() - g_rs.discovery_min < 1440)
        return 0;
    if (ha_discovery_build(&hi, t, sizeof(t), p, sizeof(p), emit_pub, 0, &h) != 0) return -1;
    g_rs.discovery_hash = h;
    g_rs.discovery_min = now_min();
    LOG("ha: discovery published\r\n");
    return 1;
}

static int publish_state(void)
{
    char t[CFG_TOPIC_MAX + 8], buf[512];
    state_info_t si = { &g_level, g_battery, g_wake, g_rssi, g_rssi != 0, g_rs.reads_since_send,
                        g_rs.boot_count, FW_VERSION };
    int n = state_json(&si, buf, sizeof(buf));
    topic(t, sizeof(t), "state");
    return n < 0 ? -1 : mq_publish(t, buf, (size_t)n, 1, true);
}

/* Apply a retained <base>/set (spec §7.4), then clear it. */
static void process_set(void)
{
    char t[CFG_TOPIC_MAX + 8], err[128];
    bool changed;
    cfg_cmd_t cmd;
    if (!g_set_pending) return;
    g_set_pending = false;
    if (config_apply_json(&g_cfg, &g_tmp, g_set_buf, g_set_len, CFG_SRC_MQTT, 0, g_scratch, sizeof(g_scratch),
                          &changed, &cmd, err, sizeof(err)) != 0) {
        LOG("set: rejected: %s\r\n", err);
    } else {
        LOG("set: applied (changed=%d cmd=%d)\r\n", changed, cmd);
        if (changed) {
            storage_save_config(&g_cfg);
            g_rs.flags &= (uint8_t)~RS_FLAG_CONFIG_PUBLISHED;
        }
        if (cmd == CFG_CMD_REPORT) g_rs.flags |= RS_FLAG_REPORT_PENDING;
        if (cmd == CFG_CMD_REBOOT) g_reboot_after = true;
    }
    topic(t, sizeof(t), "set");
    mq_publish(t, "", 0, 1, true);              /* clear the retained command */
    if (!(g_rs.flags & RS_FLAG_CONFIG_PUBLISHED)) publish_config();
    if (g_rs.flags & RS_FLAG_REPORT_PENDING) {
        publish_discovery(true);
        publish_state();
        g_rs.flags &= (uint8_t)~RS_FLAG_REPORT_PENDING;
    }
}

static void maybe_sntp(void)
{
    uint32_t t;
    if (g_rs.time_epoch && now_min() - g_rs.time_min < 1440)
        return;
    t = net_sntp(SNTP_HOST, 2000);
    if (t) {
        g_rs.time_epoch = t;
        g_rs.time_min = now_min();
    }
}

/* One battery-mode send (spec §5.1 step 3). 0 = state acknowledged by the broker. */
static int battery_send(char *err, size_t errlen)
{
    char st[CFG_TOPIC_MAX + 8];
    mq_opts_t o = { &g_cfg, g_client_id, NULL, 0, 60, on_msg, 0 };
    int rc = -1;

    if (wifi_sta_connect(&g_cfg, WIFI_TIMEOUT_MS, err, errlen) != 0)
        return -1;
    maybe_sntp();
    o.unix_time = runstate_unix_time(&g_rs);
    if (mq_open(&o, err, errlen) != 0)
        goto out;
    g_rssi = read_rssi();
    {
        int d = publish_discovery((g_rs.flags & RS_FLAG_REPORT_PENDING) != 0);
        if (d == 1) {
            /* discovery changed (e.g. switched from USB to battery): drop the USB-mode
             * retained online/offline status, battery mode has no availability topic */
            char stt[CFG_TOPIC_MAX + 8];
            topic(stt, sizeof(stt), "status");
            mq_publish(stt, "", 0, 1, true);
        }
        if (d < 0) { snprintf(err, errlen, "%s", mq_last_error()); goto out; }
    }
    if ((!(g_rs.flags & RS_FLAG_CONFIG_PUBLISHED) && publish_config() != 0) ||
        publish_state() != 0) {
        snprintf(err, errlen, "%s", mq_last_error());
        goto out;
    }
    rc = 0;
    g_rs.flags &= (uint8_t)~RS_FLAG_REPORT_PENDING;
    /* retained commands for a sleeping device: wait at most 500 ms after SUBACK */
    topic(st, sizeof(st), "set");
    if (mq_subscribe(st, 1) == 0) {
        mq_poll(500);
        process_set();
    }
out:
    mq_close(true);
    wifi_sta_stop();
    return rc;
}

/* ---------------------------------------------------------------- web API */

int app_status_json(char *buf, size_t cap)
{
    json_writer_t w;
    char mac[18];
    snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x", g_mac[0], g_mac[1], g_mac[2], g_mac[3], g_mac[4], g_mac[5]);
    jw_init(&w, buf, cap);
    jw_begin(&w);
    jw_key(&w, "status");
    jw_begin(&w);
    jw_str(&w, "fw", FW_VERSION);
    jw_str(&w, "mac", mac);
    jw_str(&w, "power", g_battery ? "battery" : "usb");
    jw_int_or_null(&w, "rssi_last", g_rssi, g_rssi == 0);
    jw_int_or_null(&w, "distance_mm", g_level.distance_mm, !g_have_reading || g_level.distance_mm == LEVEL_NULL);
    jw_int_or_null(&w, "depth_cm", g_level.depth_cm, !g_have_reading || g_level.depth_cm == LEVEL_NULL);
    jw_int_or_null(&w, "level_pct", g_level.level_pct, !g_have_reading || g_level.level_pct == LEVEL_NULL);
    jw_str(&w, "alarm", g_have_reading ? level_alarm_str(g_level.alarm) : NULL);
    jw_int_or_null(&w, "temp_c", g_sr.temp_c, !g_have_reading || !g_sr.temp_valid);
    jw_str(&w, "last_send", !g_rs.policy.has_sent && !g_rs.policy.fail_count ? NULL :
                            (g_rs.flags & RS_FLAG_LAST_SEND_OK) ? "ok" : "failed");
    jw_str(&w, "last_error", g_last_err[0] ? g_last_err : NULL);
    jw_int(&w, "boot_count", g_rs.boot_count);
    jw_end(&w);
    if (jw_finish(&w) < 0) return -1;
    {
        int n = config_to_json(&g_cfg, false, buf + w.len + 10, cap - w.len - 12);
        if (n < 0) return -1;
        memcpy(buf + w.len, ",\"config\":", 10);
        w.len += 10 + (size_t)n;
        buf[w.len++] = '}';
        buf[w.len] = 0;
    }
    return (int)w.len;
}

int app_read_json(char *buf, size_t cap)
{
    json_writer_t w;
    take_reading();
    jw_init(&w, buf, cap);
    jw_begin(&w);
    jw_int_or_null(&w, "distance_mm", g_level.distance_mm, g_level.distance_mm == LEVEL_NULL);
    jw_int_or_null(&w, "depth_cm", g_level.depth_cm, g_level.depth_cm == LEVEL_NULL);
    jw_int_or_null(&w, "level_pct", g_level.level_pct, g_level.level_pct == LEVEL_NULL);
    jw_str(&w, "alarm", level_alarm_str(g_level.alarm));
    jw_bool(&w, "sensor_ok", g_level.sensor_ok);
    jw_str(&w, "err", level_err_str(g_level.err));
    jw_int_or_null(&w, "temp_c", g_sr.temp_c, !g_sr.temp_valid);
    jw_int(&w, "polls", g_sr.polls);
    jw_int(&w, "ms", g_sr.ms);
    jw_end(&w);
    return jw_finish(&w);
}

static int ok_or_error(char *out, size_t cap, int ok, const char *err, int changed)
{
    json_writer_t w;
    jw_init(&w, out, cap);
    jw_begin(&w);
    jw_bool(&w, "ok", ok);
    if (ok) jw_bool(&w, "changed", changed);
    else jw_str(&w, "error", err);
    jw_end(&w);
    return jw_finish(&w);
}

int app_config_post(const char *body, size_t len, char *out, size_t cap)
{
    char err[128];
    bool changed;
    cfg_cmd_t cmd;
    if (config_apply_json(&g_cfg, &g_tmp, body, len, CFG_SRC_WEB, mq_ca_check, g_scratch, sizeof(g_scratch),
                          &changed, &cmd, err, sizeof(err)) != 0)
        return ok_or_error(out, cap, 0, err, 0);
    if (changed) {
        if (storage_save_config(&g_cfg) != 0)
            return ok_or_error(out, cap, 0, "flash write failed", 0);
        g_rs.flags &= (uint8_t)~RS_FLAG_CONFIG_PUBLISHED;
        storage_save_runstate(&g_rs);
    }
    return ok_or_error(out, cap, 1, 0, changed);
}

int app_test_post(const char *body, size_t len, char *out, size_t cap)
{
    json_writer_t w;
    char err[128] = "";
    char t[CFG_TOPIC_MAX + 8];
    bool changed;
    cfg_cmd_t cmd;
    int step_ok;
    size_t n;

    /* test the posted (unsaved) settings on a copy */
    memcpy(&g_test, &g_cfg, sizeof(g_test));
    if (len && config_apply_json(&g_test, &g_tmp, body, len, CFG_SRC_WEB, mq_ca_check, g_scratch,
                                 sizeof(g_scratch), &changed, &cmd, err, sizeof(err)) != 0)
        return ok_or_error(out, cap, 0, err, 0);

    n = (size_t)snprintf(out, cap, "{\"steps\":[");
#define STEP(name, okv, detail) do { \
        jw_init(&w, out + n, cap - n - 4); jw_begin(&w); jw_str(&w, "step", name); jw_bool(&w, "ok", okv); \
        jw_str(&w, "detail", detail); jw_end(&w); if (jw_finish(&w) < 0) return -1; n += w.len; } while (0)

    if (!config_wifi_ok(&g_test) || !config_mqtt_enabled(&g_test)) {
        STEP("settings", 0, "Wi-Fi SSID and MQTT host are required");
        goto done;
    }
    step_ok = wifi_sta_connect(&g_test, WIFI_TIMEOUT_MS, err, sizeof(err)) == 0;
    if (step_ok) snprintf(err, sizeof(err), "connected, %d dBm", wifi_rssi());
    STEP("wifi", step_ok, err);
    if (!step_ok) goto done;
    out[n++] = ',';
    {
        uint32_t tnow = net_sntp(SNTP_HOST, 2000);
        mq_opts_t o = { &g_test, g_test.mqtt_client_id, NULL, tnow, 30, 0, 0 };
        snprintf(err, sizeof(err), tnow ? "time synced" : "no time (certificate dates not checked)");
        STEP("time", tnow != 0, err);
        out[n++] = ',';
        step_ok = mq_open(&o, err, sizeof(err)) == 0;
        if (step_ok) snprintf(err, sizeof(err), "%s%s", g_test.mqtt_tls ? "TLS ok, " : "plain TCP, ", "broker accepted");
        STEP("mqtt", step_ok, err);
        if (step_ok) {
            snprintf(t, sizeof(t), "%s/test", g_test.mqtt_base_topic);
            step_ok = mq_publish(t, "hello from setup page", 21, 1, false) == 0;
            out[n++] = ',';
            STEP("publish", step_ok, step_ok ? t : mq_last_error());
            mq_close(true);
        }
    }
    wifi_sta_stop();
done:
#undef STEP
    if (n + 3 > cap) return -1;
    out[n++] = ']';
    out[n++] = '}';
    out[n] = 0;
    return (int)n;
}

void app_factory_reset(void)
{
    storage_factory_reset();
}

/* ---------------------------------------------------------------- modes */

static void sleep_battery(void)
{
    uint32_t awake_s = board_ms() / 1000u;
    uint32_t sleep_s = (uint32_t)g_cfg.read_interval_min * 60u;
    sleep_s = sleep_s > awake_s + 10 ? sleep_s - awake_s : 10;
    g_rs.sleep_min = (uint16_t)g_cfg.read_interval_min;
    storage_save_runstate(&g_rs);
    board_deep_sleep(sleep_s);
}

static void cap_expired(void)
{
    /* stuck somewhere (Wi-Fi/TLS): count it as a failed send and sleep */
    policy_cfg_t pc = { g_cfg.change_threshold_cm, (uint32_t)g_cfg.heartbeat_h * 60u };
    policy_on_send_result(&pc, &g_rs.policy, &g_level, now_min(), false);
    g_rs.flags &= (uint8_t)~RS_FLAG_LAST_SEND_OK;
    sleep_battery();
}

static void run_setup(void)
{
    char ssid[24];
    setup_exit_t why;
    snprintf(ssid, sizeof(ssid), "ULS-03GW-%02X%02X%02X", g_mac[3], g_mac[4], g_mac[5]);
    for (;;) {
        why = web_setup_run(ssid, g_cfg.ap_password, (uint32_t)g_cfg.ap_timeout_min * 60000u);
        if (why == SETUP_EXIT_REBOOT) board_reboot();
        /* Leaving setup: with Wi-Fi configured, start fresh in the right mode. */
        if (config_wifi_ok(&g_cfg)) board_reboot();
        if (g_battery) board_deep_sleep(0);    /* unconfigured on battery: button wakes only */
        /* USB powered and still unconfigured: keep offering the AP */
    }
}

static void battery_mode(bool button_press)
{
    policy_cfg_t pc = { g_cfg.change_threshold_cm, (uint32_t)g_cfg.heartbeat_h * 60u };
    policy_event_t ev;
    uint32_t why;
    char err[128] = "";

    board_awake_cap(AWAKE_CAP_S, cap_expired);
    if (g_wake == WAKE_TIMER)
        g_rs.now_min += g_rs.sleep_min;
    take_reading();
    g_rs.reads_since_send++;

    ev.now_min = g_rs.now_min;
    ev.button = button_press;
    ev.settings_changed = !(g_rs.flags & RS_FLAG_CONFIG_PUBLISHED);
    ev.report_cmd = (g_rs.flags & RS_FLAG_REPORT_PENDING) != 0;
    why = policy_should_send(&pc, &g_rs.policy, &g_level, &ev);
    if (!config_wifi_ok(&g_cfg) || !config_mqtt_enabled(&g_cfg))
        why = 0;
    LOG("battery: wake=%s depth=%ld send=0x%02x\r\n", wake_str(g_wake), (long)g_level.depth_cm, (unsigned)why);

    if (why) {
        if (button_press) led_set(LED_CONNECTING);
        int ok = battery_send(err, sizeof(err)) == 0;
        policy_on_send_result(&pc, &g_rs.policy, &g_level, g_rs.now_min, ok);
        if (ok) {
            g_rs.reads_since_send = 0;
            g_rs.flags |= RS_FLAG_LAST_SEND_OK;
            g_last_err[0] = 0;
        } else {
            g_rs.flags &= (uint8_t)~RS_FLAG_LAST_SEND_OK;
            snprintf(g_last_err, sizeof(g_last_err), "%s", err);
        }
        if (button_press) {
            led_set(LED_OFF);
            if (ok && g_level.sensor_ok) led_flash_ok(); else led_flash_error();
            board_delay(1000);
        }
        if (g_reboot_after) { storage_save_runstate(&g_rs); board_reboot(); }
    } else if (button_press) {
        led_flash_error();                      /* nothing configured to send to */
        board_delay(1000);
    }
    sleep_battery();
}

static void mains_mode(void)
{
    policy_cfg_t pc = { g_cfg.change_threshold_cm, (uint32_t)g_cfg.mains_heartbeat_min };
    char err[128], status_t[CFG_TOPIC_MAX + 8], set_t[CFG_TOPIC_MAX + 8];
    uint32_t next_read = 0, retry_at = 0, backoff = 5000;
    bool force = true, was_down = false;
    uint32_t down_since = 0;

    led_set(LED_MAINS);
    topic(status_t, sizeof(status_t), "status");
    topic(set_t, sizeof(set_t), "set");

    for (;;) {
        /* power source changed: restart in the right mode (spec §5.2) */
        if (board_on_battery()) board_reboot();

        /* button: short press = read + send now, hold 4 s = setup AP */
        bool down = board_button_down();
        if (down && !was_down) down_since = board_ms();
        if (down && board_ms() - down_since >= 4000) {
            led_set(LED_HOLD);
            while (board_button_down()) board_delay(50);
            mq_close(true);
            wifi_sta_stop();
            run_setup();
        }
        if (!down && was_down && board_ms() - down_since < 1000) force = true;
        was_down = down;

        bool net_ok = config_wifi_ok(&g_cfg) && config_mqtt_enabled(&g_cfg);
        if (net_ok && !mq_is_open() && (int32_t)(board_ms() - retry_at) >= 0) {
            mq_opts_t o = { &g_cfg, g_client_id, status_t, 0, 60, on_msg, 0 };
            int ok = 0;
            if (!wifi_sta_up() && wifi_sta_connect(&g_cfg, 30000, err, sizeof(err)) != 0) {
                /* err set */
            } else {
                g_rssi = read_rssi();
                maybe_sntp();
                (void)now_min();
                o.unix_time = runstate_unix_time(&g_rs);
                if (mq_open(&o, err, sizeof(err)) == 0 && mq_publish(status_t, "online", 6, 1, true) == 0) {
                    publish_discovery(false);
                    if (!(g_rs.flags & RS_FLAG_CONFIG_PUBLISHED)) publish_config();
                    mq_subscribe(set_t, 1);
                    ok = 1;
                    force = true;
                    backoff = 5000;
                }
            }
            if (!ok) {
                snprintf(g_last_err, sizeof(g_last_err), "%s", err);
                mq_close(false);
                retry_at = board_ms() + backoff;
                if (backoff < 300000) backoff *= 2;
            }
        }

        if (force || (int32_t)(board_ms() - next_read) >= 0) {
            policy_event_t ev = { now_min(), force, !(g_rs.flags & RS_FLAG_CONFIG_PUBLISHED),
                                  (g_rs.flags & RS_FLAG_REPORT_PENDING) != 0 };
            take_reading();
            g_rs.reads_since_send++;
            next_read = board_ms() + (uint32_t)g_cfg.mains_read_s * 1000u;
            uint32_t why = policy_should_send(&pc, &g_rs.policy, &g_level, &ev);
            force = false;
            if (why && mq_is_open()) {
                g_rssi = wifi_rssi();
                int ok = publish_state() == 0;
                policy_on_send_result(&pc, &g_rs.policy, &g_level, now_min(), ok);
                if (ok) {
                    g_rs.reads_since_send = 0;
                    g_rs.flags |= RS_FLAG_LAST_SEND_OK;
                    storage_save_runstate(&g_rs);
                }
            }
        }

        if (mq_is_open()) {
            if (mq_poll(200) < 0) {
                LOG("mains: connection lost\r\n");
                mq_close(false);
                retry_at = board_ms() + 2000;
            }
            process_set();
            if (g_reboot_after) { mq_close(true); board_reboot(); }
        } else {
            board_delay(200);
        }
    }
}

/* Hold time of a button press that started before boot (spec §5.3). */
static uint32_t measure_hold(void)
{
    uint32_t t0 = board_ms();
    if (!board_button_down()) return 0;
    while (board_button_down() && board_ms() - t0 < 30000) {
        uint32_t held = board_ms() + 260 - t0;  /* + measured wake-to-app time */
        led_set(held >= 15000 ? LED_RESET : held >= 4000 ? LED_HOLD : LED_OFF);
        board_delay(30);
    }
    led_set(LED_OFF);
    return board_ms() + 260 - t0;
}

static void app_thread(void *arg)
{
    uint32_t hold;
    (void)arg;

    board_init();
    sensor_init();
    wifi_init();
    board_mac(g_mac);
    storage_load(&g_cfg, &g_rs, g_mac);
    g_rs.boot_count++;
    g_boot_now_min = g_rs.now_min;
    g_wake = board_wake_reason();
    g_battery = board_on_battery();
    /* Only a timer wake knows how much time passed (the planned sleep). After a power-on,
     * reboot or button wake the stored time is stale: drop it, so the next connection
     * resyncs by SNTP (certificate dates are skipped until then, never checked against a
     * wrong time). */
    if (g_wake != WAKE_TIMER)
        g_rs.time_epoch = 0;
    snprintf(g_client_id, sizeof(g_client_id), "%s", g_cfg.mqtt_client_id);
    LOG("ULS-03GW fw %s, wake=%s, power=%s, boot #%u\r\n", FW_VERSION, wake_str(g_wake),
        g_battery ? "battery" : "usb", (unsigned)g_rs.boot_count);

    hold = (g_wake == WAKE_BUTTON || g_wake == WAKE_POWER_ON) ? measure_hold() : 0;
    if (hold >= 15000) {
        LOG("factory reset (button)\r\n");
        storage_factory_reset();
        board_reboot();
    }
    if (hold >= 4000 || (!config_wifi_ok(&g_cfg) && g_wake != WAKE_TIMER)) {
        g_rs.sleep_min = 0;
        run_setup();
    }

    if (g_battery)
        battery_mode(g_wake == WAKE_BUTTON);
    else
        mains_mode();
}

void user_main(void)
{
    rtos_create_thread(NULL, THD_APPLICATION_PRIORITY, "uls", app_thread, 16384, 0);
}
