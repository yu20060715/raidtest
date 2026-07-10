#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "test_common.h"

#define DISK_SIZE (8ULL * 1024 * 1024)
#define NUM_OPS 500
#define MAX_BLOCK 65536

static uint32_t rand_u32(void) {
    static uint32_t seed = 0xDEADBEEF;
    seed = seed * 1103515245 + 12345;
    return seed;
}

static bool run_random_io_test(void) {
    printf("=== Random IO Test: %u operations ===\n", NUM_OPS);

    DISK_INFO* d1 = test_disk_create(DISK_SIZE, 100);
    DISK_INFO* d2 = test_disk_create(DISK_SIZE, 100);
    ASSERT(d1 && d2, "disk create failed");

    DISK_INFO* disks[] = { d1, d2 };
    STRIPE_VOLUME vol;
    memset(&vol, 0, sizeof(vol));
    ASSERT(stripe_volume_create(&vol, disks, 2, 65536), "volume create failed");

    uint64_t vol_size = vol.virtual_total_bytes;
    uint8_t* ref = (uint8_t*)VirtualAlloc(NULL, (size_t)vol_size, MEM_COMMIT, PAGE_READWRITE);
    uint8_t* buf = (uint8_t*)VirtualAlloc(NULL, MAX_BLOCK, MEM_COMMIT, PAGE_READWRITE);
    ASSERT(ref && buf, "buffer alloc failed");
    memset(ref, 0, (size_t)vol_size);

    for (uint32_t op = 0; op < NUM_OPS; op++) {
        uint32_t block_size = (rand_u32() % MAX_BLOCK) + 1;
        if (block_size > vol_size) block_size = (uint32_t)vol_size;
        uint64_t offset = rand_u32() % (vol_size - block_size);
        uint32_t choice = rand_u32() % 4;

        if (choice == 0 || choice == 1) {
            /* Write random data */
            for (uint32_t i = 0; i < block_size; i++)
                buf[i] = (uint8_t)(rand_u32() & 0xFF);
            ASSERT(stripe_volume_write(&vol, buf, offset, block_size),
                   "write failed at op %u", op);
            memcpy(ref + offset, buf, block_size);
        } else if (choice == 2) {
            /* Read and verify */
            memset(buf, 0, block_size);
            ASSERT(stripe_volume_read(&vol, buf, offset, block_size),
                   "read failed at op %u", op);
            ASSERT(memcmp(buf, ref + offset, block_size) == 0,
                   "data mismatch at op %u offset %llu", op, (unsigned long long)offset);
        } else {
            /* Overwrite with known pattern */
            test_fill_pattern(buf, block_size, offset);
            ASSERT(stripe_volume_write(&vol, buf, offset, block_size),
                   "overwrite failed at op %u", op);
            memcpy(ref + offset, buf, block_size);

            memset(buf, 0, block_size);
            ASSERT(stripe_volume_read(&vol, buf, offset, block_size),
                   "read after overwrite failed at op %u", op);
            ASSERT(memcmp(buf, ref + offset, block_size) == 0,
                   "overwrite verify mismatch at op %u", op);
        }

        if ((op + 1) % 100 == 0)
            printf("  %u / %u random ops passed\n", op + 1, NUM_OPS);
    }

    /* Final: verify entire volume against reference */
    printf("  Verifying full volume integrity...\n");
    uint8_t* full = (uint8_t*)VirtualAlloc(NULL, (size_t)vol_size, MEM_COMMIT, PAGE_READWRITE);
    ASSERT(full != NULL, "full verify buffer alloc failed");
    memset(full, 0, (size_t)vol_size);
    ASSERT(stripe_volume_read(&vol, full, 0, (uint32_t)vol_size),
           "full volume read failed");
    ASSERT(memcmp(full, ref, (size_t)vol_size) == 0,
           "full volume integrity check failed");
    VirtualFree(full, 0, MEM_RELEASE);

    VirtualFree(ref, 0, MEM_RELEASE);
    VirtualFree(buf, 0, MEM_RELEASE);
    stripe_volume_destroy(&vol);
    test_disk_destroy(d1);
    test_disk_destroy(d2);

    printf("PASS: Random IO Test\n");
    return true;
}

int main(void) {
    log_init();
    log_set_level(LOG_LEVEL_WARN);
    bool ok = run_random_io_test();
    log_cleanup();
    return ok ? 0 : 1;
}
