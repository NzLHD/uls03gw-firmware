/* runstate.c - see runstate.h */
#include "runstate.h"
#include <string.h>

#define RS_MAGIC    0x5352u     /* "RS" */
#define SLOTS       (RUNSTATE_SECTOR_SIZE / RUNSTATE_RECORD_SIZE)

typedef struct {
    uint16_t   magic;
    uint8_t    version;
    uint8_t    size;
    uint32_t   seq;
    runstate_t data;
    uint32_t   crc32;
} record_t;

typedef char record_size_check[(sizeof(record_t) == RUNSTATE_RECORD_SIZE) ? 1 : -1];

typedef struct {
    int      found;
    int      sector, slot;
    uint32_t seq;
} pos_t;

static uint32_t rec_crc(const record_t *r)
{
    return crc32_ieee(0, r, offsetof(record_t, crc32));
}

static int rec_valid(const record_t *r)
{
    return r->magic == RS_MAGIC && r->version == RUNSTATE_VERSION &&
           r->size == RUNSTATE_RECORD_SIZE && rec_crc(r) == r->crc32;
}

static int rec_erased(const record_t *r)
{
    const uint8_t *p = (const uint8_t *)r;
    for (size_t i = 0; i < sizeof(*r); i++)
        if (p[i] != 0xFF) return 0;
    return 1;
}

static int seq_newer(uint32_t a, uint32_t b) { return (int32_t)(a - b) > 0; }

static uint32_t slot_addr(const runstate_store_t *s, int sector, int slot)
{
    return s->addr[sector] + (uint32_t)slot * RUNSTATE_RECORD_SIZE;
}

/* Scan both sectors for the newest valid record. */
static int find_newest(const runstate_store_t *s, pos_t *best, record_t *out)
{
    record_t r;
    best->found = 0;
    for (int sec = 0; sec < 2; sec++) {
        for (int i = 0; i < (int)SLOTS; i++) {
            if (s->ops->read(s->ops->ctx, slot_addr(s, sec, i), &r, sizeof(r)))
                return -1;
            if (rec_erased(&r))
                break;                      /* records are appended in order */
            if (!rec_valid(&r))
                continue;                   /* torn write: skip */
            if (!best->found || seq_newer(r.seq, best->seq)) {
                best->found = 1;
                best->sector = sec;
                best->slot = i;
                best->seq = r.seq;
                if (out) *out = r;
            }
        }
    }
    return 0;
}

int runstate_load(const runstate_store_t *s, runstate_t *rs)
{
    pos_t p;
    record_t r;
    memset(rs, 0, sizeof(*rs));
    if (find_newest(s, &p, &r) || !p.found)
        return -1;
    *rs = r.data;
    return 0;
}

int runstate_save(const runstate_store_t *s, const runstate_t *rs)
{
    pos_t p;
    record_t r, back;
    int sec, slot = -1;

    if (find_newest(s, &p, 0))
        return -1;

    sec = p.found ? p.sector : 0;
    /* first erased slot after the newest record in its sector */
    for (int i = p.found ? p.slot + 1 : 0; i < (int)SLOTS; i++) {
        if (s->ops->read(s->ops->ctx, slot_addr(s, sec, i), &r, sizeof(r)))
            return -1;
        if (rec_erased(&r)) { slot = i; break; }
    }
    if (slot < 0 || (!p.found && slot != 0)) {
        /* sector full (or no valid record anywhere but junk in sector 0):
         * start over in the other sector, which still holds nothing newer. */
        sec = p.found ? 1 - sec : 0;
        if (s->ops->erase(s->ops->ctx, s->addr[sec], RUNSTATE_SECTOR_SIZE))
            return -1;
        slot = 0;
    }

    memset(&r, 0, sizeof(r));
    r.magic = RS_MAGIC;
    r.version = RUNSTATE_VERSION;
    r.size = RUNSTATE_RECORD_SIZE;
    r.seq = p.found ? p.seq + 1 : 1;
    r.data = *rs;
    r.crc32 = rec_crc(&r);

    uint32_t a = slot_addr(s, sec, slot);
    if (s->ops->write(s->ops->ctx, a, &r, sizeof(r)))
        return -1;
    if (s->ops->read(s->ops->ctx, a, &back, sizeof(back)) || memcmp(&back, &r, sizeof(r)))
        return -1;
    return 0;
}

int runstate_erase(const runstate_store_t *s)
{
    int rc = s->ops->erase(s->ops->ctx, s->addr[0], RUNSTATE_SECTOR_SIZE);
    rc |= s->ops->erase(s->ops->ctx, s->addr[1], RUNSTATE_SECTOR_SIZE);
    return rc ? -1 : 0;
}

uint32_t runstate_unix_time(const runstate_t *rs)
{
    if (!rs->time_epoch || rs->now_min < rs->time_min)
        return 0;
    return rs->time_epoch + 60u * (rs->now_min - rs->time_min);
}
