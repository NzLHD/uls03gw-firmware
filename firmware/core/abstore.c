/* abstore.c - see abstore.h */
#include "abstore.h"
#include <string.h>

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t hdr_size;
    uint32_t len;
    uint32_t seq;
    uint32_t crc32;
    uint32_t reserved;      /* 0 */
} hdr_t;

typedef char hdr_size_check[(sizeof(hdr_t) == ABSTORE_HDR_SIZE) ? 1 : -1];

uint32_t crc32_ieee(uint32_t crc, const void *buf, size_t len)
{
    const uint8_t *p = buf;
    crc = ~crc;
    while (len--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

/* CRC of the record in flash, streamed in chunks (data can be several KB). */
static int record_crc(const abstore_t *s, uint32_t base, const hdr_t *h, uint32_t *out)
{
    hdr_t z = *h;
    uint8_t chunk[128];
    uint32_t crc, off = 0;

    z.crc32 = 0;
    crc = crc32_ieee(0, &z, sizeof(z));
    while (off < h->len) {
        uint32_t n = h->len - off < sizeof(chunk) ? h->len - off : (uint32_t)sizeof(chunk);
        if (s->ops->read(s->ops->ctx, base + ABSTORE_HDR_SIZE + off, chunk, n))
            return -1;
        crc = crc32_ieee(crc, chunk, n);
        off += n;
    }
    *out = crc;
    return 0;
}

static int slot_valid(const abstore_t *s, int slot, hdr_t *h)
{
    uint32_t crc;
    if (s->ops->read(s->ops->ctx, s->addr[slot], h, sizeof(*h)))
        return 0;
    if (h->magic != s->magic || h->hdr_size != ABSTORE_HDR_SIZE ||
        h->len > s->slot_size - ABSTORE_HDR_SIZE)
        return 0;
    if (record_crc(s, s->addr[slot], h, &crc))
        return 0;
    return crc == h->crc32;
}

/* seq a is newer than b (wrap-safe) */
static int seq_newer(uint32_t a, uint32_t b) { return (int32_t)(a - b) > 0; }

static int newest(const abstore_t *s, hdr_t *h_out)
{
    hdr_t h[2];
    int v0 = slot_valid(s, 0, &h[0]);
    int v1 = slot_valid(s, 1, &h[1]);
    int pick = -1;
    if (v0 && v1)  pick = seq_newer(h[1].seq, h[0].seq) ? 1 : 0;
    else if (v0)   pick = 0;
    else if (v1)   pick = 1;
    if (pick >= 0 && h_out)
        *h_out = h[pick];
    return pick;
}

int abstore_load(const abstore_t *s, void *data, uint32_t maxlen, abstore_info_t *info)
{
    hdr_t h;
    int slot = newest(s, &h);
    if (info) {
        info->slot = slot;
        info->seq = slot >= 0 ? h.seq : 0;
        info->len = slot >= 0 ? h.len : 0;
        info->version = slot >= 0 ? h.version : 0;
    }
    if (slot < 0)
        return -1;
    uint32_t n = h.len < maxlen ? h.len : maxlen;
    return s->ops->read(s->ops->ctx, s->addr[slot] + ABSTORE_HDR_SIZE, data, n) ? -1 : 0;
}

int abstore_save(const abstore_t *s, const void *data, uint32_t len)
{
    hdr_t cur, h;
    uint32_t crc;
    int now = newest(s, &cur);
    int dst = now == 0 ? 1 : 0;
    uint32_t base = s->addr[dst];

    if (len > s->slot_size - ABSTORE_HDR_SIZE)
        return -1;

    memset(&h, 0, sizeof(h));
    h.magic = s->magic;
    h.version = s->version;
    h.hdr_size = ABSTORE_HDR_SIZE;
    h.len = len;
    h.seq = now >= 0 ? cur.seq + 1 : 1;
    h.crc32 = 0;
    crc = crc32_ieee(0, &h, sizeof(h));
    h.crc32 = crc32_ieee(crc, data, len);

    if (s->ops->erase(s->ops->ctx, base, s->slot_size))
        return -1;
    if (len && s->ops->write(s->ops->ctx, base + ABSTORE_HDR_SIZE, data, len))
        return -1;
    if (s->ops->write(s->ops->ctx, base, &h, sizeof(h)))       /* commit */
        return -1;

    hdr_t back;
    if (!slot_valid(s, dst, &back) || back.seq != h.seq)
        return -1;
    return 0;
}

int abstore_erase(const abstore_t *s)
{
    int rc = s->ops->erase(s->ops->ctx, s->addr[0], s->slot_size);
    rc |= s->ops->erase(s->ops->ctx, s->addr[1], s->slot_size);
    return rc ? -1 : 0;
}
