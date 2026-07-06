#include "mirror_engine.h"
#include "storage_common.h"
#include "ram_cache.h"
#include "logger.h"

bool mirror_volume_create(STRIPE_VOLUME* vol, DISK_INFO** disks, uint32_t disk_count) {
    if (!vol || !disks || disk_count < MIN_MIRROR_DISKS || disk_count > MAX_DISKS) return false;
    memset(vol, 0, sizeof(STRIPE_VOLUME));
    vol->raid_level = RAID_LEVEL_MIRROR;
    vol->disk_count = disk_count;

    vol->virtual_total_bytes = UINT64_MAX;
    for (uint32_t i = 0; i < disk_count; i++) {
        vol->disks[i] = disks[i];
        if (disks[i]->pool_bytes < vol->virtual_total_bytes)
            vol->virtual_total_bytes = disks[i]->pool_bytes;
    }

    vol->virtual_total_bytes -= vol->virtual_total_bytes % SECTOR_SIZE;
    vol->stripe_unit = SECTOR_SIZE;
    vol->phase_count = 0;
    vol->healthy_count = disk_count;

    for (uint32_t i = 0; i < disk_count; i++)
        InterlockedExchange(&disks[i]->healthy, 1);

    QueryPerformanceCounter(&vol->start_time);
    LOG_OK("Mirror volume created: %llu MB, %u disks",
           (unsigned long long)(vol->virtual_total_bytes / (1024 * 1024)), disk_count);
    return true;
}

bool mirror_volume_read(STRIPE_VOLUME* vol, void* buffer, uint64_t virtual_offset, uint32_t length) {
    if (!vol || !buffer || length == 0) return false;
    if (virtual_offset + length > vol->virtual_total_bytes) return false;

    if (vol->cache_enabled && !vol->cache_flush_in_progress &&
        virtual_offset + length <= vol->cache.cache_size_bytes) {
        bool ok = cache_read(&vol->cache, buffer, virtual_offset, length);
        if (ok) vol->bytes_read += length;
        return ok;
    }

    uint32_t healthy_count = 0;
    for (uint32_t i = 0; i < vol->disk_count; i++) {
        if (InterlockedCompareExchange(&vol->disks[i]->healthy, 1, 1))
            healthy_count++;
    }

    if (healthy_count == 0) {
        LOG_ERROR("Mirror read: all disks unhealthy");
        return false;
    }

    for (uint32_t i = 0; i < vol->disk_count; i++) {
        if (!InterlockedCompareExchange(&vol->disks[i]->healthy, 1, 1)) continue;
        if (vol->disks[i]->faulty) {
            InterlockedExchange(&vol->disks[i]->healthy, 0);
            InterlockedDecrement(&vol->healthy_count);
            continue;
        }

        if (stripe_read_raw(vol->disks[i], buffer, virtual_offset, length)) {
            vol->bytes_read += length;
            return true;
        }

        InterlockedExchange(&vol->disks[i]->healthy, 0);
        InterlockedDecrement(&vol->healthy_count);
        LOG_WARN("Mirror disk %u read failed, marked unhealthy (%u remaining)",
                 i, (uint32_t)vol->healthy_count);
    }

    return false;
}

static bool mirror_write_to_all(STRIPE_VOLUME* vol, const void* buffer,
                                uint64_t virtual_offset, uint32_t length) {
    bool any_ok = false;
    for (uint32_t i = 0; i < vol->disk_count; i++) {
        if (!InterlockedCompareExchange(&vol->disks[i]->healthy, 1, 1)) continue;
        if (vol->disks[i]->faulty) {
            InterlockedExchange(&vol->disks[i]->healthy, 0);
            InterlockedDecrement(&vol->healthy_count);
            continue;
        }

        if (stripe_write_raw(vol->disks[i], buffer, virtual_offset, length)) {
            any_ok = true;
        } else {
            InterlockedExchange(&vol->disks[i]->healthy, 0);
            InterlockedDecrement(&vol->healthy_count);
            LOG_WARN("Mirror disk %u write failed, marked unhealthy (%u remaining)",
                     i, (uint32_t)vol->healthy_count);
        }
    }
    return any_ok;
}

bool mirror_volume_write(STRIPE_VOLUME* vol, const void* buffer,
                         uint64_t virtual_offset, uint32_t length) {
    if (!vol || !buffer || length == 0) return false;
    if (virtual_offset + length > vol->virtual_total_bytes) return false;

    if (vol->cache_enabled && !vol->cache_flush_in_progress &&
        virtual_offset + length <= vol->cache.cache_size_bytes) {
        bool ok = cache_write(&vol->cache, buffer, virtual_offset, length);
        if (ok && vol->cache.write_through) {
            if (!mirror_write_to_all(vol, buffer, virtual_offset, length)) {
                ok = false;
            }
        }
        if (ok) vol->bytes_written += length;
        return ok;
    }

    bool any_ok = mirror_write_to_all(vol, buffer, virtual_offset, length);
    if (any_ok) vol->bytes_written += length;
    return any_ok;
}

bool mirror_volume_rebuild(STRIPE_VOLUME* vol, DISK_INFO* replacement, uint32_t replace_idx) {
    if (!vol || !replacement || replace_idx >= vol->disk_count) return false;

    uint64_t total = vol->virtual_total_bytes;
    if (total == 0) return false;

    uint32_t src_idx = vol->disk_count;
    for (uint32_t i = 0; i < vol->disk_count; i++) {
        if (i != replace_idx && InterlockedCompareExchange(&vol->disks[i]->healthy, 1, 1)) {
            src_idx = i;
            break;
        }
    }
    if (src_idx >= vol->disk_count) {
        LOG_ERROR("Rebuild: no healthy source disk");
        return false;
    }

    LOG_INFO("Rebuild: reading from disk %u, writing to disk %u (%llu MB total)",
             src_idx, replace_idx, (unsigned long long)(total / (1024 * 1024)));

    uint8_t* buf = (uint8_t*)VirtualAlloc(NULL, (size_t)min_u64(total, 64ULL * 1024 * 1024),
                                          MEM_COMMIT, PAGE_READWRITE);
    if (!buf) { LOG_ERROR("Rebuild: VirtualAlloc failed"); return false; }

    bool ok = true;
    uint64_t offset = 0;
    while (offset < total) {
        uint32_t chunk = (uint32_t)min_u64(total - offset, 64ULL * 1024 * 1024);
        if (!stripe_read_raw(vol->disks[src_idx], buf, offset, chunk)) {
            LOG_ERROR("Rebuild: read failed at offset %llu", (unsigned long long)offset);
            ok = false; break;
        }
        if (!stripe_write_raw(replacement, buf, offset, chunk)) {
            LOG_ERROR("Rebuild: write failed at offset %llu", (unsigned long long)offset);
            ok = false; break;
        }
        if (!FlushFileBuffers(replacement->handle)) {
            LOG_ERROR("Rebuild: flush failed at offset %llu", (unsigned long long)offset);
            ok = false; break;
        }
        offset += chunk;
    }

    VirtualFree(buf, 0, MEM_RELEASE);

    if (ok) {
        vol->disks[replace_idx] = replacement;
        InterlockedExchange(&replacement->healthy, 1);
        InterlockedIncrement(&vol->healthy_count);
        LOG_OK("Rebuild complete: disk %u replaced", replace_idx);
    }
    return ok;
}
