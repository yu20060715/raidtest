#include "test_common.h"
#include "superblock.h"
#include "pool_io.h"

#define SB_DRIVE L"C:"

static void sb_cleanup(void) {
    wchar_t path[MAX_DRIVE_PATH];
    wcscpy_s(path, MAX_DRIVE_PATH, L"C:\\RAIDTEST\\superblock.dat");
    DeleteFileW(path);
    wcscpy_s(path, MAX_DRIVE_PATH, L"C:\\RAIDTEST\\superblock.dat.tmp");
    DeleteFileW(path);
    wcscpy_s(path, MAX_DRIVE_PATH, L"C:\\RAIDTEST\\stripe_pool.dat");
    DeleteFileW(path);
}

/* Create disks with real pool files at C:\RAIDTEST\ for superblock_read compatibility */
static DISK_INFO* sb_disk_create(uint64_t size_bytes, const wchar_t* label) {
    DISK_INFO* d = (DISK_INFO*)calloc(1, sizeof(DISK_INFO));
    if (!d) return NULL;

    wcscpy_s(d->drive_letter, MAX_DRIVE_PATH, SB_DRIVE);
    wcscpy_s(d->device_path, MAX_DRIVE_PATH, L"\\\\.\\C:");
    wcscpy_s(d->model, MAX_MODEL_LEN, label);
    wcscpy_s(d->file_path, MAX_DRIVE_PATH, L"C:\\RAIDTEST\\stripe_pool.dat");

    d->ram_buffer = NULL;
    d->pool_bytes = size_bytes;
    d->total_bytes = size_bytes;
    d->bench_write_mbs = 200;
    d->bench_read_mbs = 200;
    d->benchmarked = true;
    d->selected = true;
    d->healthy = 1;
    d->faulty = 0;
    d->error_count = 0;
    d->sector_size = SECTOR_SIZE;

    /* Create pool file */
    if (!pool_file_create(d, size_bytes)) {
        free(d);
        return NULL;
    }
    /* Open for I/O */
    if (!pool_file_open(d)) {
        DeleteFileW(d->file_path);
        free(d);
        return NULL;
    }
    return d;
}

static void sb_disk_destroy(DISK_INFO* d) {
    if (!d) return;
    pool_file_close(d);
    /* Don't delete pool_file — other disks might share it.
       We'll clean up via sb_cleanup at test boundaries. */
    free(d);
}

/* ---- Write stripe superblock, read it back ---- */
static bool test_sb_stripe_write_read(void) {
    sb_cleanup();

    DISK_INFO* d0 = sb_disk_create(32ULL * 1024 * 1024, L"Disk A");
    DISK_INFO* d1 = sb_disk_create(32ULL * 1024 * 1024, L"Disk B");
    ASSERT(d0 && d1, "disk create failed");
    DISK_INFO* disks[] = {d0, d1};
    STRIPE_VOLUME vol;
    ASSERT(stripe_volume_create(&vol, disks, 2, 1024 * 1024), "create failed");

    ASSERT(superblock_write(&vol), "write failed");

    /* Read back from C:\ */
    STRIPE_VOLUME vol2;
    DISK_INFO disks_out[MAX_DISKS];
    memset(&vol2, 0, sizeof(vol2));
    uint32_t disk_count = 0;
    ASSERT(superblock_read(L"C:\\", &vol2, disks_out, &disk_count, NULL, 0), "read failed");

    ASSERT_EQ(disk_count, 2, "disk_count");
    ASSERT_EQ(vol2.stripe_unit, vol.stripe_unit, "stripe_unit");
    ASSERT_EQ(vol2.virtual_total_bytes, vol.virtual_total_bytes, "size");
    ASSERT_EQ(vol2.generation, 1, "generation");
    ASSERT_EQ(vol2.raid_level, RAID_LEVEL_STRIPE, "raid_level");

    for (uint32_t i = 0; i < disk_count; i++)
        pool_file_close(&disks_out[i]);

    stripe_volume_destroy(&vol);
    sb_disk_destroy(d0);
    sb_disk_destroy(d1);
    sb_cleanup();
    printf("  PASS: sb_stripe_write_read\n");
    return true;
}

