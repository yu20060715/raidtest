#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "test_common.h"
#include "journal.h"
#include "superblock.h"
#include "pool_io.h"

/*
 * Power Failure Simulation (using C:\RAIDTEST\ for superblock compatibility):
 *   1. Create RAID volume with real pool files + superblock
 *   2. Write data with journal begin/data/commit
 *   3. Simulate power failure — close all handles without cleanup
 *   4. Reopen files, reload from superblock
 *   5. Verify journal recovery restores data intact
 */

#define DISK_SIZE (4ULL * 1024 * 1024)
#define DRIVE_ROOT L"C:\\"
#define POOL_DIR   L"C:\\RAIDTEST"

static bool ensure_dir(void) {
    return CreateDirectoryW(POOL_DIR, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static void cleanup_files(void) {
    wchar_t path[MAX_DRIVE_PATH];
    StringCchPrintfW(path, MAX_DRIVE_PATH, L"%ls\\%ls", POOL_DIR, SUPERBLOCK_FILENAME);
    DeleteFileW(path);
    StringCchPrintfW(path, MAX_DRIVE_PATH, L"%ls\\%ls", POOL_DIR, JOURNAL_FILENAME);
    DeleteFileW(path);
    StringCchPrintfW(path, MAX_DRIVE_PATH, L"%ls\\%ls", POOL_DIR, POOL_FILENAME);
    DeleteFileW(path);
}

static DISK_INFO* create_disk(const wchar_t* pool_file) {
    DISK_INFO* d = (DISK_INFO*)calloc(1, sizeof(DISK_INFO));
    if (!d) return NULL;

    wcscpy_s(d->drive_letter, MAX_DRIVE_PATH, DRIVE_ROOT);
    wcscpy_s(d->device_path, MAX_DRIVE_PATH, L"\\\\.\\C:");
    wcscpy_s(d->model, MAX_MODEL_LEN, L"PwrFail Disk");
    wcscpy_s(d->file_path, MAX_DRIVE_PATH, pool_file);

    d->pool_bytes = DISK_SIZE;
    d->total_bytes = DISK_SIZE;
    d->bench_write_mbs = 200;
    d->bench_read_mbs = 200;
    d->benchmarked = true;
    d->selected = true;
    d->healthy = 1;
    d->sector_size = SECTOR_SIZE;

    if (!pool_file_create(d, DISK_SIZE)) { free(d); return NULL; }
    if (!pool_file_open(d)) { DeleteFileW(d->file_path); free(d); return NULL; }
    return d;
}

static void destroy_disk(DISK_INFO* d) {
    if (!d) return;
    pool_file_close(d);
    free(d);
}

static bool test_power_failure_recovery(void) {
    printf("=== Power Failure Simulation ===\n");

    ensure_dir();
    cleanup_files();

    /* Both disks share the same pool file (like existing superblock tests) */
    DISK_INFO* d1 = create_disk(POOL_DIR L"\\" POOL_FILENAME);
    DISK_INFO* d2 = create_disk(POOL_DIR L"\\" POOL_FILENAME);
    ASSERT(d1 && d2, "disk create failed");

    DISK_INFO* disk_ptrs[] = { d1, d2 };
    STRIPE_VOLUME vol;
    memset(&vol, 0, sizeof(vol));
    ASSERT(stripe_volume_create(&vol, disk_ptrs, 2, 65536),
           "volume create failed");

    /* Write superblock */
    ASSERT(superblock_write(&vol), "superblock_write failed");

    /* Write data and journal */
    uint8_t wbuf[4096];
    for (uint32_t i = 0; i < sizeof(wbuf); i++)
        wbuf[i] = (uint8_t)((0xABC + i) & 0xFF);

    ASSERT(journal_begin(&vol), "journal begin failed");
    ASSERT(stripe_volume_write(&vol, wbuf, 0, sizeof(wbuf)),
           "pre-crash write failed");
    ASSERT(journal_data(&vol, 0, sizeof(wbuf), wbuf), "journal data failed");
    ASSERT(journal_commit(&vol), "journal commit failed");

    printf("  Data written and journal committed. Simulating power failure...\n");

    /* Simulate power failure: close handles without stripe_volume_destroy */
    CloseHandle(d1->handle); d1->handle = INVALID_HANDLE_VALUE;
    CloseHandle(d2->handle); d2->handle = INVALID_HANDLE_VALUE;

    /* "Power on" — reopen pool files */
    ASSERT(pool_file_open(d1), "reopen d1 failed");
    ASSERT(pool_file_open(d2), "reopen d2 failed");

    /* Reload volume from superblock (same pattern as existing tests) */
    STRIPE_VOLUME recovered;
    memset(&recovered, 0, sizeof(recovered));
    DISK_INFO recovered_disks[2];
    uint32_t disk_count_out = 0;

    ASSERT(superblock_read(DRIVE_ROOT, &recovered, recovered_disks,
                           &disk_count_out, NULL, 0),
           "superblock_read failed after power failure");

    /* Journal recovery — replay any uncommitted transactions */
    ASSERT(journal_recover_all(&recovered), "journal recovery failed");

    /* Verify data survived crash */
    uint8_t rbuf[4096];
    memset(rbuf, 0, sizeof(rbuf));
    ASSERT(stripe_volume_read(&recovered, rbuf, 0, sizeof(rbuf)),
           "post-recovery read failed");
    ASSERT(memcmp(wbuf, rbuf, sizeof(wbuf)) == 0,
           "post-recovery data mismatch");

    /* Cleanup: close handles that superblock_restore opened, then our own */
    for (uint32_t i = 0; i < disk_count_out; i++)
        pool_file_close(&recovered_disks[i]);

    stripe_volume_destroy(&recovered);
    destroy_disk(d1);
    destroy_disk(d2);
    cleanup_files();

    printf("PASS: Power Failure Recovery (data intact after crash)\n");
    return true;
}

int main(void) {
    log_init();
    log_set_level(LOG_LEVEL_WARN);
    bool ok = test_power_failure_recovery();
    log_cleanup();
    return ok ? 0 : 1;
}
