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
    int rc = abstore_save(&cfg_store, cfg, sizeof(*cfg));
    LOG("config saved: %s\r\n", rc ? "FAILED" : "ok");
    return rc;
}

int storage_save_runstate(const runstate_t *rs)
{
    int rc = runstate_save(&rs_store, rs);
    if (rc) LOG("runstate save FAILED\r\n");
    return rc;
}

int storage_factory_reset(void)
{
    int rc = abstore_erase(&cfg_store);
    rc |= runstate_erase(&rs_store);
    LOG("factory reset: %s\r\n", rc ? "FAILED" : "ok");
    return rc;
}