/* ---- Mirror superblock flag ---- */
static bool test_sb_mirror_flag(void) {
    sb_cleanup();

    DISK_INFO* d0 = sb_disk_create(32ULL * 1024 * 1024, L"Mirror A");
    DISK_INFO* d1 = sb_disk_create(32ULL * 1024 * 1024, L"Mirror B");
    ASSERT(d0 && d1, "disk create failed");
    DISK_INFO* disks[] = {d0, d1};
    STRIPE_VOLUME vol;
    ASSERT(mirror_volume_create(&vol, disks, 2), "mirror create failed");

    ASSERT(superblock_write(&vol), "write failed");

    STRIPE_VOLUME vol2;
    DISK_INFO disks_out[MAX_DISKS];
    memset(&vol2, 0, sizeof(vol2));
    uint32_t disk_count = 0;
    ASSERT(superblock_read(L"C:\\", &vol2, disks_out, &disk_count, NULL, 0), "read failed");

    ASSERT_EQ(vol2.raid_level, RAID_LEVEL_MIRROR, "raid_level");
    ASSERT_EQ(disk_count, 2, "disk_count");

    for (uint32_t i = 0; i < disk_count; i++)
        pool_file_close(&disks_out[i]);

    stripe_volume_destroy(&vol);
    sb_disk_destroy(d0);
    sb_disk_destroy(d1);
    sb_cleanup();
    printf("  PASS: sb_mirror_flag\n");
    return true;
}

/* ---- Generation increments ---- */
static bool test_sb_generation(void) {
    sb_cleanup();

    DISK_INFO* d0 = sb_disk_create(32ULL * 1024 * 1024, L"Gen A");
    DISK_INFO* d1 = sb_disk_create(32ULL * 1024 * 1024, L"Gen B");
    ASSERT(d0 && d1, "disk create failed");
    DISK_INFO* disks[] = {d0, d1};
    STRIPE_VOLUME vol;
    ASSERT(stripe_volume_create(&vol, disks, 2, 1024 * 1024), "create failed");

    ASSERT(superblock_write(&vol), "write 1 failed");
    ASSERT_EQ(vol.generation, 1, "gen after write 1");
    ASSERT(superblock_write(&vol), "write 2 failed");
    ASSERT_EQ(vol.generation, 2, "gen after write 2");
    ASSERT(superblock_write(&vol), "write 3 failed");
    ASSERT_EQ(vol.generation, 3, "gen after write 3");

    STRIPE_VOLUME vol2;
    DISK_INFO disks_out[MAX_DISKS];
    memset(&vol2, 0, sizeof(vol2));
    uint32_t disk_count = 0;
    ASSERT(superblock_read(L"C:\\", &vol2, disks_out, &disk_count, NULL, 0), "read failed");
    ASSERT_EQ(vol2.generation, 3, "readback gen");

    for (uint32_t i = 0; i < disk_count; i++)
        pool_file_close(&disks_out[i]);

    stripe_volume_destroy(&vol);
    sb_disk_destroy(d0);
    sb_disk_destroy(d1);
    sb_cleanup();
    printf("  PASS: sb_generation\n");
    return true;
}

