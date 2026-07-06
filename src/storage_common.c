#include "storage_common.h"

static bool stripe_io_ok(DISK_INFO* disk, bool ok) {
    if (!ok) {
        InterlockedIncrement(&disk->error_count);
        if (disk->error_count > 5) {
            InterlockedExchange(&disk->faulty, 1);
            LOG_ERROR("DISK FAULTY: %ls (%lu consecutive errors)", disk->model, (unsigned long)disk->error_count);
        }
    } else {
        if (disk->error_count > 0) InterlockedExchange(&disk->error_count, 0);
    }
    return ok;
}

static bool async_io_wait(HANDLE h, OVERLAPPED* ov, DWORD* transferred) {
    if (GetLastError() == ERROR_IO_PENDING)
        return GetOverlappedResult(h, ov, transferred, TRUE);
    return false;
}

bool stripe_read_raw(DISK_INFO* disk, void* buffer, uint64_t offset, uint32_t length) {
    if (!disk || !buffer) return false;
    if (disk->ram_buffer) { memcpy(buffer, (uint8_t*)disk->ram_buffer + offset, length); return true; }
    if (disk->faulty) { LOG_WARN("Read skipped: disk %ls is marked faulty", disk->model); return false; }
    if (disk->handle != INVALID_HANDLE_VALUE) {
        OVERLAPPED ov = {0};
        ov.Offset = (DWORD)(offset & 0xFFFFFFFF);
        ov.OffsetHigh = (DWORD)((offset >> 32) & 0xFFFFFFFF);
        DWORD read_bytes = 0;
        if (ReadFile(disk->handle, buffer, length, &read_bytes, &ov))
            return stripe_io_ok(disk, read_bytes == length);
        return stripe_io_ok(disk, async_io_wait(disk->handle, &ov, &read_bytes) && read_bytes == length);
    }
    return stripe_io_ok(disk, false);
}

bool stripe_write_raw(DISK_INFO* disk, const void* buffer, uint64_t offset, uint32_t length) {
    if (!disk || !buffer) return false;
    if (disk->ram_buffer) { memcpy((uint8_t*)disk->ram_buffer + offset, buffer, length); return true; }
    if (disk->faulty) { LOG_WARN("Write skipped: disk %ls is marked faulty", disk->model); return false; }
    if (disk->handle != INVALID_HANDLE_VALUE) {
        OVERLAPPED ov = {0};
        ov.Offset = (DWORD)(offset & 0xFFFFFFFF);
        ov.OffsetHigh = (DWORD)((offset >> 32) & 0xFFFFFFFF);
        DWORD written = 0;
        if (WriteFile(disk->handle, buffer, length, &written, &ov))
            return stripe_io_ok(disk, written == length);
        return stripe_io_ok(disk, async_io_wait(disk->handle, &ov, &written) && written == length);
    }
    return stripe_io_ok(disk, false);
}
