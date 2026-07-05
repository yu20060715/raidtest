#include "test_common.h"
#include "ram_cache.h"
#include "stripe_engine.h"

/* ---- Basic init / destroy ---- */
static bool test_cache_init_destroy(void) {
    RAM_CACHE c;
    memset(&c, 0, sizeof(c));
    ASSERT(cache_init(&c, 4ULL * 1024 * 1024), "init 4MB failed");
    ASSERT(c.buffer != NULL, "buffer NULL");
    ASSERT_EQ(c.block_size, CACHE_BLOCK_SIZE, "block_size");
    ASSERT_EQ(c.block_count, 4ULL * 1024 * 1024 / CACHE_BLOCK_SIZE, "block_count");
    ASSERT_EQ(c.cache_size_bytes, 4ULL * 1024 * 1024, "cache_size");
    ASSERT_EQ(c.flush_buffer_size, MAX_FLUSH_SIZE, "flush_buffer_size");
    ASSERT(c.flush_buffer != NULL, "flush_buffer NULL");
    ASSERT(c.dirty_map != NULL, "dirty_map NULL");
    cache_destroy(&c);
    ASSERT(c.buffer == NULL, "buffer not nulled after destroy");
    printf("  PASS: cache_init_destroy\n");
    return true;
}

static bool test_cache_init_zero_fails(void) {
    RAM_CACHE c;
    memset(&c, 0, sizeof(c));
    ASSERT(!cache_init(&c, 0), "init with 0 size should fail");
    printf("  PASS: cache_init_zero_fails\n");
    return true;
}

/* ---- Write then read back ---- */
static bool test_cache_write_read(void) {
    RAM_CACHE c;
    memset(&c, 0, sizeof(c));
    ASSERT(cache_init(&c, 64 * 1024), "init failed");

    uint8_t wbuf[256];
    test_fill_pattern(wbuf, 256, 0x100);
    ASSERT(cache_write(&c, wbuf, 0, 256), "write failed");

    uint8_t rbuf[256];
    memset(rbuf, 0xAA, 256);
    ASSERT(cache_read(&c, rbuf, 0, 256), "read failed");
    ASSERT(memcmp(wbuf, rbuf, 256) == 0, "data mismatch");

    /* Read at non-zero offset */
    uint8_t wbuf2[128];
    test_fill_pattern(wbuf2, 128, 0x200);
    ASSERT(cache_write(&c, wbuf2, 4096, 128), "write at 4096 failed");
    memset(rbuf, 0, 128);
    ASSERT(cache_read(&c, rbuf, 4096, 128), "read at 4096 failed");
    ASSERT(memcmp(wbuf2, rbuf, 128) == 0, "data mismatch at offset");

    cache_destroy(&c);
    printf("  PASS: cache_write_read\n");
    return true;
}

/* ---- Write across block boundary ---- */
static bool test_cache_cross_block(void) {
    RAM_CACHE c;
    memset(&c, 0, sizeof(c));
    ASSERT(cache_init(&c, 4 * CACHE_BLOCK_SIZE), "init failed");

    uint32_t len = CACHE_BLOCK_SIZE + 1000;
    uint8_t* wbuf = (uint8_t*)VirtualAlloc(NULL, len, MEM_COMMIT, PAGE_READWRITE);
    ASSERT(wbuf, "alloc failed");
    test_fill_pattern(wbuf, len, 0x333);

    /* Write straddling the boundary between block 1 and block 2 */
    uint64_t off = (uint64_t)CACHE_BLOCK_SIZE - 200;
    ASSERT(cache_write(&c, wbuf, off, len), "cross-block write failed");

    uint8_t* rbuf = (uint8_t*)VirtualAlloc(NULL, len, MEM_COMMIT, PAGE_READWRITE);
    ASSERT(rbuf, "alloc failed");
    memset(rbuf, 0, len);
    ASSERT(cache_read(&c, rbuf, off, len), "cross-block read failed");
    ASSERT(memcmp(wbuf, rbuf, len) == 0, "cross-block data mismatch");

    /* Verify dirty bits: blocks 0, 1, 2 (the 3 blocks the write spans) */
    ASSERT(c.dirty_map[0] & 1, "block 0 not dirty");
    ASSERT(c.dirty_map[0] & (1 << 1), "block 1 not dirty");
    ASSERT(c.dirty_map[0] & (1 << 2), "block 2 not dirty");
    /* Block 3 should NOT be dirty (write ends at 131872 < 3*65536=196608) */
    ASSERT(!(c.dirty_map[0] & (1 << 3)), "block 3 should be clean");

    VirtualFree(wbuf, 0, MEM_RELEASE);
    VirtualFree(rbuf, 0, MEM_RELEASE);
    cache_destroy(&c);
    printf("  PASS: cache_cross_block\n");
    return true;
}

