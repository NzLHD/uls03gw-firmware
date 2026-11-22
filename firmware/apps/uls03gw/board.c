/* board.c - see board.h */
#include "include.h"
#include "rtos_pub.h"
#include "gpio_pub.h"
#include "uart_pub.h"
#include "drv_model_pub.h"
#include "manual_ps_pub.h"
#include "start_type_pub.h"
#include "sys_ctrl_pub.h"
#include "wlan_ui_pub.h"
#include "mem_pub.h"
#include "BkDriverFlash.h"
#include "board.h"

/* wake registers sampled at boot (sdk-patches/0003) */
extern UINT32 uls_boot_retention, uls_boot_rosc_timer, uls_boot_gpio_wake_raw, uls_boot_gpio_wake_bak;
#define ROSC_TIMER_INT_STATUS  (1UL << 8)       /* SCTRL_ROSC_TIMER, see sys_ctrl.h */

#undef BIT
#define BIT(n) (1UL << (n))

static volatile led_mode_t led_mode = LED_OFF;
static volatile int        led_oneshot;     /* 1 = ok flash, 2 = error flashes */
static beken_timer_t       cap_timer;
static void              (*cap_cb)(void);

uint32_t board_ms(void)            { return rtos_get_time(); }
void     board_delay(uint32_t ms)  { rtos_delay_milliseconds(ms); }

static void out(UINT32 pin, UINT32 v) { gpio_config(pin, GMODE_OUTPUT); gpio_output(pin, v); }

static void led_thread(void *arg)
{
    uint32_t t = 0;
    (void)arg;
    for (;;) {
        int blue = 0, red = 0;
        if (led_oneshot == 1) {
            gpio_output(PIN_LED_BLUE, 1); gpio_output(PIN_LED_RED, 0);
            rtos_delay_milliseconds(200);
            led_oneshot = 0;
            continue;
        }
        if (led_oneshot == 2) {
            gpio_output(PIN_LED_BLUE, 0);
            for (int i = 0; i < 3; i++) {
                gpio_output(PIN_LED_RED, 1); rtos_delay_milliseconds(150);
                gpio_output(PIN_LED_RED, 0); rtos_delay_milliseconds(150);
            }
            led_oneshot = 0;
            continue;
        }
        switch (led_mode) {
        case LED_AP:         blue = (t % 1000) < 500; break;
        case LED_HOLD:       blue = 1; break;
        case LED_RESET:      blue = red = (t % 200) < 100; break;
        case LED_CONNECTING: blue = (t % 200) < 100; break;
        case LED_MAINS:      red = 1; break;
        default:             break;
        }
        gpio_output(PIN_LED_BLUE, blue);
        gpio_output(PIN_LED_RED, red);
        if (led_mode == LED_OFF && !led_oneshot) {
            rtos_delay_milliseconds(100);
            t += 100;
        } else {
            rtos_delay_milliseconds(50);
            t += 50;
        }
    }
}

void led_set(led_mode_t m)  { led_mode = m; }
void led_flash_ok(void)     { led_oneshot = 1; }
void led_flash_error(void)  { led_oneshot = 2; }

void board_init(void)
{
    out(PIN_SENSOR_PWR, 0);
    out(PIN_LED_RED, 0);
    out(PIN_LED_BLUE, 0);
    gpio_config(PIN_BUTTON, GMODE_INPUT_PULLUP);
    gpio_config(PIN_PWRMODE, GMODE_INPUT_PULLUP);
    gpio_config(PIN_UART1_TX, GMODE_INPUT);     /* head unpowered: don't back-power it */
    gpio_config(PIN_UART1_RX, GMODE_INPUT);
    rtos_create_thread(NULL, BEKEN_DEFAULT_WORKER_PRIORITY, "led", led_thread, 1024, 0);
}

uls_wake_t board_wake_reason(void)
{
    RESET_SOURCE_STATUS st = bk_misc_get_start_type();
    uls_wake_t w;
    switch (st) {
    case RESET_SOURCE_DEEPPS_RTC:  w = WAKE_TIMER; break;
    case RESET_SOURCE_DEEPPS_GPIO: w = WAKE_BUTTON; break;
    case RESET_SOURCE_WATCHDOG:    w = WAKE_WATCHDOG; break;
    case RESET_SOURCE_POWERON:
        /* Tuya's bootloader: retention reads 2 after any deep-sleep wake (spec §0). A button
         * wake shows up as DEEPPS_GPIO (patch 0003), so a deep-sleep wake reported as
         * power-on is the RTC unless the P6 bit is set after all. */
        if ((uls_boot_retention & SW_RETENTION_VAL_MASK) == 2) {
            if ((uls_boot_gpio_wake_raw | uls_boot_gpio_wake_bak) & BIT(PIN_BUTTON))
                w = WAKE_BUTTON;
            else
                w = WAKE_TIMER;
        } else {
            w = WAKE_POWER_ON;
        }
        break;
    default:
        w = WAKE_POWER_ON;
        break;
    }
    LOG("wake: start_type=%u retention=0x%x rosc=0x%x gpio_raw=0x%x gpio_bak=0x%x\r\n", (unsigned)st,
        (unsigned)uls_boot_retention, (unsigned)uls_boot_rosc_timer, (unsigned)uls_boot_gpio_wake_raw,
        (unsigned)uls_boot_gpio_wake_bak);
    return w;
}

