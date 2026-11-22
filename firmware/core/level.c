/* level.c - see level.h and spec §3 (stock dp_process.c maths, with the stock bugs fixed). */
#include "level.h"
#include "uls_proto.h"

static int32_t clamp_i32(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void level_compute(const level_cfg_t *cfg, bool sensor_ok, uint32_t distance_mm,
                   level_result_t *out)
{
    int32_t span = cfg->install_height_cm - cfg->full_distance_cm;

    out->sensor_ok   = sensor_ok && distance_mm > 0;
    out->configured  = span > 0;
    out->distance_mm = LEVEL_NULL;
    out->depth_cm    = LEVEL_NULL;
    out->level_pct   = LEVEL_NULL;
    out->volume_l    = LEVEL_NULL;
    out->alarm       = ALARM_NONE;
    out->err         = ULS_ERR_NONE;

    if (!out->sensor_ok) {
        out->err = ULS_ERR_SENSOR_TIMEOUT;
        return;
    }

    if (distance_mm > 65535)            /* the head reports 16 bits */
        distance_mm = 65535;
    out->distance_mm = (int32_t)distance_mm;

    /* round to the nearest cm (stock truncated) */
    int32_t depth = cfg->install_height_cm - (int32_t)((distance_mm + 5) / 10);
    if (depth < 0) {
        depth = 0;                      /* stock skipped the report instead */
        out->err = ULS_ERR_DISTANCE_GT_HEIGHT;
    }
    out->depth_cm = depth;

    if (!out->configured) {             /* stock divided by zero here */
        if (out->err == ULS_ERR_NONE)
            out->err = ULS_ERR_NOT_CONFIGURED;
        return;
    }

    int32_t pct = clamp_i32(depth * 100 / span, 0, 100);    /* stock had no lower clamp */
    out->level_pct = pct;

    if (cfg->upper_alarm_pct > 0 && pct >= cfg->upper_alarm_pct)   /* 0 = off (stock: always on) */
        out->alarm = ALARM_UPPER;
    else if (cfg->lower_alarm_pct > 0 && pct < cfg->lower_alarm_pct)
        out->alarm = ALARM_LOWER;
    else
        out->alarm = ALARM_NORMAL;

    if (cfg->tank_volume_l > 0)
        out->volume_l = (int32_t)(((int64_t)pct * cfg->tank_volume_l + 50) / 100);
}

uint32_t level_sample_avg(const uint32_t *v, size_t n)
{
    if (n == 0) return 0;
    if (n == 1) return v[0];
    if (n == 2) return (v[0] + v[1] + 1) / 2;
    return uls_trimmed_avg(v, n);
}

const char *level_alarm_str(uls_alarm_t a)
{
    switch (a) {
    case ALARM_NORMAL: return "normal";
    case ALARM_LOWER:  return "lower";
    case ALARM_UPPER:  return "upper";
    default:           return 0;
    }
}

const char *level_err_str(uls_err_t e)
{
    switch (e) {
    case ULS_ERR_SENSOR_TIMEOUT:     return "sensor_timeout";
    case ULS_ERR_DISTANCE_GT_HEIGHT: return "distance_gt_height";
    case ULS_ERR_NOT_CONFIGURED:     return "not_configured";
    default:                         return 0;
    }
}