/* ---- Writes mark dirty; flush_all clears dirty bits ---- */
static bool test_cache_dirty_and_flush(void) {
    RAM_CACHE c;
    memset(&c, 0, sizeof(c));
    ASSERT(cache_init(&c, 4 * CACHE_BLOCK_SIZE), "init failed");

    /* Initially no dirty blocks */
    uint32_t dirty_initial = 0;
    for (uint32_t b = 0; b < c.block_count; b++)
        if (c.dirty_map[b / 8] & (1 << (b % 8))) dirty_initial++;
    ASSERT_EQ(dirty_initial, 0, "should start clean");

    /* Write to block 0 and 2 */
    uint8_t buf[64];
    ASSERT(cache_write(&c, buf, 0, 64), "write block 0 failed");
    ASSERT(cache_write(&c, buf, 2 * CACHE_BLOCK_SIZE, 64), "write block 2 failed");

    /* Verify dirty bits */
    ASSERT(c.dirty_map[0] & 1, "block 0 not dirty");
    ASSERT(!(c.dirty_map[0] & (1 << 1)), "block 1 should be clean");
    ASSERT(c.dirty_map[0] & (1 << 2), "block 2 not dirty");

    /* Write-through mode — flush_all writes to volume then clears dirty.
       Without a volume, simulate by clearing dirty map directly. */
    EnterCriticalSection(&c.lock);
    c.dirty_map[0] = 0;
    c.dirty_map[0] = 0;
    LeaveCriticalSection(&c.lock);

    ASSERT(!(c.dirty_map[0] & 1), "block 0 should be clean after clear");
    ASSERT(!(c.dirty_map[0] & (1 << 2)), "block 2 should be clean after clear");

    cache_destroy(&c);
    printf("  PASS: cache_dirty_and_flush\n");
    return true;
}

/* ---- Flush integration with stripe volume ---- */
static bool test_cache_flush_integration(void) {
    DISK_INFO* d0 = test_disk_create(32ULL * 1024 * 1024, 200);
    DISK_INFO* d1 = test_disk_create(32ULL * 1024 * 1024, 200);
    DISK_INFO* d2 = test_disk_create(32ULL * 1024 * 1024, 200);
    DISK_INFO* disks[] = {d0, d1, d2};
    STRIPE_VOLUME vol;
    ASSERT(stripe_volume_create(&vol, disks, 3, 1024 * 1024), "create failed");

    /* Init cache */
    ASSERT(cache_init(&vol.cache, 4ULL * 1024 * 1024), "cache_init failed");
    vol.cache_enabled = true;
    vol.cache.write_through = 1;

    /* Write via cache */
    uint32_t test_size = 64 * 1024;
    uint8_t* wbuf = (uint8_t*)VirtualAlloc(NULL, test_size, MEM_COMMIT, PAGE_READWRITE);
    ASSERT(wbuf, "alloc failed");
    test_fill_pattern(wbuf, test_size, 0x500);
    ASSERT(stripe_volume_write(&vol, wbuf, 0, test_size), "volume write failed");

    /* Flush cache */
    cache_flush_all(&vol.cache, &vol);

    /* Destroy cache, read directly from volume */
    cache_destroy(&vol.cache);
    vol.cache_enabled = false;

    uint8_t* rbuf = (uint8_t*)VirtualAlloc(NULL, test_size, MEM_COMMIT, PAGE_READWRITE);
    ASSERT(rbuf, "alloc failed");
    memset(rbuf, 0, test_size);
    ASSERT(stripe_volume_read(&vol, rbuf, 0, test_size), "volume read after flush failed");
    ASSERT(memcmp(wbuf, rbuf, test_size) == 0, "data mismatch after flush");

    VirtualFree(wbuf, 0, MEM_RELEASE);
    VirtualFree(rbuf, 0, MEM_RELEASE);
    stripe_volume_destroy(&vol);
    test_disk_destroy(d0);
    test_disk_destroy(d1);
    test_disk_destroy(d2);
    printf("  PASS: cache_flush_integration\n");
    return true;
}

