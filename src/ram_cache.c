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
    if (!cache->valid_map) { free(cache->dirty_map); cache->dirty_map = NULL; VirtualFree(cache->buffer, 0, MEM_RELEASE); cache->buffer = NULL; return false; }
    memset(cache->valid_map, 0, bitmap_bytes);
    cache->hit_count = 0;
    cache->miss_count = 0;
    cache->running = 1;
    cache->thread_stop = 0;
    cache->flush_thread = NULL;
    cache->write_through = 0;
    cache->flush_buffer = (uint8_t*)VirtualAlloc(NULL, MAX_FLUSH_SIZE, MEM_COMMIT, PAGE_READWRITE);
    if (!cache->flush_buffer) {
        free(cache->valid_map); cache->valid_map = NULL;
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
    InterlockedExchange(&cache->running, 0);
    InterlockedExchange(&cache->thread_stop, 1);
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

void cache_invalidate(RAM_CACHE* cache, uint64_t offset, uint32_t length) {
    if (!cache || !cache->buffer) return;
    uint64_t end = offset + length;
    if (length == 0 || end > cache->cache_size_bytes) return;
    EnterCriticalSection(&cache->lock);
    uint64_t start_block = offset / cache->block_size;
    uint64_t end_block = (end + cache->block_size - 1) / cache->block_size;
    for (uint64_t b = start_block; b < end_block && b < cache->block_count; b++) {
        cache->valid_map[b / 8] &= ~(1 << (b % 8));
        cache->dirty_map[b / 8] &= ~(1 << (b % 8));
    }
    LeaveCriticalSection(&cache->lock);
}

void cache_flush_all(RAM_CACHE* cache, STRIPE_VOLUME* vol) {
    if (!cache || !vol) return;
    /* Re-entrancy guard: if another thread is already flushing, skip */
    if (InterlockedExchange(&vol->cache_flush_in_progress, 1) != 0)
        return;

    uint32_t flushed = 0;
    uint32_t failed = 0;
    uint32_t max_batch = 256;

    journal_begin(vol);

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

        bool journal_ok = journal_data(vol, block_offset, total_len, cache->flush_buffer);
        if (!journal_ok) {
            LeaveCriticalSection(&cache->lock);
            failed++;
            b += run;
            continue;
        }

        for (uint32_t i = b; i < b + run; i++)
            cache->dirty_map[i / 8] &= ~(1 << (i % 8));
        LeaveCriticalSection(&cache->lock);

        IO_MAPPING_ENTRY entries[MAX_IO_ENTRIES];
        uint32_t entry_count = 0;
        bool ok = stripe_volume_map_lba(vol, block_offset, entries, &entry_count, total_len);
        if (ok) {
            uint32_t mapped = 0;
            OVERLAPPED ovs[MAX_IO_ENTRIES];
            HANDLE events[MAX_IO_ENTRIES];
            memset(events, 0, sizeof(events));

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
                if (!WriteFile(disk->handle, cache->flush_buffer + mapped, entries[e].length_bytes, NULL, &ovs[e])) {
                    if (GetLastError() != ERROR_IO_PENDING) {
                        CloseHandle(evt); events[e] = NULL; ok = false; break;
                    }
                }
                mapped += entries[e].length_bytes;
            }
            if (ok) {
                DWORD xfer = 0;
                for (uint32_t e = 0; e < entry_count; e++) {
                    if (!GetOverlappedResult(vol->disks[entries[e].disk_index]->handle, &ovs[e], &xfer, TRUE))
                        ok = false;
                    CloseHandle(events[e]);
                }
                if (ok) {
                    /* Blocks may have been re-dirtied by concurrent cache_write during I/O */
                    EnterCriticalSection(&cache->lock);
                    uint32_t first_redirty = UINT32_MAX;
                    for (uint32_t i = b; i < b + run && i < cache->block_count; i++) {
                        if (cache->dirty_map[i / 8] & (1 << (i % 8))) {
                            first_redirty = i;
                            break;
                        }
                    }
                    LeaveCriticalSection(&cache->lock);
                    if (first_redirty != UINT32_MAX) {
                        b = first_redirty;
                        continue;
                    }
                } else {
                    EnterCriticalSection(&cache->lock);
                    for (uint32_t i = b; i < b + run; i++)
                        cache->dirty_map[i / 8] |= (1 << (i % 8));
                    LeaveCriticalSection(&cache->lock);
                    failed++;
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

        if (ok) flushed += run;
        b += run;
    }

    if (flushed > 0) {
        journal_commit(vol);
    }

    InterlockedExchange(&vol->cache_flush_in_progress, 0);

    if (flushed > 0 || failed > 0)
        LOG_INFO("Cache flush: %u blocks flushed, %u batch(es) failed", flushed, failed);
}

unsigned int __stdcall cache_flush_thread(void* arg) {
    if (!arg) return 1;
    STRIPE_VOLUME* vol = (STRIPE_VOLUME*)arg;
    RAM_CACHE* cache = &vol->cache;
    while (!cache->thread_stop) {
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
        if (!cache->thread_stop) cache_flush_all(cache, vol);
    }
    return 0;
}