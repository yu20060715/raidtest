#include "raid_service.h"
#include "device_manager.h"
#include "stripe_engine.h"
#include "planner_engine.h"
#include "metadata_manager.h"
#include "superblock.h"
#include "config.h"

static APP_STATE* S(void) { return &g_state; }

/* ---- Info / Status / Display ---- */

RC raid_info(void) {
    gs_lock();
    LOG_INFO("=== RAIDTEST Status ===  State: %s", raid_state_str(S()->rt.state));
    uint32_t n = device_get_count();
    if (n > 0) {
        LOG_INFO("--- Physical disks (%u) ---", n);
        for (uint32_t i = 0; i < n; i++) {
            DISK_INFO* d = device_get(i);
            char model_a[MAX_MODEL_LEN] = {0}; wcstombs(model_a, d->model, MAX_MODEL_LEN - 1);
            char drive_a[32] = {0}; wcstombs(drive_a, d->drive_letter, 31);
            LOG_INFO("  [%02u] %s | %s | %s | %s | %s",
                     i, model_a, drive_a,
                     d->serial_number[0] ? d->serial_number : "(no serial)",
                     d->selected ? "SELECTED" : "",
                     d->benchmarked ? "BENCHED" : "");
        }
    } else {
        LOG_INFO("No physical disks scanned. Type 'scan' to discover.");
    }
    if (!S()->vol.volume_valid) { LOG_INFO("No virtual volume."); gs_unlock(); return RC_OK; }
    STRIPE_VOLUME* vol = &S()->vol.volume;
    LOG_INFO("--- Virtual Volume ---");
    LOG_INFO("RAID level: %s", vol->raid_level == RAID_LEVEL_MIRROR ? "Mirror (RAID1)" : "Stripe (RAID0)");
    LOG_INFO("Volume disks: %u (healthy: %u)", vol->disk_count, (uint32_t)vol->healthy_count);
    for (uint32_t i = 0; i < vol->disk_count; i++) {
        char model_a[MAX_MODEL_LEN] = {0}; wcstombs(model_a, vol->disks[i]->model, MAX_MODEL_LEN - 1);
        char path_a[MAX_DRIVE_PATH] = {0}; wcstombs(path_a, vol->disks[i]->file_path, MAX_DRIVE_PATH - 1);
        uint32_t spd = vol->disks[i]->benchmarked ? vol->disks[i]->bench_write_mbs : vol->disks[i]->write_speed_mbs;
        LONG healthy = InterlockedCompareExchange(&vol->disks[i]->healthy, 1, 1);
        LOG_INFO("  Disk%u: %s | %s | %u MB/s W | pool=%llu MB [%s] | SN: %s",
                 i, model_a, path_a, spd, (unsigned long long)(vol->disks[i]->pool_bytes / (1024 * 1024)),
                 healthy ? (vol->disks[i]->faulty ? "FAULTY" : "OK") : "DEGRADED",
                 vol->disks[i]->serial_number);
    }
    if (vol->raid_level == RAID_LEVEL_STRIPE) {
        LOG_INFO("Stripe unit: %u KB", vol->stripe_unit / 1024);
        LOG_INFO("Phases: %u", vol->phase_count);
    }
    LOG_INFO("Virtual capacity: %.1f GB (%llu bytes)", (double)vol->virtual_total_bytes / (1024.0 * 1024.0 * 1024.0), (unsigned long long)vol->virtual_total_bytes);
    uint32_t dirty = 0;
    if (vol->cache.block_count > 0) {
        EnterCriticalSection(&vol->cache.lock);
        for (uint32_t b = 0; b < vol->cache.block_count; b++)
            if (vol->cache.dirty_map[b / 8] & (1 << (b % 8))) dirty++;
        LeaveCriticalSection(&vol->cache.lock);
    }
    double dirty_ratio = vol->cache.block_count > 0 ? (double)dirty / vol->cache.block_count * 100.0 : 0;
    LOG_INFO("Cache: %s (%u MB, dirty=%.1f%%)", vol->cache_enabled ? "ON" : "OFF", S()->cache.cache_mb, dirty_ratio);
    if (vol->cache_enabled) {
        double hit_rate = (vol->cache.hit_count + vol->cache.miss_count) > 0 ? (double)vol->cache.hit_count / (vol->cache.hit_count + vol->cache.miss_count) * 100.0 : 0;
        LOG_INFO("  Cache hit rate: %.1f%%  (hits=%llu, misses=%llu)", hit_rate, (unsigned long long)vol->cache.hit_count, (unsigned long long)vol->cache.miss_count);
    }
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now); QueryPerformanceFrequency(&freq);
    double elapsed = (double)(now.QuadPart - vol->start_time.QuadPart) / freq.QuadPart;
    LOG_INFO("Runtime: %.0f s | Written: %llu MB (%.0f MB/s) | Read: %llu MB (%.0f MB/s)", elapsed,
             (unsigned long long)(vol->bytes_written / (1024 * 1024)), elapsed > 0 ? (vol->bytes_written / (1024.0*1024.0)) / elapsed : 0,
             (unsigned long long)(vol->bytes_read / (1024 * 1024)), elapsed > 0 ? (vol->bytes_read / (1024.0*1024.0)) / elapsed : 0);
    if (vol->raid_level == RAID_LEVEL_STRIPE) {
        for (uint32_t p = 0; p < vol->phase_count; p++) {
            MAPPING_PHASE* ph = &vol->phases[p];
            LOG_INFO("Phase %u: [%.1f GB - %.1f GB]  %u disk(s)  ratio=%u", p,
                     (double)ph->virtual_start_bytes / (1024.0*1024.0*1024.0),
                     (double)(ph->virtual_start_bytes + ph->virtual_size_bytes) / (1024.0*1024.0*1024.0),
                     ph->active_count, ph->total_ratio);
        }
    }
    gs_unlock();
    return RC_OK;
}

