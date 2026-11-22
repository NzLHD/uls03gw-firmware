/*
 * runstate.h - state that must survive deep sleep (a reset: no RAM is kept), spec §4.
 * Pure C99; flash access through flash_ops_t (abstore.h), so it runs on a PC.
 *
 * Storage: append-only ring of fixed 64-byte records over two 4 KB sectors (64 per
 * sector). Each record: { magic u16, version u8, size u8, seq u32, payload 52 B, crc32 }.
 * Load = the valid record with the highest seq (wrap-safe). Append writes the next erased
 * slot after it; when a sector is full the other sector is erased and used. A torn write
 * fails its CRC and is skipped, so the previous record stays current.
 * One record per 30-min read is ~17,500 a year = ~140 erases per sector a year.
 *
 * Rarely-changing Wi-Fi data (BSSID/channel/PMK, ~44 B) does not belong here; it goes in
 * its own A/B record (abstore) and is written only when it changes.
 */
#ifndef ULS_RUNSTATE_H
#define ULS_RUNSTATE_H

#include <stdint.h>
#include "abstore.h"
#include "uls_policy.h"

#define RUNSTATE_RECORD_SIZE   64u
#define RUNSTATE_SECTOR_SIZE   4096u
#define RUNSTATE_VERSION       1

/* 52 bytes; append fields only in the reserved space, bump RUNSTATE_VERSION if the
 * meaning of an existing field changes (older records are then ignored). */
typedef struct {
    policy_state_t policy;          /* 20: last successful send, back-off            */
    uint32_t now_min;               /* monotonic minutes across sleeps               */
    uint32_t boot_count;            /* every boot, incl. deep-sleep wakes            */
    uint16_t reads_since_send;
    uint16_t last_distance_mm;      /* last good reading (for the setup page)        */
    uint32_t discovery_hash;        /* crc32 of the last published HA discovery set  */
    uint32_t discovery_min;         /* now_min when discovery was last published     */
    uint32_t time_epoch;            /* unix time at time_min (0 = never synced)      */
    uint32_t time_min;              /* now_min when time_epoch was valid             */
    uint8_t  flags;                 /* RS_FLAG_*                                     */
    uint8_t  reserved;
    uint16_t sleep_min;             /* length of the deep sleep that was started     */
} runstate_t;

#define RS_FLAG_CONFIG_PUBLISHED  0x01   /* <base>/config published for the current config */
#define RS_FLAG_REPORT_PENDING    0x02   /* {"cmd":"report"} received                      */
#define RS_FLAG_LAST_SEND_OK      0x04   /* result of the last send attempt (status page)   */

typedef char runstate_size_check[(sizeof(runstate_t) == 52) ? 1 : -1];

typedef struct {
    const flash_ops_t *ops;
    uint32_t addr[2];               /* two sector-aligned sectors */
} runstate_store_t;

/* Load the newest valid record into *rs. Returns 0, or -1 if none (rs zeroed). */
int runstate_load(const runstate_store_t *s, runstate_t *rs);

/* Append *rs as a new record. Returns 0, or -1 on a flash error / failed verify. */
int runstate_save(const runstate_store_t *s, const runstate_t *rs);

/* Erase both sectors (factory reset). */
int runstate_erase(const runstate_store_t *s);

/* Unix time now (0 if never synced): time_epoch + 60 * (now_min - time_min). */
uint32_t runstate_unix_time(const runstate_t *rs);

#endif
