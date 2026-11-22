/* storage.h - config (A/B, 8 KB slots) and run state (2-sector ring) in the old Tuya KV area. */
#ifndef ULS_STORAGE_H
#define ULS_STORAGE_H

#include "uls_config.h"
#include "runstate.h"

/* Load both; defaults / zeroed run state if nothing valid is stored. */
void storage_load(uls_config_t *cfg, runstate_t *rs, const uint8_t mac[6]);
int  storage_save_config(const uls_config_t *cfg);
int  storage_save_runstate(const runstate_t *rs);
int  storage_factory_reset(void);

#endif
