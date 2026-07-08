#include "test_common.h"
#include "mirror_engine.h"
#include "stripe_engine.h"
#include "storage_common.h"
#include "ram_cache.h"

static bool test_mirror_create_2disks(void) {
    DISK_INFO* d0 = test_disk_create(64ULL * 1024 * 1024, 200);
    DISK_INFO* d1 = test_disk_create(64ULL * 1024 * 1024, 400);
    DISK_INFO* disks[] = {d0, d1};
    STRIPE_VOLUME vol;
    ASSERT(mirror_volume_create(&vol, disks, 2), "create failed");
    ASSERT_EQ(vol.raid_level, RAID_LEVEL_MIRROR, "raid_level");
    ASSERT_EQ(vol.disk_count, 2, "disk_count");
    ASSERT(vol.virtual_total_bytes == 64ULL * 1024 * 1024, "wrong size");

    ASSERT(test_verify_volume_read_write(&vol, 1024 * 1024), "rw on mirror failed");

    stripe_volume_destroy(&vol);
    test_disk_destroy(d0);
    test_disk_destroy(d1);
    printf("  PASS: mirror_create_2disks\n");
    return true;
}

static bool test_mirror_degraded_read(void) {
    DISK_INFO* d0 = test_disk_create(16ULL * 1024 * 1024, 200);
    DISK_INFO* d1 = test_disk_create(16ULL * 1024 * 1024, 200);
    DISK_INFO* disks[] = {d0, d1};
    STRIPE_VOLUME vol;
    ASSERT(mirror_volume_create(&vol, disks, 2), "create failed");

    /* Write data */
    uint8_t buf[4096];
    test_fill_pattern(buf, 4096, 0);
    ASSERT(mirror_volume_write(&vol, buf, 0, 4096), "write failed");

    /* Mark first disk unhealthy */
    InterlockedExchange(&d0->healthy, 0);
    InterlockedExchange(&vol.healthy_count, 1);

    /* Read should succeed from disk 1 */
    uint8_t rbuf[4096];
    memset(rbuf, 0, 4096);
    ASSERT(mirror_volume_read(&vol, rbuf, 0, 4096), "degraded read failed");
    ASSERT(memcmp(buf, rbuf, 4096) == 0, "data mismatch on degraded read");

    stripe_volume_destroy(&vol);
    test_disk_destroy(d0);
    test_disk_destroy(d1);
    printf("  PASS: mirror_degraded_read\n");
    return true;
}

static bool test_mirror_all_dead_read_fails(void) {
    DISK_INFO* d0 = test_disk_create(16ULL * 1024 * 1024, 200);
    DISK_INFO* d1 = test_disk_create(16ULL * 1024 * 1024, 200);
    DISK_INFO* disks[] = {d0, d1};
    STRIPE_VOLUME vol;
    ASSERT(mirror_volume_create(&vol, disks, 2), "create failed");

    InterlockedExchange(&d0->healthy, 0);
    InterlockedExchange(&d1->healthy, 0);
    InterlockedExchange(&vol.healthy_count, 0);

    uint8_t buf[512];
    ASSERT(!mirror_volume_read(&vol, buf, 0, 512), "should have failed (all dead)");

    stripe_volume_destroy(&vol);
    test_disk_destroy(d0);
    test_disk_destroy(d1);
    printf("  PASS: mirror_all_dead_read_fails\n");
    return true;
}

