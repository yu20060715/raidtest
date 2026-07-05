#include "stripe_engine.h"
#include "mirror_engine.h"
#include "ram_cache.h"
#include "profiler.h"

static bool map_single_byte(const STRIPE_VOLUME* vol, uint64_t virtual_offset,
                             uint32_t* out_disk_idx, uint64_t* out_phys_offset) {
    if (virtual_offset >= vol->virtual_total_bytes) return false;
    for (uint32_t p = 0; p < vol->phase_count; p++) {
        const MAPPING_PHASE* ph = &vol->phases[p];
        if (virtual_offset < ph->virtual_start_bytes) continue;
        uint64_t end = ph->virtual_start_bytes + ph->virtual_size_bytes;
        if (virtual_offset >= end) continue;
        uint64_t offset_in_phase = virtual_offset - ph->virtual_start_bytes;

        if (ph->active_count == 1) {
            *out_disk_idx = ph->active_disk_indices[0];
            *out_phys_offset = ph->physical_starts_bytes[0] + offset_in_phase;
            return true;
        }
        uint64_t cycle_index = offset_in_phase / ph->cycle_size_bytes;
        uint64_t offset_in_cycle = offset_in_phase % ph->cycle_size_bytes;
        uint32_t cum = 0;
        for (uint32_t j = 0; j < ph->active_count; j++) {
            uint64_t seg_start = (uint64_t)cum * vol->stripe_unit;
            uint64_t seg_end = ((uint64_t)cum + ph->ratios[j]) * vol->stripe_unit;
            if (offset_in_cycle >= seg_start && offset_in_cycle < seg_end) {
                *out_disk_idx = ph->active_disk_indices[j];
                *out_phys_offset = ph->physical_starts_bytes[j] +
                    cycle_index * (uint64_t)ph->ratios[j] * vol->stripe_unit +
                    (offset_in_cycle - seg_start);
                return true;
            }
            cum += ph->ratios[j];
        }
    }
    return false;
}

bool stripe_volume_normalize_ratios(uint32_t* speeds, uint32_t count, uint32_t* ratios, uint32_t* total_out) {
    if (!speeds || count == 0 || !ratios) return false;
    uint32_t min_speed = speeds[0];
    for (uint32_t i = 1; i < count; i++)
        if (speeds[i] < min_speed) min_speed = speeds[i];
    if (min_speed == 0) { for (uint32_t i = 0; i < count; i++) ratios[i] = 1; *total_out = count; return true; }

    uint32_t raw[MAX_DISKS];
    uint32_t max_raw = 0;
    for (uint32_t i = 0; i < count; i++) {
        raw[i] = speeds[i] / min_speed;
        if (raw[i] > max_raw) max_raw = raw[i];
    }
    if (max_raw == 0) { for (uint32_t i = 0; i < count; i++) ratios[i] = 1; *total_out = count; return true; }

    uint32_t g = raw[0];
    for (uint32_t i = 1; i < count; i++) g = gcd_u32(g, raw[i]);
    if (g == 0) g = 1;

    uint32_t total = 0;
    for (uint32_t i = 0; i < count; i++) { ratios[i] = raw[i] / g; total += ratios[i]; }
    *total_out = total;
    return true;
}

