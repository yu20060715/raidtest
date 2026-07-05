#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "common.h"
#include "stripe_engine.h"
#include "test_common.h"

#define DISK_SIZE (64ULL * 1024 * 1024)
#define TEST_SIZE_MB 32
#define BLOCK_KB 64
#define RANDOM_OPS 200

static double g_freq_ghz = 0;

static bool bench_sequential(STRIPE_VOLUME* vol, uint32_t size_mb, uint32_t block_kb,
                             double* write_mbs, double* read_mbs, double* write_lat, double* read_lat) {
    uint64_t size_bytes = (uint64_t)size_mb * 1024 * 1024;
    uint32_t block_size = block_kb * 1024;
    uint32_t blocks = (uint32_t)(size_bytes / block_size);
    if (size_bytes > vol->virtual_total_bytes) size_bytes = vol->virtual_total_bytes;
    blocks = (uint32_t)(size_bytes / block_size);

    uint8_t* buf = (uint8_t*)VirtualAlloc(NULL, block_size, MEM_COMMIT, PAGE_READWRITE);
    if (!buf) return false;
    memset(buf, 0xFF, block_size);

    LARGE_INTEGER freq, t1, t2;
    QueryPerformanceFrequency(&freq);

    /* Sequential Write */
    QueryPerformanceCounter(&t1);
    uint64_t written = 0;
    double max_wlat = 0;
    for (uint32_t i = 0; i < blocks; i++) {
        LARGE_INTEGER wt1, wt2;
        QueryPerformanceCounter(&wt1);
        if (!stripe_volume_write(vol, buf, (uint64_t)i * block_size, block_size)) {
            VirtualFree(buf, 0, MEM_RELEASE); return false;
        }
        QueryPerformanceCounter(&wt2);
        double lat = (double)(wt2.QuadPart - wt1.QuadPart) / freq.QuadPart;
        if (lat > max_wlat) max_wlat = lat;
        written += block_size;
    }
    QueryPerformanceCounter(&t2);
    double welapsed = (double)(t2.QuadPart - t1.QuadPart) / freq.QuadPart;
    *write_mbs = welapsed > 0 ? (written / (1024.0 * 1024.0)) / welapsed : 0;
    *write_lat = (max_wlat * 1000.0); /* ms */

    /* Sequential Read */
    QueryPerformanceCounter(&t1);
    uint64_t read_bytes = 0;
    double max_rlat = 0;
    for (uint32_t i = 0; i < blocks; i++) {
        LARGE_INTEGER rt1, rt2;
        QueryPerformanceCounter(&rt1);
        if (!stripe_volume_read(vol, buf, (uint64_t)i * block_size, block_size)) {
            VirtualFree(buf, 0, MEM_RELEASE); return false;
        }
        QueryPerformanceCounter(&rt2);
        double lat = (double)(rt2.QuadPart - rt1.QuadPart) / freq.QuadPart;
        if (lat > max_rlat) max_rlat = lat;
        read_bytes += block_size;
    }
    QueryPerformanceCounter(&t2);
    double relapsed = (double)(t2.QuadPart - t1.QuadPart) / freq.QuadPart;
    *read_mbs = relapsed > 0 ? (read_bytes / (1024.0 * 1024.0)) / relapsed : 0;
    *read_lat = (max_rlat * 1000.0);

    VirtualFree(buf, 0, MEM_RELEASE);
    return true;
}

static bool bench_random(STRIPE_VOLUME* vol, uint32_t ops, uint32_t block_size,
                         double* rand_read_mbs, double* rand_write_mbs,
                         double* rand_read_lat, double* rand_write_lat) {
    uint8_t* buf = (uint8_t*)VirtualAlloc(NULL, block_size, MEM_COMMIT, PAGE_READWRITE);
    if (!buf) return false;
    memset(buf, 0xFF, block_size);

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    uint64_t vol_size = vol->virtual_total_bytes;
    double total_wlat = 0, total_rlat = 0;
    uint32_t wcount = 0, rcount = 0;

    /* Random Write */
    LARGE_INTEGER wt1, wt2;
    QueryPerformanceCounter(&wt1);
    for (uint32_t i = 0; i < ops; i++) {
        uint64_t off = ((uint64_t)rand() * (uint64_t)rand()) % (vol_size - block_size);
        LARGE_INTEGER l1, l2;
        QueryPerformanceCounter(&l1);
        if (!stripe_volume_write(vol, buf, off, block_size)) {
            VirtualFree(buf, 0, MEM_RELEASE); return false;
        }
        QueryPerformanceCounter(&l2);
        total_wlat += (double)(l2.QuadPart - l1.QuadPart) / freq.QuadPart;
        wcount++;
    }
    QueryPerformanceCounter(&wt2);
    double welapsed = (double)(wt2.QuadPart - wt1.QuadPart) / freq.QuadPart;
    *rand_write_mbs = welapsed > 0 ? ((double)ops * block_size / (1024.0 * 1024.0)) / welapsed : 0;
    *rand_write_lat = wcount > 0 ? (total_wlat / wcount * 1000.0) : 0;

    /* Random Read */
    LARGE_INTEGER rt1, rt2;
    QueryPerformanceCounter(&rt1);
    for (uint32_t i = 0; i < ops; i++) {
        uint64_t off = ((uint64_t)rand() * (uint64_t)rand()) % (vol_size - block_size);
        LARGE_INTEGER l1, l2;
        QueryPerformanceCounter(&l1);
        if (!stripe_volume_read(vol, buf, off, block_size)) {
            VirtualFree(buf, 0, MEM_RELEASE); return false;
        }
        QueryPerformanceCounter(&l2);
        total_rlat += (double)(l2.QuadPart - l1.QuadPart) / freq.QuadPart;
        rcount++;
    }
    QueryPerformanceCounter(&rt2);
    double relapsed = (double)(rt2.QuadPart - rt1.QuadPart) / freq.QuadPart;
    *rand_read_mbs = relapsed > 0 ? ((double)ops * block_size / (1024.0 * 1024.0)) / relapsed : 0;
    *rand_read_lat = rcount > 0 ? (total_rlat / rcount * 1000.0) : 0;

    VirtualFree(buf, 0, MEM_RELEASE);
    return true;
}