static bool test_mirror_write_to_all(void) {
    DISK_INFO* d0 = test_disk_create(16ULL * 1024 * 1024, 200);
    DISK_INFO* d1 = test_disk_create(16ULL * 1024 * 1024, 200);
    DISK_INFO* disks[] = {d0, d1};
    STRIPE_VOLUME vol;
    ASSERT(mirror_volume_create(&vol, disks, 2), "create failed");

    uint8_t buf[4096];
    test_fill_pattern(buf, 4096, 0xABCD);
    ASSERT(mirror_volume_write(&vol, buf, 0, 4096), "write failed");

    /* Verify on both disks via raw read */
    uint8_t r0[4096], r1[4096];
    ASSERT(stripe_read_raw(d0, r0, 0, 4096), "raw read d0 failed");
    ASSERT(stripe_read_raw(d1, r1, 0, 4096), "raw read d1 failed");
    ASSERT(memcmp(buf, r0, 4096) == 0, "d0 data mismatch");
    ASSERT(memcmp(buf, r1, 4096) == 0, "d1 data mismatch");

    stripe_volume_destroy(&vol);
    test_disk_destroy(d0);
    test_disk_destroy(d1);
    printf("  PASS: mirror_write_to_all\n");
    return true;
}

static bool test_mirror_rebuild(void) {
    DISK_INFO* d0 = test_disk_create(16ULL * 1024 * 1024, 200);
    DISK_INFO* d1 = test_disk_create(8ULL * 1024 * 1024, 200); /* smaller — limits volume */
    DISK_INFO* disks[] = {d0, d1};
    STRIPE_VOLUME vol;
    ASSERT(mirror_volume_create(&vol, disks, 2), "create failed");

    /* Write data */
    uint32_t test_size = 1024 * 1024;
    uint8_t* wbuf = (uint8_t*)VirtualAlloc(NULL, test_size, MEM_COMMIT, PAGE_READWRITE);
    ASSERT(wbuf, "alloc failed");
    test_fill_pattern(wbuf, test_size, 0);
    ASSERT(stripe_volume_write(&vol, wbuf, 0, test_size), "write failed");

    /* Mark disk 0 as failed, replace it */
    DISK_INFO* replacement = test_disk_create(8ULL * 1024 * 1024, 300);
    InterlockedExchange(&d0->healthy, 0);
    InterlockedExchange(&vol.healthy_count, 1);

    ASSERT(mirror_volume_rebuild(&vol, replacement, 0), "rebuild failed");

    /* Verify rebuild data */
    uint8_t* rbuf = (uint8_t*)VirtualAlloc(NULL, test_size, MEM_COMMIT, PAGE_READWRITE);
    ASSERT(rbuf, "alloc failed");
    memset(rbuf, 0, test_size);
    ASSERT(stripe_read_raw(replacement, rbuf, 0, test_size), "read rebuild failed");
    ASSERT(memcmp(wbuf, rbuf, test_size) == 0, "rebuild data mismatch");

    VirtualFree(wbuf, 0, MEM_RELEASE);
    VirtualFree(rbuf, 0, MEM_RELEASE);
    stripe_volume_destroy(&vol);
    test_disk_destroy(d0);
    test_disk_destroy(d1);
    test_disk_destroy(replacement);
    printf("  PASS: mirror_rebuild\n");
    return true;
}

static bool test_mirror_cache_integration(void) {
    DISK_INFO* d0 = test_disk_create(32ULL * 1024 * 1024, 200);
    DISK_INFO* d1 = test_disk_create(32ULL * 1024 * 1024, 200);
    DISK_INFO* disks[] = {d0, d1};
    STRIPE_VOLUME vol;
    ASSERT(mirror_volume_create(&vol, disks, 2), "create failed");

    /* Init cache (write-through for simplicity) */
    ASSERT(cache_init(&vol.cache, 4ULL * 1024 * 1024), "cache_init failed");
    vol.cache_enabled = true;
    vol.cache.write_through = 1;

    uint8_t buf[4096];
    test_fill_pattern(buf, 4096, 0x1234);
    ASSERT(stripe_volume_write(&vol, buf, 0, 4096), "cache write failed");

    uint8_t rbuf[4096];
    memset(rbuf, 0, 4096);
    ASSERT(stripe_volume_read(&vol, rbuf, 0, 4096), "cache read failed");
    ASSERT(memcmp(buf, rbuf, 4096) == 0, "cache data mismatch");

    /* Flush cache and verify on disk */
    cache_flush_all(&vol.cache, &vol);
    cache_destroy(&vol.cache);
    vol.cache_enabled = false;

    memset(rbuf, 0, 4096);
    ASSERT(stripe_volume_read(&vol, rbuf, 0, 4096), "post-cache read failed");
    ASSERT(memcmp(buf, rbuf, 4096) == 0, "post-cache data mismatch");

    stripe_volume_destroy(&vol);
    test_disk_destroy(d0);
    test_disk_destroy(d1);
    printf("  PASS: mirror_cache_integration\n");
    return true;
}

