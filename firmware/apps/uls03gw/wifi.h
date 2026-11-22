/* wifi.h - station connect, setup access point, scan (SDK wlan API). */
#ifndef ULS_WIFI_H
#define ULS_WIFI_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "uls_config.h"

#define WIFI_AP_IP      "192.168.4.1"

typedef struct {
    char    ssid[33];
    int8_t  rssi;           /* dBm (approx.) */
    uint8_t ch;
    uint8_t auth;           /* 0 open, 1 WEP, 2 WPA, 3 WPA2, 4 WPA3 */
} wifi_ap_t;

void wifi_init(void);
/* Connect as a station with the config's SSID/password/static IP. 0 = got IP. */
int  wifi_sta_connect(const uls_config_t *cfg, uint32_t timeout_ms, char *err, size_t errlen);
bool wifi_sta_up(void);
int  wifi_rssi(void);                       /* dBm of the current link, 0 if none */
void wifi_sta_stop(void);
/* Start the setup AP (WPA2 with pass, open if pass is empty) on 192.168.4.1 with DHCP. */
int  wifi_ap_start(const char *ssid, const char *pass);
void wifi_ap_stop(void);
/* Blocking scan (~3 s). Returns the number of entries written. */
int  wifi_scan(wifi_ap_t *list, int max);

#endif