bool stripe_volume_create(STRIPE_VOLUME* vol, DISK_INFO** disks, uint32_t disk_count, uint32_t stripe_unit) {
    if (!vol || !disks || disk_count < MIN_DISKS || disk_count > MAX_DISKS) return false;
    memset(vol, 0, sizeof(STRIPE_VOLUME));
    vol->disk_count = disk_count;
    vol->stripe_unit = stripe_unit;

    uint32_t speeds[MAX_DISKS];
    for (uint32_t i = 0; i < disk_count; i++) {
        vol->disks[i] = disks[i];
        speeds[i] = disks[i]->benchmarked ? disks[i]->bench_write_mbs : disks[i]->write_speed_mbs;
        if (speeds[i] == 0) speeds[i] = 100;
    }

    LOG_INFO("Speed ratios:");
    uint32_t ratios[MAX_DISKS], total_ratio;
    stripe_volume_normalize_ratios(speeds, disk_count, ratios, &total_ratio);
    for (uint32_t i = 0; i < disk_count; i++)
        LOG_INFO("  Disk%u: speed=%u MB/s, ratio=%u", i, speeds[i], ratios[i]);

    uint64_t remaining_phys[MAX_DISKS];
    uint64_t used_phys[MAX_DISKS];
    for (uint32_t i = 0; i < disk_count; i++) {
        remaining_phys[i] = disks[i]->pool_bytes;
        used_phys[i] = 0;
    }

    uint32_t remaining_disk_count = disk_count;
    uint32_t remaining_indices[MAX_DISKS];
    uint32_t remaining_ratios[MAX_DISKS];
    for (uint32_t i = 0; i < disk_count; i++) { remaining_indices[i] = i; remaining_ratios[i] = ratios[i]; }

    vol->phase_count = 0;
    uint64_t virt_offset = 0;

    while (remaining_disk_count > 0 && vol->phase_count < MAX_DISKS) {
        MAPPING_PHASE* ph = &vol->phases[vol->phase_count];
        ph->active_count = remaining_disk_count;
        ph->total_ratio = 0;

        for (uint32_t i = 0; i < remaining_disk_count; i++) {
            ph->active_disk_indices[i] = remaining_indices[i];
            ph->ratios[i] = remaining_ratios[i];
            ph->total_ratio += remaining_ratios[i];
            ph->physical_starts_bytes[i] = used_phys[remaining_indices[i]];
        }

        ph->cycle_size_bytes = (uint64_t)ph->total_ratio * stripe_unit;
        ph->virtual_start_bytes = virt_offset;

        if (remaining_disk_count == 1) {
            uint64_t rem = remaining_phys[remaining_indices[0]];
            if (rem < (uint64_t)stripe_unit) break;
            ph->virtual_size_bytes = rem - (rem % stripe_unit);
            if (ph->virtual_size_bytes == 0) break;
            virt_offset += ph->virtual_size_bytes;
            used_phys[remaining_indices[0]] += ph->virtual_size_bytes;
            remaining_phys[remaining_indices[0]] = 0;
            remaining_disk_count = 0;
        } else {
            uint64_t min_remaining_virtual = UINT64_MAX;
            uint32_t min_idx = UINT32_MAX;
            for (uint32_t i = 0; i < remaining_disk_count; i++) {
                uint32_t di = remaining_indices[i];
                uint64_t max_virtual = (remaining_phys[di] * (uint64_t)ph->total_ratio) / (uint64_t)remaining_ratios[i];
                if (max_virtual < min_remaining_virtual) {
                    min_remaining_virtual = max_virtual;
                    min_idx = i;
                }
            }
            uint64_t cycles = min_remaining_virtual / ph->cycle_size_bytes;
            if (cycles == 0) {
                for (uint32_t i = min_idx; i + 1 < remaining_disk_count; i++) {
                    remaining_indices[i] = remaining_indices[i + 1];
                    remaining_ratios[i] = remaining_ratios[i + 1];
                }
                remaining_disk_count--;
                continue;
            }
            ph->virtual_size_bytes = cycles * ph->cycle_size_bytes;
            virt_offset += ph->virtual_size_bytes;

            uint32_t new_count = 0;
            for (uint32_t i = 0; i < remaining_disk_count; i++) {
                uint32_t di = remaining_indices[i];
                uint64_t used = cycles * (uint64_t)remaining_ratios[i] * stripe_unit;
                used_phys[di] += used;
                remaining_phys[di] -= used;
                if (remaining_phys[di] >= (uint64_t)remaining_ratios[i] * stripe_unit) {
                    remaining_indices[new_count] = di;
                    remaining_ratios[new_count] = remaining_ratios[i];
                    new_count++;
                }
            }
            remaining_disk_count = new_count;
        }
        vol->phase_count++;
    }

    vol->virtual_total_bytes = virt_offset;
    QueryPerformanceCounter(&vol->start_time);

    LOG_OK("Virtual volume created: %llu MB (%llu sectors)",
           (unsigned long long)(virt_offset / (1024 * 1024)),
           (unsigned long long)(virt_offset / SECTOR_SIZE));

    for (uint32_t p = 0; p < vol->phase_count; p++) {
        MAPPING_PHASE* ph = &vol->phases[p];
        LOG_INFO("Phase %u: %u disk(s), ratio sum=%u, virtual=%llu MB",
                 p, ph->active_count, ph->total_ratio,
                 (unsigned long long)(ph->virtual_size_bytes / (1024 * 1024)));
        for (uint32_t j = 0; j < ph->active_count; j++) {
            uint32_t di = ph->active_disk_indices[j];
            uint64_t pool = vol->disks[di]->pool_bytes;
            uint64_t phys_used = (ph->active_count == 1)
                ? ph->virtual_size_bytes
                : (ph->virtual_size_bytes / ph->total_ratio * ph->ratios[j]);
            uint64_t rem_after = pool - ph->physical_starts_bytes[j] - phys_used;
            LOG_INFO("  Disk%u: ratio=%u, phys_start=%llu MB, remaining=%llu MB",
                     di, ph->ratios[j],
                     (unsigned long long)(ph->physical_starts_bytes[j] / (1024 * 1024)),
                     (unsigned long long)(rem_after / (1024 * 1024)));
        }
    }
    return true;
}