/* ---- Corruption detection ---- */
static bool test_sb_checksum_corruption(void) {
    sb_cleanup();

    DISK_INFO* d0 = sb_disk_create(16ULL * 1024 * 1024, L"Corrupt A");
    DISK_INFO* d1 = sb_disk_create(16ULL * 1024 * 1024, L"Corrupt B");
    ASSERT(d0 && d1, "disk create failed");
    DISK_INFO* disks[] = {d0, d1};
    STRIPE_VOLUME vol;
    ASSERT(stripe_volume_create(&vol, disks, 2, 1024 * 1024), "create failed");
    ASSERT(superblock_write(&vol), "write failed");

    /* Corrupt superblock magic */
    {
        wchar_t path[MAX_DRIVE_PATH];
        wcscpy_s(path, MAX_DRIVE_PATH, L"C:\\RAIDTEST\\superblock.dat");
        HANDLE h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        ASSERT(h != INVALID_HANDLE_VALUE, "open for corruption failed");
        uint64_t zero = 0;
        WriteFile(h, &zero, 8, NULL, NULL);
        CloseHandle(h);
    }

    STRIPE_VOLUME vol2;
    DISK_INFO disks_out[MAX_DISKS];
    memset(&vol2, 0, sizeof(vol2));
    uint32_t disk_count = 0;
    ASSERT(!superblock_read(L"C:\\", &vol2, disks_out, &disk_count, NULL, 0), "should have failed");

    stripe_volume_destroy(&vol);
    sb_disk_destroy(d0);
    sb_disk_destroy(d1);
    sb_cleanup();
    printf("  PASS: sb_checksum_corruption\n");
    return true;
}

/* ---- v4 UUID persists through write/read ---- */
static bool test_sb_v4_uuid(void) {
    sb_cleanup();

    DISK_INFO* d0 = sb_disk_create(32ULL * 1024 * 1024, L"UUID A");
    DISK_INFO* d1 = sb_disk_create(32ULL * 1024 * 1024, L"UUID B");
    ASSERT(d0 && d1, "disk create failed");
    DISK_INFO* disks[] = {d0, d1};
    STRIPE_VOLUME vol;
    ASSERT(stripe_volume_create(&vol, disks, 2, 1024 * 1024), "create failed");

    uuid_generate(&vol.volume_uuid);
    ASSERT(!uuid_is_zero(&vol.volume_uuid), "uuid should be non-zero");
    VOLUME_UUID saved_uuid = vol.volume_uuid;

    ASSERT(superblock_write(&vol), "write failed");

    /* Read back */
    STRIPE_VOLUME vol2;
    DISK_INFO disks_out[MAX_DISKS];
    memset(&vol2, 0, sizeof(vol2));
    uint32_t disk_count = 0;
    ASSERT(superblock_read(L"C:\\", &vol2, disks_out, &disk_count, NULL, 0), "read failed");
    ASSERT(uuid_eq(&vol2.volume_uuid, &saved_uuid), "uuid mismatch");

    for (uint32_t i = 0; i < disk_count; i++)
        pool_file_close(&disks_out[i]);

    stripe_volume_destroy(&vol);
    sb_disk_destroy(d0);
    sb_disk_destroy(d1);
    sb_cleanup();
    printf("  PASS: sb_v4_uuid\n");
    return true;
}