RC raid_status(void) {
    gs_lock();
    if (S()->rt.state != STATE_MOUNTED && S()->rt.state != STATE_DEGRADED) {
        LOG_ERROR("Not allowed in '%s' (need MOUNTED or DEGRADED)", raid_state_str(S()->rt.state));
        gs_unlock(); return RC_ERR_INVALID_STATE;
    }
    STRIPE_VOLUME* vol = &S()->vol.volume;
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now); QueryPerformanceFrequency(&freq);
    double elapsed = (double)(now.QuadPart - vol->start_time.QuadPart) / freq.QuadPart;
    system("cls");
    printf("\n");
    printf("  ========== RAIDTEST LIVE STATUS ==========\n");
    printf("  State:    %s\n", raid_state_str(S()->rt.state));
    printf("  RAID:     %s\n", vol->raid_level == RAID_LEVEL_MIRROR ? "Mirror (RAID1)" : "Stripe (RAID0)");
    printf("  Virtual size: %.1f GB\n", (double)vol->virtual_total_bytes / (1024.0*1024.0*1024.0));
    printf("  Runtime: %.0f s\n", elapsed);
    printf("  Written: %llu MB  (%.0f MB/s)\n", (unsigned long long)(vol->bytes_written / (1024*1024)), elapsed > 0 ? (vol->bytes_written / (1024.0*1024.0)) / elapsed : 0);
    printf("  Read:    %llu MB  (%.0f MB/s)\n", (unsigned long long)(vol->bytes_read / (1024*1024)), elapsed > 0 ? (vol->bytes_read / (1024.0*1024.0)) / elapsed : 0);
    printf("  Cache:   %s  %u MB\n", vol->cache_enabled ? "ON" : "OFF", S()->cache.cache_mb);
    printf("  Physical disks: %u\n", device_get_count());
    for (uint32_t i = 0; i < device_get_count(); i++) {
        DISK_INFO* d = device_get(i);
        char path_a[MAX_DRIVE_PATH] = {0}; wcstombs(path_a, d->file_path, MAX_DRIVE_PATH - 1);
        char serial_a[MAX_SERIAL_LEN] = {0}; strncpy_s(serial_a, MAX_SERIAL_LEN, d->serial_number, _TRUNCATE);
        char drive_a[32] = {0}; wcstombs(drive_a, d->drive_letter, 31);
        printf("    [%02u] %s | %s | %s | pool=%llu MB%s\n",
               i, drive_a, path_a, serial_a,
               (unsigned long long)(d->pool_bytes / (1024 * 1024)),
               d->selected ? " [SELECTED]" : "");
    }
    printf("  Volume disks: %u  (healthy: %u)\n", vol->disk_count, (uint32_t)vol->healthy_count);
    for (uint32_t i = 0; i < vol->disk_count; i++) {
        char path_a[MAX_DRIVE_PATH] = {0}; wcstombs(path_a, vol->disks[i]->file_path, MAX_DRIVE_PATH - 1);
        uint32_t spd = vol->disks[i]->benchmarked ? vol->disks[i]->bench_write_mbs : vol->disks[i]->write_speed_mbs;
        LONG healthy = InterlockedCompareExchange(&vol->disks[i]->healthy, 1, 1);
        const char* status_str = healthy ? (vol->disks[i]->faulty ? "FAULTY" : "OK") : "DEGRADED";
        printf("    Disk%u: %s  (%u MB/s W)  [%s]\n", i, path_a, spd, status_str);
    }
    printf("  ==========================================\n\n");
    gs_unlock();
    return RC_OK;
}