/* ---- Expand an existing volume by adding new disks ---- */
bool stripe_volume_expand(STRIPE_VOLUME* vol, DISK_INFO** new_disks, uint32_t new_count) {
    if (!vol || !new_disks || new_count == 0) return false;
    uint32_t old_count = vol->disk_count;
    uint32_t total = old_count + new_count;
    if (total > MAX_DISKS || vol->phase_count + new_count > MAX_DISKS) {
        LOG_ERROR("Expand: max %u disks total", MAX_DISKS);
        return false;
    }

    /* Add new disks to volume */
    for (uint32_t i = 0; i < new_count; i++)
        vol->disks[old_count + i] = new_disks[i];

    /* Compute speed ratios for new disks */
    uint32_t speeds[MAX_DISKS];
    for (uint32_t i = 0; i < new_count; i++) {
        speeds[i] = new_disks[i]->benchmarked ? new_disks[i]->bench_write_mbs : new_disks[i]->write_speed_mbs;
        if (speeds[i] == 0) speeds[i] = 100;
    }

    uint32_t ratios[MAX_DISKS], total_ratio;
    stripe_volume_normalize_ratios(speeds, new_count, ratios, &total_ratio);

    LOG_INFO("Expand: adding %u disks, ratio total=%u", new_count, total_ratio);

    /* Phase computation for new disks (reuses the same algorithm) */
    uint64_t remaining_phys[MAX_DISKS];
    uint64_t used_phys[MAX_DISKS];
    uint32_t local_idx[MAX_DISKS];
    uint32_t local_ratios[MAX_DISKS];

    for (uint32_t i = 0; i < new_count; i++) {
        remaining_phys[i] = new_disks[i]->pool_bytes;
        used_phys[i] = 0;
        local_idx[i] = i;
        local_ratios[i] = ratios[i];
    }

    uint32_t remaining_disk_count = new_count;
    uint32_t phase_base = vol->phase_count;
    uint64_t virt_offset = vol->virtual_total_bytes;

    while (remaining_disk_count > 0 && vol->phase_count < MAX_DISKS) {
        MAPPING_PHASE* ph = &vol->phases[vol->phase_count];
        ph->active_count = remaining_disk_count;
        ph->total_ratio = 0;

        for (uint32_t i = 0; i < remaining_disk_count; i++) {
            ph->active_disk_indices[i] = old_count + local_idx[i];
            ph->ratios[i] = local_ratios[i];
            ph->total_ratio += local_ratios[i];
            ph->physical_starts_bytes[i] = used_phys[local_idx[i]];
        }

        ph->cycle_size_bytes = (uint64_t)ph->total_ratio * vol->stripe_unit;
        ph->virtual_start_bytes = virt_offset;

        if (remaining_disk_count == 1) {
            uint64_t rem = remaining_phys[local_idx[0]];
            if (rem < (uint64_t)vol->stripe_unit) break;
            ph->virtual_size_bytes = rem - (rem % vol->stripe_unit);
            if (ph->virtual_size_bytes == 0) break;
            virt_offset += ph->virtual_size_bytes;
            used_phys[local_idx[0]] += ph->virtual_size_bytes;
            remaining_phys[local_idx[0]] = 0;
            remaining_disk_count = 0;
        } else {
            uint64_t min_remaining_virtual = UINT64_MAX;
            uint32_t min_idx = UINT32_MAX;
            for (uint32_t i = 0; i < remaining_disk_count; i++) {
                uint32_t di = local_idx[i];
                uint64_t max_virtual = (remaining_phys[di] * (uint64_t)ph->total_ratio) / (uint64_t)local_ratios[i];
                if (max_virtual < min_remaining_virtual) {
                    min_remaining_virtual = max_virtual;
                    min_idx = i;
                }
            }
            uint64_t cycles = min_remaining_virtual / ph->cycle_size_bytes;
            if (cycles == 0) {
                for (uint32_t i = min_idx; i + 1 < remaining_disk_count; i++) {
                    local_idx[i] = local_idx[i + 1];
                    local_ratios[i] = local_ratios[i + 1];
                }
                remaining_disk_count--;
                continue;
            }
            ph->virtual_size_bytes = cycles * ph->cycle_size_bytes;
            virt_offset += ph->virtual_size_bytes;

            uint32_t newc = 0;
            for (uint32_t i = 0; i < remaining_disk_count; i++) {
                uint32_t di = local_idx[i];
                uint64_t used = cycles * (uint64_t)local_ratios[i] * vol->stripe_unit;
                used_phys[di] += used;
                remaining_phys[di] -= used;
                if (remaining_phys[di] >= (uint64_t)local_ratios[i] * vol->stripe_unit) {
                    local_idx[newc] = di;
                    local_ratios[newc] = local_ratios[i];
                    newc++;
                }
            }
            remaining_disk_count = newc;
        }
        vol->phase_count++;
    }

    uint64_t added_bytes = virt_offset - vol->virtual_total_bytes;
    vol->disk_count = total;
    vol->virtual_total_bytes = virt_offset;

    LOG_OK("Volume expanded: +%llu MB (%u new phases, %u disks total)",
           (unsigned long long)(added_bytes / (1024 * 1024)),
           vol->phase_count - phase_base, total);

    for (uint32_t p = phase_base; p < vol->phase_count; p++) {
        MAPPING_PHASE* ph = &vol->phases[p];
        LOG_INFO("  New phase %u: %u disk(s), virtual=%llu MB",
                 p, ph->active_count,
                 (unsigned long long)(ph->virtual_size_bytes / (1024 * 1024)));
    }
    return true;
}

