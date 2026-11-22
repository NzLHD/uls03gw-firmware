/*
 * sensor.c - see sensor.h. Recipe measured on hardware (spec §2):
 *   P26 HIGH -> wait sensor_warmup_ms -> flush the 5 power-up junk bytes -> trigger request
 *   (reply carries no measurement) -> every sensor_poll_ms (>= 450) another request; each
 *   reply carries the result of the measurement started by the previous request.
 *
 * SDK notes: UART1 has no RX FIFO in this SDK (bk_uart_recv never returns data), so bytes
 * are drained from the RX callback into our ring; bk_uart_send() stops at NUL, so bytes are
 * sent one at a time with bk_send_byte().
 */
#include "include.h"
#include "rtos_pub.h"
#include "gpio_pub.h"
#include "uart_pub.h"
#include "drv_model_pub.h"
#include "BkDriverUart.h"
#include "mem_pub.h"
#include "board.h"
#include "sensor.h"
#include "uls_proto.h"
#include "level.h"

extern void bk_send_byte(UINT8 uport, UINT8 data);

#define RX_RING     256
#define REPLY_MS    120
#define MAX_POLLS   6       /* after the trigger: give up (spec §2 step 4) */
#define MAX_SAMPLES 15

static volatile uint8_t  rx_ring[RX_RING];
static volatile uint32_t rx_head, rx_tail;

static void rx_cb(int uport, void *param)
{
    int c;
    (void)param;
    while ((c = uart_read_byte(uport)) >= 0) {
        uint32_t next = (rx_head + 1) % RX_RING;
        if (next != rx_tail) { rx_ring[rx_head] = (uint8_t)c; rx_head = next; }
    }
}

static int rx_get(uint8_t *b)
{
    if (rx_tail == rx_head) return 0;
    *b = rx_ring[rx_tail];
    rx_tail = (rx_tail + 1) % RX_RING;
    return 1;
}

static void rx_flush(void) { uint8_t b; while (rx_get(&b)) ; }

static void pins_uart(void)
{
    UINT32 p = GFUNC_MODE_UART1;
    sddev_control(GPIO_DEV_NAME, CMD_GPIO_ENABLE_SECOND, &p);
}

static void pins_park(void)
{
    gpio_config(PIN_UART1_TX, GMODE_INPUT);
    gpio_config(PIN_UART1_RX, GMODE_INPUT);
}

void sensor_init(void)
{
    bk_uart_config_t cfg = { 9600, BK_DATA_WIDTH_8BIT, BK_PARITY_NO, BK_STOP_BITS_1, FLOW_CTRL_DISABLED, 0 };
    pins_park();
    bk_uart_initialize(BK_UART_1, &cfg, NULL);
    bk_uart_set_rx_callback(BK_UART_1, rx_cb, NULL);
    pins_park();
}

/* Send one request and wait for one reply frame. Returns 1 with the frame, 0 on timeout. */
static int transact(uint8_t frame[ULS_RSP_LEN])
{
    uint8_t req[ULS_REQ_LEN];
    int n = 0;
    uint32_t t0;

    uls_build_distance_req(req);
    for (int i = 0; i < ULS_REQ_LEN; i++)
        bk_send_byte(UART1_PORT, req[i]);
    t0 = board_ms();
    while (board_ms() - t0 < REPLY_MS) {
        uint8_t b;
        if (!rx_get(&b)) { board_delay(2); continue; }
        frame[n++] = b;
        if (n == 1 && b != ULS_HDR) { n = 0; continue; }
        if (n == 2 && b != ULS_RSP_LEN) { n = 0; continue; }
        if (n == ULS_RSP_LEN) {
            if (frame[2] == ULS_CMD_DISTANCE && frame[13] == ULS_TAIL)
                return 1;
            n = 0;
        }
    }
    return 0;
}

void sensor_read(const uls_config_t *cfg, sensor_reading_t *out)
{
    uint32_t samples[MAX_SAMPLES], t_on, next;
    uint32_t want = (uint32_t)cfg->sensor_samples, got = 0;
    uint32_t poll = (uint32_t)cfg->sensor_poll_ms;
    uint8_t f[ULS_RSP_LEN];

    if (want < 1) want = 1;
    if (want > MAX_SAMPLES) want = MAX_SAMPLES;
    if (poll < 450) poll = 450;
    os_memset(out, 0, sizeof(*out));

    t_on = board_ms();
    gpio_output(PIN_SENSOR_PWR, 1);
    board_delay((uint32_t)cfg->sensor_warmup_ms);
    pins_uart();
    board_delay(5);
    rx_flush();                                     /* the head's power-up bytes */

    next = board_ms() + poll;
    if (transact(f)) {                              /* trigger: no measurement yet */
        out->temp_c = f[10];
        out->temp_valid = true;
    }
    out->polls = 1;
    for (uint32_t i = 0; i < MAX_POLLS + want - 1 && got < want; i++) {
        int32_t wait = (int32_t)(next - board_ms());
        if (wait > 0) board_delay((uint32_t)wait);
        next = board_ms() + poll;
        out->polls++;
        if (!transact(f)) continue;                 /* timeout */
        out->temp_c = f[10];
        out->temp_valid = true;
        uint16_t d = uls_parse_distance(f, ULS_RSP_LEN, 0);
        if (d)
            samples[got++] = d;
    }

    pins_park();
    gpio_output(PIN_SENSOR_PWR, 0);
    out->ms = board_ms() - t_on;
    out->ok = got >= want;
    if (out->ok)
        out->distance_mm = level_sample_avg(samples, got);
    LOG("sensor: ok=%d d=%u mm temp=%d polls=%u %u ms\r\n", out->ok, (unsigned)out->distance_mm,
        out->temp_c, (unsigned)out->polls, (unsigned)out->ms);
}
