/*
 * abstore.h - two-slot (A/B) record store on NOR flash, for the config (spec §4 Storage).
 * Pure C99; flash access goes through callbacks so it runs on a PC with a RAM "flash".
 *
 * Slot layout: [header 24 B][data len B] ... erased (0xFF) to the end of the slot.
 *   header = { magic, version, hdr_size, len, seq, crc32 }
 *   crc32 (IEEE) covers the header with crc32 = 0, followed by the data.
 *
 * Save writes the slot NOT holding the newest valid record: erase, write data, then the
 * header last. A power cut at any point leaves the previous record intact and valid.
 * Load picks the valid slot with the higher seq (wrap-safe comparison).
 */
#ifndef ULS_ABSTORE_H
#define ULS_ABSTORE_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    int  (*read)(void *ctx, uint32_t addr, void *buf, uint32_t len);
    int  (*erase)(void *ctx, uint32_t addr, uint32_t len);       /* whole sectors */
    int  (*write)(void *ctx, uint32_t addr, const void *buf, uint32_t len);
    void *ctx;
} flash_ops_t;

typedef struct {
    const flash_ops_t *ops;
    uint32_t addr[2];       /* slot A and B start (sector aligned)   */
    uint32_t slot_size;     /* bytes per slot (multiple of a sector) */
    uint32_t magic;
    uint16_t version;       /* stored; the loader reports it back    */
} abstore_t;

#define ABSTORE_HDR_SIZE 24u

typedef struct {
    int      slot;          /* 0/1, or -1 if none valid */
    uint32_t seq;
    uint32_t len;
    uint16_t version;
} abstore_info_t;

uint32_t crc32_ieee(uint32_t crc, const void *buf, size_t len);   /* start with crc = 0 */

/* Find the newest valid record and copy up to maxlen bytes of it into data.
 * Returns 0 if one was found (info filled), -1 if neither slot is valid. */
int abstore_load(const abstore_t *s, void *data, uint32_t maxlen, abstore_info_t *info);

/* Write a new record (seq = newest + 1) into the other slot and verify it by reading back.
 * Returns 0 on success, -1 on a flash error or verify failure. */
int abstore_save(const abstore_t *s, const void *data, uint32_t len);

/* Erase both slots (factory reset). */
int abstore_erase(const abstore_t *s);

#endif
