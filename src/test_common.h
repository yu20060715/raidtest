#pragma once
#include "common.h"
#include "stripe_engine.h"
#include "mirror_engine.h"

#define TEST_MAX 64

typedef bool (*test_fn)(void);
extern test_fn g_tests[TEST_MAX];
extern uint32_t g_test_count;
extern uint32_t g_test_passed;
extern uint32_t g_test_failed;

void test_register(test_fn fn);
int test_run_all(void);

#define TEST(name) \
    static bool test_impl_##name(void); \
    __attribute__((constructor)) static void test_reg_##name(void) { test_register(test_impl_##name); } \
    static bool test_impl_##name(void)

#define ASSERT(cond, ...) do { \
    if (!(cond)) { \
        printf("  FAIL (%s:%d): ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
        return false; \
    } \
} while(0)

#define ASSERT_EQ(a, b, ...) do { \
    uint64_t _a = (uint64_t)(a); \
    uint64_t _b = (uint64_t)(b); \
    if (_a != _b) { \
        printf("  FAIL (%s:%d): expected %llu got %llu. ", __FILE__, __LINE__, (unsigned long long)_b, (unsigned long long)_a); \
        printf(__VA_ARGS__); \
        printf("\n"); \
        return false; \
    } \
} while(0)

#define ASSERT_MEM(buf, val, len, ...) do { \
    for (uint32_t _i = 0; _i < (len); _i++) { \
        if (((uint8_t*)(buf))[_i] != (uint8_t)(val)) { \
            printf("  FAIL (%s:%d): byte %u mismatch. ", __FILE__, __LINE__, _i); \
            printf(__VA_ARGS__); \
            printf("\n"); \
            return false; \
        } \
    } \
} while(0)

/* Factory: create a RAM-backed DISK_INFO for testing */
DISK_INFO* test_disk_create(uint64_t size_bytes, uint32_t write_mbs);
void test_disk_destroy(DISK_INFO* disk);
void test_disk_reset(DISK_INFO* disk);

/* Convenience: fill buffer with predictable pattern */
void test_fill_pattern(uint8_t* buf, uint64_t size, uint64_t offset);
bool test_check_pattern(const uint8_t* buf, uint64_t size, uint64_t offset);

/* Run all stripe_volume engine phases on a volume */
bool test_verify_volume_read_write(STRIPE_VOLUME* vol, uint64_t test_size);
