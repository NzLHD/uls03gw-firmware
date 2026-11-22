/*
 * log_filter.c - keep secrets out of the UART log (spec §10, acceptance test 16).
 *
 * The SDK's closed wpa_supplicant library (libsupplicant) prints the Wi-Fi SSID with its
 * passphrase ("PSKC: ssid %s, passphrase %s") and the session keys byte by byte
 * ("WPA: TK " / "WPA: GTK " followed by one "%02x" call per byte). It calls bk_printf()
 * directly, so sdk_glue.mk links with -Wl,--wrap=bk_printf and every call from the
 * libraries lands here first.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

extern void __real_bk_printf(const char *fmt, ...);

static volatile int in_key_dump;

void __wrap_bk_printf(const char *fmt, ...)
{
    char buf[160];
    va_list ap;

    if (!fmt)
        return;
    if (in_key_dump) {
        if (!strcmp(fmt, "%02x") || !strcmp(fmt, "%02X"))
            return;                         /* one key byte */
        in_key_dump = 0;
        if (fmt[0] == '\r' || fmt[0] == '\n')
            return;                         /* end of the dump line */
    }
    if (!strncmp(fmt, "WPA: TK", 7) || !strncmp(fmt, "WPA: GTK", 8) || !strncmp(fmt, "WPA: PMK", 8) ||
        !strncmp(fmt, "WPA: PTK", 8) || !strncmp(fmt, "WPA: KCK", 8) || !strncmp(fmt, "WPA: KEK", 8)) {
        __real_bk_printf("%.8s(hidden)\r\n", fmt);
        in_key_dump = 1;
        return;
    }
    if (strstr(fmt, "passphrase") || strstr(fmt, "password") || strstr(fmt, "psk=")) {
        __real_bk_printf("[log line with a secret hidden]\r\n");
        return;
    }
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    __real_bk_printf("%s", buf);
}
