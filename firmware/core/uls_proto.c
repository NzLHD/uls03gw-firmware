/*
 * uls_proto.c - ULS-03GW sensor head protocol helpers.
 * Plain C99, no SDK dependency, so it can be reused in OpenBeken / ESPHome /
 * LibreTiny ports. See uls_proto.h for the frame layout.
 */
#include "uls_proto.h"

uint16_t uls_crc16_modbus(const uint8_t *buf, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
    }
    return crc;
}

size_t uls_build_distance_req(uint8_t out[ULS_REQ_LEN])
{
    out[0] = ULS_HDR;
    out[1] = ULS_REQ_LEN;
    out[2] = ULS_CMD_DISTANCE;
    out[3] = 0xFF;
    out[4] = 0xFF;
    uint16_t crc = uls_crc16_modbus(out, 5);
    out[5] = crc & 0xFF;        /* 0x27 */
    out[6] = crc >> 8;          /* 0xCE */
    out[7] = ULS_TAIL;
    return ULS_REQ_LEN;
}

uint16_t uls_parse_distance(const uint8_t *buf, size_t len, int check_crc)
{
    if (len < ULS_RSP_LEN)
        return 0;
    if (buf[0] != ULS_HDR || buf[1] != ULS_RSP_LEN || buf[2] != ULS_CMD_DISTANCE ||
        buf[13] != ULS_TAIL)
        return 0;
    if (check_crc) {
        uint16_t crc = uls_crc16_modbus(buf, 11);
        if (buf[11] != (crc & 0xFF) || buf[12] != (crc >> 8))
            return 0;
    }
    return (uint16_t)(buf[5] | (buf[6] << 8));
}

uint32_t uls_trimmed_avg(const uint32_t *v, size_t n)
{
    uint32_t mx = v[0], mn = v[0], sum = v[0];
    for (size_t i = 1; i < n; i++) {
        if (v[i] > mx) mx = v[i];
        if (v[i] < mn) mn = v[i];
        sum += v[i];
    }
    /* stock: round((double)(sum - mx - mn) / 3.0) using soft-float.
     * Same result in integer maths with round-half-up. */
    uint32_t d = (uint32_t)(n - 2);
    return ((sum - mx - mn) * 2 + d) / (2 * d);
}
