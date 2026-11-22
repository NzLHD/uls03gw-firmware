/*
 * ha_discovery.h - MQTT payloads: the state message (spec §7.3) and Home Assistant
 * discovery (§7.5). Pure C99.
 *
 * Discovery topics: <ha_prefix>/<component>/uls03gw_xxxxxx/<object>/config, retained.
 * A disabled entity (volume when tank_volume_l = 0) gets an empty retained payload so
 * Home Assistant removes it. The caller publishes; this module only builds payloads and
 * a CRC32 over all of them, stored in run state so normal sends can skip discovery.
 */
#ifndef ULS_HA_DISCOVERY_H
#define ULS_HA_DISCOVERY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "uls_config.h"
#include "level.h"

typedef enum { WAKE_TIMER, WAKE_BUTTON, WAKE_POWER_ON, WAKE_WATCHDOG } uls_wake_t;

typedef struct {
    const level_result_t *level;
    bool        battery;            /* power: "battery" / "usb"           */
    uls_wake_t  wake;
    int32_t     rssi;               /* dBm; ignored if !rssi_valid        */
    bool        rssi_valid;
    uint32_t    reads_since_send;
    uint32_t    boot_count;
    const char *fw;
} state_info_t;

/* Build the <base>/state JSON. Returns its length, or -1 if cap is too small. */
int state_json(const state_info_t *si, char *buf, size_t cap);

const char *wake_str(uls_wake_t w);

/* Called once per discovery message; payload "" means "remove this entity".
 * Return non-zero to abort (returned from ha_discovery_build). */
typedef int (*ha_emit_fn)(void *ctx, const char *topic, const char *payload);

typedef struct {
    const uls_config_t *cfg;
    const uint8_t      *mac;        /* station MAC, for the node id */
    const char         *fw;
    bool                battery;    /* battery: expire_after; mains: availability topic */
} ha_info_t;

/* Build every discovery message, call emit for each (emit may be NULL to only hash).
 * topic/payload are work buffers (topic >= 128 B, payload >= 1024 B recommended).
 * *hash = CRC32 over all topics and payloads. Returns 0, -1 if a buffer was too
 * small, or emit's non-zero value. */
int ha_discovery_build(const ha_info_t *hi, char *topic, size_t tcap, char *payload, size_t pcap,
                       ha_emit_fn emit, void *ctx, uint32_t *hash);

/* Number of discovery messages ha_discovery_build emits (for tests / buffer planning). */
int ha_discovery_count(void);

#endif
