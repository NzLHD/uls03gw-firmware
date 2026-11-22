/* Same cases as src/tests/test_uls_proto.py, against the C implementation. */
#include "unit.h"
#include "uls_proto.h"

static void make_rsp(uint8_t r[ULS_RSP_LEN], uint16_t mm)
{
    memset(r, 0, ULS_RSP_LEN);
    r[0] = 0x18; r[1] = 0x0E; r[2] = 0x6A;
    r[5] = mm & 0xFF; r[6] = mm >> 8;
    uint16_t c = uls_crc16_modbus(r, 11);
    r[11] = c & 0xFF; r[12] = c >> 8; r[13] = 0x0D;
}

void test_uls_proto(void)
{
    static const uint8_t want[8] = { 0x18, 0x08, 0x6A, 0xFF, 0xFF, 0x27, 0xCE, 0x0D };
    uint8_t req[ULS_REQ_LEN], rsp[ULS_RSP_LEN];

    CHECK_EQ(uls_build_distance_req(req), 8);
    CHECK(memcmp(req, want, 8) == 0);           /* == stock constant @0xe5a51 */

    /* optional: compare with the stock image when the RE files are present */
    FILE *f = fopen("../../re/images/app_0x10000.bin", "rb");
    if (f) {
        uint8_t stock[8];
        fseek(f, 0xe5a51 - 0x10000, SEEK_SET);
        CHECK(fread(stock, 1, 8, f) == 8 && memcmp(stock, want, 8) == 0);
        fclose(f);
    }

    make_rsp(rsp, 1234);
    CHECK_EQ(uls_parse_distance(rsp, sizeof(rsp), 0), 1234);
    CHECK_EQ(uls_parse_distance(rsp, sizeof(rsp), 1), 1234);
    CHECK_EQ(uls_parse_distance(rsp, 13, 0), 0);
    rsp[11] ^= 1;
    CHECK_EQ(uls_parse_distance(rsp, sizeof(rsp), 0), 1234);    /* stock ignores the CRC */
    CHECK_EQ(uls_parse_distance(rsp, sizeof(rsp), 1), 0);
    make_rsp(rsp, 1234);
    rsp[13] = 0;
    CHECK_EQ(uls_parse_distance(rsp, sizeof(rsp), 0), 0);

    uint32_t a[5] = { 1000, 1010, 1020, 5000, 10 };
    uint32_t b[5] = { 1, 1, 2, 2, 9 };
    CHECK_EQ(uls_trimmed_avg(a, 5), 1010);
    CHECK_EQ(uls_trimmed_avg(b, 5), 2);         /* 5/3 = 1.67 -> 2 */
}