bool board_on_battery(void)    { return gpio_input(PIN_PWRMODE) != 0; }
bool board_button_down(void)   { return gpio_input(PIN_BUTTON) == 0; }

void board_mac(uint8_t mac[6])
{
    char m[6] = {0};
    bk_wifi_get_station_mac_address(m);
    os_memcpy(mac, m, 6);
}

void board_deep_sleep(uint32_t seconds)
{
    PS_DEEP_CTRL_PARAM p;

    led_mode = LED_OFF;
    led_oneshot = 0;
    gpio_output(PIN_SENSOR_PWR, 0);
    gpio_output(PIN_LED_RED, 0);
    gpio_output(PIN_LED_BLUE, 0);
    gpio_config(PIN_UART1_TX, GMODE_INPUT);
    gpio_config(PIN_UART1_RX, GMODE_INPUT);

    os_memset(&p, 0, sizeof(p));
    p.wake_up_way      = PS_DEEP_WAKEUP_GPIO | (seconds ? PS_DEEP_WAKEUP_RTC : 0);
    p.gpio_index_map   = BIT(PIN_BUTTON);
    p.gpio_edge_map    = BIT(PIN_BUTTON);
    p.gpio_stay_lo_map = BIT(PIN_SENSOR_PWR) | BIT(PIN_LED_RED) | BIT(PIN_LED_BLUE);
    p.sleep_time       = seconds;
    p.lpo_32k_src      = LPO_SELECT_ROSC;
    LOG("deep sleep %u s\r\n", (unsigned)seconds);
    rtos_delay_milliseconds(20);
    bk_enter_deep_sleep_mode(&p);
    for (;;)
        ;
}

void board_reboot(void)
{
    LOG("reboot\r\n");
    rtos_delay_milliseconds(50);
    bk_reboot();
    for (;;)
        ;
}

static void cap_expired(void *arg)
{
    (void)arg;
    LOG("awake cap reached\r\n");
    if (cap_cb)
        cap_cb();
}

void board_awake_cap(uint32_t seconds, void (*on_expire)(void))
{
    cap_cb = on_expire;
    rtos_init_timer(&cap_timer, seconds * 1000u, cap_expired, 0);
    rtos_start_timer(&cap_timer);
}

/* ---------------------------------------------------------------- watchdog */

/* The SDK feeds the hardware watchdog from the tick interrupt and its task watchdog only
 * prints, so neither catches a stuck application thread. This one runs in the timer task
 * and reboots when the main loop hasn't fed it for wd_limit_ms. */
static beken_timer_t       wd_timer;
static volatile uint32_t   wd_fed, wd_limit_ms;

static void wd_check(void *arg)
{
    (void)arg;
    if (wd_limit_ms && board_ms() - wd_fed > wd_limit_ms) {
        LOG("watchdog: main loop stuck for %u s, rebooting\r\n", (unsigned)((board_ms() - wd_fed) / 1000u));
        board_reboot();
    }
}

void board_watchdog_start(uint32_t seconds)
{
    static bool started;
    wd_fed = board_ms();
    wd_limit_ms = seconds * 1000u;
    if (!started) {
        started = true;
        rtos_init_timer(&wd_timer, 5000, wd_check, 0);
        rtos_start_timer(&wd_timer);
    }
}

void board_watchdog_feed(void) { wd_fed = board_ms(); }

/* ---------------------------------------------------------------- flash */

static int in_own_area(uint32_t addr, uint32_t len)
{
    return addr >= FLASH_OWN_START && len <= FLASH_OWN_END - addr;
}

static int fl_read(void *ctx, uint32_t addr, void *buf, uint32_t len)
{
    (void)ctx;
    if (!in_own_area(addr, len)) return -1;
    bk_flash_abs_addr_read(addr, buf, len);
    return 0;
}

static int fl_erase(void *ctx, uint32_t addr, uint32_t len)
{
    (void)ctx;
    if (!in_own_area(addr, len) || (addr & 0xFFF) || (len & 0xFFF)) return -1;
    bk_flash_abs_addr_erase(addr, len);
    return 0;
}

static int fl_write(void *ctx, uint32_t addr, const void *buf, uint32_t len)
{
    (void)ctx;
    if (!in_own_area(addr, len)) return -1;
    /* write in chunks so interrupts aren't held off for long */
    while (len) {
        uint32_t n = len > 256 ? 256 : len;
        bk_flash_abs_addr_write(addr, buf, n, 0);
        addr += n;
        buf = (const uint8_t *)buf + n;
        len -= n;
    }
    return 0;
}

static const flash_ops_t flash_ops = { fl_read, fl_erase, fl_write, 0 };

const flash_ops_t *board_flash_ops(void) { return &flash_ops; }
