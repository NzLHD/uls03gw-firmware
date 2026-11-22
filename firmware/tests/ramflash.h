/* ramflash.h - RAM flash with NOR semantics for the tests: erase sets 0xFF per 4 KB sector,
 * write can only clear bits, optional "power cut" after n writes. */
#ifndef RAMFLASH_H
#define RAMFLASH_H
#include <stdint.h>
#include "abstore.h"

#define RF_SECTOR 4096u
#define RF_SIZE   (8 * RF_SECTOR)

typedef struct {
    uint8_t mem[RF_SIZE];
    int     fail_write_after;   /* -1 = never; else writes left before "power cut" */
    int     erases, writes;
} ramflash_t;

extern ramflash_t rf;
extern const flash_ops_t rf_ops;
void rf_reset(void);            /* all 0xFF, no failures, counters 0 */

int rf_read(void *ctx, uint32_t addr, void *buf, uint32_t len);
int rf_erase(void *ctx, uint32_t addr, uint32_t len);
int rf_write(void *ctx, uint32_t addr, const void *buf, uint32_t len);

#endif
