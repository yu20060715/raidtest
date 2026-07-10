#include "stripe_engine.h"
#include "mirror_engine.h"
#include "storage_common.h"
#include "ram_cache.h"
#include "profiler.h"

/* ===================================================================
 * Internal types — per-disk worker threads with lock-free SPSC ring
 * =================================================================== */

#define IO_RING_SIZE 64

typedef enum { IO_OP_READ = 0, IO_OP_WRITE } IO_OP_TYPE;

typedef struct IO_COMPLETION IO_COMPLETION;
struct IO_COMPLETION {
    volatile LONG pending;
    bool          error;
    HANDLE        event;
};

typedef struct {
    IO_OP_TYPE    op;
    void*         buffer;
    uint64_t      physical_offset;
    uint32_t      length;
    IO_COMPLETION*completion;
} IO_REQUEST;

typedef struct {
    IO_REQUEST    entries[IO_RING_SIZE];
    volatile LONG write_seq;
    volatile LONG read_seq;
} IO_RING;

typedef struct {
    HANDLE        thread;
    HANDLE        wake_event;
    HANDLE        io_event;
    CRITICAL_SECTION push_lock;  // protects ring_push from concurrent callers
    DISK_INFO*    disk;
    IO_RING       ring;
    OVERLAPPED    ov;
    volatile bool stop;
    uint32_t      disk_index;
} DISK_WORKER;

/* ---- Multi-producer ring buffer operations ----------------------- */

static bool ring_push(DISK_WORKER* w, const IO_REQUEST* req) {
    IO_RING* ring = &w->ring;
    EnterCriticalSection(&w->push_lock);
    LONG ws = ring->write_seq;
    LONG rs = ring->read_seq;
    bool ok = (ws - rs < IO_RING_SIZE);
    if (ok) {
        ring->entries[ws & (IO_RING_SIZE - 1)] = *req;
        ring->write_seq = ws + 1;
    }
    LeaveCriticalSection(&w->push_lock);
    return ok;
}

static IO_REQUEST* ring_pop(IO_RING* ring) {
    LONG rs = ring->read_seq;
    if (ring->write_seq == rs) return NULL;
    LONG slot = rs & (IO_RING_SIZE - 1);
    IO_REQUEST* req = &ring->entries[slot];
    ring->read_seq = rs + 1;
    return req;
}

/* ---- Per-disk worker thread -------------------------------------- */

static unsigned int __stdcall disk_worker_thread(void* arg) {
    DISK_WORKER* w = (DISK_WORKER*)arg;
    DWORD transferred;

    while (!w->stop) {
        IO_REQUEST* req = ring_pop(&w->ring);
        if (!req) {
            WaitForSingleObject(w->wake_event, 100);
            continue;
        }

        w->ov.Offset     = (DWORD)(req->physical_offset & 0xFFFFFFFF);
        w->ov.OffsetHigh = (DWORD)((req->physical_offset >> 32) & 0xFFFFFFFF);
        w->ov.hEvent     = w->io_event;

        BOOL ok;
        if (req->op == IO_OP_WRITE) {
            ok = WriteFile(w->disk->handle, req->buffer, req->length, NULL, &w->ov);
        } else {
            ok = ReadFile(w->disk->handle, req->buffer, req->length, NULL, &w->ov);
        }

        if (!ok) {
            if (GetLastError() != ERROR_IO_PENDING) {
                InterlockedExchange(&w->disk->faulty, 1);
                InterlockedExchange(&w->disk->healthy, 0);
                if (req->completion) {
                    req->completion->error = true;
                    if (InterlockedDecrement(&req->completion->pending) == 0)
                        SetEvent(req->completion->event);
                }
                continue;
            }
            ok = GetOverlappedResult(w->disk->handle, &w->ov, &transferred, TRUE);
        }

        if (!ok) {
            InterlockedExchange(&w->disk->faulty, 1);
            InterlockedExchange(&w->disk->healthy, 0);
            if (req->completion) {
                req->completion->error = true;
                if (InterlockedDecrement(&req->completion->pending) == 0)
                    SetEvent(req->completion->event);
            }
            continue;
        }

        if (req->completion) {
            if (InterlockedDecrement(&req->completion->pending) == 0)
                SetEvent(req->completion->event);
        }
    }

    return 0;
}

