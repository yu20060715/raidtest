#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "test_common.h"

#define LONG_RUN_ITERATIONS 10000
#define TEST_BLOCK_SIZE 4096
#define DISK_SIZE (4ULL * 1024 * 1024)

static bool run_longrun_test(void) {
    printf("=== Long Run Test: %u write/read/verify cycles ===\n", LONG_RUN_ITERATIONS);

    DISK_INFO* d1 = test_disk_create(DISK_SIZE, 100);
    DISK_INFO* d2 = test_disk_create(DISK_SIZE, 100);
    ASSERT(d1 && d2, "disk create failed");

    DISK_INFO* disks[] = { d1, d2 };
    STRIPE_VOLUME vol;
    memset(&vol, 0, sizeof(vol));
    ASSERT(stripe_volume_create(&vol, disks, 2, 65536), "volume create failed");

    uint8_t* wbuf = (uint8_t*)VirtualAlloc(NULL, TEST_BLOCK_SIZE, MEM_COMMIT, PAGE_READWRITE);
    uint8_t* rbuf = (uint8_t*)VirtualAlloc(NULL, TEST_BLOCK_SIZE, MEM_COMMIT, PAGE_READWRITE);
    ASSERT(wbuf && rbuf, "buffer alloc failed");

    for (uint32_t i = 0; i < LONG_RUN_ITERATIONS; i++) {
        test_fill_pattern(wbuf, TEST_BLOCK_SIZE, i * TEST_BLOCK_SIZE);
        ASSERT(stripe_volume_write(&vol, wbuf, 0, TEST_BLOCK_SIZE),
               "write failed at iteration %u", i);

        memset(rbuf, 0, TEST_BLOCK_SIZE);
        ASSERT(stripe_volume_read(&vol, rbuf, 0, TEST_BLOCK_SIZE),
               "read failed at iteration %u", i);

        ASSERT(memcmp(wbuf, rbuf, TEST_BLOCK_SIZE) == 0,
               "data mismatch at iteration %u", i);

        if ((i + 1) % 1000 == 0)
            printf("  %u / %u passed\n", i + 1, LONG_RUN_ITERATIONS);
    }

    VirtualFree(wbuf, 0, MEM_RELEASE);
    VirtualFree(rbuf, 0, MEM_RELEASE);
    stripe_volume_destroy(&vol);
    test_disk_destroy(d1);
    test_disk_destroy(d2);

    printf("PASS: Long Run Test (%u iterations)\n", LONG_RUN_ITERATIONS);
    return true;
}

int main(void) {
    log_init();
    log_set_level(LOG_LEVEL_WARN);
    bool ok = run_longrun_test();
    log_cleanup();
    return ok ? 0 : 1;
}
