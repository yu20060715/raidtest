#include "ram_cache.h"
#include "stripe_engine.h"
#include "journal.h"

bool cache_init(RAM_CACHE* cache, uint64_t size_bytes) {
    if (!cache || size_bytes == 0) return false;
    cache->block_size = CACHE_BLOCK_SIZE;
    cache->block_count = (uint32_t)(size_bytes / cache->block_size);
    size_bytes = (uint64_t)cache->block_count * cache->block_size;
    cache->cache_size_bytes = size_bytes;
    cache->buffer = (uint8_t*)VirtualAlloc(NULL, size_bytes, MEM_COMMIT, PAGE_READWRITE);
    if (!cache->buffer) return false;
    uint32_t bitmap_bytes = (cache->block_count + 7) / 8;
    cache->dirty_map = (uint8_t*)malloc(bitmap_bytes);
    if (!cache->dirty_map) { VirtualFree(cache->buffer, 0, MEM_RELEASE); cache->buffer = NULL; return false; }
    memset(cache->dirty_map, 0, bitmap_bytes);
    cache->valid_map = (uint8_t*)malloc(bitmap_bytes);
    if (!cache->valid_map) { free(cache->dirty_map); VirtualFree(cache->buffer, 0, MEM_RELEASE); return false; }
    memset(cache->valid_map, 0, bitmap_bytes);
    cache->hit_count = 0;
    cache->miss_count = 0;
    cache->running = 1;
    cache->flush_thread = NULL;
    cache->write_through = 0;
    cache->flush_buffer = (uint8_t*)VirtualAlloc(NULL, MAX_FLUSH_SIZE, MEM_COMMIT, PAGE_READWRITE);
    if (!cache->flush_buffer) {
        free(cache->dirty_map); cache->dirty_map = NULL;
        VirtualFree(cache->buffer, 0, MEM_RELEASE); cache->buffer = NULL;
        return false;
    }
    cache->flush_buffer_size = (uint32_t)MAX_FLUSH_SIZE;
    InitializeCriticalSection(&cache->lock);
    return true;
}

void cache_destroy(RAM_CACHE* cache) {
    if (!cache || !cache->buffer) return;
    cache->running = 0;
    if (cache->flush_thread) {
        WaitForSingleObject(cache->flush_thread, INFINITE);
        CloseHandle(cache->flush_thread);
        cache->flush_thread = NULL;
    }
    if (cache->dirty_map) free(cache->dirty_map);
    if (cache->valid_map) free(cache->valid_map);
    if (cache->flush_buffer) VirtualFree(cache->flush_buffer, 0, MEM_RELEASE);
    VirtualFree(cache->buffer, 0, MEM_RELEASE);
    DeleteCriticalSection(&cache->lock);
    memset(cache, 0, sizeof(RAM_CACHE));
}

bool cache_write(RAM_CACHE* cache, const void* data, uint64_t offset, uint32_t length) {
    if (!cache || !cache->buffer || !data) return false;
    uint64_t end = offset + length;
    if (length == 0 || end < offset || end > cache->cache_size_bytes) return false;
    EnterCriticalSection(&cache->lock);
    memcpy(cache->buffer + offset, data, length);
    uint64_t start_block = offset / cache->block_size;
    uint64_t end_block = (end + cache->block_size - 1) / cache->block_size;
    for (uint64_t b = start_block; b < end_block && b < cache->block_count; b++) {
        cache->dirty_map[b / 8] |= (1 << (b % 8));
        cache->valid_map[b / 8] |= (1 << (b % 8));
    }
    LeaveCriticalSection(&cache->lock);
    return true;
}

bool cache_read(RAM_CACHE* cache, void* data, uint64_t offset, uint32_t length) {
    if (!cache || !cache->buffer || !data) return false;
    uint64_t end = offset + length;
    if (length == 0 || end < offset || end > cache->cache_size_bytes) return false;
    EnterCriticalSection(&cache->lock);
    uint64_t start_block = offset / cache->block_size;
    uint64_t end_block = (end + cache->block_size - 1) / cache->block_size;
    bool all_valid = true;
    for (uint64_t b = start_block; b < end_block && b < cache->block_count; b++) {
        if (!(cache->valid_map[b / 8] & (1 << (b % 8)))) { all_valid = false; break; }
    }
    if (!all_valid) { cache->miss_count++; LeaveCriticalSection(&cache->lock); return false; }
    memcpy(data, cache->buffer + offset, length);
    cache->hit_count++;
    LeaveCriticalSection(&cache->lock);
    return true;
}

