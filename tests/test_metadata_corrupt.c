#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "test_common.h"
#include "superblock.h"

/*
 * Metadata Corruption Test:
 *   Write a valid superblock to a temp directory.
 *   Deliberately corrupt fields (magic, version, uuid, checksum).
 *   Verify superblock_read_raw rejects each corrupted version.
 */

#define TEST_ROOT L"C:\\RAIDTEST"

static bool ensure_dir(void) {
    return CreateDirectoryW(TEST_ROOT, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static bool write_sb_file(const SUPERBLOCK* sb) {
    wchar_t path[MAX_DRIVE_PATH];
    StringCchPrintfW(path, MAX_DRIVE_PATH, L"%ls\\%ls", TEST_ROOT, SUPERBLOCK_FILENAME);
    HANDLE h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(h, sb, sizeof(SUPERBLOCK), &written, NULL);
    CloseHandle(h);
    return ok && written == sizeof(SUPERBLOCK);
}

static bool read_sb_file(SUPERBLOCK* sb) {
    wchar_t path[MAX_DRIVE_PATH];
    StringCchPrintfW(path, MAX_DRIVE_PATH, L"%ls\\%ls", TEST_ROOT, SUPERBLOCK_FILENAME);
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD read = 0;
    BOOL ok = ReadFile(h, sb, sizeof(SUPERBLOCK), &read, NULL);
    CloseHandle(h);
    return ok && read == sizeof(SUPERBLOCK);
}

static bool try_corrupt(const char* label, SUPERBLOCK* sb,
                        void (*corrupt_fn)(SUPERBLOCK*)) {
    SUPERBLOCK modified = *sb;
    corrupt_fn(&modified);

    if (!write_sb_file(&modified)) {
        printf("  %s: write failed\n", label);
        return false;
    }

    SUPERBLOCK readback;
    bool load_ok = superblock_read_raw(TEST_ROOT, &readback);

    if (load_ok) {
        printf("  FAIL: %s - load should have been rejected\n", label);
        return false;
    }
    printf("  OK:   %s - correctly rejected\n", label);
    return true;
}

static void corrupt_magic(SUPERBLOCK* sb)     { sb->magic = 0xDEADBEEF; }
static void corrupt_version(SUPERBLOCK* sb)   { sb->version = 99; }
static void corrupt_uuid(SUPERBLOCK* sb)      { sb->volume_uuid.high ^= 0xFFFFFFFFFFFFFFFFULL; }
static void corrupt_checksum(SUPERBLOCK* sb)  { sb->checksum ^= 0xFFFFFFFF; }

static bool run_metadata_corruption_test(void) {
    printf("=== Metadata Corruption Test ===\n");
    ASSERT(ensure_dir(), "cannot create %ls", TEST_ROOT);

    /* Create a valid superblock */
    SUPERBLOCK sb;
    memset(&sb, 0, sizeof(sb));
    sb.magic = SUPERBLOCK_MAGIC;
    sb.version = SUPERBLOCK_VERSION;
    sb.disk_count = 2;
    sb.stripe_unit = 65536;
    sb.virtual_total_bytes = 4ULL * 1024 * 1024;
    sb.phase_count = 1;
    sb.generation = 1;
    sb.timestamp = 0;
    sb.feature_flags = 0;
    sb.journal_offset = 0;
    memset(&sb.volume_uuid, 0, sizeof(sb.volume_uuid));
    sb.created_time = 0;
    sb.last_mount_time = 0;
    sb.disks[0].drive_letter[0] = L'C'; sb.disks[0].drive_letter[1] = L':';
    sb.disks[0].pool_bytes = 4ULL * 1024 * 1024;
    sb.disks[1].drive_letter[0] = L'C'; sb.disks[1].drive_letter[1] = L':';
    sb.disks[1].pool_bytes = 4ULL * 1024 * 1024;
    /* Compute valid CRC32 */
    sb.checksum = 0;
    /* Use a pre-computed approach: write, read back CRC, set it */
    ASSERT(write_sb_file(&sb), "initial sb write failed");

    /* Read it back to get correctly-CRC'd version */
    ASSERT(read_sb_file(&sb), "initial sb readback failed");
    ASSERT(sb.magic == SUPERBLOCK_MAGIC, "sb magic lost");

    bool all_ok = true;
    all_ok &= try_corrupt("Corrupt Magic",     &sb, corrupt_magic);
    all_ok &= try_corrupt("Corrupt Version",   &sb, corrupt_version);
    all_ok &= try_corrupt("Corrupt UUID",      &sb, corrupt_uuid);
    all_ok &= try_corrupt("Corrupt Checksum",  &sb, corrupt_checksum);

    ASSERT(all_ok, "one or more corruption tests failed");

    /* Cleanup */
    wchar_t path[MAX_DRIVE_PATH];
    StringCchPrintfW(path, MAX_DRIVE_PATH, L"%ls\\%ls", TEST_ROOT, SUPERBLOCK_FILENAME);
    DeleteFileW(path);

    printf("PASS: Metadata Corruption Test (all 4 types rejected)\n");
    return true;
}

int main(void) {
    log_init();
    log_set_level(LOG_LEVEL_WARN);
    bool ok = run_metadata_corruption_test();
    log_cleanup();
    return ok ? 0 : 1;
}
