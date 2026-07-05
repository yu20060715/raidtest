#include "test_common.h"
#include <windows.h>

test_fn g_tests[TEST_MAX];
uint32_t g_test_count = 0;
uint32_t g_test_passed = 0;
uint32_t g_test_failed = 0;

static uint32_t s_disk_serial = 0;

void test_register(test_fn fn) {
    if (g_test_count < TEST_MAX)
        g_tests[g_test_count++] = fn;
}

int test_run_all(void) {
    log_init();
    log_set_level(LOG_LEVEL_WARN);
    printf("\n===== RAIDTEST Test Suite =====\n\n");
    for (uint32_t i = 0; i < g_test_count; i++) {
        printf("[%u/%u] Running... ", i + 1, g_test_count);
        fflush(stdout);
        if (g_tests[i]()) {
            g_test_passed++;
        } else {
            g_test_failed++;
        }
    }
    printf("\n===== Results: %u passed, %u failed =====\n",
           (unsigned)g_test_passed, (unsigned)g_test_failed);
    log_cleanup();
    return (g_test_failed == 0) ? 0 : 1;
}

DISK_INFO* test_disk_create(uint64_t size_bytes, uint32_t write_mbs) {
    DISK_INFO* d = (DISK_INFO*)calloc(1, sizeof(DISK_INFO));
    if (!d) return NULL;

    wchar_t temp_dir[MAX_DRIVE_PATH];
    DWORD ret = GetTempPathW(MAX_DRIVE_PATH, temp_dir);
    if (ret == 0 || ret > MAX_DRIVE_PATH) wcscpy_s(temp_dir, MAX_DRIVE_PATH, L"C:\\Temp\\");

    uint32_t serial = InterlockedIncrement((volatile LONG*)&s_disk_serial);
    swprintf(d->file_path, MAX_DRIVE_PATH, L"%sraidtest_%u.dat", temp_dir, serial);

    d->ram_buffer = NULL;
    d->pool_bytes = size_bytes;
    d->total_bytes = size_bytes;
    d->handle = INVALID_HANDLE_VALUE;
    d->bench_write_mbs = write_mbs;
    d->bench_read_mbs = write_mbs;
    d->benchmarked = true;
    d->selected = true;
    d->healthy = 1;
    d->faulty = 0;
    d->error_count = 0;
    d->sector_size = SECTOR_SIZE;
    wcscpy_s(d->model, MAX_MODEL_LEN, L"Test Disk");
    wcscpy_s(d->drive_letter, MAX_DRIVE_PATH, L"T:");
    wcscpy_s(d->device_path, MAX_DRIVE_PATH, L"\\\\.\\T:");

    /* Create file with overlapped support */
    HANDLE h = CreateFileW(d->file_path, GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, CREATE_ALWAYS, FILE_FLAG_OVERLAPPED, NULL);
    if (h == INVALID_HANDLE_VALUE) { free(d); return NULL; }

    LARGE_INTEGER li; li.QuadPart = size_bytes;
    if (!SetFilePointerEx(h, li, NULL, FILE_BEGIN) || !SetEndOfFile(h)) {
        CloseHandle(h); DeleteFileW(d->file_path); free(d); return NULL;
    }
    CloseHandle(h);

    /* Re-open for overlapped I/O */
    d->handle = CreateFileW(d->file_path, GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (d->handle == INVALID_HANDLE_VALUE) {
        DeleteFileW(d->file_path); free(d); return NULL;
    }

    return d;
}

void test_disk_destroy(DISK_INFO* disk) {
    if (!disk) return;
    if (disk->handle != INVALID_HANDLE_VALUE) {
        CloseHandle(disk->handle);
        disk->handle = INVALID_HANDLE_VALUE;
    }
    DeleteFileW(disk->file_path);
    free(disk);
}

void test_disk_reset(DISK_INFO* disk) {
    if (!disk) return;
    LARGE_INTEGER li; li.QuadPart = 0;
    SetFilePointerEx(disk->handle, li, NULL, FILE_BEGIN);
    uint64_t zero = 0;
    WriteFile(disk->handle, &zero, 0, NULL, NULL);
    InterlockedExchange(&disk->healthy, 1);
    InterlockedExchange(&disk->faulty, 0);
    InterlockedExchange(&disk->error_count, 0);
}

void test_fill_pattern(uint8_t* buf, uint64_t size, uint64_t offset) {
    for (uint64_t i = 0; i < size; i++)
        buf[i] = (uint8_t)((offset + i) & 0xFF);
}

bool test_check_pattern(const uint8_t* buf, uint64_t size, uint64_t offset) {
    for (uint64_t i = 0; i < size; i++) {
        if (buf[i] != (uint8_t)((offset + i) & 0xFF))
            return false;
    }
    return true;
}

/* We can only use this for mirror (which goes through stripe_write_raw/stripe_read_raw).
   Stripe engine uses direct ReadFile/WriteFile with OVERLAPPED. */
bool test_verify_volume_read_write(STRIPE_VOLUME* vol, uint64_t test_size) {
    if (test_size > vol->virtual_total_bytes) test_size = vol->virtual_total_bytes;
    uint8_t* wbuf = (uint8_t*)VirtualAlloc(NULL, (size_t)test_size, MEM_COMMIT, PAGE_READWRITE);
    uint8_t* rbuf = (uint8_t*)VirtualAlloc(NULL, (size_t)test_size, MEM_COMMIT, PAGE_READWRITE);
    if (!wbuf || !rbuf) { VirtualFree(wbuf, 0, MEM_RELEASE); VirtualFree(rbuf, 0, MEM_RELEASE); return false; }
    test_fill_pattern(wbuf, test_size, 0);

    if (!stripe_volume_write(vol, wbuf, 0, (uint32_t)test_size)) {
        VirtualFree(wbuf, 0, MEM_RELEASE); VirtualFree(rbuf, 0, MEM_RELEASE); return false;
    }
    memset(rbuf, 0, (size_t)test_size);
    if (!stripe_volume_read(vol, rbuf, 0, (uint32_t)test_size)) {
        VirtualFree(wbuf, 0, MEM_RELEASE); VirtualFree(rbuf, 0, MEM_RELEASE); return false;
    }
    bool ok = (memcmp(wbuf, rbuf, (size_t)test_size) == 0);
    VirtualFree(wbuf, 0, MEM_RELEASE);
    VirtualFree(rbuf, 0, MEM_RELEASE);
    return ok;
}
