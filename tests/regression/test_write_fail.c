#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "../../src/common.h"
#include "../../src/journal.h"
#include "../../src/stripe_engine.h"
#include "../../src/pool_io.h"

/*
 * Regression Test for B7: Unchecked WriteFile
 *
 * journal_begin / journal_data / journal_commit were missing
 * partial-write checks: WriteFile's BOOL result stored in `ok` but
 * `written != requested` was undetected.
 *
 * Fix: added `&& written == expected_size` after every WriteFile.
 *
 * Test verifies:
 *   1. Normal journal roundtrip works (no regression)
 *   2. journal_data(NULL) returns false
 *   3. journal_begin(NULL) returns false
 *   4. journal_commit(NULL) returns false
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

#define JOURNAL_ROOT L"C:\\RAIDTEST\\"

static bool ensure_dir(void) {
    if (CreateDirectoryW(JOURNAL_ROOT, NULL)) return true;
    if (GetLastError() == ERROR_ALREADY_EXISTS) return true;
    return false;
}

static void cleanup_dir(void) {
    wchar_t jpath[MAX_DRIVE_PATH];
    wcscpy_s(jpath, MAX_DRIVE_PATH, L"C:\\RAIDTEST\\journal.dat");
    DeleteFileW(jpath);
}

static bool create_volume(STRIPE_VOLUME* vol, DISK_INFO* disks[2]) {
    wchar_t temp_dir[MAX_DRIVE_PATH];
    DWORD ret = GetTempPathW(MAX_DRIVE_PATH, temp_dir);
    if (ret == 0 || ret > MAX_DRIVE_PATH) wcscpy_s(temp_dir, MAX_DRIVE_PATH, L"C:\\Temp\\");

    for (int i = 0; i < 2; i++) {
        disks[i] = (DISK_INFO*)calloc(1, sizeof(DISK_INFO));
        if (!disks[i]) return false;
        swprintf(disks[i]->file_path, MAX_DRIVE_PATH, L"%sjournal_b7_%d.dat", temp_dir, i);
        disks[i]->pool_bytes = 0;
        disks[i]->total_bytes = 16ULL * 1024 * 1024;
        disks[i]->handle = INVALID_HANDLE_VALUE;
        disks[i]->sector_size = 512;
        wcscpy_s(disks[i]->drive_letter, MAX_DRIVE_PATH, L"C:");
        wcscpy_s(disks[i]->device_path, MAX_DRIVE_PATH, L"\\\\.\\C:");
        wcscpy_s(disks[i]->model, MAX_MODEL_LEN, L"B7 Regression");
        disks[i]->selected = true;
        disks[i]->healthy = 1;

        if (!pool_file_create(disks[i], 16ULL * 1024 * 1024)) {
            printf("FAIL: pool_file_create disk %d\n", i);
            return false;
        }
    }
    if (!stripe_volume_create(vol, disks, 2, 16ULL * 1024 * 1024)) {
        printf("FAIL: stripe_volume_create\n");
        return false;
    }
    return true;
}

static void destroy_volume(STRIPE_VOLUME* vol) {
    for (uint32_t i = 0; i < vol->disk_count; i++) {
        if (vol->disks[i]) {
            if (vol->disks[i]->handle != INVALID_HANDLE_VALUE) CloseHandle(vol->disks[i]->handle);
            DeleteFileW(vol->disks[i]->file_path);
            free(vol->disks[i]);
        }
    }
    cleanup_dir();
}

int main(void) {
    log_init();
    log_set_level(LOG_LEVEL_ERROR);
    printf("=== Regression Test B7: Unchecked WriteFile ===\n");

    TEST(ensure_dir(), "create C:\\RAIDTEST\\");

    STRIPE_VOLUME vol;
    DISK_INFO* disks[2] = {NULL, NULL};

    /* 1. Normal journal roundtrip */
    TEST(create_volume(&vol, disks), "create volume");

    uint8_t data[1024];
    memset(data, 0xBB, sizeof(data));

    bool ok = journal_begin(&vol);
    TEST(ok, "journal_begin succeeds");

    ok = journal_data(&vol, 4096, 1024, data);
    TEST(ok, "journal_data succeeds");

    ok = journal_commit(&vol);
    TEST(ok, "journal_commit succeeds");

    destroy_volume(&vol);

    /* 2. journal_data with NULL volume */
    ok = journal_data(NULL, 0, 0, NULL);
    TEST(!ok, "journal_data(NULL) returns false");

    /* 3. journal_begin with NULL volume */
    ok = journal_begin(NULL);
    TEST(!ok, "journal_begin(NULL) returns false");

    /* 4. journal_commit with NULL volume */
    ok = journal_commit(NULL);
    TEST(!ok, "journal_commit(NULL) returns false");

    log_cleanup();

    printf("\n=== B7 Results: %d failures ===\n", failures);
    return failures > 0 ? 1 : 0;
}