void stripe_volume_destroy(STRIPE_VOLUME* vol) {
    if (!vol) return;
    for (uint32_t i = 0; i < vol->disk_count; i++) {
        if (vol->disks[i]->handle != INVALID_HANDLE_VALUE) {
            CloseHandle(vol->disks[i]->handle);
            vol->disks[i]->handle = INVALID_HANDLE_VALUE;
        }
    }
}

bool stripe_volume_map_lba(const STRIPE_VOLUME* vol, uint64_t virtual_offset,
                            IO_MAPPING_ENTRY* entries, uint32_t* out_count, uint32_t request_length) {
    if (!vol || !entries || !out_count || request_length == 0) return false;
    if (virtual_offset >= vol->virtual_total_bytes) return false;
    uint32_t remaining = request_length;
    if (virtual_offset + remaining > vol->virtual_total_bytes)
        remaining = (uint32_t)(vol->virtual_total_bytes - virtual_offset);

    uint64_t current_vo = virtual_offset;
    uint32_t entry_idx = 0;

    while (remaining > 0 && entry_idx < MAX_IO_ENTRIES) {
        uint32_t disk_idx;
        uint64_t phys_offset;
        if (!map_single_byte(vol, current_vo, &disk_idx, &phys_offset)) return false;

        const MAPPING_PHASE* current_phase = &vol->phases[0];
        for (uint32_t p = 1; p < vol->phase_count; p++) {
            if (current_vo >= vol->phases[p].virtual_start_bytes) current_phase = &vol->phases[p];
        }

        uint64_t oip = current_vo - current_phase->virtual_start_bytes;
        uint64_t phase_remain = current_phase->virtual_size_bytes - oip;
        uint64_t chunk;

        if (current_phase->active_count == 1) {
            chunk = phase_remain;
            if (chunk > remaining) chunk = remaining;
            bool merged = false;
            if (entry_idx > 0 &&
                entries[entry_idx - 1].disk_index == disk_idx &&
                entries[entry_idx - 1].physical_offset + entries[entry_idx - 1].length_bytes == phys_offset) {
                entries[entry_idx - 1].length_bytes += (uint32_t)chunk;
                merged = true;
            }
            if (!merged) {
                entries[entry_idx].disk_index = disk_idx;
                entries[entry_idx].physical_offset = phys_offset;
                entries[entry_idx].length_bytes = (uint32_t)chunk;
                entry_idx++;
            }
            current_vo += chunk;
            remaining -= (uint32_t)chunk;
            continue;
        }

        uint64_t oic = oip % current_phase->cycle_size_bytes;
        uint32_t cum = 0;
        uint32_t seg_disk_count = current_phase->active_count;
        uint32_t seg_idx = 0;
        for (uint32_t j = 0; j < seg_disk_count; j++) {
            if (current_phase->active_disk_indices[j] == disk_idx &&
                oic >= (uint64_t)cum * vol->stripe_unit &&
                oic < ((uint64_t)cum + current_phase->ratios[j]) * vol->stripe_unit) {
                seg_idx = j; break;
            }
            cum += current_phase->ratios[j];
        }

        uint64_t seg_end = ((uint64_t)cum + current_phase->ratios[seg_idx]) * vol->stripe_unit;
        uint64_t seg_remain = seg_end - oic;
        chunk = seg_remain;
        if (chunk > phase_remain) chunk = phase_remain;
        if (chunk > remaining) chunk = remaining;

        bool merged = false;
        if (entry_idx > 0 &&
            entries[entry_idx - 1].disk_index == disk_idx &&
            entries[entry_idx - 1].physical_offset + entries[entry_idx - 1].length_bytes == phys_offset) {
            entries[entry_idx - 1].length_bytes += (uint32_t)chunk;
            merged = true;
        }
        if (!merged) {
            entries[entry_idx].disk_index = disk_idx;
            entries[entry_idx].physical_offset = phys_offset;
            entries[entry_idx].length_bytes = (uint32_t)chunk;
            entry_idx++;
        }
        current_vo += chunk;
        remaining -= (uint32_t)chunk;
    }
    *out_count = entry_idx;
    return (remaining == 0);
}

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