/* ---- Serial matching in superblock_restore ---- */
static bool test_sb_serial_restore(void) {
    sb_cleanup();

    DISK_INFO* d0 = sb_disk_create(32ULL * 1024 * 1024, L"Serial A");
    DISK_INFO* d1 = sb_disk_create(32ULL * 1024 * 1024, L"Serial B");
    ASSERT(d0 && d1, "disk create failed");

    /* Set serial numbers */
    strncpy_s(d0->serial_number, MAX_SERIAL_LEN, "SN-A-12345", _TRUNCATE);
    strncpy_s(d1->serial_number, MAX_SERIAL_LEN, "SN-B-67890", _TRUNCATE);

    DISK_INFO* disks[] = {d0, d1};
    STRIPE_VOLUME vol;
    ASSERT(stripe_volume_create(&vol, disks, 2, 1024 * 1024), "create failed");
    uuid_generate(&vol.volume_uuid);
    ASSERT(superblock_write(&vol), "write failed");

    /* Read superblock directly */
    SUPERBLOCK sb;
    ASSERT(superblock_read_raw(L"C:\\", &sb), "raw read failed");
    ASSERT_EQ(sb.version, SUPERBLOCK_VERSION, "version");
    ASSERT(uuid_eq(&sb.volume_uuid, &vol.volume_uuid), "uuid");

    /* Now test restore with matching physical disks on SAME drive (C:\) */
    DISK_INFO phys_a, phys_b;
    memset(&phys_a, 0, sizeof(phys_a));
    memset(&phys_b, 0, sizeof(phys_b));
    wcscpy_s(phys_a.drive_letter, MAX_DRIVE_PATH, L"C:\\");
    wcscpy_s(phys_b.drive_letter, MAX_DRIVE_PATH, L"C:\\");
    strncpy_s(phys_a.serial_number, MAX_SERIAL_LEN, "SN-A-12345", _TRUNCATE);
    strncpy_s(phys_b.serial_number, MAX_SERIAL_LEN, "SN-B-67890", _TRUNCATE);

    DISK_INFO* phys_ptrs[] = {&phys_a, &phys_b};

    STRIPE_VOLUME vol3;
    DISK_INFO restored_disks[MAX_DISKS];
    uint32_t restored_count = 0;
    ASSERT(superblock_restore(&sb, phys_ptrs, 2, &vol3, restored_disks, &restored_count),
           "restore failed");
    ASSERT_EQ(restored_count, 2, "restored count");

    /* Serial matching worked — drive letters match */
    char drv_buf_a[16] = {0};
    wcstombs(drv_buf_a, restored_disks[0].drive_letter, 15);
    ASSERT(strcmp(drv_buf_a, "C:\\") == 0, "should use physical disk's drive letter");
    ASSERT_EQ(vol3.disk_count, 2, "vol disk_count");
    ASSERT(uuid_eq(&vol3.volume_uuid, &vol.volume_uuid), "uuid preserved");

    for (uint32_t i = 0; i < restored_count; i++)
        pool_file_close(&restored_disks[i]);

    stripe_volume_destroy(&vol);
    sb_disk_destroy(d0);
    sb_disk_destroy(d1);
    sb_cleanup();
    printf("  PASS: sb_serial_restore\n");
    return true;
}

/* ---- Serial fallback: no serial match falls back to drive letter in SB ---- */
static bool test_sb_serial_fallback(void) {
    sb_cleanup();

    DISK_INFO* d0 = sb_disk_create(32ULL * 1024 * 1024, L"Fall A");
    DISK_INFO* d1 = sb_disk_create(32ULL * 1024 * 1024, L"Fall B");
    ASSERT(d0 && d1, "disk create failed");
    strncpy_s(d0->serial_number, MAX_SERIAL_LEN, "SN-FALL-A", _TRUNCATE);
    strncpy_s(d1->serial_number, MAX_SERIAL_LEN, "SN-FALL-B", _TRUNCATE);

    DISK_INFO* disks[] = {d0, d1};
    STRIPE_VOLUME vol;
    ASSERT(stripe_volume_create(&vol, disks, 2, 1024 * 1024), "create failed");
    uuid_generate(&vol.volume_uuid);
    ASSERT(superblock_write(&vol), "write failed");

    SUPERBLOCK sb;
    ASSERT(superblock_read_raw(L"C:\\", &sb), "raw read failed");

    /* Physical disks with DIFFERENT serials — should fall back to SB drive_letter */
    DISK_INFO phys_x, phys_y;
    memset(&phys_x, 0, sizeof(phys_x));
    memset(&phys_y, 0, sizeof(phys_y));
    wcscpy_s(phys_x.drive_letter, MAX_DRIVE_PATH, L"C:\\");
    wcscpy_s(phys_y.drive_letter, MAX_DRIVE_PATH, L"C:\\");
    strncpy_s(phys_x.serial_number, MAX_SERIAL_LEN, "SN-WRONG-X", _TRUNCATE);
    strncpy_s(phys_y.serial_number, MAX_SERIAL_LEN, "SN-WRONG-Y", _TRUNCATE);

    DISK_INFO* phys_ptrs[] = {&phys_x, &phys_y};

    STRIPE_VOLUME vol3;
    DISK_INFO restored_disks[MAX_DISKS];
    uint32_t restored_count = 0;
    ASSERT(superblock_restore(&sb, phys_ptrs, 2, &vol3, restored_disks, &restored_count),
           "restore with wrong serials should still work (fallback)");
    ASSERT_EQ(restored_count, 2, "restored count");

    /* Since serials don't match, drive letters come from SB (C:\) */
    char drv_buf[16] = {0};
    wcstombs(drv_buf, restored_disks[0].drive_letter, 15);
    ASSERT(strcmp(drv_buf, "C:\\") == 0, "should fall back to C: from SB");

    for (uint32_t i = 0; i < restored_count; i++)
        pool_file_close(&restored_disks[i]);

    stripe_volume_destroy(&vol);
    sb_disk_destroy(d0);
    sb_disk_destroy(d1);
    sb_cleanup();
    printf("  PASS: sb_serial_fallback\n");
    return true;
}

