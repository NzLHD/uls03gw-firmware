/*
 * board.h - ULS-03GW board support on the Beken SDK: pins, LEDs, button, power mode,
 * wake reason, deep sleep, flash access (guarded), timing.
 */
#ifndef ULS_BOARD_H
#define ULS_BOARD_H

#include <stdint.h>
#include <stdbool.h>
#include "abstore.h"
#include "ha_discovery.h"   /* uls_wake_t */

#define FW_VERSION      "0.2.2"

/* Pins (spec §1), Beken GPIO numbers */
#define PIN_BUTTON      6
#define PIN_PWRMODE     9
#define PIN_UART1_RX    10
#define PIN_UART1_TX    11
#define PIN_LED_RED     17
#define PIN_LED_BLUE    21
#define PIN_SENSOR_PWR  26

/* Flash areas (spec §4, firmware/README.md) - the only range board_flash_ops may touch */
#define FLASH_CFG_A     0x1FA000u
#define FLASH_CFG_B     0x1FC000u
#define FLASH_CFG_SLOT  0x2000u
#define FLASH_RS_A      0x1FE000u
#define FLASH_RS_B      0x1FF000u
#define FLASH_OWN_START 0x1FA000u
#define FLASH_OWN_END   0x200000u

#define LOG(...)        bk_printf("[uls] " __VA_ARGS__)
extern void bk_printf(const char *fmt, ...);

typedef enum {
    LED_OFF,            /* both off (timer wakes, battery) */
    LED_AP,             /* setup AP: blue slow blink 1 Hz */
    LED_HOLD,           /* hold >= 4 s reached: blue solid */
    LED_RESET,          /* hold >= 15 s: blue + red fast blink */
    LED_CONNECTING,     /* blue fast blink */
    LED_MAINS,          /* red on */
} led_mode_t;

void        board_init(void);
uls_wake_t  board_wake_reason(void);
bool        board_on_battery(void);         /* P9 HIGH */
bool        board_button_down(void);        /* P6 LOW */
void        board_mac(uint8_t mac[6]);
uint32_t    board_ms(void);
void        board_delay(uint32_t ms);

void        led_set(led_mode_t m);
void        led_flash_ok(void);             /* blue 200 ms, then back to the mode */
void        led_flash_error(void);          /* red 3 flashes */

/* Spec §9 power-down: head off, LEDs off, UART pins safe, wake on P6 LOW or after
 * `seconds` (0 = button only). Does not return. */
void        board_deep_sleep(uint32_t seconds) __attribute__((noreturn));
void        board_reboot(void) __attribute__((noreturn));

/* Battery awake cap (spec §5.1): after `seconds`, call `on_expire` (from a timer task),
 * which should save state and sleep. */
void        board_awake_cap(uint32_t seconds, void (*on_expire)(void));

const flash_ops_t *board_flash_ops(void);

#endif