bool stripe_volume_read(STRIPE_VOLUME* vol, void* buffer, uint64_t virtual_offset, uint32_t length) {
    uint32_t ps = 0;
    if (!vol || !buffer || length == 0) return false;
    if (virtual_offset + length > vol->virtual_total_bytes) return false;
    profiler_read_begin(&ps);
    if (vol->raid_level == RAID_LEVEL_MIRROR) {
        bool ok = mirror_volume_read(vol, buffer, virtual_offset, length);
        if (ok) vol->bytes_read += length;
        profiler_read_end(ps, ok ? length : 0);
        return ok;
    }
    if (vol->cache_enabled && !vol->cache_flush_in_progress &&
        virtual_offset + length <= vol->cache.cache_size_bytes) {
        bool ok = cache_read(&vol->cache, buffer, virtual_offset, length);
        if (ok) vol->bytes_read += length;
        profiler_read_end(ps, ok ? length : 0);
        return ok;
    }
    IO_MAPPING_ENTRY entries[MAX_IO_ENTRIES];
    uint32_t entry_count = 0;
    if (!stripe_volume_map_lba(vol, virtual_offset, entries, &entry_count, length)) {
        profiler_read_end(ps, 0);
        return false;
    }

    uint8_t* buf = (uint8_t*)buffer;
    OVERLAPPED ovs[MAX_IO_ENTRIES];
    HANDLE events[MAX_IO_ENTRIES];
    bool ok = true;

    for (uint32_t i = 0; i < entry_count; i++) {
        DISK_INFO* disk = vol->disks[entries[i].disk_index];
        HANDLE evt = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (!evt) {
            LOG_ERROR("CreateEventW failed at disk %u (read)", entries[i].disk_index);
            for (uint32_t j = 0; j < i; j++) {
                CancelIoEx(vol->disks[entries[j].disk_index]->handle, &ovs[j]);
                CloseHandle(events[j]);
            }
            profiler_read_end(ps, 0);
            return false;
        }
        events[i] = evt;
        memset(&ovs[i], 0, sizeof(OVERLAPPED));
        ovs[i].Offset = (DWORD)(entries[i].physical_offset & 0xFFFFFFFF);
        ovs[i].OffsetHigh = (DWORD)((entries[i].physical_offset >> 32) & 0xFFFFFFFF);
        ovs[i].hEvent = evt;
        DWORD read_bytes = 0;
        if (!ReadFile(disk->handle, buf, entries[i].length_bytes, &read_bytes, &ovs[i])) {
            if (GetLastError() != ERROR_IO_PENDING) {
                LOG_ERROR("Read failed on disk %u (async)", entries[i].disk_index);
                CloseHandle(evt);
                for (uint32_t j = 0; j < i; j++) {
                    CancelIoEx(vol->disks[entries[j].disk_index]->handle, &ovs[j]);
                    CloseHandle(events[j]);
                }
                profiler_read_end(ps, 0);
                return false;
            }
        }
        buf += entries[i].length_bytes;
    }

    for (uint32_t i = 0; i < entry_count; i++) {
        DWORD transferred = 0;
        if (!GetOverlappedResult(vol->disks[entries[i].disk_index]->handle, &ovs[i], &transferred, TRUE)) {
            LOG_ERROR("Read completion failed on disk %u", entries[i].disk_index);
            ok = false;
        }
        CloseHandle(events[i]);
    }

    vol->bytes_read += ok ? length : 0;
    profiler_read_end(ps, ok ? length : 0);
    return ok;
}

