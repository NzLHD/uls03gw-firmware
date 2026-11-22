#include "unit.h"
#include "uls_policy.h"

static level_result_t reading(const level_cfg_t *c, bool ok, uint32_t mm)
{
    level_result_t r;
    level_compute(c, ok, mm, &r);
    return r;
}

void test_policy(void)
{
    level_cfg_t lc = { 300, 10, 94, 12, 0 };
    policy_cfg_t pc = { 2, 6 * 60 };
    policy_state_t st;
    policy_event_t ev = { 0, false, false, false };
    level_result_t r = reading(&lc, true, 1000);    /* depth 200 */

    memset(&st, 0, sizeof(st));

    /* first wake ever: send */
    CHECK_EQ(policy_should_send(&pc, &st, &r, &ev), SEND_FIRST);
    policy_on_send_result(&pc, &st, &r, 0, true);
    CHECK(st.has_sent && st.fail_count == 0 && st.sent_depth_cm == 200);

    /* unchanged, small changes: no send; hysteresis is strictly > threshold */
    ev.now_min = 30;
    CHECK_EQ(policy_should_send(&pc, &st, &r, &ev), 0);
    r = reading(&lc, true, 980);                    /* depth 202 */
    CHECK_EQ(policy_should_send(&pc, &st, &r, &ev), 0);
    r = reading(&lc, true, 970);                    /* depth 203 */
    CHECK_EQ(policy_should_send(&pc, &st, &r, &ev), SEND_DEPTH);
    r = reading(&lc, true, 1030);                   /* depth 197 */
    CHECK_EQ(policy_should_send(&pc, &st, &r, &ev), SEND_DEPTH);

    /* alarm change */
    r = reading(&lc, true, 2800);
    CHECK(policy_should_send(&pc, &st, &r, &ev) & SEND_ALARM);

    /* sensor failure: status + depth (value disappeared) + alarm (unknown) */
    r = reading(&lc, false, 0);
    CHECK_EQ(policy_should_send(&pc, &st, &r, &ev), SEND_DEPTH | SEND_ALARM | SEND_STATUS);

    /* heartbeat */
    r = reading(&lc, true, 1000);
    ev.now_min = 359;
    CHECK_EQ(policy_should_send(&pc, &st, &r, &ev), 0);
    ev.now_min = 360;
    CHECK_EQ(policy_should_send(&pc, &st, &r, &ev), SEND_HEARTBEAT);

    /* explicit requests */
    ev.now_min = 100;
    ev.button = true;
    CHECK_EQ(policy_should_send(&pc, &st, &r, &ev), SEND_BUTTON);
    ev.button = false; ev.settings_changed = true; ev.report_cmd = true;
    CHECK_EQ(policy_should_send(&pc, &st, &r, &ev), SEND_SETTINGS | SEND_COMMAND);
    ev.settings_changed = false; ev.report_cmd = false;

    /* back-off schedule: 1, 2, 4 ... heartbeats, capped at 24 h */
    CHECK_EQ(policy_backoff_min(360, 1), 360);
    CHECK_EQ(policy_backoff_min(360, 2), 720);
    CHECK_EQ(policy_backoff_min(360, 3), 1440);
    CHECK_EQ(policy_backoff_min(360, 4), 1440);
    CHECK_EQ(policy_backoff_min(60, 3), 240);
    CHECK_EQ(policy_backoff_min(60, 200), 1440);

    /* a failed send at t=1000 (depth change pending) */
    r = reading(&lc, true, 900);                    /* depth 210 */
    ev.now_min = 1000;
    CHECK_EQ(policy_should_send(&pc, &st, &r, &ev), SEND_DEPTH | SEND_HEARTBEAT);
    policy_on_send_result(&pc, &st, &r, 1000, false);
    CHECK_EQ(st.fail_count, 1);
    CHECK_EQ(st.retry_after_min, 1360);
    CHECK_EQ(st.sent_depth_cm, 200);                /* last *successful* send kept */

    ev.now_min = 1100;                              /* backing off: depth change ignored */
    CHECK_EQ(policy_should_send(&pc, &st, &r, &ev), 0);
    r = reading(&lc, true, 200);                    /* upper alarm: one try per change */
    CHECK_EQ(policy_should_send(&pc, &st, &r, &ev), SEND_ALARM);
    policy_on_send_result(&pc, &st, &r, 1100, false);
    CHECK_EQ(st.fail_count, 2);
    CHECK_EQ(st.retry_after_min, 1100 + 720);
    ev.now_min = 1200;
    CHECK_EQ(policy_should_send(&pc, &st, &r, &ev), 0);     /* same alarm: already tried */
    ev.button = true;
    CHECK_EQ(policy_should_send(&pc, &st, &r, &ev), SEND_BUTTON);  /* button always tries */
    ev.button = false;
    r = reading(&lc, true, 1000);                   /* back to normal = sent state: no try */
    CHECK_EQ(policy_should_send(&pc, &st, &r, &ev), 0);

    ev.now_min = 1820;                              /* back-off over: normal rules */
    r = reading(&lc, true, 200);
    CHECK_EQ(policy_should_send(&pc, &st, &r, &ev), SEND_DEPTH | SEND_ALARM | SEND_HEARTBEAT);
    policy_on_send_result(&pc, &st, &r, 1820, true);
    CHECK_EQ(st.fail_count, 0);
    CHECK_EQ(st.last_send_min, 1820);
    CHECK_EQ(st.sent_alarm, ALARM_UPPER);

    /* failure before the first success must still back off (no Wi-Fi every read) */
    memset(&st, 0, sizeof(st));
    r = reading(&lc, true, 1000);
    ev.now_min = 0;
    CHECK_EQ(policy_should_send(&pc, &st, &r, &ev), SEND_FIRST);
    policy_on_send_result(&pc, &st, &r, 0, false);
    ev.now_min = 30;
    CHECK_EQ(policy_should_send(&pc, &st, &r, &ev), 0);
    ev.now_min = 360;
    CHECK_EQ(policy_should_send(&pc, &st, &r, &ev), SEND_FIRST);

    /* clock went backwards (corrupt run state): treat as heartbeat due */
    memset(&st, 0, sizeof(st));
    policy_on_send_result(&pc, &st, &r, 5000, true);
    ev.now_min = 10;
    CHECK_EQ(policy_should_send(&pc, &st, &r, &ev), SEND_HEARTBEAT);
}
