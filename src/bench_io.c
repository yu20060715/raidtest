#include "bench_io.h"
#include "stripe_engine.h"
#include "ram_cache.h"

bool bench_single_disk(DISK_INFO* disk, uint32_t size_mb) {
    if (!disk) return false;
    wchar_t bench_dir[MAX_DRIVE_PATH];
    if (disk->drive_letter[0] == L'C' && disk->drive_letter[1] == L':') {
        LOG_WARN("Warning: benchmarking system drive C: may impact performance");
    }
    wchar_t test_path[MAX_DRIVE_PATH];
    StringCchPrintfW(bench_dir, MAX_DRIVE_PATH, L"%lsRAIDTEST_BENCH", disk->drive_letter);
    CreateDirectoryW(bench_dir, NULL);
    StringCchPrintfW(test_path, MAX_DRIVE_PATH, L"%ls\\raid_bench.tmp", bench_dir);

    uint64_t size_bytes = (uint64_t)size_mb * 1024ULL * 1024ULL;
    uint32_t block_size = BENCH_BLOCK_SIZE;
    uint32_t num_blocks = (uint32_t)(size_bytes / block_size);

    HANDLE h = CreateFileW(test_path, GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, CREATE_ALWAYS,
                           FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        h = CreateFileW(test_path, GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        NULL, CREATE_ALWAYS, FILE_FLAG_OVERLAPPED | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    }
    if (h == INVALID_HANDLE_VALUE) {
        LOG_ERROR("Cannot create benchmark file on %ls", disk->drive_letter);
        RemoveDirectoryW(bench_dir);
        return false;
    }

    LARGE_INTEGER li; li.QuadPart = size_bytes;
    if (!SetFilePointerEx(h, li, NULL, FILE_BEGIN) || !SetEndOfFile(h)) {
        CloseHandle(h); DeleteFileW(test_path); RemoveDirectoryW(bench_dir);
        LOG_ERROR("Failed to allocate benchmark file");
        return false;
    }

    char* block = (char*)VirtualAlloc(NULL, block_size, MEM_COMMIT, PAGE_READWRITE);
    if (!block) { CloseHandle(h); DeleteFileW(test_path); RemoveDirectoryW(bench_dir); return false; }
    memset(block, 0xFF, block_size);

    LARGE_INTEGER freq, t1, t2;
    QueryPerformanceFrequency(&freq);

    HANDLE* events = (HANDLE*)malloc(num_blocks * sizeof(HANDLE));
    OVERLAPPED* ovs = (OVERLAPPED*)malloc(num_blocks * sizeof(OVERLAPPED));
    if (!events || !ovs) {
        free(events); free(ovs);
        VirtualFree(block, 0, MEM_RELEASE); CloseHandle(h);
        DeleteFileW(test_path); RemoveDirectoryW(bench_dir);
        return false;
    }

    uint32_t write_results[BENCH_PASSES] = {0};
    uint32_t read_results[BENCH_PASSES] = {0};

    // Helper macro: one write batch + one read batch
    #define DO_BATCH(pass_idx) \
        do { \
            LARGE_INTEGER zz; zz.QuadPart = 0; SetFilePointerEx(h, zz, NULL, FILE_BEGIN); \
            bool w_ok = true; \
            QueryPerformanceCounter(&t1); \
            for (uint32_t ii = 0; ii < num_blocks; ii++) { \
                events[ii] = CreateEvent(NULL, TRUE, FALSE, NULL); \
                memset(&ovs[ii], 0, sizeof(OVERLAPPED)); \
                ovs[ii].hEvent = events[ii]; \
                ovs[ii].Offset = (DWORD)((uint64_t)ii * block_size & 0xFFFFFFFF); \
                ovs[ii].OffsetHigh = (DWORD)(((uint64_t)ii * block_size >> 32) & 0xFFFFFFFF); \
                DWORD dd; \
                if (!WriteFile(h, block, block_size, &dd, &ovs[ii])) \
                    if (GetLastError() != ERROR_IO_PENDING) { SetEvent(events[ii]); w_ok = false; } \
            } \
            for (uint32_t bb = 0; bb < num_blocks; bb += MAXIMUM_WAIT_OBJECTS) { \
                uint32_t nn = num_blocks - bb; \
                if (nn > MAXIMUM_WAIT_OBJECTS) nn = MAXIMUM_WAIT_OBJECTS; \
                WaitForMultipleObjects(nn, &events[bb], TRUE, INFINITE); \
            } \
            for (uint32_t ii = 0; ii < num_blocks; ii++) CloseHandle(events[ii]); \
            QueryPerformanceCounter(&t2); \
            double ws = (double)(t2.QuadPart - t1.QuadPart) / freq.QuadPart; \
            write_results[pass_idx] = (w_ok && ws > 0) ? (uint32_t)((size_bytes / (1024.0 * 1024.0)) / ws) : 0; \
            FlushFileBuffers(h); \
            bool r_ok = true; \
            QueryPerformanceCounter(&t1); \
            for (uint32_t ii = 0; ii < num_blocks; ii++) { \
                events[ii] = CreateEvent(NULL, TRUE, FALSE, NULL); \
                memset(&ovs[ii], 0, sizeof(OVERLAPPED)); \
                ovs[ii].hEvent = events[ii]; \
                ovs[ii].Offset = (DWORD)((uint64_t)ii * block_size & 0xFFFFFFFF); \
                ovs[ii].OffsetHigh = (DWORD)(((uint64_t)ii * block_size >> 32) & 0xFFFFFFFF); \
                DWORD dd; \
                if (!ReadFile(h, block, block_size, &dd, &ovs[ii])) \
                    if (GetLastError() != ERROR_IO_PENDING) { SetEvent(events[ii]); r_ok = false; } \
            } \
            for (uint32_t bb = 0; bb < num_blocks; bb += MAXIMUM_WAIT_OBJECTS) { \
                uint32_t nn = num_blocks - bb; \
                if (nn > MAXIMUM_WAIT_OBJECTS) nn = MAXIMUM_WAIT_OBJECTS; \
                WaitForMultipleObjects(nn, &events[bb], TRUE, INFINITE); \
            } \
            for (uint32_t ii = 0; ii < num_blocks; ii++) CloseHandle(events[ii]); \
            QueryPerformanceCounter(&t2); \
            double rs = (double)(t2.QuadPart - t1.QuadPart) / freq.QuadPart; \
            read_results[pass_idx] = (r_ok && rs > 0) ? (uint32_t)((size_bytes / (1024.0 * 1024.0)) / rs) : 0; \
        } while(0)

    // Pass 0: measure to determine speed tier
    DO_BATCH(0);
    printf("  Initial: W=%u MB/s, R=%u MB/s\n", write_results[0], read_results[0]);

    uint32_t total_passes;
    uint32_t combined = (write_results[0] + read_results[0]) / 2;
    if (combined < 500)       total_passes = 5;
    else if (combined < 2000) total_passes = 8;
    else if (combined < 5000) total_passes = 10;
    else                      total_passes = BENCH_PASSES;

    for (uint32_t pass = 1; pass < total_passes; pass++) {
        DO_BATCH(pass);
        printf("  Pass %u/%u: W=%u MB/s, R=%u MB/s\n",
               pass + 1, total_passes,
               write_results[pass], read_results[pass]);
    }

    #undef DO_BATCH

    free(events);
    free(ovs);

    uint64_t wsum = 0, rsum = 0;
    for (uint32_t i = 0; i < total_passes; i++) {
        wsum += write_results[i];
        rsum += read_results[i];
    }
    disk->bench_write_mbs = (uint32_t)(wsum / total_passes);
    disk->bench_read_mbs  = (uint32_t)(rsum / total_passes);
    disk->benchmarked = true;

    printf("  Average (%u passes): W=%u MB/s, R=%u MB/s\n",
           total_passes, disk->bench_write_mbs, disk->bench_read_mbs);

    VirtualFree(block, 0, MEM_RELEASE);
    CloseHandle(h);
    DeleteFileW(test_path);
    RemoveDirectoryW(bench_dir);
    return true;
}

bool bench_volume(STRIPE_VOLUME* vol, uint32_t size_mb, uint32_t block_kb) {
    if (!vol) return false;
    uint64_t size_bytes = (uint64_t)size_mb * 1024 * 1024;
    uint32_t block_size = block_kb * 1024;
    uint32_t blocks = (uint32_t)(size_bytes / block_size);
    if (size_bytes > vol->virtual_total_bytes) size_bytes = vol->virtual_total_bytes;
    size_mb = (uint32_t)(size_bytes / (1024 * 1024));
    blocks = (uint32_t)(size_bytes / block_size);

    void* buf = VirtualAlloc(NULL, block_size, MEM_COMMIT, PAGE_READWRITE);
    if (!buf) { LOG_ERROR("Out of memory"); return false; }
    memset(buf, 0xFF, block_size);

    LARGE_INTEGER freq, t1, t2;
    QueryPerformanceFrequency(&freq);
    LOG_INFO("BenchFS: %u MB, block=%u KB, %u blocks...", size_mb, block_kb, blocks);

    QueryPerformanceCounter(&t1);
    uint64_t written = 0;
    bool ok = true;
    for (uint32_t i = 0; i < blocks; i++) {
        if (!stripe_volume_write(vol, buf, (uint64_t)i * block_size, block_size)) {
            LOG_ERROR("Write failed at block %u", i);
            ok = false; break;
        }
        written += block_size;
        if ((i + 1) % 64 == 0) { printf("."); fflush(stdout); }
    }
    QueryPerformanceCounter(&t2);
    if (ok) {
        double elapsed = (double)(t2.QuadPart - t1.QuadPart) / freq.QuadPart;
        double mbs = elapsed > 0 ? (written / (1024.0 * 1024.0)) / elapsed : 0;
        LOG_OK("Write: %.2f MB in %.2f s = %.0f MB/s", written / (1024.0 * 1024.0), elapsed, mbs);
    }

    if (vol->cache_enabled) {
        cache_flush_all(&vol->cache, vol);
    }

    QueryPerformanceCounter(&t1);
    uint64_t read_bytes = 0;
    ok = true;
    for (uint32_t i = 0; i < blocks; i++) {
        if (!stripe_volume_read(vol, buf, (uint64_t)i * block_size, block_size)) {
            LOG_ERROR("Read failed at block %u", i);
            ok = false; break;
        }
        read_bytes += block_size;
        if ((i + 1) % 64 == 0) { printf("."); fflush(stdout); }
    }
    QueryPerformanceCounter(&t2);
    if (ok) {
        double elapsed = (double)(t2.QuadPart - t1.QuadPart) / freq.QuadPart;
        double mbs = elapsed > 0 ? (read_bytes / (1024.0 * 1024.0)) / elapsed : 0;
        LOG_OK("Read:  %.2f MB in %.2f s = %.0f MB/s", read_bytes / (1024.0 * 1024.0), elapsed, mbs);
    }

    VirtualFree(buf, 0, MEM_RELEASE);
    return ok;
}