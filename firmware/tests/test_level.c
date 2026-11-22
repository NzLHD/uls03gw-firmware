#include "unit.h"
#include "level.h"

void test_level(void)
{
    level_result_t r;
    level_cfg_t c = { 300, 10, 94, 12, 0 };     /* t_sensor_conf values from the dump */

    /* same as stock where stock was right */
    level_compute(&c, true, 1000, &r);
    CHECK(r.sensor_ok && r.configured);
    CHECK_EQ(r.distance_mm, 1000);
    CHECK_EQ(r.depth_cm, 200);
    CHECK_EQ(r.level_pct, 68);                  /* 200*100/290 = 68.97, truncated as stock */
    CHECK_EQ(r.alarm, ALARM_NORMAL);
    CHECK_EQ(r.err, ULS_ERR_NONE);
    CHECK_EQ(r.volume_l, LEVEL_NULL);
    level_compute(&c, true, 200, &r);
    CHECK_EQ(r.alarm, ALARM_UPPER);
    level_compute(&c, true, 2800, &r);
    CHECK_EQ(r.alarm, ALARM_LOWER);

    /* mm -> cm rounds to nearest (stock truncated) */
    level_compute(&c, true, 1004, &r);  CHECK_EQ(r.depth_cm, 200);
    level_compute(&c, true, 1005, &r);  CHECK_EQ(r.depth_cm, 199);

    /* echo beyond the install height: report depth 0 + error (stock skipped the report) */
    level_compute(&c, true, 3100, &r);
    CHECK_EQ(r.depth_cm, 0);
    CHECK_EQ(r.level_pct, 0);
    CHECK_EQ(r.err, ULS_ERR_DISTANCE_GT_HEIGHT);
    CHECK_EQ(r.alarm, ALARM_LOWER);

    /* above the full line: clamp to 100 */
    level_compute(&c, true, 50, &r);
    CHECK_EQ(r.depth_cm, 295);
    CHECK_EQ(r.level_pct, 100);

    /* sensor failure: everything null */
    level_compute(&c, false, 1000, &r);
    CHECK(!r.sensor_ok);
    CHECK_EQ(r.distance_mm, LEVEL_NULL);
    CHECK_EQ(r.depth_cm, LEVEL_NULL);
    CHECK_EQ(r.level_pct, LEVEL_NULL);
    CHECK_EQ(r.alarm, ALARM_NONE);
    CHECK_EQ(r.err, ULS_ERR_SENSOR_TIMEOUT);
    level_compute(&c, true, 0, &r);             /* 0 mm = no echo */
    CHECK_EQ(r.err, ULS_ERR_SENSOR_TIMEOUT);

    /* stock defaults (height == full distance): no divide by zero, not configured */
    level_cfg_t d = { 10, 10, 0, 0, 0 };
    level_compute(&d, true, 50, &r);
    CHECK(!r.configured);
    CHECK_EQ(r.depth_cm, 5);
    CHECK_EQ(r.level_pct, LEVEL_NULL);
    CHECK_EQ(r.alarm, ALARM_NONE);
    CHECK_EQ(r.err, ULS_ERR_NOT_CONFIGURED);
    level_cfg_t neg = { 100, 150, 0, 0, 500 };  /* full line below the sensor height */
    level_compute(&neg, true, 500, &r);
    CHECK(!r.configured);
    CHECK_EQ(r.volume_l, LEVEL_NULL);

    /* upper alarm 0 = off (stock: every reading was an upper alarm) */
    level_cfg_t off = { 300, 10, 0, 0, 0 };
    level_compute(&off, true, 100, &r);
    CHECK_EQ(r.level_pct, 100);
    CHECK_EQ(r.alarm, ALARM_NORMAL);
    off.upper_alarm_pct = 100;
    level_compute(&off, true, 100, &r);
    CHECK_EQ(r.alarm, ALARM_UPPER);             /* >= */
    off.lower_alarm_pct = 50; off.upper_alarm_pct = 0;
    level_compute(&off, true, 1550, &r);        /* depth 145 -> 50 % */
    CHECK_EQ(r.level_pct, 50);
    CHECK_EQ(r.alarm, ALARM_NORMAL);            /* lower is strictly < */

    /* volume */
    level_cfg_t v = { 300, 10, 0, 0, 1000 };
    level_compute(&v, true, 1000, &r);
    CHECK_EQ(r.volume_l, 680);
    v.tank_volume_l = 15;                       /* 68 % of 15 = 10.2 -> 10 */
    level_compute(&v, true, 1000, &r);
    CHECK_EQ(r.volume_l, 10);

    /* sample averaging for 1..n samples */
    {
        uint32_t one[1] = { 1516 }, two[2] = { 1515, 1520 }, five[5] = { 1500, 1516, 1518, 1520, 2000 };
        CHECK_EQ(level_sample_avg(one, 1), 1516);
        CHECK_EQ(level_sample_avg(two, 2), 1518);       /* 1517.5 -> 1518 */
        CHECK_EQ(level_sample_avg(five, 5), 1518);      /* drop 1500 and 2000 */
        CHECK_EQ(level_sample_avg(one, 0), 0);
    }

    /* strings */
    CHECK_STR(level_alarm_str(ALARM_UPPER), "upper");
    CHECK(level_alarm_str(ALARM_NONE) == 0);
    CHECK_STR(level_err_str(ULS_ERR_DISTANCE_GT_HEIGHT), "distance_gt_height");
    CHECK(level_err_str(ULS_ERR_NONE) == 0);
}
