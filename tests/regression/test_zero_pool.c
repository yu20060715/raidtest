#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "../../src/common.h"
#include "../../src/pool_io.h"

/*
 * Regression Test for B4: Zero-byte Pool Crash
 *
 * Verify that pool_file_create rejects:
 *   size_bytes = 0
 *   size_bytes < SECTOR_SIZE
 *   (the function also takes size_bytes < 512)
 *
 * Also verify validate_pool_size rejects all invalid sizes.
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

static DISK_INFO* create_test_disk(void) {
    DISK_INFO* d = (DISK_INFO*)calloc(1, sizeof(DISK_INFO));
    if (!d) return NULL;
    wchar_t temp_dir[MAX_DRIVE_PATH];
    DWORD ret = GetTempPathW(MAX_DRIVE_PATH, temp_dir);
    if (ret == 0 || ret > MAX_DRIVE_PATH) wcscpy_s(temp_dir, MAX_DRIVE_PATH, L"C:\\Temp\\");

    swprintf(d->file_path, MAX_DRIVE_PATH, L"%sregression_b4_test.dat", temp_dir);
    d->pool_bytes = 0;
    d->total_bytes = 1024 * 1024;
    d->handle = INVALID_HANDLE_VALUE;
    d->sector_size = 512;
    wcscpy_s(d->drive_letter, MAX_DRIVE_PATH, L"T:");
    wcscpy_s(d->model, MAX_MODEL_LEN, L"B4 Regression");
    d->selected = true;
    d->healthy = 1;
    return d;
}

static void destroy_test_disk(DISK_INFO* d) {
    if (!d) return;
    if (d->handle != INVALID_HANDLE_VALUE) CloseHandle(d->handle);
    DeleteFileW(d->file_path);
    free(d);
}

int main(void) {
    log_init();
    log_set_level(LOG_LEVEL_ERROR);
    printf("=== Regression Test B4: Zero-byte Pool ===\n");

    /* 1. validate_pool_size rejects size=0 */
    DISK_INFO* d0 = create_test_disk();
    TEST(d0 != NULL, "create test disk");

    RC rc = validate_pool_size(0);
    TEST(rc == RC_ERR_INVALID_ARG, "validate_pool_size(0) returns INVALID_ARG");

    /* 2. validate_pool_size rejects size < SECTOR_SIZE */
    rc = validate_pool_size(128);
    TEST(rc == RC_ERR_INVALID_ARG, "validate_pool_size(128) returns INVALID_ARG");

    /* 3. validate_pool_size accepts valid size */
    rc = validate_pool_size(1024 * 1024);
    TEST(rc == RC_OK, "validate_pool_size(1MB) returns OK");

    /* 4. pool_file_create rejects size=0 */
    bool ok = pool_file_create(d0, 0);
    TEST(!ok, "pool_file_create(0) returns false");

    /* 5. pool_file_create rejects size < SECTOR_SIZE */
    ok = pool_file_create(d0, 128);
    TEST(!ok, "pool_file_create(128) returns false");

    /* 6. pool_file_create accepts valid size (1MB) */
    ok = pool_file_create(d0, 1024 * 1024);
    TEST(ok, "pool_file_create(1MB) returns true");

    /* Cleanup */
    destroy_test_disk(d0);
    log_cleanup();

    /* Summary */
    printf("\n=== B4 Results: %d failures ===\n", failures);
    return failures > 0 ? 1 : 0;
}