bool stripe_volume_write(STRIPE_VOLUME* vol, const void* buffer, uint64_t virtual_offset, uint32_t length) {
    uint32_t ps = 0;
    if (!vol || !buffer || length == 0) return false;
    if (virtual_offset + length > vol->virtual_total_bytes) return false;
    profiler_write_begin(&ps);
    if (vol->raid_level == RAID_LEVEL_MIRROR) {
        bool ok = mirror_volume_write(vol, buffer, virtual_offset, length);
        if (ok) vol->bytes_written += length;
        profiler_write_end(ps, ok ? length : 0);
        return ok;
    }
    if (vol->cache_enabled && !vol->cache_flush_in_progress &&
        virtual_offset + length <= vol->cache.cache_size_bytes) {
        bool ok = cache_write(&vol->cache, buffer, virtual_offset, length);
        if (ok && vol->cache.write_through) {
            IO_MAPPING_ENTRY entries[MAX_IO_ENTRIES];
            uint32_t entry_count = 0;
            if (stripe_volume_map_lba(vol, virtual_offset, entries, &entry_count, length)) {
                const uint8_t* buf = (const uint8_t*)buffer;
                OVERLAPPED ovs[MAX_IO_ENTRIES];
                HANDLE events[MAX_IO_ENTRIES];
                memset(events, 0, sizeof(events));
                bool wt_ok = true;
                for (uint32_t i = 0; i < entry_count; i++) {
                    DISK_INFO* disk = vol->disks[entries[i].disk_index];
                    HANDLE evt = CreateEventW(NULL, TRUE, FALSE, NULL);
                    if (!evt) {
                        for (uint32_t j = 0; j < i; j++) {
                            if (events[j]) CancelIoEx(vol->disks[entries[j].disk_index]->handle, &ovs[j]);
                            CloseHandle(events[j]);
                        }
                        wt_ok = false; break;
                    }
                    events[i] = evt;
                    memset(&ovs[i], 0, sizeof(OVERLAPPED));
                    ovs[i].Offset = (DWORD)(entries[i].physical_offset & 0xFFFFFFFF);
                    ovs[i].OffsetHigh = (DWORD)((entries[i].physical_offset >> 32) & 0xFFFFFFFF);
                    ovs[i].hEvent = evt;
                    DWORD written = 0;
                    if (!WriteFile(disk->handle, buf, entries[i].length_bytes, &written, &ovs[i])) {
                        if (GetLastError() != ERROR_IO_PENDING) {
                            CloseHandle(evt); events[i] = NULL; wt_ok = false; break;
                        }
                    }
                    buf += entries[i].length_bytes;
                }
                if (wt_ok) {
                    for (uint32_t i = 0; i < entry_count; i++) {
                        DWORD transferred = 0;
                        if (!GetOverlappedResult(vol->disks[entries[i].disk_index]->handle, &ovs[i], &transferred, TRUE))
                            wt_ok = false;
                        CloseHandle(events[i]); events[i] = NULL;
                    }
                }
                if (!wt_ok) {
                    LOG_WARN("Write-through flush failed at offset %llu", (unsigned long long)virtual_offset);
                    for (uint32_t i = 0; i < entry_count; i++) {
                        if (events[i]) { CancelIoEx(vol->disks[entries[i].disk_index]->handle, &ovs[i]); CloseHandle(events[i]); }
                    }
                    ok = false;
                }
            }
        }
        if (ok) vol->bytes_written += length;
        profiler_write_end(ps, ok ? length : 0);
        return ok;
    }
    IO_MAPPING_ENTRY entries[MAX_IO_ENTRIES];
    uint32_t entry_count = 0;
    if (!stripe_volume_map_lba(vol, virtual_offset, entries, &entry_count, length)) {
        profiler_write_end(ps, 0);
        return false;
    }

    const uint8_t* buf = (const uint8_t*)buffer;
    OVERLAPPED ovs[MAX_IO_ENTRIES];
    HANDLE events[MAX_IO_ENTRIES];
    memset(events, 0, sizeof(events));
    bool ok = true;

    for (uint32_t i = 0; i < entry_count; i++) {
        DISK_INFO* disk = vol->disks[entries[i].disk_index];
        HANDLE evt = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (!evt) {
            LOG_ERROR("CreateEventW failed at disk %u (write)", entries[i].disk_index);
            for (uint32_t j = 0; j < i; j++) {
                CancelIoEx(vol->disks[entries[j].disk_index]->handle, &ovs[j]);
                CloseHandle(events[j]);
            }
            profiler_write_end(ps, 0);
            return false;
        }
        events[i] = evt;
        memset(&ovs[i], 0, sizeof(OVERLAPPED));
        ovs[i].Offset = (DWORD)(entries[i].physical_offset & 0xFFFFFFFF);
        ovs[i].OffsetHigh = (DWORD)((entries[i].physical_offset >> 32) & 0xFFFFFFFF);
        ovs[i].hEvent = evt;
        DWORD written = 0;
        if (!WriteFile(disk->handle, buf, entries[i].length_bytes, &written, &ovs[i])) {
            if (GetLastError() != ERROR_IO_PENDING) {
                LOG_ERROR("Write failed on disk %u (async)", entries[i].disk_index);
                CloseHandle(evt);
                for (uint32_t j = 0; j < i; j++) {
                    CancelIoEx(vol->disks[entries[j].disk_index]->handle, &ovs[j]);
                    CloseHandle(events[j]);
                }
                profiler_write_end(ps, 0);
                return false;
            }
        }
        buf += entries[i].length_bytes;
    }

    for (uint32_t i = 0; i < entry_count; i++) {
        DWORD transferred = 0;
        if (!GetOverlappedResult(vol->disks[entries[i].disk_index]->handle, &ovs[i], &transferred, TRUE)) {
            LOG_ERROR("Write completion failed on disk %u", entries[i].disk_index);
            ok = false;
        }
        CloseHandle(events[i]);
    }

    vol->bytes_written += ok ? length : 0;
    profiler_write_end(ps, ok ? length : 0);
    return ok;
}

