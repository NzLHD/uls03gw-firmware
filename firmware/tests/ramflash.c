#include "ramflash.h"
#include <string.h>

ramflash_t rf;
const flash_ops_t rf_ops = { rf_read, rf_erase, rf_write, &rf };

void rf_reset(void)
{
    memset(rf.mem, 0xFF, sizeof(rf.mem));
    rf.fail_write_after = -1;
    rf.erases = rf.writes = 0;
}

int rf_read(void *ctx, uint32_t addr, void *buf, uint32_t len)
{
    ramflash_t *f = ctx;
    if (addr + len > RF_SIZE) return -1;
    memcpy(buf, f->mem + addr, len);
    return 0;
}

int rf_erase(void *ctx, uint32_t addr, uint32_t len)
{
    ramflash_t *f = ctx;
    if (addr % RF_SECTOR || len % RF_SECTOR || addr + len > RF_SIZE) return -1;
    memset(f->mem + addr, 0xFF, len);
    f->erases++;
    return 0;
}

int rf_write(void *ctx, uint32_t addr, const void *buf, uint32_t len)
{
    ramflash_t *f = ctx;
    const uint8_t *b = buf;
    if (addr + len > RF_SIZE) return -1;
    if (f->fail_write_after == 0) return -1;
    if (f->fail_write_after > 0) f->fail_write_after--;
    for (uint32_t i = 0; i < len; i++) f->mem[addr + i] &= b[i];
    f->writes++;
    return 0;
}
