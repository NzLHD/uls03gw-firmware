/*
 * sdk_stubs.c - symbols the Beken SDK (beken_freertos_sdk, BK7238) expects
 * from its host application. Kept weak so a real implementation in an app wins.
 */
#include "include.h"

/* Referenced by librwnx/libfunc low-voltage PS code; no-op as in OpenBeken. */
void __attribute__((weak)) phy_enter_11b_low_power(void) {}
void __attribute__((weak)) phy_exit_11b_low_power(void) {}

/*
 * lwIP ethernetif.c (MY_OPENBK7231N_USE_MAC_AS_WLAN_NAME) asks the app for the
 * DHCP hostname. Returning NULL/"" makes it fall back to a MAC-based name.
 * The real firmware overrides this with uls03gw-XXXXXX from the config.
 */
const char *__attribute__((weak)) CFG_GetOpenBekenHostName(void)
{
    return 0;
}

/* Captive-portal DNS answer for the SDK's DHCP-server DNS responder (sdk-patches/0004).
 * Returns the reply length, or -1 to keep the SDK's default (REFUSED) answer. */
#include <stdint.h>
#include <stddef.h>
int __attribute__((weak)) uls_dns_reply(const uint8_t *q, size_t qlen, uint8_t *out, size_t cap)
{
    (void)q; (void)qlen; (void)out; (void)cap;
    return -1;
}
