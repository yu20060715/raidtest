#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "../../src/common.h"
#include "../../src/ram_cache.h"

/*
 * Regression Test for B10: Cache Thread Race
 *
 * cache_destroy did not wait for the flush thread before
 * freeing cache->buffer, causing use-after-free.
 *
 * Fix: cache_destroy now waits on cache->flush_thread (if set)
 *       before freeing any memory. cache_init initializes
 *       flush_thread = NULL and write_through = 0.
 *
 * Test verifies:
 *   1. cache_init initializes flush_thread to NULL and write_through to 0
 *   2. cache_destroy with NULL flush_thread works (no crash)
 *   3. cache_destroy with a valid event handle waits and cleans up
 *   4. Normal init/write/read/destroy lifecycle works
 */

static int failures = 0;

#define TEST(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s\n", msg); \
        failures++; \
    } else { \
        printf("  PASS: %s\n", msg); \
    } \
} while(0)

int main(void) {
    log_init();
    log_set_level(LOG_LEVEL_ERROR);
    printf("=== Regression Test B10: Cache Thread Race ===\n");

    RAM_CACHE c;
    memset(&c, 0xFF, sizeof(c)); /* Fill with garbage to ensure init clears fields */

    /* 1. cache_init initializes fields */
    bool ok = cache_init(&c, 1024 * 1024);
    TEST(ok, "cache_init(1MB) succeeds");
    TEST(c.flush_thread == NULL, "cache_init sets flush_thread = NULL");
    TEST(c.write_through == 0, "cache_init sets write_through = 0");
    TEST(c.buffer != NULL, "cache_init allocates buffer");
    TEST(c.running == 1, "cache_init sets running = 1");

    /* 2. cache_destroy with NULL flush_thread works */
    cache_destroy(&c);
    TEST(c.buffer == NULL, "cache_destroy frees buffer");
    TEST(c.flush_thread == NULL, "cache_destroy clears flush_thread");

    /* 3. cache_destroy with valid event handle */
    ok = cache_init(&c, 1024 * 1024);
    TEST(ok, "cache_init succeeds for test 3");

    HANDLE evt = CreateEventW(NULL, TRUE, FALSE, NULL);
    TEST(evt != NULL, "create test event handle");

    c.flush_thread = evt;
    /* The event is unsignaled: WaitForSingleObject would block.
       Set it to signaled to let cache_destroy pass through. */
    SetEvent(evt);
    cache_destroy(&c);
    TEST(c.buffer == NULL, "cache_destroy with event handle frees buffer");
    TEST(c.flush_thread == NULL, "cache_destroy clears flush_thread");

    /* 4. Normal lifecycle with write/read */
    ok = cache_init(&c, 64 * 1024);
    TEST(ok, "cache_init(64KB) succeeds");

    char test_data[256];
    char read_buf[256] = {0};
    memset(test_data, 0xAB, sizeof(test_data));

    ok = cache_write(&c, test_data, 0, sizeof(test_data));
    TEST(ok, "cache_write succeeds");

    ok = cache_read(&c, read_buf, 0, sizeof(read_buf));
    TEST(ok, "cache_read succeeds");
    TEST(memcmp(test_data, read_buf, sizeof(test_data)) == 0, "cache_read returns written data");

    cache_destroy(&c);
    TEST(c.buffer == NULL, "final cache_destroy succeeds");

    log_cleanup();

    printf("\n=== B10 Results: %d failures ===\n", failures);
    return failures > 0 ? 1 : 0;
}
