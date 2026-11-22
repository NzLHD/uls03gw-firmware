/* sensor.h - sensor head reading (spec §2): P26 power, UART1 9600 8N1, measured recipe. */
#ifndef ULS_SENSOR_H
#define ULS_SENSOR_H

#include <stdint.h>
#include <stdbool.h>
#include "uls_config.h"

typedef struct {
    bool     ok;            /* sensor_samples valid distances collected */
    uint32_t distance_mm;   /* combined (level_sample_avg) */
    int      temp_c;        /* reply byte 10 (very likely degC) */
    bool     temp_valid;
    uint32_t polls;         /* requests sent */
    uint32_t ms;            /* head-on time */
} sensor_reading_t;

void sensor_init(void);
/* Power the head, run the recipe, power it off. Blocks ~1 s with the defaults. */
void sensor_read(const uls_config_t *cfg, sensor_reading_t *out);

#endif