bool stripe_volume_dump_mapping(const STRIPE_VOLUME* vol, uint64_t start, uint64_t end) {
    if (!vol || start >= end) return false;
    if (start > vol->virtual_total_bytes) start = 0;
    if (end > vol->virtual_total_bytes) end = vol->virtual_total_bytes;
    LOG_INFO("Mapping table dump [%llu MB, %llu MB):",
             (unsigned long long)(start / (1024 * 1024)),
             (unsigned long long)(end / (1024 * 1024)));
    log_printf(LOG_LEVEL_INFO, "  %-20s %-14s %-20s", "Virtual Offset", "Disk", "Physical Offset");
    uint64_t step = vol->stripe_unit / 4;
    if (step < SECTOR_SIZE) step = SECTOR_SIZE;
    for (uint64_t vo = start; vo < end; vo += step) {
        if (vo > vol->virtual_total_bytes) break;
        uint32_t di; uint64_t po;
        if (map_single_byte(vol, vo, &di, &po)) {
            double vmb = (double)vo / (1024.0 * 1024.0);
            double pmb = (double)po / (1024.0 * 1024.0);
            log_printf(LOG_LEVEL_INFO, "  %12.2f MB       Disk %u     %12.2f MB", vmb, di, pmb);
        }
    }
    return true;
}