int main(int argc, char* argv[]) {
    log_init();
    log_set_level(LOG_LEVEL_WARN);

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    g_freq_ghz = freq.QuadPart / 1.0e9;

    /* Determine CSV output path */
    const char* csv_path = "benchmark_results.csv";
    if (argc > 1) csv_path = argv[1];

    printf("=== RAIDTEST CLI Benchmark ===\n");
    printf("Creating test volume (%llu MB)...\n", (unsigned long long)(DISK_SIZE / 1024 / 1024));

    DISK_INFO* d1 = test_disk_create(DISK_SIZE, 500);
    DISK_INFO* d2 = test_disk_create(DISK_SIZE, 500);
    if (!d1 || !d2) {
        printf("FAIL: disk create\n");
        log_cleanup(); return 1;
    }

    DISK_INFO* disks[] = { d1, d2 };
    STRIPE_VOLUME vol;
    memset(&vol, 0, sizeof(vol));
    if (!stripe_volume_create(&vol, disks, 2, 65536)) {
        printf("FAIL: volume create\n");
        test_disk_destroy(d1); test_disk_destroy(d2);
        log_cleanup(); return 1;
    }

    double seq_w_mbs, seq_r_mbs, seq_w_lat, seq_r_lat;
    double rand_w_mbs, rand_r_mbs, rand_w_lat, rand_r_lat;

    printf("Running Sequential Benchmark...\n");
    if (!bench_sequential(&vol, TEST_SIZE_MB, BLOCK_KB,
                          &seq_w_mbs, &seq_r_mbs, &seq_w_lat, &seq_r_lat)) {
        printf("FAIL: sequential bench\n");
        stripe_volume_destroy(&vol);
        test_disk_destroy(d1); test_disk_destroy(d2);
        log_cleanup(); return 1;
    }

    printf("  Seq Write: %.1f MB/s  (latency: %.2f ms)\n", seq_w_mbs, seq_w_lat);
    printf("  Seq Read:  %.1f MB/s  (latency: %.2f ms)\n", seq_r_mbs, seq_r_lat);

    printf("Running Random Benchmark...\n");
    if (!bench_random(&vol, RANDOM_OPS, 4096,
                      &rand_r_mbs, &rand_w_mbs, &rand_r_lat, &rand_w_lat)) {
        printf("FAIL: random bench\n");
        stripe_volume_destroy(&vol);
        test_disk_destroy(d1); test_disk_destroy(d2);
        log_cleanup(); return 1;
    }

    printf("  Rand Write: %.1f MB/s  (latency: %.2f ms)\n", rand_w_mbs, rand_w_lat);
    printf("  Rand Read:  %.1f MB/s  (latency: %.2f ms)\n", rand_r_mbs, rand_r_lat);

    /* CSV Output */
    FILE* csv = fopen(csv_path, "w");
    if (csv) {
        fprintf(csv, "Metric,Value,Unit\n");
        fprintf(csv, "Sequential Write,%.1f,MB/s\n", seq_w_mbs);
        fprintf(csv, "Sequential Read,%.1f,MB/s\n", seq_r_mbs);
        fprintf(csv, "Random Write,%.1f,MB/s\n", rand_w_mbs);
        fprintf(csv, "Random Read,%.1f,MB/s\n", rand_r_mbs);
        fprintf(csv, "Sequential Write Latency,%.2f,ms\n", seq_w_lat);
        fprintf(csv, "Sequential Read Latency,%.2f,ms\n", seq_r_lat);
        fprintf(csv, "Random Write Latency,%.4f,ms\n", rand_w_lat);
        fprintf(csv, "Random Read Latency,%.4f,ms\n", rand_r_lat);
        fclose(csv);
        printf("\nCSV saved to: %s\n", csv_path);
    }

    stripe_volume_destroy(&vol);
    test_disk_destroy(d1);
    test_disk_destroy(d2);

    printf("\nPASS: CLI Benchmark\n");
    log_cleanup();
    return 0;
}
