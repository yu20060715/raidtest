#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <process.h>
#include "test_common.h"

#define DISK_SIZE (8ULL * 1024 * 1024)
#define THREAD_COUNT 4
#define OPS_PER_THREAD 250
#define BLOCK_SIZE 4096

typedef struct {
    STRIPE_VOLUME* vol;
    uint32_t id;
    volatile LONG* stop;
    volatile LONG* errors;
    volatile LONG* completed;
} THREAD_CTX;

static unsigned __stdcall worker_write(void* arg) {
    THREAD_CTX* ctx = (THREAD_CTX*)arg;
    uint8_t buf[BLOCK_SIZE];
    for (uint32_t i = 0; i < OPS_PER_THREAD; i++) {
        if (*ctx->stop) break;
        uint64_t offset = (ctx->id * OPS_PER_THREAD + i) * BLOCK_SIZE;
        if (offset + BLOCK_SIZE > ctx->vol->virtual_total_bytes) {
            offset = 0;
        }
        memset(buf, ctx->id + 1, BLOCK_SIZE);
        if (!stripe_volume_write(ctx->vol, buf, offset, BLOCK_SIZE)) {
            InterlockedIncrement(ctx->errors);
            break;
        }
    }
    InterlockedIncrement(ctx->completed);
    return 0;
}

static unsigned __stdcall worker_read(void* arg) {
    THREAD_CTX* ctx = (THREAD_CTX*)arg;
    uint8_t buf[BLOCK_SIZE];
    for (uint32_t i = 0; i < OPS_PER_THREAD; i++) {
        if (*ctx->stop) break;
        uint64_t offset = (ctx->id * OPS_PER_THREAD + i) * BLOCK_SIZE;
        if (offset + BLOCK_SIZE > ctx->vol->virtual_total_bytes) {
            offset = 0;
        }
        memset(buf, 0, BLOCK_SIZE);
        if (!stripe_volume_read(ctx->vol, buf, offset, BLOCK_SIZE)) {
            InterlockedIncrement(ctx->errors);
            break;
        }
    }
    InterlockedIncrement(ctx->completed);
    return 0;
}

static bool run_concurrent_test(void) {
    printf("=== Thread Safety Test: %u threads ===\n", THREAD_COUNT);

    DISK_INFO* d1 = test_disk_create(DISK_SIZE, 100);
    DISK_INFO* d2 = test_disk_create(DISK_SIZE, 100);
    ASSERT(d1 && d2, "disk create failed");

    DISK_INFO* disks[] = { d1, d2 };
    STRIPE_VOLUME vol;
    memset(&vol, 0, sizeof(vol));
    ASSERT(stripe_volume_create(&vol, disks, 2, 65536), "volume create failed");

    volatile LONG stop = 0;
    volatile LONG errors = 0;
    volatile LONG completed = 0;
    HANDLE threads[THREAD_COUNT];
    THREAD_CTX contexts[THREAD_COUNT];

    for (uint32_t i = 0; i < THREAD_COUNT; i++) {
        contexts[i].vol = &vol;
        contexts[i].id = i;
        contexts[i].stop = &stop;
        contexts[i].errors = &errors;
        contexts[i].completed = &completed;
        threads[i] = (HANDLE)_beginthreadex(NULL, 0,
            (i % 2 == 0) ? worker_write : worker_read,
            &contexts[i], 0, NULL);
        ASSERT(threads[i] != NULL, "thread %u create failed", i);
    }

    /* Wait for all threads */
    WaitForMultipleObjects(THREAD_COUNT, threads, TRUE, 30000);

    for (uint32_t i = 0; i < THREAD_COUNT; i++) {
        CloseHandle(threads[i]);
    }

    ASSERT(errors == 0, "%ld concurrent operation(s) failed", (long)errors);

    /* Final read verification: check known patterns */
    uint8_t buf[BLOCK_SIZE];
    for (uint32_t i = 0; i < THREAD_COUNT / 2; i++) {
        uint64_t offset = (i * OPS_PER_THREAD) * BLOCK_SIZE;
        if (offset + BLOCK_SIZE > vol.virtual_total_bytes) break;
        memset(buf, 0, BLOCK_SIZE);
        ASSERT(stripe_volume_read(&vol, buf, offset, BLOCK_SIZE),
               "final read failed after concurrent ops");
    }

    stripe_volume_destroy(&vol);
    test_disk_destroy(d1);
    test_disk_destroy(d2);

    printf("PASS: Thread Safety Test (%u threads, %u ops each)\n",
           THREAD_COUNT, OPS_PER_THREAD);
    return true;
}

int main(void) {
    log_init();
    log_set_level(LOG_LEVEL_WARN);
    bool ok = run_concurrent_test();
    log_cleanup();
    return ok ? 0 : 1;
}
