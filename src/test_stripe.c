#include "test_common.h"
#include "stripe_engine.h"

static bool test_normalize_equal_speeds(void) {
    uint32_t speeds[] = {100, 100, 100};
    uint32_t ratios[3], total;
    ASSERT(stripe_volume_normalize_ratios(speeds, 3, ratios, &total), "normalize failed");
    ASSERT_EQ(total, 3, "total_ratio wrong");
    ASSERT_EQ(ratios[0], 1, "ratio[0] wrong");
    ASSERT_EQ(ratios[1], 1, "ratio[1] wrong");
    ASSERT_EQ(ratios[2], 1, "ratio[2] wrong");
    printf("  PASS: normalize_equal_speeds\n");
    return true;
}

static bool test_normalize_zero_speeds(void) {
    uint32_t speeds[] = {0, 0, 100, 0};
    uint32_t ratios[4], total;
    ASSERT(stripe_volume_normalize_ratios(speeds, 4, ratios, &total), "normalize failed");
    ASSERT_EQ(total, 4, "total_ratio wrong");
    ASSERT_EQ(ratios[0], 1, "ratio[0] wrong");
    printf("  PASS: normalize_zero_speeds\n");
    return true;
}

static bool test_normalize_asymmetric(void) {
    uint32_t speeds[] = {500, 1000, 2000};
    uint32_t ratios[3], total;
    ASSERT(stripe_volume_normalize_ratios(speeds, 3, ratios, &total), "normalize failed");
    ASSERT_EQ(ratios[0], 1, "ratio[0]");
    ASSERT_EQ(ratios[1], 2, "ratio[1]");
    ASSERT_EQ(ratios[2], 4, "ratio[2]");
    ASSERT_EQ(total, 7, "total");
    printf("  PASS: normalize_asymmetric\n");
    return true;
}

static bool test_stripe_create_2disks(void) {
    DISK_INFO* d0 = test_disk_create(64ULL * 1024 * 1024, 200);
    DISK_INFO* d1 = test_disk_create(64ULL * 1024 * 1024, 400);
    DISK_INFO* disks[] = {d0, d1};
    STRIPE_VOLUME vol;
    ASSERT(stripe_volume_create(&vol, disks, 2, 1024 * 1024), "create failed");
    ASSERT(vol.phase_count > 0, "no phases");
    ASSERT(vol.virtual_total_bytes > 0, "zero size");
    ASSERT(vol.virtual_total_bytes <= 128ULL * 1024 * 1024, "too large");

    /* Read/write test */
    ASSERT(test_verify_volume_read_write(&vol, 1024 * 1024), "rw failed");

    stripe_volume_destroy(&vol);
    test_disk_destroy(d0);
    test_disk_destroy(d1);
    printf("  PASS: stripe_create_2disks\n");
    return true;
}

static bool test_stripe_create_3disks_asymmetric(void) {
    DISK_INFO* d0 = test_disk_create(128ULL * 1024 * 1024, 200);
    DISK_INFO* d1 = test_disk_create(128ULL * 1024 * 1024, 400);
    DISK_INFO* d2 = test_disk_create(128ULL * 1024 * 1024, 800);
    DISK_INFO* disks[] = {d0, d1, d2};
    STRIPE_VOLUME vol;
    ASSERT(stripe_volume_create(&vol, disks, 3, 512 * 1024), "create failed");
    ASSERT(vol.virtual_total_bytes > 0, "zero size");

    ASSERT(test_verify_volume_read_write(&vol, 2 * 1024 * 1024), "rw failed");

    stripe_volume_destroy(&vol);
    test_disk_destroy(d0);
    test_disk_destroy(d1);
    test_disk_destroy(d2);
    printf("  PASS: stripe_create_3disks_asymmetric\n");
    return true;
}

static bool test_stripe_map_single_phase(void) {
    DISK_INFO* d0 = test_disk_create(16ULL * 1024 * 1024, 100);
    DISK_INFO* d1 = test_disk_create(16ULL * 1024 * 1024, 100);
    DISK_INFO* disks[] = {d0, d1};
    STRIPE_VOLUME vol;
    ASSERT(stripe_volume_create(&vol, disks, 2, 1024 * 1024), "create failed");

    IO_MAPPING_ENTRY entries[MAX_IO_ENTRIES];
    uint32_t count;
    ASSERT(stripe_volume_map_lba(&vol, 0, entries, &count, SECTOR_SIZE), "map_lba failed");
    ASSERT(count > 0, "no entries");
    ASSERT(entries[0].length_bytes >= SECTOR_SIZE, "entry too short");
    ASSERT(entries[0].disk_index < 2, "bad disk index");

    stripe_volume_destroy(&vol);
    test_disk_destroy(d0);
    test_disk_destroy(d1);
    printf("  PASS: stripe_map_single_phase\n");
    return true;
}

static bool test_stripe_expand(void) {
    DISK_INFO* d0 = test_disk_create(32ULL * 1024 * 1024, 100);
    DISK_INFO* d1 = test_disk_create(32ULL * 1024 * 1024, 100);
    DISK_INFO* disks[] = {d0, d1};
    STRIPE_VOLUME vol;
    ASSERT(stripe_volume_create(&vol, disks, 2, 1024 * 1024), "create failed");
    uint64_t orig_size = vol.virtual_total_bytes;

    DISK_INFO* d2 = test_disk_create(32ULL * 1024 * 1024, 200);
    DISK_INFO* d3 = test_disk_create(32ULL * 1024 * 1024, 100);
    DISK_INFO* new_disks[] = {d2, d3};
    ASSERT(stripe_volume_expand(&vol, new_disks, 2), "expand failed");
    ASSERT(vol.virtual_total_bytes > orig_size, "size not increased");
    ASSERT_EQ(vol.disk_count, 4, "disk_count");

    /* Write to expanded area */
    ASSERT(test_verify_volume_read_write(&vol, 2 * 1024 * 1024), "rw on expanded vol failed");

    stripe_volume_destroy(&vol);
    test_disk_destroy(d0);
    test_disk_destroy(d1);
    test_disk_destroy(d2);
    test_disk_destroy(d3);
    printf("  PASS: stripe_expand\n");
    return true;
}

TEST(stripe_normalize_equal_speeds)    { return test_normalize_equal_speeds(); }
TEST(stripe_normalize_zero_speeds)     { return test_normalize_zero_speeds(); }
TEST(stripe_normalize_asymmetric)      { return test_normalize_asymmetric(); }
TEST(stripe_create_2disks)             { return test_stripe_create_2disks(); }
TEST(stripe_create_3disks_asymmetric)  { return test_stripe_create_3disks_asymmetric(); }
TEST(stripe_map_single_phase)          { return test_stripe_map_single_phase(); }
TEST(stripe_expand)                    { return test_stripe_expand(); }
