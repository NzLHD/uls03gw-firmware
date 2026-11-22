#include "unit.h"
#include "abstore.h"
#include "ramflash.h"

#define SECTOR RF_SECTOR
#define fl rf
#define ops rf_ops

void test_abstore(void)
{
    abstore_t s = { &ops, { 0, 2 * SECTOR }, 2 * SECTOR, 0x55434647 /* "UCFG" */, 1 };
    abstore_info_t info;
    char buf[6000], big[6000];

    rf_reset();

    CHECK_EQ(crc32_ieee(0, "123456789", 9), 0xCBF43926u);     /* IEEE check value */

    CHECK_EQ(abstore_load(&s, buf, sizeof(buf), &info), -1);
    CHECK_EQ(info.slot, -1);

    CHECK_EQ(abstore_save(&s, "first", 6), 0);
    CHECK_EQ(abstore_load(&s, buf, sizeof(buf), &info), 0);
    CHECK_STR(buf, "first");
    CHECK(info.slot == 0 && info.seq == 1 && info.len == 6 && info.version == 1);

    CHECK_EQ(abstore_save(&s, "second", 7), 0);
    CHECK_EQ(abstore_load(&s, buf, sizeof(buf), &info), 0);
    CHECK_STR(buf, "second");
    CHECK(info.slot == 1 && info.seq == 2);

    CHECK_EQ(abstore_save(&s, "third", 6), 0);                /* back to slot A */
    CHECK_EQ(abstore_load(&s, buf, sizeof(buf), &info), 0);
    CHECK(info.slot == 0 && info.seq == 3);

    /* a record larger than one sector (CA up to 4 KB + the rest of the config) */
    for (size_t i = 0; i < sizeof(big); i++) big[i] = (char)('a' + i % 26);
    CHECK_EQ(abstore_save(&s, big, 5000), 0);
    memset(buf, 0, sizeof(buf));
    CHECK_EQ(abstore_load(&s, buf, sizeof(buf), &info), 0);
    CHECK(info.len == 5000 && memcmp(buf, big, 5000) == 0);
    CHECK_EQ(abstore_save(&s, big, 2 * SECTOR), -1);           /* too big for a slot */

    /* corruption in the newest slot -> fall back to the other one */
    CHECK_EQ(abstore_save(&s, "good", 5), 0);                  /* seq 5, slot A */
    CHECK_EQ(abstore_save(&s, "newer", 6), 0);                 /* seq 6, slot B */
    fl.mem[2 * SECTOR + ABSTORE_HDR_SIZE + 1] ^= 0x40;
    CHECK_EQ(abstore_load(&s, buf, sizeof(buf), &info), 0);
    CHECK_STR(buf, "good");
    CHECK(info.seq == 5);

    /* power cut after the data, before the header: previous record survives */
    CHECK_EQ(abstore_save(&s, "base", 5), 0);                  /* rewrites corrupt slot B */
    fl.fail_write_after = 1;                                   /* data ok, header write fails */
    CHECK_EQ(abstore_save(&s, "torn", 5), -1);
    fl.fail_write_after = -1;
    CHECK_EQ(abstore_load(&s, buf, sizeof(buf), &info), 0);
    CHECK_STR(buf, "base");
    /* power cut right after the erase */
    fl.fail_write_after = 0;
    CHECK_EQ(abstore_save(&s, "torn2", 6), -1);
    fl.fail_write_after = -1;
    CHECK_EQ(abstore_load(&s, buf, sizeof(buf), &info), 0);
    CHECK_STR(buf, "base");
    /* and the next save after a failed one still works */
    CHECK_EQ(abstore_save(&s, "after", 6), 0);
    CHECK_EQ(abstore_load(&s, buf, sizeof(buf), &info), 0);
    CHECK_STR(buf, "after");

    /* another store's magic in the slot is ignored */
    abstore_t other = s;
    other.magic = 0x52554E53;
    CHECK_EQ(abstore_load(&other, buf, sizeof(buf), &info), -1);

    /* sequence wrap-around */
    {
        uint8_t raw[ABSTORE_HDR_SIZE];
        abstore_erase(&s);
        CHECK_EQ(abstore_save(&s, "w1", 3), 0);
        /* forge slot A's seq to 0xFFFFFFFF with a correct CRC by re-saving through a
         * helper: easiest is to write records until seq is known, then patch + recrc */
        memcpy(raw, fl.mem, sizeof(raw));
        uint32_t seq = 0xFFFFFFFFu, crc;
        memcpy(raw + 12, &seq, 4);
        memset(raw + 16, 0, 4);
        crc = crc32_ieee(0, raw, sizeof(raw));
        crc = crc32_ieee(crc, "w1", 3);
        memcpy(raw + 16, &crc, 4);
        rf_erase(&fl, 0, 2 * SECTOR);
        rf_write(&fl, ABSTORE_HDR_SIZE, "w1", 3);
        rf_write(&fl, 0, raw, sizeof(raw));
        CHECK_EQ(abstore_load(&s, buf, sizeof(buf), &info), 0);
        CHECK(info.seq == 0xFFFFFFFFu);
        CHECK_EQ(abstore_save(&s, "w2", 3), 0);                /* seq wraps to 0 */
        CHECK_EQ(abstore_load(&s, buf, sizeof(buf), &info), 0);
        CHECK_STR(buf, "w2");
        CHECK(info.seq == 0 && info.slot == 1);
    }

    CHECK_EQ(abstore_erase(&s), 0);
    CHECK_EQ(abstore_load(&s, buf, sizeof(buf), &info), -1);
}