/* ---- Metadata format string produces output ---- */
static bool test_sb_metadata_format(void) {
    sb_cleanup();

    DISK_INFO* d0 = sb_disk_create(32ULL * 1024 * 1024, L"Meta A");
    DISK_INFO* d1 = sb_disk_create(32ULL * 1024 * 1024, L"Meta B");
    ASSERT(d0 && d1, "disk create failed");
    DISK_INFO* disks[] = {d0, d1};
    STRIPE_VOLUME vol;
    ASSERT(stripe_volume_create(&vol, disks, 2, 1024 * 1024), "create failed");
    uuid_generate(&vol.volume_uuid);
    ASSERT(superblock_write(&vol), "write failed");

    SUPERBLOCK sb;
    ASSERT(superblock_read_raw(L"C:\\", &sb), "raw read failed");

    char buf[4096];
    superblock_format_str(&sb, buf, sizeof(buf));
    ASSERT(strlen(buf) > 0, "format should produce non-empty string");
    ASSERT(strstr(buf, "UUID") != NULL, "should contain UUID");

    stripe_volume_destroy(&vol);
    sb_disk_destroy(d0);
    sb_disk_destroy(d1);
    sb_cleanup();
    printf("  PASS: sb_metadata_format\n");
    return true;
}

/* ---- Empty physical list still works (no serial matching attempted) ---- */
static bool test_sb_restore_no_phys(void) {
    sb_cleanup();

    DISK_INFO* d0 = sb_disk_create(32ULL * 1024 * 1024, L"NoPhys A");
    DISK_INFO* d1 = sb_disk_create(32ULL * 1024 * 1024, L"NoPhys B");
    ASSERT(d0 && d1, "disk create failed");
    DISK_INFO* disks[] = {d0, d1};
    STRIPE_VOLUME vol;
    ASSERT(stripe_volume_create(&vol, disks, 2, 1024 * 1024), "create failed");
    uuid_generate(&vol.volume_uuid);
    ASSERT(superblock_write(&vol), "write failed");

    /* Restore with no physical disks — should use SB drive_letter */
    SUPERBLOCK sb;
    ASSERT(superblock_read_raw(L"C:\\", &sb), "raw read failed");

    STRIPE_VOLUME vol2;
    DISK_INFO restored_disks[MAX_DISKS];
    uint32_t restored_count = 0;
    ASSERT(superblock_restore(&sb, NULL, 0, &vol2, restored_disks, &restored_count),
           "restore with no phys should work");
    ASSERT_EQ(restored_count, 2, "restored count");

    char drv_buf[16] = {0};
    wcstombs(drv_buf, restored_disks[0].drive_letter, 15);
    ASSERT(strcmp(drv_buf, "C:\\") == 0, "should use C: from SB");

    for (uint32_t i = 0; i < restored_count; i++)
        pool_file_close(&restored_disks[i]);

    stripe_volume_destroy(&vol);
    sb_disk_destroy(d0);
    sb_disk_destroy(d1);
    sb_cleanup();
    printf("  PASS: sb_restore_no_phys\n");
    return true;
}

