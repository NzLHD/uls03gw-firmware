#include "unit.h"
#include "runstate.h"
#include "ramflash.h"

static const runstate_store_t S = { &rf_ops, { 2 * RF_SECTOR, 3 * RF_SECTOR } };

/* Forge a valid record (layout from runstate.c) to test sequence wrap-around. */
static void forge(uint32_t addr, uint32_t seq, const runstate_t *rs)
{
    uint8_t r[RUNSTATE_RECORD_SIZE];
    uint32_t crc;
    memset(r, 0, sizeof(r));
    r[0] = 0x52; r[1] = 0x53;               /* magic 0x5352 little-endian */
    r[2] = RUNSTATE_VERSION;
    r[3] = RUNSTATE_RECORD_SIZE;
    memcpy(r + 4, &seq, 4);
    memcpy(r + 8, rs, sizeof(*rs));
    crc = crc32_ieee(0, r, 60);
    memcpy(r + 60, &crc, 4);
    rf_write(&rf, addr, r, sizeof(r));
}

void test_runstate(void)
{
    runstate_t rs, got;

    rf_reset();
    memset(&got, 0xAA, sizeof(got));
    CHECK_EQ(runstate_load(&S, &got), -1);
    CHECK_EQ(got.boot_count, 0);                    /* zeroed when nothing stored */

    memset(&rs, 0, sizeof(rs));
    rs.boot_count = 1;
    rs.now_min = 30;
    rs.policy.has_sent = 1;
    rs.policy.sent_depth_cm = 123;
    CHECK_EQ(runstate_save(&S, &rs), 0);
    CHECK_EQ(runstate_load(&S, &got), 0);
    CHECK(memcmp(&got, &rs, sizeof(rs)) == 0);

    /* fill past both sectors: always reads back the newest, sectors rotate */
    int ok = 1;
    for (uint32_t i = 2; i <= 200; i++) {
        rs.boot_count = i;
        if (runstate_save(&S, &rs) || runstate_load(&S, &got) || got.boot_count != i)
            ok = 0;
    }
    CHECK(ok);
    CHECK_EQ(rf.erases, 3);                         /* at records 65, 129, 193 */

    /* a torn write (half a record programmed) is skipped */
    {
        uint8_t junk[32];
        uint32_t newest_addr = 0;
        /* find the first erased slot = where the next record would go */
        for (uint32_t a = S.addr[0]; a < S.addr[0] + 2 * RF_SECTOR; a += RUNSTATE_RECORD_SIZE) {
            int erased = 1;
            for (int k = 0; k < (int)RUNSTATE_RECORD_SIZE; k++) if (rf.mem[a + k] != 0xFF) erased = 0;
            if (erased) { newest_addr = a; break; }
        }
        CHECK(newest_addr != 0);
        memset(junk, 0x12, sizeof(junk));
        rf_write(&rf, newest_addr, junk, sizeof(junk));
        CHECK_EQ(runstate_load(&S, &got), 0);
        CHECK_EQ(got.boot_count, 200);
        rs.boot_count = 201;
        CHECK_EQ(runstate_save(&S, &rs), 0);        /* goes after the torn slot */
        CHECK_EQ(runstate_load(&S, &got), 0);
        CHECK_EQ(got.boot_count, 201);
        CHECK(rf.mem[newest_addr] == 0x12);         /* torn slot untouched */

        /* corrupt the newest -> previous good one */
        rf.mem[newest_addr + RUNSTATE_RECORD_SIZE + 20] ^= 0x01;
        CHECK_EQ(runstate_load(&S, &got), 0);
        CHECK_EQ(got.boot_count, 200);
    }

    /* failed write: previous record stays */
    rf_reset();
    rs.boot_count = 7;
    CHECK_EQ(runstate_save(&S, &rs), 0);
    rf.fail_write_after = 0;
    rs.boot_count = 8;
    CHECK_EQ(runstate_save(&S, &rs), -1);
    rf.fail_write_after = -1;
    CHECK_EQ(runstate_load(&S, &got), 0);
    CHECK_EQ(got.boot_count, 7);

    /* junk in an otherwise empty area (e.g. old Tuya KV bytes) */
    rf_reset();
    {
        uint8_t junk[RUNSTATE_RECORD_SIZE];
        memset(junk, 0x00, sizeof(junk));
        rf_write(&rf, S.addr[0], junk, sizeof(junk));
        rf_write(&rf, S.addr[1] + 64, junk, sizeof(junk));
    }
    CHECK_EQ(runstate_load(&S, &got), -1);
    rs.boot_count = 1;
    CHECK_EQ(runstate_save(&S, &rs), 0);
    CHECK_EQ(runstate_load(&S, &got), 0);
    CHECK_EQ(got.boot_count, 1);

    /* sequence wrap-around */
    rf_reset();
    rs.boot_count = 99;
    forge(S.addr[0], 0xFFFFFFFFu, &rs);
    CHECK_EQ(runstate_load(&S, &got), 0);
    CHECK_EQ(got.boot_count, 99);
    rs.boot_count = 100;
    CHECK_EQ(runstate_save(&S, &rs), 0);            /* seq 0 */
    CHECK_EQ(runstate_load(&S, &got), 0);
    CHECK_EQ(got.boot_count, 100);

    /* erase */
    CHECK_EQ(runstate_erase(&S), 0);
    CHECK_EQ(runstate_load(&S, &got), -1);

    /* time keeping across sleeps */
    memset(&rs, 0, sizeof(rs));
    CHECK_EQ(runstate_unix_time(&rs), 0);
    rs.time_epoch = 1795000000u;
    rs.time_min = 100;
    rs.now_min = 160;
    CHECK_EQ(runstate_unix_time(&rs), 1795003600u);
    rs.now_min = 50;                                /* corrupt: clock went backwards */
    CHECK_EQ(runstate_unix_time(&rs), 0);
}