static bool test_mirror_rebuild_with_concurrent_write(void) {
    DISK_INFO* d0 = test_disk_create(16ULL * 1024 * 1024, 200);
    DISK_INFO* d1 = test_disk_create(16ULL * 1024 * 1024, 200);
    DISK_INFO* disks[] = {d0, d1};
    STRIPE_VOLUME vol;
    ASSERT(mirror_volume_create(&vol, disks, 2), "create failed");

    /* Write initial data */
    uint32_t test_size = 1024 * 1024;
    uint8_t* wbuf = (uint8_t*)VirtualAlloc(NULL, test_size, MEM_COMMIT, PAGE_READWRITE);
    ASSERT(wbuf, "alloc failed");
    test_fill_pattern(wbuf, test_size, 0);
    ASSERT(stripe_volume_write(&vol, wbuf, 0, test_size), "initial write failed");

    /* Simulate: concurrent write happens after old disk is disabled (by B3 fix)
       but before rebuild completes. The write goes only to remaining healthy disk. */
    InterlockedExchange(&d0->healthy, 0);
    InterlockedExchange(&vol.healthy_count, 1);
    uint32_t extra_size = 4096;
    uint8_t* extra = (uint8_t*)VirtualAlloc(NULL, extra_size, MEM_COMMIT, PAGE_READWRITE);
    ASSERT(extra, "alloc failed");
    test_fill_pattern(extra, extra_size, 0xFF);
    ASSERT(mirror_volume_write(&vol, extra, test_size, extra_size), "concurrent write failed");

    /* Now rebuild disk 0 */
    DISK_INFO* replacement = test_disk_create(16ULL * 1024 * 1024, 300);
    ASSERT(mirror_volume_rebuild(&vol, replacement, 0), "rebuild failed");

    /* Verify: replacement has all data (initial + concurrent write) */
    uint8_t* rbuf = (uint8_t*)VirtualAlloc(NULL, test_size + extra_size, MEM_COMMIT, PAGE_READWRITE);
    ASSERT(rbuf, "alloc failed");
    ASSERT(stripe_read_raw(replacement, rbuf, 0, test_size), "read initial data from replacement failed");
    ASSERT(memcmp(wbuf, rbuf, test_size) == 0, "initial data mismatch on replacement");
    ASSERT(stripe_read_raw(replacement, rbuf, test_size, extra_size), "read extra data from replacement failed");
    ASSERT(memcmp(extra, rbuf, extra_size) == 0, "concurrent write data mismatch on replacement");

    VirtualFree(wbuf, 0, MEM_RELEASE);
    VirtualFree(extra, 0, MEM_RELEASE);
    VirtualFree(rbuf, 0, MEM_RELEASE);
    stripe_volume_destroy(&vol);
    test_disk_destroy(d0);
    test_disk_destroy(d1);
    test_disk_destroy(replacement);
    printf("  PASS: mirror_rebuild_with_concurrent_write\n");
    return true;
}