bool stripe_volume_verify_io(STRIPE_VOLUME* vol) {
    if (!vol) return false;
    uint64_t test_size = vol->virtual_total_bytes;
    if (test_size > 64ULL * 1024 * 1024) test_size = 64ULL * 1024 * 1024;
    LOG_INFO("Running IO verification: %llu MB ...",
             (unsigned long long)(test_size / (1024 * 1024)));
    uint8_t* write_buf = (uint8_t*)VirtualAlloc(NULL, test_size, MEM_COMMIT, PAGE_READWRITE);
    uint8_t* read_buf = (uint8_t*)VirtualAlloc(NULL, test_size, MEM_COMMIT, PAGE_READWRITE);
    if (!write_buf || !read_buf) { VirtualFree(write_buf, 0, MEM_RELEASE); VirtualFree(read_buf, 0, MEM_RELEASE); return false; }
    for (uint64_t i = 0; i < test_size; i++) write_buf[i] = (uint8_t)(i & 0xFF);
    LARGE_INTEGER freq, start, end;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    bool write_ok = stripe_volume_write(vol, write_buf, 0, (uint32_t)test_size);
    QueryPerformanceCounter(&end);
    double write_sec = (double)(end.QuadPart - start.QuadPart) / freq.QuadPart;
    double write_mbs = write_sec > 0 ? (test_size / (1024.0 * 1024.0)) / write_sec : 0;
    memset(read_buf, 0, test_size);
    QueryPerformanceCounter(&start);
    bool read_ok = stripe_volume_read(vol, read_buf, 0, (uint32_t)test_size);
    QueryPerformanceCounter(&end);
    double read_sec = (double)(end.QuadPart - start.QuadPart) / freq.QuadPart;
    double read_mbs = read_sec > 0 ? (test_size / (1024.0 * 1024.0)) / read_sec : 0;
    bool match = write_ok && read_ok && (memcmp(write_buf, read_buf, test_size) == 0);
    LOG_OK("Write: %.0f MB/s | Read: %.0f MB/s | Verify: %s",
           write_mbs, read_mbs, match ? "PASS" : "FAIL");
    VirtualFree(write_buf, 0, MEM_RELEASE);
    VirtualFree(read_buf, 0, MEM_RELEASE);
    return match;
}

bool stripe_volume_random_test(STRIPE_VOLUME* vol, uint32_t operations, uint32_t max_size_kb) {
    if (!vol || operations == 0) return false;
    uint64_t vmax = vol->virtual_total_bytes;
    if (vmax < 65536) { LOG_ERROR("Volume too small for random test"); return false; }
    uint32_t max_size = max_size_kb * 1024;
    if (max_size < 4096) max_size = 4096;
    if (max_size > 1024 * 1024) max_size = 1024 * 1024;
    LOG_INFO("Running random I/O: %u ops, max block=%u KB ...", operations, max_size / 1024);
    uint8_t* write_buf = (uint8_t*)VirtualAlloc(NULL, max_size, MEM_COMMIT, PAGE_READWRITE);
    uint8_t* read_buf = (uint8_t*)VirtualAlloc(NULL, max_size, MEM_COMMIT, PAGE_READWRITE);
    if (!write_buf || !read_buf) { VirtualFree(write_buf, 0, MEM_RELEASE); VirtualFree(read_buf, 0, MEM_RELEASE); return false; }
    uint32_t seed = (uint32_t)(vol->start_time.LowPart);
    srand(seed);
    LARGE_INTEGER freq, t1, t2;
    QueryPerformanceFrequency(&freq);
    uint64_t total_written = 0, total_read = 0;
    uint32_t passed = 0, failed = 0;
    QueryPerformanceCounter(&t1);
    for (uint32_t op = 0; op < operations; op++) {
        uint32_t size = 4096 + (rand() % (max_size - 4095));
        size -= (size % 512);
        uint64_t max_off = vmax - size;
        uint64_t off = (uint64_t)rand() * (uint64_t)rand() % max_off;
        off -= (off % 512);
        for (uint32_t i = 0; i < size; i++) write_buf[i] = (uint8_t)((off + i) & 0xFF);
        if (!stripe_volume_write(vol, write_buf, off, size)) { failed++; continue; }
        total_written += size;
        memset(read_buf, 0, size);
        if (!stripe_volume_read(vol, read_buf, off, size)) { failed++; continue; }
        total_read += size;
        if (memcmp(write_buf, read_buf, size) == 0) passed++;
        else failed++;
    }
    QueryPerformanceCounter(&t2);
    double sec = (double)(t2.QuadPart - t1.QuadPart) / freq.QuadPart;
    double wr_mbs = sec > 0 ? (total_written / (1024.0 * 1024.0)) / sec : 0;
    double rd_mbs = sec > 0 ? (total_read / (1024.0 * 1024.0)) / sec : 0;
    LOG_OK("Random test: %u/%u passed (%.0f MB/s W, %.0f MB/s R)", passed, passed + failed, wr_mbs, rd_mbs);
    VirtualFree(write_buf, 0, MEM_RELEASE);
    VirtualFree(read_buf, 0, MEM_RELEASE);
    return (failed == 0);
}