RC raid_map(void) {
    gs_lock();
    if (S()->rt.state != STATE_MOUNTED) {
        LOG_ERROR("Not allowed in '%s' (need 'MOUNTED')", raid_state_str(S()->rt.state));
        gs_unlock(); return RC_ERR_INVALID_STATE;
    }
    uint64_t dump_size = 64ULL * 1024 * 1024;
    if (dump_size > S()->vol.volume.virtual_total_bytes) dump_size = S()->vol.volume.virtual_total_bytes;
    stripe_volume_dump_mapping(&S()->vol.volume, 0, dump_size);
    gs_unlock();
    return RC_OK;
}

RC raid_check(void) {
    gs_lock();
    if (S()->rt.state != STATE_MOUNTED && S()->rt.state != STATE_DEGRADED) {
        LOG_ERROR("Not allowed in '%s' (need MOUNTED or DEGRADED)", raid_state_str(S()->rt.state));
        gs_unlock(); return RC_ERR_INVALID_STATE;
    }
    STRIPE_VOLUME* vol = &S()->vol.volume;
    bool all_ok = true;
    for (uint32_t i = 0; i < vol->disk_count; i++) {
        DISK_INFO* d = vol->disks[i];
        LONG healthy = InterlockedCompareExchange(&d->healthy, 1, 1);
        bool disk_ok = healthy && !d->faulty;
        char path_a[MAX_DRIVE_PATH] = {0};
        wcstombs(path_a, d->file_path, MAX_DRIVE_PATH - 1);
        printf("  Disk %u: %s (%s) [SN: %s] %s\n",
               i, path_a,
               disk_ok ? "OK" : "FAIL",
               d->serial_number[0] ? d->serial_number : "(no serial)",
               disk_ok ? "" : "<-- NEEDS REPLACEMENT");
        if (!disk_ok) all_ok = false;
        HANDLE h = CreateFileW(d->file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            printf("         WARN: pool file not accessible\n");
            all_ok = false;
        } else {
            CloseHandle(h);
        }
    }
    printf("  RAID level: %s\n", vol->raid_level == RAID_LEVEL_MIRROR ? "RAID1" : "RAID0");
    printf("  Healthy: %u/%u\n", (uint32_t)vol->healthy_count, vol->disk_count);
    printf("  Result: %s\n", all_ok ? "HEALTHY" : "DEGRADED");
    uint32_t sb_found = 0;
    for (uint32_t i = 0; i < vol->disk_count; i++) {
        wchar_t root[4];
        wcscpy_s(root, 4, vol->disks[i]->drive_letter);
        SUPERBLOCK sb;
        if (metadata_read(root, &sb)) {
            sb_found++;
            char uuid_str[64] = "0000000000000000-0000000000000000";
            if (sb.version >= 4) uuid_to_str(&sb.volume_uuid, uuid_str, sizeof(uuid_str));
            printf("  Superblock on %ls: v%u gen=%llu UUID=%s\n",
                   root, sb.version, (unsigned long long)sb.generation, uuid_str);
        } else {
            printf("  Superblock on %ls: MISSING\n", root);
        }
    }
    printf("  Superblock consistency: %u/%u\n", sb_found, vol->disk_count);
    if (all_ok && sb_found == vol->disk_count)
        LOG_OK("Volume is HEALTHY");
    else
        LOG_WARN("Volume is DEGRADED (%u issues)", vol->disk_count - sb_found + (all_ok ? 0 : 1));
    gs_unlock();
    return all_ok ? RC_OK : RC_ERR_IO;
}