/* ---- Worker lifecycle -------------------------------------------- */

bool stripe_volume_workers_init(STRIPE_VOLUME* vol) {
    if (!vol || vol->disk_count == 0) return false;

    DISK_WORKER* arr = (DISK_WORKER*)calloc(vol->disk_count, sizeof(DISK_WORKER));
    if (!arr) return false;

    for (uint32_t i = 0; i < vol->disk_count; i++) {
        DISK_WORKER* w = &arr[i];
        w->disk        = vol->disks[i];
        w->disk_index  = i;
        InitializeCriticalSection(&w->push_lock);
        w->wake_event  = CreateEventW(NULL, FALSE, FALSE, NULL);
        w->io_event    = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (!w->wake_event || !w->io_event) {
            LOG_ERROR("Worker %u: event creation failed", i);
            DeleteCriticalSection(&w->push_lock);
            if (w->wake_event) CloseHandle(w->wake_event);
            if (w->io_event)   CloseHandle(w->io_event);
            for (uint32_t j = 0; j < i; j++) {
                arr[j].stop = true;
                SetEvent(arr[j].wake_event);
                WaitForSingleObject(arr[j].thread, INFINITE);
                CloseHandle(arr[j].thread);
                DeleteCriticalSection(&arr[j].push_lock);
                CloseHandle(arr[j].wake_event);
                CloseHandle(arr[j].io_event);
            }
            free(arr);
            return false;
        }
        w->thread = (HANDLE)_beginthreadex(NULL, 0, disk_worker_thread, w,
                                           0, NULL);
        if (!w->thread) {
            LOG_ERROR("Worker %u: thread creation failed", i);
            DeleteCriticalSection(&w->push_lock);
            CloseHandle(w->wake_event);
            CloseHandle(w->io_event);
            for (uint32_t j = 0; j < i; j++) {
                arr[j].stop = true;
                SetEvent(arr[j].wake_event);
                WaitForSingleObject(arr[j].thread, INFINITE);
                CloseHandle(arr[j].thread);
                DeleteCriticalSection(&arr[j].push_lock);
                CloseHandle(arr[j].wake_event);
                CloseHandle(arr[j].io_event);
            }
            free(arr);
            return false;
        }
    }

    vol->worker_state = arr;
    return true;
}

void stripe_volume_workers_stop(STRIPE_VOLUME* vol) {
    if (!vol || !vol->worker_state) return;
    DISK_WORKER* arr = (DISK_WORKER*)vol->worker_state;

    for (uint32_t i = 0; i < vol->disk_count; i++) {
        arr[i].stop = true;
        SetEvent(arr[i].wake_event);
    }
    for (uint32_t i = 0; i < vol->disk_count; i++) {
        if (arr[i].thread) {
            WaitForSingleObject(arr[i].thread, INFINITE);
            CloseHandle(arr[i].thread);
        }
        DeleteCriticalSection(&arr[i].push_lock);
        if (arr[i].wake_event) CloseHandle(arr[i].wake_event);
        if (arr[i].io_event)   CloseHandle(arr[i].io_event);
    }

    free(arr);
    vol->worker_state = NULL;
}

/* ---- Submit mapped entries to per-disk workers and wait ---------- */

typedef struct {
    uint64_t buffer_offset;
    uint32_t disk_index;
    uint64_t physical_offset;
    uint32_t length;
} SUBMIT_ENTRY;