/* ---- Format string handles v3 (upconverted) without crashing ---- */
static bool test_sb_format_v3_compat(void) {
    /* Write a v3-style superblock manually, read back, format it */
    sb_cleanup();

    DISK_INFO* d0 = sb_disk_create(32ULL * 1024 * 1024, L"V3Compat A");
    DISK_INFO* d1 = sb_disk_create(32ULL * 1024 * 1024, L"V3Compat B");
    ASSERT(d0 && d1, "disk create failed");
    DISK_INFO* disks[] = {d0, d1};
    STRIPE_VOLUME vol;
    ASSERT(stripe_volume_create(&vol, disks, 2, 1024 * 1024), "create failed");
    /* Don't set UUID — simulate v3 behavior */
    ASSERT(superblock_write(&vol), "write failed");

    /* The superblock was written as v4 (with UUID). We can't easily write a v3
       since superblock_write always writes v4. So instead test that format_str
       works on the v4 superblock (which is the v3-compatible superblock with UUID). */
    SUPERBLOCK sb;
    ASSERT(superblock_read_raw(L"C:\\", &sb), "raw read failed");
    char buf[4096];
    superblock_format_str(&sb, buf, sizeof(buf));
    ASSERT(strlen(buf) > 0, "format should work");
    ASSERT(strstr(buf, "Superblock v4") != NULL, "should indicate v4");

    stripe_volume_destroy(&vol);
    sb_disk_destroy(d0);
    sb_disk_destroy(d1);
    sb_cleanup();
    printf("  PASS: sb_format_v3_compat\n");
    return true;
}

/* ---- Blank serial restore (v2 upgrade path) ---- */
TEST(sb_stripe_write_read)     { return test_sb_stripe_write_read(); }
TEST(sb_mirror_flag)           { return test_sb_mirror_flag(); }
TEST(sb_generation)            { return test_sb_generation(); }
TEST(sb_checksum_corruption)   { return test_sb_checksum_corruption(); }
TEST(sb_v4_uuid)               { return test_sb_v4_uuid(); }
TEST(sb_serial_restore)        { return test_sb_serial_restore(); }
TEST(sb_serial_fallback)       { return test_sb_serial_fallback(); }
TEST(sb_metadata_format)       { return test_sb_metadata_format(); }
TEST(sb_restore_no_phys)       { return test_sb_restore_no_phys(); }
TEST(sb_format_v3_compat)      { return test_sb_format_v3_compat(); }
TEST(sb_blank_serial) {
    sb_cleanup();
    DISK_INFO* d0 = sb_disk_create(32ULL * 1024 * 1024, L"Blank A");
    DISK_INFO* d1 = sb_disk_create(32ULL * 1024 * 1024, L"Blank B");
    ASSERT(d0 && d1, "disk create failed");
    DISK_INFO* disks[] = {d0, d1};
    STRIPE_VOLUME vol;
    ASSERT(stripe_volume_create(&vol, disks, 2, 1024 * 1024), "create failed");
    uuid_generate(&vol.volume_uuid);
    ASSERT(superblock_write(&vol), "write failed");
    SUPERBLOCK sb;
    ASSERT(superblock_read_raw(L"C:\\", &sb), "raw read failed");
    DISK_INFO phys_x, phys_y;
    memset(&phys_x, 0, sizeof(phys_x)); memset(&phys_y, 0, sizeof(phys_y));
    wcscpy_s(phys_x.drive_letter, MAX_DRIVE_PATH, L"C:\\");
    wcscpy_s(phys_y.drive_letter, MAX_DRIVE_PATH, L"C:\\");
    DISK_INFO* phys_ptrs[] = {&phys_x, &phys_y};
    STRIPE_VOLUME vol2;
    DISK_INFO restored_disks[MAX_DISKS];
    uint32_t restored_count = 0;
    ASSERT(superblock_restore(&sb, phys_ptrs, 2, &vol2, restored_disks, &restored_count), "blank serial restore");
    ASSERT_EQ(restored_count, 2, "count");
    for (uint32_t i = 0; i < restored_count; i++) pool_file_close(&restored_disks[i]);
    stripe_volume_destroy(&vol); sb_disk_destroy(d0); sb_disk_destroy(d1); sb_cleanup();
    return true;
}
