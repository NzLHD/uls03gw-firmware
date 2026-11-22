/*
 * uls_policy.h - "should this wake send?" and the failure back-off (spec §5.1, §5.2, §5.3).
 * Pure C99, no SDK calls. The state lives in the run-state record (survives deep sleep).
 *
 * Time is a monotonic minute counter (now_min) kept by the caller across deep sleeps
 * (sleep duration + time awake). It only has to be roughly right.
 */
#ifndef ULS_POLICY_H
#define ULS_POLICY_H

#include <stdint.h>
#include <stdbool.h>
#include "level.h"

#define POLICY_BACKOFF_CAP_MIN  (24u * 60u)

/* Reasons a send was chosen (bitmask; also handy for logs). */
enum {
    SEND_FIRST      = 1u << 0,  /* nothing sent since boot-from-reset / factory reset */
    SEND_DEPTH      = 1u << 1,  /* |depth - last sent| > change_threshold_cm          */
    SEND_ALARM      = 1u << 2,  /* alarm state changed                                */
    SEND_STATUS     = 1u << 3,  /* configured or sensor_ok changed                    */
    SEND_HEARTBEAT  = 1u << 4,  /* heartbeat elapsed                                  */
    SEND_SETTINGS   = 1u << 5,  /* settings changed on the device                     */
    SEND_BUTTON     = 1u << 6,  /* short press: report now                            */
    SEND_COMMAND    = 1u << 7,  /* pending {"cmd":"report"}                           */
};

/* Wake / event inputs for one decision. */
typedef struct {
    uint32_t now_min;
    bool     button;            /* button wake or press */
    bool     settings_changed;
    bool     report_cmd;
} policy_event_t;

typedef struct {
    int32_t  change_threshold_cm;   /* 1..50 */
    uint32_t heartbeat_min;         /* battery: heartbeat_h*60, mains: mains_heartbeat_min */
} policy_cfg_t;

/* Persistent state (part of the run-state record). Zero-initialised = "never sent". */
typedef struct {
    uint8_t  has_sent;          /* a send has succeeded since the state was cleared */
    uint8_t  fail_count;        /* consecutive failed sends                         */
    int8_t   sent_alarm;        /* uls_alarm_t of the last successful send          */
    uint8_t  sent_flags;        /* bit0 configured, bit1 sensor_ok                  */
    int32_t  sent_depth_cm;     /* LEVEL_NULL if none                               */
    uint32_t last_send_min;     /* now_min of the last success                      */
    uint32_t retry_after_min;   /* during back-off: no regular sends before this    */
    int8_t   tried_alarm;       /* alarm value already tried during this back-off   */
    uint8_t  reserved[3];
} policy_state_t;

/* Returns a SEND_* bitmask, 0 = don't send. */
uint32_t policy_should_send(const policy_cfg_t *cfg, const policy_state_t *st,
                            const level_result_t *cur, const policy_event_t *ev);

/* Record the outcome of a send attempt. */
void policy_on_send_result(const policy_cfg_t *cfg, policy_state_t *st,
                           const level_result_t *cur, uint32_t now_min, bool ok);

/* Minutes of back-off after the n-th consecutive failure (n >= 1). */
uint32_t policy_backoff_min(uint32_t heartbeat_min, uint32_t n);

#endif