void cache_flush_all(RAM_CACHE* cache, STRIPE_VOLUME* vol) {
    if (!cache || !vol) return;
    uint32_t flushed = 0;
    uint32_t failed = 0;
    uint32_t max_batch = 256;

    /* ---- Journal: record flush cycle start (write-ahead) ---- */
    bool has_work = false;
    for (uint32_t cb = 0; cb < cache->block_count; cb++) {
        if (cache->dirty_map[cb / 8] & (1 << (cb % 8))) { has_work = true; break; }
    }
    if (has_work) journal_begin(vol);

    uint32_t b = 0;
    while (b < cache->block_count) {
        EnterCriticalSection(&cache->lock);
        if (!(cache->dirty_map[b / 8] & (1 << (b % 8)))) {
            LeaveCriticalSection(&cache->lock);
            b++;
            continue;
        }

        uint32_t run = 1;
        while (b + run < cache->block_count && run < max_batch &&
               (cache->dirty_map[(b + run) / 8] & (1 << ((b + run) % 8)))) {
            run++;
        }

        uint64_t block_offset = (uint64_t)b * cache->block_size;
        uint32_t total_len = run * cache->block_size;
        if (block_offset + total_len > vol->virtual_total_bytes) {
            while (run > 1 && block_offset + run * cache->block_size > vol->virtual_total_bytes) run--;
            if (run == 0) { LeaveCriticalSection(&cache->lock); break; }
            total_len = run * cache->block_size;
        }

        if (total_len > cache->flush_buffer_size) {
            LeaveCriticalSection(&cache->lock);
            failed++;
            b += run;
            continue;
        }
        memcpy(cache->flush_buffer, cache->buffer + block_offset, total_len);
        for (uint32_t i = b; i < b + run; i++)
            cache->dirty_map[i / 8] &= ~(1 << (i % 8));
        LeaveCriticalSection(&cache->lock);

        /* ---- Journal: record DATA entry before physical IO ---- */
        journal_data(vol, block_offset, total_len, cache->flush_buffer);

        /* ---- Physical I/O ---- */
        InterlockedExchange(&vol->cache_flush_in_progress, 1);
        IO_MAPPING_ENTRY entries[MAX_IO_ENTRIES];
        uint32_t entry_count = 0;
        bool ok = stripe_volume_map_lba(vol, block_offset, entries, &entry_count, total_len);
        if (ok) {
            uint32_t mapped = 0;
            OVERLAPPED ovs[MAX_IO_ENTRIES];
            HANDLE events[MAX_IO_ENTRIES];
            memset(events, 0, sizeof(events));
            DWORD dummy = 0;

            for (uint32_t e = 0; e < entry_count; e++) {
                DISK_INFO* disk = vol->disks[entries[e].disk_index];
                HANDLE evt = CreateEventW(NULL, TRUE, FALSE, NULL);
                if (!evt) {
                    for (uint32_t j = 0; j < e; j++) {
                        if (events[j]) CancelIoEx(vol->disks[entries[j].disk_index]->handle, &ovs[j]);
                        CloseHandle(events[j]);
                    }
                    ok = false; break;
                }
                events[e] = evt;
                memset(&ovs[e], 0, sizeof(OVERLAPPED));
                ovs[e].Offset = (DWORD)(entries[e].physical_offset & 0xFFFFFFFF);
                ovs[e].OffsetHigh = (DWORD)((entries[e].physical_offset >> 32) & 0xFFFFFFFF);
                ovs[e].hEvent = evt;
                if (!WriteFile(disk->handle, cache->flush_buffer + mapped, entries[e].length_bytes, &dummy, &ovs[e])) {
                    if (GetLastError() != ERROR_IO_PENDING) {
                        CloseHandle(evt); events[e] = NULL; ok = false; break;
                    }
                }
                mapped += entries[e].length_bytes;
            }
            if (ok) {
                for (uint32_t e = 0; e < entry_count; e++) {
                    if (!GetOverlappedResult(vol->disks[entries[e].disk_index]->handle, &ovs[e], &dummy, TRUE))
                        ok = false;
                    CloseHandle(events[e]);
                }
            } else {
                EnterCriticalSection(&cache->lock);
                for (uint32_t i = b; i < b + run; i++)
                    cache->dirty_map[i / 8] |= (1 << (i % 8));
                LeaveCriticalSection(&cache->lock);
                for (uint32_t e = 0; e < entry_count; e++)
                    if (events[e]) { CancelIoEx(vol->disks[entries[e].disk_index]->handle, &ovs[e]); CloseHandle(events[e]); }
                failed++;
            }
        } else {
            EnterCriticalSection(&cache->lock);
            for (uint32_t i = b; i < b + run; i++)
                cache->dirty_map[i / 8] |= (1 << (i % 8));
            LeaveCriticalSection(&cache->lock);
            failed++;
        }
        InterlockedExchange(&vol->cache_flush_in_progress, 0);

        if (ok) flushed += run;
        b += run;
    }

    /* ---- Journal: commit if any work was done ---- */
    if (has_work && flushed > 0) {
        journal_commit(vol);
    } else if (has_work && flushed == 0 && failed > 0) {
        /* All batches failed — discard the journal (data is inconsistent anyway) */
        /* The failed dirty bits were restored, so next flush will retry */
    }

    if (flushed > 0 || failed > 0)
        LOG_INFO("Cache flush: %u blocks flushed, %u batch(es) failed", flushed, failed);
}

unsigned int __stdcall cache_flush_thread(void* arg) {
    STRIPE_VOLUME* vol = (STRIPE_VOLUME*)arg;
    RAM_CACHE* cache = &vol->cache;
    while (cache->running) {
        uint32_t dirty_count = 0;
        EnterCriticalSection(&cache->lock);
        for (uint32_t b = 0; b < cache->block_count; b++) {
            if (cache->dirty_map[b / 8] & (1 << (b % 8))) dirty_count++;
        }
        LeaveCriticalSection(&cache->lock);

        uint32_t sleep_ms = 1000;
        if (cache->block_count > 0) {
            double ratio = (double)dirty_count / cache->block_count;
            if (ratio > 0.75)        sleep_ms = 10;
            else if (ratio > 0.50)   sleep_ms = 50;
            else if (ratio > 0.25)   sleep_ms = 100;
            else if (ratio > 0.10)   sleep_ms = 250;
        }

        Sleep(sleep_ms);
        if (cache->running) cache_flush_all(cache, vol);
    }
    return 0;
}