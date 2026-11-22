/* web.h - setup access point: captive DNS + HTTP server with the setup page (spec §6). */
#ifndef ULS_WEB_H
#define ULS_WEB_H

#include <stdint.h>

typedef enum {
    SETUP_EXIT_TIMEOUT,     /* no request for ap_timeout_min */
    SETUP_EXIT_BUTTON,      /* button pressed */
    SETUP_EXIT_REBOOT,      /* reboot / factory reset requested from the page */
} setup_exit_t;

/* Scan, start the AP, serve until timeout / button / reboot request, stop the AP. */
setup_exit_t web_setup_run(const char *ssid, const char *pass, uint32_t idle_timeout_ms);

#endif