static bool submit_entries(STRIPE_VOLUME* vol,
                           const IO_MAPPING_ENTRY* entries,
                           uint32_t entry_count,
                           const uint8_t* buffer,
                           IO_OP_TYPE op) {
    if (entry_count == 0) return true;
    if (!vol->worker_state) return false;

    DISK_WORKER* arr = (DISK_WORKER*)vol->worker_state;

    IO_COMPLETION comp;
    comp.pending = (LONG)entry_count;
    comp.error   = false;
    comp.event   = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!comp.event) return false;

    /* Pre‑compute cumulative buffer offsets for each entry */
    SUBMIT_ENTRY se[IO_RING_SIZE];
    uint64_t cum = 0;
    for (uint32_t i = 0; i < entry_count; i++) {
        se[i].buffer_offset   = cum;
        se[i].disk_index      = entries[i].disk_index;
        se[i].physical_offset = entries[i].physical_offset;
        se[i].length          = entries[i].length_bytes;
        cum                  += entries[i].length_bytes;
    }

    /* Push all entries to their respective disk rings */
    for (uint32_t i = 0; i < entry_count; i++) {
        uint32_t di = se[i].disk_index;
        if (di >= vol->disk_count) {
            comp.error = true;
            InterlockedDecrement(&comp.pending);
            continue;
        }

        IO_REQUEST req;
        req.op              = op;
        req.buffer          = (void*)(buffer + se[i].buffer_offset);
        req.physical_offset = se[i].physical_offset;
        req.length          = se[i].length;
        req.completion      = &comp;

        while (!ring_push(&arr[di], &req)) Sleep(0);
        SetEvent(arr[di].wake_event);
    }

    /* Wait for all workers to finish */
    WaitForSingleObject(comp.event, INFINITE);
    CloseHandle(comp.event);

    return !comp.error;
}

/* ===================================================================
 * Existing stripe_engine functions (unchanged except destroy/write/read)
 * =================================================================== */

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

#define MAX_RATIO 32u

bool stripe_volume_normalize_ratios(uint32_t* speeds, uint32_t count, uint32_t* ratios, uint32_t* total_out) {
    if (!speeds || count == 0 || !ratios) return false;

    uint32_t max_speed = speeds[0];
    for (uint32_t i = 1; i < count; i++)
        if (speeds[i] > max_speed) max_speed = speeds[i];
    if (max_speed == 0) { for (uint32_t i = 0; i < count; i++) ratios[i] = 1; *total_out = count; return true; }

    uint32_t raw[MAX_DISKS];
    uint64_t half = (uint64_t)max_speed / 2;
    for (uint32_t i = 0; i < count; i++) {
        uint64_t num = (uint64_t)speeds[i] * MAX_RATIO + half;
        raw[i] = (uint32_t)(num / max_speed);
        if (raw[i] == 0) raw[i] = 1;
    }

    uint32_t g = raw[0];
    for (uint32_t i = 1; i < count; i++) g = gcd_u32(g, raw[i]);
    if (g == 0) g = 1;

    uint32_t total = 0;
    for (uint32_t i = 0; i < count; i++) { ratios[i] = raw[i] / g; total += ratios[i]; }
    *total_out = total;
    return true;
}

#undef MAX_RATIO

