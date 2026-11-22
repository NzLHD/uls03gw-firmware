/* wifi.c - see wifi.h */
#include "include.h"
#include "rtos_pub.h"
#include "wlan_ui_pub.h"
#include "rw_msg_pub.h"
#include "mem_pub.h"
#include "str_pub.h"
#include "board.h"
#include "wifi.h"
#include <stdio.h>
#include <string.h>

static beken_semaphore_t evt_sem, scan_sem;
static volatile rw_evt_type last_evt = RW_EVT_STA_IDLE;
static volatile bool sta_up;

static void status_cb(void *ctxt)
{
    rw_evt_type e = *(rw_evt_type *)ctxt;
    last_evt = e;
    if (e == RW_EVT_STA_GOT_IP)
        sta_up = true;
    else if (e == RW_EVT_STA_BEACON_LOSE || e == RW_EVT_STA_DEAUTH || e == RW_EVT_STA_DISASSOC ||
             e == RW_EVT_STA_ACTIVE_DISCONNECTED)
        sta_up = false;
    if (e == RW_EVT_STA_GOT_IP || e == RW_EVT_STA_PASSWORD_WRONG || e == RW_EVT_STA_NO_AP_FOUND ||
        e == RW_EVT_STA_AUTH_FAILED || e == RW_EVT_STA_ASSOC_FAILED || e == RW_EVT_STA_ASSOC_FULL)
        rtos_set_semaphore(&evt_sem);
}

static void scan_cb(void *ctxt, uint8_t param)
{
    (void)ctxt; (void)param;
    rtos_set_semaphore(&scan_sem);
}

void wifi_init(void)
{
    rtos_init_semaphore(&evt_sem, 1);
    rtos_init_semaphore(&scan_sem, 1);
    bk_wlan_status_register_cb(status_cb);
}

