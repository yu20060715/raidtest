#include "pool_io.h"

static bool is_sector_aligned(uint64_t value, uint32_t sector_size) {
    return (value % sector_size) == 0;
}

bool pool_file_create(DISK_INFO* disk, uint64_t size_bytes) {
    if (!disk || !disk->file_path[0]) return false;
    if (!is_sector_aligned(size_bytes, SECTOR_SIZE)) {
        LOG_WARN("Pool size %llu not aligned to sector size %u, adjusting",
                 (unsigned long long)size_bytes, (unsigned int)SECTOR_SIZE);
        size_bytes = (size_bytes / SECTOR_SIZE) * SECTOR_SIZE;
        if (size_bytes == 0) size_bytes = SECTOR_SIZE;
    }
    disk->pool_bytes = size_bytes;
    HANDLE h = CreateFileW(disk->file_path, GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, CREATE_ALWAYS, FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        h = CreateFileW(disk->file_path, GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        NULL, CREATE_ALWAYS, FILE_FLAG_WRITE_THROUGH, NULL);
    }
    if (h == INVALID_HANDLE_VALUE) {
        LOG_ERROR("Failed to create pool file %ls", disk->file_path);
        return false;
    }
    LARGE_INTEGER li; li.QuadPart = size_bytes;
    if (!SetFilePointerEx(h, li, NULL, FILE_BEGIN) || !SetEndOfFile(h)) {
        LOG_ERROR("Failed to allocate pool file %ls (%llu bytes)", disk->file_path, (unsigned long long)size_bytes);
        CloseHandle(h); return false;
    }
    CloseHandle(h);
    char path_a[MAX_DRIVE_PATH] = {0};
    wcstombs(path_a, disk->file_path, MAX_DRIVE_PATH - 1);
    LOG_OK("Pool file created: %s  (%llu MB)", path_a, (unsigned long long)(size_bytes / (1024 * 1024)));
    return true;
}

bool pool_file_open(DISK_INFO* disk) {
    if (!disk || !disk->file_path[0]) return false;
    if (!is_sector_aligned(disk->pool_bytes, SECTOR_SIZE)) {
        LOG_WARN("Pool file size %llu not aligned to sector size", (unsigned long long)disk->pool_bytes);
    }
    disk->handle = CreateFileW(disk->file_path, GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED, NULL);
    if (disk->handle == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        LOG_WARN("NO_BUFFERING open failed for %ls (err=%lu), falling back to buffered I/O", disk->file_path, err);
        disk->handle = CreateFileW(disk->file_path, GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    }
    if (disk->handle == INVALID_HANDLE_VALUE) {
        LOG_ERROR("Failed to open pool file %ls (err=%lu)", disk->file_path, GetLastError());
        return false;
    }
    return true;
}

void pool_file_close(DISK_INFO* disk) {
    if (disk && disk->handle != INVALID_HANDLE_VALUE) {
        CloseHandle(disk->handle);
        disk->handle = INVALID_HANDLE_VALUE;
    }
}

bool pool_file_delete(DISK_INFO* disk) {
    if (!disk || !disk->file_path[0]) return false;
    pool_file_close(disk);
    BOOL ok = DeleteFileW(disk->file_path);
    if (ok) {
        char path_a[MAX_DRIVE_PATH] = {0};
        wcstombs(path_a, disk->file_path, MAX_DRIVE_PATH - 1);
        LOG_INFO("Deleted pool file: %s", path_a);
    }
    return ok;
}

bool pool_dir_delete(DISK_INFO* disk) {
    if (!disk || !disk->drive_letter[0]) return false;
    wchar_t dir_path[MAX_DRIVE_PATH];
    wcscpy_s(dir_path, MAX_DRIVE_PATH, disk->drive_letter);
    size_t len = wcslen(dir_path);
    if (len > 1 && dir_path[len-1] == L'\\') dir_path[len-1] = L'\0';
    BOOL ok = RemoveDirectoryW(dir_path);
    if (ok) {
        char path_a[MAX_DRIVE_PATH] = {0};
        wcstombs(path_a, dir_path, MAX_DRIVE_PATH - 1);
        LOG_INFO("Removed directory: %s", path_a);
    }
    return ok;
}