static bool test_mirror_rebuild_failure_rollback(void) {
    DISK_INFO* d0 = test_disk_create(16ULL * 1024 * 1024, 200);
    DISK_INFO* d1 = test_disk_create(16ULL * 1024 * 1024, 200);
    DISK_INFO* disks[] = {d0, d1};
    STRIPE_VOLUME vol;
    ASSERT(mirror_volume_create(&vol, disks, 2), "create failed");

    uint32_t test_size = 4096;
    uint8_t* wbuf = (uint8_t*)VirtualAlloc(NULL, test_size, MEM_COMMIT, PAGE_READWRITE);
    ASSERT(wbuf, "alloc failed");
    test_fill_pattern(wbuf, test_size, 0);
    ASSERT(stripe_volume_write(&vol, wbuf, 0, test_size), "write failed");
    VirtualFree(wbuf, 0, MEM_RELEASE);

    /* Create replacement and mark it faulty to trigger write failure */
    DISK_INFO* replacement = test_disk_create(16ULL * 1024 * 1024, 200);
    replacement->faulty = 1;
    uint32_t initial_healthy = (uint32_t)vol.healthy_count;

    ASSERT(!mirror_volume_rebuild(&vol, replacement, 0), "rebuild should have failed");

    /* Rollback verification: old disk restored to healthy */
    ASSERT(InterlockedCompareExchange(&d0->healthy, 1, 1) == 1,
           "old disk should be healthy after rollback");
    ASSERT_EQ(vol.healthy_count, initial_healthy, "healthy_count should be restored after rollback");
    ASSERT(vol.disks[0] == d0, "disks[0] should still point to old disk after failed rebuild");

    replacement->faulty = 0;
    test_disk_destroy(replacement);

    stripe_volume_destroy(&vol);
    test_disk_destroy(d0);
    test_disk_destroy(d1);
    printf("  PASS: mirror_rebuild_failure_rollback\n");
    return true;
}

static bool test_mirror_rebuild_healthy_count(void) {
    DISK_INFO* d0 = test_disk_create(16ULL * 1024 * 1024, 200);
    DISK_INFO* d1 = test_disk_create(16ULL * 1024 * 1024, 200);
    DISK_INFO* d2 = test_disk_create(16ULL * 1024 * 1024, 200);
    DISK_INFO* disks[] = {d0, d1, d2};
    STRIPE_VOLUME vol;
    ASSERT(mirror_volume_create(&vol, disks, 3), "create failed");
    ASSERT_EQ(vol.healthy_count, 3, "initial healthy_count should be 3");

    /* Mark disk 0 unhealthy (simulates pre-existing failure) */
    InterlockedExchange(&d0->healthy, 0);
    InterlockedExchange(&vol.healthy_count, 2);

    DISK_INFO* replacement = test_disk_create(16ULL * 1024 * 1024, 200);
    ASSERT(mirror_volume_rebuild(&vol, replacement, 0), "rebuild failed");

    /* After rebuild: replacement replaces d0, so healthy_count = d1 + d2 + replacement = 3 */
    ASSERT_EQ(vol.healthy_count, 3, "healthy_count should be 3 after rebuild");
    ASSERT(vol.disks[0] == replacement, "disks[0] should point to replacement");

    stripe_volume_destroy(&vol);
    test_disk_destroy(d0);
    test_disk_destroy(d1);
    test_disk_destroy(d2);
    test_disk_destroy(replacement);
    printf("  PASS: mirror_rebuild_healthy_count\n");
    return true;
}

TEST(mirror_create_2disks)            { return test_mirror_create_2disks(); }
TEST(mirror_degraded_read)            { return test_mirror_degraded_read(); }
TEST(mirror_all_dead_read_fails)      { return test_mirror_all_dead_read_fails(); }
TEST(mirror_write_to_all)             { return test_mirror_write_to_all(); }
TEST(mirror_rebuild)                  { return test_mirror_rebuild(); }
TEST(mirror_cache_integration)        { return test_mirror_cache_integration(); }
TEST(mirror_rebuild_with_concurrent_write) { return test_mirror_rebuild_with_concurrent_write(); }
TEST(mirror_rebuild_failure_rollback)      { return test_mirror_rebuild_failure_rollback(); }
TEST(mirror_rebuild_healthy_count)         { return test_mirror_rebuild_healthy_count(); }
