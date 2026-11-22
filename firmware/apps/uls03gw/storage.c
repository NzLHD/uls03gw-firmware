/* storage.c - see storage.h */
#include "include.h"
#include "board.h"
#include "storage.h"
#include "abstore.h"
#include <string.h>

#define CFG_MAGIC 0x55434647u   /* "UCFG" */

static abstore_t cfg_store;
static runstate_store_t rs_store;
static uls_config_t blob;       /* load buffer (4.8 KB: keep it off the stack) */
static volatile bool s_busy, s_frozen;

/* Claim the flash for one write. Set busy first, then check frozen: storage_freeze() sets
 * frozen first, then waits for busy to clear, so the two never write at the same time. */
static bool claim(void)
{
    s_busy = true;
    if (s_frozen) {
        s_busy = false;
        return false;
    }
    return true;
}

static void release(void) { s_busy = false; }

static void init_stores(void)
{
    cfg_store.ops = board_flash_ops();
    cfg_store.addr[0] = FLASH_CFG_A;
    cfg_store.addr[1] = FLASH_CFG_B;
    cfg_store.slot_size = FLASH_CFG_SLOT;
    cfg_store.magic = CFG_MAGIC;
    cfg_store.version = CFG_VERSION;
    rs_store.ops = board_flash_ops();
    rs_store.addr[0] = FLASH_RS_A;
    rs_store.addr[1] = FLASH_RS_B;
}

void storage_load(uls_config_t *cfg, runstate_t *rs, const uint8_t mac[6])
{
    abstore_info_t info;
    init_stores();
    memset(&blob, 0, sizeof(blob));
    if (abstore_load(&cfg_store, &blob, sizeof(blob), &info) == 0) {
        int fixed = config_load_blob(cfg, &blob, info.len, mac);
        LOG("config: slot %d seq %u len %u, %d field(s) reset\r\n", info.slot, (unsigned)info.seq,
            (unsigned)info.len, fixed);
    } else {
        config_defaults(cfg, mac);
        LOG("config: none stored, defaults\r\n");
    }
    if (runstate_load(&rs_store, rs) != 0)
        LOG("runstate: none stored\r\n");
}

int storage_save_config(const uls_config_t *cfg)
{
    int rc;
    if (!claim()) return -1;
    rc = abstore_save(&cfg_store, cfg, sizeof(*cfg));
    release();
    LOG("config saved: %s\r\n", rc ? "FAILED" : "ok");
    return rc;
}

int storage_save_runstate(const runstate_t *rs)
{
    int rc;
    if (!claim()) return -1;
    rc = runstate_save(&rs_store, rs);
    release();
    if (rc) LOG("runstate save FAILED\r\n");
    return rc;
}

int storage_factory_reset(void)
{
    int rc;
    if (!claim()) return -1;
    rc = abstore_erase(&cfg_store);
    rc |= runstate_erase(&rs_store);
    release();
    LOG("factory reset: %s\r\n", rc ? "FAILED" : "ok");
    return rc;
}

void storage_freeze(void)
{
    s_frozen = true;
    for (int i = 0; s_busy && i < 300; i++)     /* a config save takes well under 1 s */
        board_delay(10);
    if (s_busy) LOG("storage: write still in progress after 3 s\r\n");
}

int storage_save_runstate_final(const runstate_t *rs)
{
    int rc;
    if (s_busy) {
        LOG("storage: flash busy, run state not saved\r\n");
        return -1;
    }
    rc = runstate_save(&rs_store, rs);
    if (rc) LOG("runstate save FAILED\r\n");
    return rc;
}