bool stripe_volume_create(STRIPE_VOLUME* vol, DISK_INFO** disks, uint32_t disk_count, uint32_t stripe_unit) {
    if (!vol || !disks || disk_count < MIN_DISKS || disk_count > MAX_DISKS) return false;
    memset(vol, 0, sizeof(STRIPE_VOLUME));
    vol->raid_level = RAID_LEVEL_STRIPE;
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
    /* Stop workers before closing handles */
    stripe_volume_workers_stop(vol);
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
    uint64_t remaining = request_length;
    if (virtual_offset + remaining > vol->virtual_total_bytes)
        remaining = vol->virtual_total_bytes - virtual_offset;

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

/* ---- Read with per-disk workers ---------------------------------- */

bool stripe_volume_read(STRIPE_VOLUME* vol, void* buffer, uint64_t virtual_offset, uint32_t length) {
    uint32_t ps = 0;
    if (!vol || !buffer || length == 0) return false;
    if (virtual_offset + length > vol->virtual_total_bytes) return false;
    CHECK_DISKS(vol);
    profiler_read_begin(&ps);

    if (vol->raid_level == RAID_LEVEL_MIRROR) {
        bool ok = mirror_volume_read(vol, buffer, virtual_offset, length);
        if (ok) InterlockedExchangeAdd64((volatile LONG64*)&vol->bytes_read, length);
        profiler_read_end(ps, ok ? length : 0);
        return ok;
    }

    if (vol->cache_enabled &&
        virtual_offset + length <= vol->cache.cache_size_bytes) {
        bool ok = cache_read(&vol->cache, buffer, virtual_offset, length);
        if (ok) {
            InterlockedExchangeAdd64((volatile LONG64*)&vol->bytes_read, length);
            profiler_read_end(ps, ok ? length : 0);
            return ok;
        }
    }

    IO_MAPPING_ENTRY entries[MAX_IO_ENTRIES];
    uint32_t entry_count = 0;
    if (!stripe_volume_map_lba(vol, virtual_offset, entries, &entry_count, length)) {
        profiler_read_end(ps, 0);
        return false;
    }

    bool ok = submit_entries(vol, entries, entry_count, (const uint8_t*)buffer, IO_OP_READ);
    if (ok) InterlockedExchangeAdd64((volatile LONG64*)&vol->bytes_read, length);
    profiler_read_end(ps, ok ? length : 0);
    return ok;
}

/* ---- Write with per-disk workers --------------------------------- */

bool stripe_volume_write(STRIPE_VOLUME* vol, const void* buffer, uint64_t virtual_offset, uint32_t length) {
    uint32_t ps = 0;
    if (!vol || !buffer || length == 0) return false;
    if (virtual_offset + length > vol->virtual_total_bytes) return false;
    CHECK_DISKS(vol);
    profiler_write_begin(&ps);

    if (vol->raid_level == RAID_LEVEL_MIRROR) {
        bool ok = mirror_volume_write(vol, buffer, virtual_offset, length);
        if (ok) InterlockedExchangeAdd64((volatile LONG64*)&vol->bytes_written, length);
        profiler_write_end(ps, ok ? length : 0);
        return ok;
    }

    /* Cached path — unchanged logic, only the write‑through flush
     * now uses per‑disk workers via submit_entries. */
    if (vol->cache_enabled && !vol->cache_flush_in_progress &&
        virtual_offset + length <= vol->cache.cache_size_bytes) {
        bool ok = false;
        if (vol->cache.write_through) {
            IO_MAPPING_ENTRY entries[MAX_IO_ENTRIES];
            uint32_t entry_count = 0;
            if (stripe_volume_map_lba(vol, virtual_offset, entries, &entry_count, length)) {
                ok = submit_entries(vol, entries, entry_count,
                                    (const uint8_t*)buffer, IO_OP_WRITE);
                if (ok)
                    ok = cache_write(&vol->cache, buffer, virtual_offset, length);
            }
        } else {
            ok = cache_write(&vol->cache, buffer, virtual_offset, length);
        }
        if (ok) InterlockedExchangeAdd64((volatile LONG64*)&vol->bytes_written, length);
        profiler_write_end(ps, ok ? length : 0);
        return ok;
    }

    /* Direct I/O path */
    IO_MAPPING_ENTRY entries[MAX_IO_ENTRIES];
    uint32_t entry_count = 0;
    if (!stripe_volume_map_lba(vol, virtual_offset, entries, &entry_count, length)) {
        profiler_write_end(ps, 0);
        return false;
    }

    bool ok = submit_entries(vol, entries, entry_count, (const uint8_t*)buffer, IO_OP_WRITE);
    if (ok) InterlockedExchangeAdd64((volatile LONG64*)&vol->bytes_written, length);
    profiler_write_end(ps, ok ? length : 0);
    return ok;
}

/* ---- Debug / test helpers (unchanged) ---------------------------- */

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
