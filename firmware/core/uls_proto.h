/*
 * uls_proto.h - ULS-03GW ultrasonic head serial protocol (SDK-independent)
 *
 * Recovered from app_sensor.c (stock fw 1.2.22). 9600 8N1, host polls.
 *
 *  Request  (8 bytes, constant @0xe5a51):
 *      18 08 6A FF FF 27 CE 0D
 *      |  |  |  |---| |---| '- tail 0x0D
 *      |  |  |  data  CRC-16/MODBUS over bytes[0..4], low byte first
 *      |  |  '- command 0x6A (read distance)
 *      |  '- total frame length (8)
 *      '- header 0x18
 *
 *  Response (14 bytes):
 *      18 0E 6A ?? ?? DL DH ?? ?? ?? ?? CL CH 0D
 *      distance_mm = DH<<8 | DL  (bytes 5,6, little-endian)
 *      The stock firmware checks only [0]=0x18 [1]=0x0E [2]=0x6A [13]=0x0D.
 *      It does NOT check the CRC. Bytes 3,4,7..10 are not used (meaning
 *      unknown - capture a few frames to find out). A distance of 0 is
 *      treated as "no reading".
 *      CL/CH is presumed to be CRC-16/MODBUS by analogy with the request
 *      (not verified against a real frame).
 */
#ifndef __ULS_PROTO_H__
#define __ULS_PROTO_H__

#include <stdint.h>
#include <stddef.h>

#define ULS_HDR          0x18
#define ULS_TAIL         0x0D
#define ULS_CMD_DISTANCE 0x6A
#define ULS_REQ_LEN      8
#define ULS_RSP_LEN      14

/* CRC-16/MODBUS: poly 0xA001 (reflected 0x8005), init 0xFFFF */
uint16_t uls_crc16_modbus(const uint8_t *buf, size_t len);

/* Build the 8-byte distance request. Returns ULS_REQ_LEN. */
size_t uls_build_distance_req(uint8_t out[ULS_REQ_LEN]);

/* Parse a response the way the stock firmware does.
 * Returns distance in mm, or 0 if the frame is not valid (same as stock).
 * If check_crc is non-zero the trailing CRC is also checked (stricter than stock). */
uint16_t uls_parse_distance(const uint8_t *buf, size_t len, int check_crc);

/* Trimmed mean used by stock fw: (sum - max - min) / 3.0, rounded.
 * n must be APP_SENSOR_SAMPLES (5); kept generic: divides by (n-2). */
uint32_t uls_trimmed_avg(const uint32_t *v, size_t n);

#endif