RC raid_metadata(int argc, char* argv[]) {
    gs_lock();
    wchar_t root[4] = L"C:\\";
    if (argc > 0 && argv[0][0] >= 'A' && argv[0][0] <= 'Z') root[0] = (wchar_t)argv[0][0];
    SUPERBLOCK sb;
    memset(&sb, 0, sizeof(sb));
    if (!metadata_read(root, &sb)) {
        LOG_ERROR("No valid superblock on %ls", root);
        gs_unlock(); return RC_ERR_NOT_FOUND;
    }
    char report[4096];
    metadata_dump(&sb, report, sizeof(report));
    printf("%s\n", report);
    gs_unlock();
    return RC_OK;
}

RC raid_planner(void) {
    gs_lock();
    uint32_t n = device_get_count();
    if (n == 0) { printf("  No physical disks. Use 'scan' first.\n"); gs_unlock(); return RC_OK; }
    PLANNER_DISK pdisks[MAX_CUSTOM_DISKS];
    uint32_t pcount = 0;
    for (uint32_t i = 0; i < n && pcount < MAX_CUSTOM_DISKS; i++) {
        DISK_INFO* d = device_get(i);
        if (!d) continue;
        pdisks[pcount].disk_index = i;
        pdisks[pcount].capacity_bytes = d->pool_bytes > 0 ? d->pool_bytes : d->total_bytes;
        pdisks[pcount].speed_mbs = d->benchmarked ? d->bench_write_mbs : d->write_speed_mbs;
        strncpy_s(pdisks[pcount].serial, MAX_SERIAL_LEN, d->serial_number, _TRUNCATE);
        pdisks[pcount].selected = d->selected;
        pcount++;
    }
    PLANNER_RESULT result;
    planner_calculate(pdisks, pcount, &result);
    planner_print(&result, pdisks, pcount);
    gs_unlock();
    return RC_OK;
}

RC raid_events(void) {
    gs_lock();
    if (!S()->rt.appdata_path[0]) { LOG_INFO("No appdata path. No event log."); gs_unlock(); return RC_OK; }
    wchar_t path[MAX_DRIVE_PATH];
    StringCchPrintfW(path, MAX_DRIVE_PATH, L"%ls\\%ls", S()->rt.appdata_path, L"events.log");
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { LOG_INFO("No events logged yet."); gs_unlock(); return RC_OK; }
    DWORD size = GetFileSize(h, NULL);
    if (size > 0) {
        char* buf = (char*)malloc(size + 1);
        if (buf) {
            DWORD read = 0;
            ReadFile(h, buf, size, &read, NULL);
            buf[size] = 0;
            printf("%s", buf);
            free(buf);
        }
    }
    CloseHandle(h);
    gs_unlock();
    return RC_OK;
}