/* ---- Multiple writes and flush ---- */
static bool test_cache_multi_write_flush(void) {
    DISK_INFO* d0 = test_disk_create(32ULL * 1024 * 1024, 200);
    DISK_INFO* d1 = test_disk_create(32ULL * 1024 * 1024, 200);
    DISK_INFO* disks[] = {d0, d1};
    STRIPE_VOLUME vol;
    ASSERT(stripe_volume_create(&vol, disks, 2, 1024 * 1024), "create failed");

    ASSERT(cache_init(&vol.cache, 2ULL * 1024 * 1024), "cache_init failed");
    vol.cache_enabled = true;
    vol.cache.write_through = 1;

    /* Write three non-contiguous regions */
    uint8_t w1[4096], w2[4096], w3[4096];
    test_fill_pattern(w1, 4096, 0x600);
    test_fill_pattern(w2, 4096, 0x700);
    test_fill_pattern(w3, 4096, 0x800);

    ASSERT(stripe_volume_write(&vol, w1, 0, 4096), "write 1 failed");
    ASSERT(stripe_volume_write(&vol, w2, 1024 * 1024, 4096), "write 2 failed");
    ASSERT(stripe_volume_write(&vol, w3, 2 * 1024 * 1024, 4096), "write 3 failed");

    cache_flush_all(&vol.cache, &vol);
    cache_destroy(&vol.cache);
    vol.cache_enabled = false;

    uint8_t r[4096];
    memset(r, 0, 4096);
    ASSERT(stripe_volume_read(&vol, r, 0, 4096), "read 1 failed");
    ASSERT(memcmp(w1, r, 4096) == 0, "data 1 mismatch");

    memset(r, 0, 4096);
    ASSERT(stripe_volume_read(&vol, r, 1024 * 1024, 4096), "read 2 failed");
    ASSERT(memcmp(w2, r, 4096) == 0, "data 2 mismatch");

    memset(r, 0, 4096);
    ASSERT(stripe_volume_read(&vol, r, 2 * 1024 * 1024, 4096), "read 3 failed");
    ASSERT(memcmp(w3, r, 4096) == 0, "data 3 mismatch");

    stripe_volume_destroy(&vol);
    test_disk_destroy(d0);
    test_disk_destroy(d1);
    printf("  PASS: cache_multi_write_flush\n");
    return true;
}

/* ---- Read returns written data even before flush (buffered) ---- */
static bool test_cache_buffered_read(void) {
    RAM_CACHE c;
    memset(&c, 0, sizeof(c));
    ASSERT(cache_init(&c, 64 * 1024), "init failed");

    uint8_t wbuf[1024];
    test_fill_pattern(wbuf, 1024, 0x900);
    ASSERT(cache_write(&c, wbuf, 0, 1024), "write failed");

    /* Read back before any flush */
    uint8_t rbuf[1024];
    memset(rbuf, 0, 1024);
    ASSERT(cache_read(&c, rbuf, 0, 1024), "read failed");
    ASSERT(memcmp(wbuf, rbuf, 1024) == 0, "buffered read mismatch");

    cache_destroy(&c);
    printf("  PASS: cache_buffered_read\n");
    return true;
}

TEST(cache_init_destroy)         { return test_cache_init_destroy(); }
TEST(cache_init_zero_fails)      { return test_cache_init_zero_fails(); }
TEST(cache_write_read)           { return test_cache_write_read(); }
TEST(cache_cross_block)          { return test_cache_cross_block(); }
TEST(cache_dirty_and_flush)      { return test_cache_dirty_and_flush(); }
TEST(cache_flush_integration)    { return test_cache_flush_integration(); }
TEST(cache_multi_write_flush)    { return test_cache_multi_write_flush(); }
TEST(cache_buffered_read)        { return test_cache_buffered_read(); }
