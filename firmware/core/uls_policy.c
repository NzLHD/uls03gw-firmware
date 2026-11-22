/* uls_policy.c - see uls_policy.h */
#include "uls_policy.h"

#define FLAG_CONFIGURED 0x01u
#define FLAG_SENSOR_OK  0x02u

static uint8_t flags_of(const level_result_t *r)
{
    return (uint8_t)((r->configured ? FLAG_CONFIGURED : 0) | (r->sensor_ok ? FLAG_SENSOR_OK : 0));
}

/* now - then, treating a counter that went backwards (corrupt state) as "long ago". */
static uint32_t elapsed(uint32_t now, uint32_t then)
{
    return now >= then ? now - then : UINT32_MAX;
}

uint32_t policy_backoff_min(uint32_t heartbeat_min, uint32_t n)
{
    uint64_t m = heartbeat_min ? heartbeat_min : 1;
    if (n == 0)
        return 0;
    for (uint32_t i = 1; i < n && m < POLICY_BACKOFF_CAP_MIN; i++)
        m *= 2;                                 /* 1, 2, 4 ... heartbeats */
    return m > POLICY_BACKOFF_CAP_MIN ? POLICY_BACKOFF_CAP_MIN : (uint32_t)m;
}

uint32_t policy_should_send(const policy_cfg_t *cfg, const policy_state_t *st,
                            const level_result_t *cur, const policy_event_t *ev)
{
    uint32_t why = 0;

    /* Explicit requests always try, even during back-off. */
    if (ev->button)
        why |= SEND_BUTTON;
    if (ev->settings_changed)
        why |= SEND_SETTINGS;
    if (ev->report_cmd)
        why |= SEND_COMMAND;

    bool alarm_changed = !st->has_sent || (int8_t)cur->alarm != st->sent_alarm;

    if (st->fail_count > 0 && ev->now_min < st->retry_after_min) {
        /* Backing off (also before the first success, so a wrong broker setting
         * can't turn every read into a Wi-Fi attempt): only an alarm change gets
         * one try per change. */
        if (alarm_changed && (int8_t)cur->alarm != st->tried_alarm)
            why |= SEND_ALARM;
        return why;
    }

    if (!st->has_sent)
        return why | SEND_FIRST;

    if (cur->depth_cm != LEVEL_NULL && st->sent_depth_cm != LEVEL_NULL) {
        int32_t d = cur->depth_cm - st->sent_depth_cm;
        if (d < 0)
            d = -d;
        if (d > cfg->change_threshold_cm)
            why |= SEND_DEPTH;
    } else if (cur->depth_cm != st->sent_depth_cm) {
        why |= SEND_DEPTH;                      /* value appeared or disappeared */
    }
    if (alarm_changed)
        why |= SEND_ALARM;
    if (flags_of(cur) != st->sent_flags)
        why |= SEND_STATUS;
    if (elapsed(ev->now_min, st->last_send_min) >= cfg->heartbeat_min)
        why |= SEND_HEARTBEAT;

    return why;
}

void policy_on_send_result(const policy_cfg_t *cfg, policy_state_t *st,
                           const level_result_t *cur, uint32_t now_min, bool ok)
{
    if (ok) {
        st->has_sent        = 1;
        st->fail_count      = 0;
        st->sent_alarm      = (int8_t)cur->alarm;
        st->sent_flags      = flags_of(cur);
        st->sent_depth_cm   = cur->depth_cm;
        st->last_send_min   = now_min;
        st->retry_after_min = 0;
        st->tried_alarm     = (int8_t)cur->alarm;
        return;
    }
    if (st->fail_count < 255)
        st->fail_count++;
    st->retry_after_min = now_min + policy_backoff_min(cfg->heartbeat_min, st->fail_count);
    st->tried_alarm     = (int8_t)cur->alarm;
}
