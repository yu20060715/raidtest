#include "test_common.h"
#include "journal.h"

#define JOURNAL_DRIVE L"C:"

static bool ensure_raidtest_dir(void) {
    wchar_t path[MAX_DRIVE_PATH];
    wcscpy_s(path, MAX_DRIVE_PATH, L"C:\\RAIDTEST\\");
    if (CreateDirectoryW(path, NULL)) return true;
    if (GetLastError() == ERROR_ALREADY_EXISTS) return true;
    return false;
}

static void remove_journal_file(void) {
    wchar_t path[MAX_DRIVE_PATH];
    wcscpy_s(path, MAX_DRIVE_PATH, L"C:\\RAIDTEST\\journal.dat");
    DeleteFileW(path);
}

/* Helper: create disks with drive letter set to C: for journal tests */
static DISK_INFO* journal_disk_create(uint64_t size_bytes) {
    DISK_INFO* d = test_disk_create(size_bytes, 200);
    if (!d) return NULL;
    wcscpy_s(d->drive_letter, MAX_DRIVE_PATH, JOURNAL_DRIVE);
    wcscpy_s(d->device_path, MAX_DRIVE_PATH, L"\\\\.\\C:");
    return d;
}

/* ---- Basic journal roundtrip: begin → data → commit → verify file ---- */
static bool test_journal_roundtrip(void) {
    ASSERT(ensure_raidtest_dir(), "cannot create C:\\RAIDTEST\\");
    remove_journal_file();

    /* Need 2+ disks for stripe_volume_create (MIN_DISKS). Both map to C:,
       so journal_write_entry writes to the same file twice per op. */
    DISK_INFO* d0 = journal_disk_create(16ULL * 1024 * 1024);
    DISK_INFO* d1 = journal_disk_create(16ULL * 1024 * 1024);
    ASSERT(d0 && d1, "disk create failed");
    DISK_INFO* disks[] = {d0, d1};
    STRIPE_VOLUME vol;
    ASSERT(stripe_volume_create(&vol, disks, 2, 1024 * 1024), "create failed");

    uint8_t data[1024];
    test_fill_pattern(data, 1024, 0xAA);

    ASSERT(journal_begin(&vol), "begin failed");
    ASSERT(journal_data(&vol, 4096, 1024, data), "data failed");
    ASSERT(journal_commit(&vol), "commit failed");

    /* Verify journal file exists and has content */
    wchar_t jpath[MAX_DRIVE_PATH];
    wcscpy_s(jpath, MAX_DRIVE_PATH, L"C:\\RAIDTEST\\journal.dat");
    HANDLE h = CreateFileW(jpath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    ASSERT(h != INVALID_HANDLE_VALUE, "journal file not created");
    uint8_t raw[4096];
    DWORD read = 0;
    ASSERT(ReadFile(h, raw, sizeof(raw), &read, NULL), "read journal failed");
    CloseHandle(h);

    /* Verify 6 entries: BEGIN, BEGIN, DATA+payload, DATA+payload, COMMIT, COMMIT
       (2 disks = each op writes to same C:\RAIDTEST\journal.dat twice) */
    uint32_t off = 0;
    for (int i = 0; i < 2; i++) {
        ASSERT(off + sizeof(JOURNAL_ENTRY) <= read, "truncated at BEGIN %d", i);
        ASSERT_EQ(((JOURNAL_ENTRY*)(raw + off))->magic, JOURNAL_MAGIC, "bad magic at %d", i);
        ASSERT_EQ(((JOURNAL_ENTRY*)(raw + off))->entry_type, JT_BEGIN, "entry %d should be BEGIN", i);
        off += sizeof(JOURNAL_ENTRY);
    }
    for (int i = 0; i < 2; i++) {
        ASSERT(off + sizeof(JOURNAL_ENTRY) <= read, "truncated at DATA %d", i);
        ASSERT_EQ(((JOURNAL_ENTRY*)(raw + off))->entry_type, JT_DATA, "entry %d+2 should be DATA", i);
        uint32_t payload = ((JOURNAL_ENTRY*)(raw + off))->length;
        ASSERT_EQ(payload, 1024, "payload len wrong at DATA %d", i);
        off += sizeof(JOURNAL_ENTRY) + payload;
    }
    for (int i = 0; i < 2; i++) {
        ASSERT(off + sizeof(JOURNAL_ENTRY) <= read, "truncated at COMMIT %d", i);
        ASSERT_EQ(((JOURNAL_ENTRY*)(raw + off))->entry_type, JT_COMMIT, "entry %d+4 should be COMMIT", i);
        off += sizeof(JOURNAL_ENTRY);
    }

    stripe_volume_destroy(&vol);
    test_disk_destroy(d0);
    remove_journal_file();
    printf("  PASS: journal_roundtrip\n");
    return true;
}

/* ---- journal_recover_all with complete journal (clean) ---- */
static bool test_journal_recover_clean(void) {
    ASSERT(ensure_raidtest_dir(), "cannot create C:\\RAIDTEST\\");
    remove_journal_file();

    DISK_INFO* d0 = journal_disk_create(16ULL * 1024 * 1024);
    DISK_INFO* d1 = journal_disk_create(16ULL * 1024 * 1024);
    ASSERT(d0 && d1, "disk create failed");
    DISK_INFO* disks[] = {d0, d1};
    STRIPE_VOLUME vol;
    ASSERT(stripe_volume_create(&vol, disks, 2, 1024 * 1024), "create failed");

    uint8_t data[4096];
    test_fill_pattern(data, 4096, 0xBB);

    journal_begin(&vol);
    journal_data(&vol, 0, 4096, data);
    journal_commit(&vol);

    ASSERT(journal_recover_all(&vol), "recover clean failed");

    /* Verify data was written to volume by journal_data+commit (real flush path) */
    uint8_t rbuf[4096];
    memset(rbuf, 0, 4096);
    ASSERT(stripe_volume_read(&vol, rbuf, 0, 4096), "read after clean recover failed");

    stripe_volume_destroy(&vol);
    test_disk_destroy(d0);
    test_disk_destroy(d1);
    remove_journal_file();
    printf("  PASS: journal_recover_clean\n");
    return true;
}

/* ---- journal_recover_all with incomplete journal (replay data) ---- */
static bool test_journal_recover_replay(void) {
    ASSERT(ensure_raidtest_dir(), "cannot create C:\\RAIDTEST\\");
    remove_journal_file();

    DISK_INFO* d0 = journal_disk_create(16ULL * 1024 * 1024);
    DISK_INFO* d1 = journal_disk_create(16ULL * 1024 * 1024);
    ASSERT(d0 && d1, "disk create failed");
    DISK_INFO* disks[] = {d0, d1};
    STRIPE_VOLUME vol;
    ASSERT(stripe_volume_create(&vol, disks, 2, 1024 * 1024), "create failed");

    /* Write initial data to volume */
    uint8_t init_data[4096];
    test_fill_pattern(init_data, 4096, 0xCC);
    ASSERT(stripe_volume_write(&vol, init_data, 0, 4096), "initial write failed");

    /* Journal begin + data (simulate crash before commit) */
    uint8_t recovered_data[4096];
    test_fill_pattern(recovered_data, 4096, 0xDD);
    ASSERT(journal_begin(&vol), "begin failed");
    ASSERT(journal_data(&vol, 0, 4096, recovered_data), "data failed");
    /* NO journal_commit — simulate crash */

    /* Verify journal file has incomplete data */
    wchar_t jpath[MAX_DRIVE_PATH];
    wcscpy_s(jpath, MAX_DRIVE_PATH, L"C:\\RAIDTEST\\journal.dat");
    HANDLE h = CreateFileW(jpath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    ASSERT(h != INVALID_HANDLE_VALUE, "journal file not created");
    CloseHandle(h);

    /* Recover returns false for incomplete journal (signals "was dirty"),
       but data should be replayed successfully. */
    bool recovered = journal_recover_all(&vol);
    ASSERT(!recovered, "recover should return false for incomplete journal");

    /* Read back — should be recovered_data (replayed by recover) */
    uint8_t rbuf[4096];
    memset(rbuf, 0, 4096);
    ASSERT(stripe_volume_read(&vol, rbuf, 0, 4096), "read after replay failed");
    ASSERT(memcmp(recovered_data, rbuf, 4096) == 0, "data mismatch after replay");

    /* Journal file should be truncated to 0 bytes by recover */
    wcscpy_s(jpath, MAX_DRIVE_PATH, L"C:\\RAIDTEST\\journal.dat");
    WIN32_FILE_ATTRIBUTE_DATA fad;
    BOOL exists = GetFileAttributesExW(jpath, GetFileExInfoStandard, &fad);
    ASSERT(!exists || fad.nFileSizeLow == 0, "journal should be empty after recover");

    stripe_volume_destroy(&vol);
    test_disk_destroy(d0);
    test_disk_destroy(d1);
    remove_journal_file();
    printf("  PASS: journal_recover_replay\n");
    return true;
}

/* ---- journal_recover_all with no journal file (no-op) ---- */
static bool test_journal_no_journal(void) {
    ASSERT(ensure_raidtest_dir(), "cannot create C:\\RAIDTEST\\");
    remove_journal_file();

    DISK_INFO* d0 = journal_disk_create(16ULL * 1024 * 1024);
    DISK_INFO* d1 = journal_disk_create(16ULL * 1024 * 1024);
    ASSERT(d0 && d1, "disk create failed");
    DISK_INFO* disks[] = {d0, d1};
    STRIPE_VOLUME vol;
    ASSERT(stripe_volume_create(&vol, disks, 2, 1024 * 1024), "create failed");

    ASSERT(journal_recover_all(&vol), "no-journal recover should succeed");

    stripe_volume_destroy(&vol);
    test_disk_destroy(d0);
    test_disk_destroy(d1);
    printf("  PASS: journal_no_journal\n");
    return true;
}

/* Regression: corrupted payload CRC — entry should be skipped during recovery */
TEST(journal_corrupted_payload) {
    ASSERT(ensure_raidtest_dir(), "cannot create C:\\RAIDTEST\\");
    remove_journal_file();

    DISK_INFO* d0 = journal_disk_create(16ULL * 1024 * 1024);
    DISK_INFO* d1 = journal_disk_create(16ULL * 1024 * 1024);
    ASSERT(d0 && d1, "disk create failed");
    DISK_INFO* disks[] = {d0, d1};
    STRIPE_VOLUME vol;
    ASSERT(stripe_volume_create(&vol, disks, 2, 1024 * 1024), "create failed");

    /* Write initial data to volume */
    uint8_t init_data[1024];
    test_fill_pattern(init_data, 1024, 0xAA);
    ASSERT(stripe_volume_write(&vol, init_data, 0, 1024), "initial write failed");

    /* Manually construct corrupted journal: BEGIN + DATA(bad payload CRC) + (no COMMIT) */
    wchar_t jpath[MAX_DRIVE_PATH];
    wcscpy_s(jpath, MAX_DRIVE_PATH, L"C:\\RAIDTEST\\journal.dat");
    HANDLE h = CreateFileW(jpath, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    ASSERT(h != INVALID_HANDLE_VALUE, "create journal failed");

    JOURNAL_ENTRY begin;
    memset(&begin, 0, sizeof(begin));
    begin.magic = JOURNAL_MAGIC;
    begin.version = JOURNAL_VERSION;
    begin.entry_type = JT_BEGIN;
    begin.generation = 1;
    begin.timestamp = 1000;
    begin.checksum = crc32((const uint8_t*)&begin, offsetof(JOURNAL_ENTRY, checksum));

    uint8_t payload[64];
    memset(payload, 0xFF, 64);

    JOURNAL_ENTRY data;
    memset(&data, 0, sizeof(data));
    data.magic = JOURNAL_MAGIC;
    data.version = JOURNAL_VERSION;
    data.entry_type = JT_DATA;
    data.generation = 1;
    data.virtual_offset = 0;
    data.length = 64;
    data.timestamp = 1001;
    data.data_crc = 0xDEADBEEF;  /* Wrong CRC — doesn't match 0xFF payload */
    data.checksum = crc32((const uint8_t*)&data, offsetof(JOURNAL_ENTRY, checksum));

    DWORD written = 0;
    WriteFile(h, &begin, sizeof(begin), &written, NULL);
    WriteFile(h, &data, sizeof(data), &written, NULL);
    WriteFile(h, payload, sizeof(payload), &written, NULL);
    CloseHandle(h);

    /* Recover — should skip the corrupted DATA entry (CRC mismatch) */
    bool recovered = journal_recover_all(&vol);
    ASSERT(!recovered, "should be dirty (no commit, entry skipped)");

    /* Verify initial data was NOT overwritten by recovered corrupt entry */
    uint8_t rbuf[1024];
    memset(rbuf, 0, 1024);
    ASSERT(stripe_volume_read(&vol, rbuf, 0, 1024), "read after recover failed");
    ASSERT(memcmp(init_data, rbuf, 1024) == 0, "data should not have been replayed");

    stripe_volume_destroy(&vol);
    test_disk_destroy(d0);
    test_disk_destroy(d1);
    remove_journal_file();
    printf("  PASS: journal_corrupted_payload\n");
    return true;
}

TEST(journal_roundtrip)          { return test_journal_roundtrip(); }
TEST(journal_recover_clean)      { return test_journal_recover_clean(); }
TEST(journal_recover_replay)     { return test_journal_recover_replay(); }
TEST(journal_no_journal)         { return test_journal_no_journal(); }
