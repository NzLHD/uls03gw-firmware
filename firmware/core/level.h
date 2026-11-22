/*
 * level.h - tank level maths (spec §3). Pure C99, no SDK calls.
 *
 *   depth_cm   = install_height_cm - round(distance_mm / 10)
 *   span_cm    = install_height_cm - full_distance_cm
 *   configured = span_cm > 0
 *   level_pct  = configured ? clamp(depth_cm * 100 / span_cm, 0, 100) : null
 *   alarm      = upper if upper_alarm_pct > 0 && level_pct >= upper_alarm_pct
 *                lower if lower_alarm_pct > 0 && level_pct <  lower_alarm_pct
 *                normal otherwise                     (null when level_pct is null)
 *   volume_l   = tank_volume_l > 0 && configured ? round(level_pct * tank_volume_l / 100) : null
 */
#ifndef ULS_LEVEL_H
#define ULS_LEVEL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define LEVEL_NULL  (-1)    /* "no value" for level_pct, depth_cm, volume_l, alarm */

typedef enum {
    ALARM_NONE   = LEVEL_NULL,  /* unknown: no level (sensor failed or not configured) */
    ALARM_NORMAL = 0,
    ALARM_LOWER  = 1,
    ALARM_UPPER  = 2,
} uls_alarm_t;

typedef enum {
    ULS_ERR_NONE = 0,
    ULS_ERR_SENSOR_TIMEOUT,     /* no valid reading                          */
    ULS_ERR_DISTANCE_GT_HEIGHT, /* echo farther than the install height      */
    ULS_ERR_NOT_CONFIGURED,     /* install_height_cm <= full_distance_cm     */
} uls_err_t;

typedef struct {
    int32_t install_height_cm;  /* 1..1000   */
    int32_t full_distance_cm;   /* 0..999    */
    int32_t upper_alarm_pct;    /* 0 = off   */
    int32_t lower_alarm_pct;    /* 0 = off   */
    int32_t tank_volume_l;      /* 0 = off   */
} level_cfg_t;

typedef struct {
    bool        sensor_ok;
    bool        configured;
    int32_t     distance_mm;    /* LEVEL_NULL if !sensor_ok */
    int32_t     depth_cm;       /* >= 0, or LEVEL_NULL      */
    int32_t     level_pct;      /* 0..100, or LEVEL_NULL    */
    int32_t     volume_l;       /* >= 0, or LEVEL_NULL      */
    uls_alarm_t alarm;
    uls_err_t   err;            /* highest-priority problem */
} level_result_t;

/* distance_mm is only used when sensor_ok is true (0 is never a valid echo). */
void level_compute(const level_cfg_t *cfg, bool sensor_ok, uint32_t distance_mm,
                   level_result_t *out);

/* Combine n >= 1 distance samples (spec §2 step 3): n >= 3 trimmed mean (drop min and max),
 * n == 2 rounded mean, n == 1 the value. Returns 0 for n == 0. */
uint32_t level_sample_avg(const uint32_t *v, size_t n);

const char *level_alarm_str(uls_alarm_t a);   /* "normal"/"lower"/"upper", NULL for none */
const char *level_err_str(uls_err_t e);       /* NULL for ULS_ERR_NONE */

#endif