static void ip_str(char *dst, const uint8_t ip[4])
{
    snprintf(dst, 16, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

int wifi_sta_connect(const uls_config_t *cfg, uint32_t timeout_ms, char *err, size_t errlen)
{
    static network_InitTypeDef_st p;        /* large: keep off the stack */
    uint32_t t0 = board_ms();

    memset(&p, 0, sizeof(p));
    p.wifi_mode = BK_STATION;
    snprintf(p.wifi_ssid, sizeof(p.wifi_ssid), "%s", cfg->wifi_ssid);
    snprintf(p.wifi_key, sizeof(p.wifi_key), "%s", cfg->wifi_pass);
    if (cfg->wifi_static) {
        p.dhcp_mode = DHCP_DISABLE;
        ip_str(p.local_ip_addr, cfg->wifi_ip);
        ip_str(p.net_mask, cfg->wifi_mask);
        ip_str(p.gateway_ip_addr, cfg->wifi_gw);
        ip_str(p.dns_server_ip_addr, (cfg->wifi_dns[0] ? cfg->wifi_dns : cfg->wifi_gw));
    } else {
        p.dhcp_mode = DHCP_CLIENT;
    }
    p.wifi_retry_interval = 100;

    sta_up = false;
    last_evt = RW_EVT_STA_IDLE;
    while (rtos_get_semaphore(&evt_sem, 0) == kNoErr)
        ;                                   /* drop stale events */
    LOG("wifi: connecting (%s)\r\n", cfg->wifi_static ? "static IP" : "DHCP");
    bk_wlan_start(&p);

    while (board_ms() - t0 < timeout_ms) {
        rtos_get_semaphore(&evt_sem, 200);
        if (sta_up) {
            LOG("wifi: got IP in %u ms, rssi %d\r\n", (unsigned)(board_ms() - t0), wifi_rssi());
            return 0;
        }
        if (last_evt == RW_EVT_STA_PASSWORD_WRONG) {
            snprintf(err, errlen, "wifi: wrong password");
            goto fail;
        }
        if (last_evt == RW_EVT_STA_NO_AP_FOUND && board_ms() - t0 > timeout_ms / 2) {
            snprintf(err, errlen, "wifi: network not found");
            goto fail;
        }
    }
    snprintf(err, errlen, last_evt == RW_EVT_STA_CONNECTED ? "wifi: no IP address (DHCP timeout)"
                                                           : "wifi: connect timeout");
fail:
    LOG("%s\r\n", err);
    wifi_sta_stop();
    return -1;
}

bool wifi_sta_up(void) { return sta_up; }

int wifi_rssi(void)
{
    LinkStatusTypeDef ls;
    memset(&ls, 0, sizeof(ls));
    if (bk_wlan_get_link_status(&ls) != kNoErr || !ls.conn_state)
        return 0;
    return ls.wifi_strength;
}

void wifi_sta_stop(void)
{
    bk_wlan_stop(BK_STATION);
    sta_up = false;
}

int wifi_ap_start(const char *ssid, const char *pass)
{
    static network_InitTypeDef_st p;
    memset(&p, 0, sizeof(p));
    p.wifi_mode = BK_SOFT_AP;
    snprintf(p.wifi_ssid, sizeof(p.wifi_ssid), "%s", ssid);
    snprintf(p.wifi_key, sizeof(p.wifi_key), "%s", pass ? pass : "");
    snprintf(p.local_ip_addr, sizeof(p.local_ip_addr), WIFI_AP_IP);
    snprintf(p.net_mask, sizeof(p.net_mask), "255.255.255.0");
    snprintf(p.gateway_ip_addr, sizeof(p.gateway_ip_addr), WIFI_AP_IP);
    snprintf(p.dns_server_ip_addr, sizeof(p.dns_server_ip_addr), WIFI_AP_IP);
    p.dhcp_mode = DHCP_SERVER;
    p.wifi_retry_interval = 100;
    LOG("wifi: AP \"%s\" (%s)\r\n", ssid, (pass && pass[0]) ? "WPA2" : "open");
    return bk_wlan_start(&p) == kNoErr ? 0 : -1;
}

void wifi_ap_stop(void) { bk_wlan_stop(BK_SOFT_AP); }

static uint8_t auth_of(wlan_sec_type_t s)
{
    switch (s) {
    case BK_SECURITY_TYPE_NONE: return 0;
    case BK_SECURITY_TYPE_WEP:  return 1;
    case BK_SECURITY_TYPE_WPA_TKIP: case BK_SECURITY_TYPE_WPA_AES: case BK_SECURITY_TYPE_WPA_MIXED: return 2;
    case BK_SECURITY_TYPE_WPA3_SAE: case BK_SECURITY_TYPE_WPA3_WPA2_MIXED: return 4;
    default: return 3;
    }
}

int wifi_scan(wifi_ap_t *list, int max)
{
    ScanResult_adv res;
    int n = 0;

    memset(&res, 0, sizeof(res));
    while (rtos_get_semaphore(&scan_sem, 0) == kNoErr)
        ;
    bk_wlan_scan_ap_reg_cb(scan_cb);
    bk_wlan_start_scan();
    rtos_get_semaphore(&scan_sem, 5000);
    /* with the setup AP up, results come from the AP side (as in the SDK scan demo) */
    int rc = bk_wlan_ap_is_up() > 0 ? wlan_ap_scan_result(&res) : wlan_sta_scan_result(&res);
    if (rc == 0 && res.ApList) {
        for (int i = 0; i < res.ApNum && n < max; i++) {
            if (!res.ApList[i].ssid[0])
                continue;                   /* hidden */
            int dup = 0;
            for (int k = 0; k < n; k++)
                if (!strcmp(list[k].ssid, res.ApList[i].ssid)) { dup = 1; break; }
            if (dup)
                continue;
            snprintf(list[n].ssid, sizeof(list[n].ssid), "%s", res.ApList[i].ssid);
            list[n].rssi = (int8_t)res.ApList[i].ApPower;
            list[n].ch = (uint8_t)res.ApList[i].channel;
            list[n].auth = auth_of(res.ApList[i].security);
            n++;
        }
    }
    if (res.ApList)
        os_free(res.ApList);
    bk_wlan_scan_ap_reg_cb(NULL);
    LOG("wifi: scan found %d network(s)\r\n", n);
    return n;
}
