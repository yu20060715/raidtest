#include "raid_service.h"
#include "disk_scanner.h"
#include "bench_io.h"
#include "config.h"
#include "wizard.h"
#include "journal.h"
#include "cleanup.h"
#include "ram_cache.h"
#include "event_bus.h"
#include "pool_io.h"
#include "stripe_engine.h"

static APP_STATE* S(void) { return &g_state; }

/* ---- Event log subscriber ---- */
static void event_log_callback(EVENT_TYPE type, const char* data, void* userdata) {
    (void)userdata;
    if (!S()->appdata_path[0]) return;
    wchar_t path[MAX_DRIVE_PATH];
    StringCchPrintfW(path, MAX_DRIVE_PATH, L"%ls\\%ls", S()->appdata_path, L"events.log");
    HANDLE h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    SetFilePointer(h, 0, NULL, FILE_END);
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[4096];
    int len = snprintf(buf, sizeof(buf), "[%04u-%02u-%02u %02u:%02u:%02u] %s%s%s\n",
                       st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                       event_type_str(type), data && data[0] ? ": " : "", data && data[0] ? data : "");
    DWORD written = 0;
    WriteFile(h, buf, (DWORD)len, &written, NULL);
    CloseHandle(h);
    /* Trim if >512KB */
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (GetFileAttributesExW(path, GetFileExInfoStandard, &info) && info.nFileSizeLow > 512 * 1024) {
        HANDLE h2 = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h2 != INVALID_HANDLE_VALUE) {
            LONG high = 0;
            DWORD low = SetFilePointer(h2, -256 * 1024, &high, FILE_END);
            if (low != INVALID_SET_FILE_POINTER) SetEndOfFile(h2);
            CloseHandle(h2);
        }
    }
}

static RC require(RAID_STATE s) {
    if (S()->state != s) {
        LOG_ERROR("Not allowed in '%s' (need '%s')", raid_state_str(S()->state), raid_state_str(s));
        return RC_ERR_INVALID_STATE;
    }
    return RC_OK;
}

/* ---- Lifecycle ---- */
RC raid_init(void) {
    InitializeCriticalSection(&g_state_cs);
    atexit(cleanup_cs);
    config_load(&S()->config);
    S()->cache_mb = S()->config.cache_mb;
    S()->state = STATE_DISCONNECTED;
    cleanup_bench_dirs();
    GetEnvironmentVariableW(L"APPDATA", S()->appdata_path, MAX_DRIVE_PATH);
    event_bus_init();
    event_bus_subscribe(EVENT_DISK_FOUND, event_log_callback, NULL);
    event_bus_subscribe(EVENT_DISK_REMOVED, event_log_callback, NULL);
    event_bus_subscribe(EVENT_DISK_BENCHED, event_log_callback, NULL);
    event_bus_subscribe(EVENT_VOLUME_CREATED, event_log_callback, NULL);
    event_bus_subscribe(EVENT_VOLUME_LOADED, event_log_callback, NULL);
    event_bus_subscribe(EVENT_VOLUME_DESTROYED, event_log_callback, NULL);
    event_bus_subscribe(EVENT_MOUNT, event_log_callback, NULL);
    event_bus_subscribe(EVENT_UNMOUNT, event_log_callback, NULL);
    event_bus_subscribe(EVENT_REBUILD, event_log_callback, NULL);
    event_bus_subscribe(EVENT_EXPAND, event_log_callback, NULL);
    event_bus_subscribe(EVENT_METADATA_UPDATED, event_log_callback, NULL);
    event_bus_subscribe(EVENT_CACHE_CHANGED, event_log_callback, NULL);
    event_bus_subscribe(EVENT_ERROR, event_log_callback, NULL);
    return RC_OK;
}

void raid_cleanup(void) {
    cleanup_all();
    device_cleanup();
    S()->state = STATE_DISCONNECTED;
    S()->volume_valid = false;
    S()->cache_on = false;
    S()->mounted = false;
}

/* ---- Scan ---- */
RC raid_scan(void) {
    S()->state = STATE_DISCONNECTED;
    device_cleanup();
    if (!device_refresh()) {
        LOG_WARN("No physical disks detected");
        return RC_ERR_NOT_FOUND;
    }
    S()->state = STATE_DISCOVERED;
    uint32_t n = device_get_count();
    LOG_INFO("Found %u physical disk(s). Running quick benchmark (%u MB)...", n, BENCH_SIZE_MB);
    for (uint32_t i = 0; i < n; i++) {
        DISK_INFO* d = device_get(i);
        if (!d->drive_letter[0]) continue;
        printf("  Testing disk %u...\n", i);
        bench_single_disk(d, BENCH_SIZE_MB);
    }
    device_print_list();
    LOG_INFO("Type 'select <id1> <id2> ...' to choose disks, then 'init <id:mb ...>'");
    return RC_OK;
}

RC raid_select(int argc, char* argv[]) {
    RC rc; if ((rc = require(STATE_DISCOVERED)) != RC_OK) return rc;
    uint32_t count = (uint32_t)argc;
    if (count == 0) { LOG_ERROR("Usage: select <id1> <id2> ..."); return RC_ERR_INVALID_ARG; }
    uint32_t ids[MAX_DISKS];
    for (uint32_t i = 0; i < count && i < MAX_DISKS; i++) {
        ids[i] = (uint32_t)atoi(argv[i]);
        if (ids[i] >= device_get_count()) { LOG_ERROR("Invalid disk id %u", ids[i]); return RC_ERR_INVALID_DISK; }
    }
    device_select(ids, count);
    LOG_OK("Selected %u disk(s)", count);
    return RC_OK;
}

RC raid_mapdrive(uint32_t disk_id, const char* drive_letter) {
    RC rc; if ((rc = require(STATE_DISCOVERED)) != RC_OK) return rc;
    if (device_get_count() == 0) { LOG_ERROR("Run 'scan' first"); return RC_ERR_INVALID_STATE; }
    if (!device_map_drive(disk_id, drive_letter)) { LOG_ERROR("Invalid disk id"); return RC_ERR_INVALID_DISK; }
    DISK_INFO* d = device_get(disk_id);
    if (!d) return RC_ERR_INVALID_DISK;
    char model_a[MAX_MODEL_LEN] = {0}; wcstombs(model_a, d->model, MAX_MODEL_LEN - 1);
    char path_a[MAX_DRIVE_PATH] = {0}; wcstombs(path_a, d->file_path, MAX_DRIVE_PATH - 1);
    LOG_OK("Disk %u mapped -> %s  (pool: %s)", disk_id, drive_letter, path_a);
    return RC_OK;
}

RC raid_bench(int argc, char* argv[]) {
    RC rc; if ((rc = require(STATE_DISCOVERED)) != RC_OK) return rc;
    uint32_t size_mb = BENCH_SIZE_MB;
    if (argc > 0) {
        size_mb = (uint32_t)atoi(argv[0]);
        if (size_mb < 64) size_mb = 64;
        if (size_mb > 2048) size_mb = 2048;
    }
    device_bench_all_selected(size_mb);
    printf("\n");
    device_print_list();
    return RC_OK;
}

/* ---- Init Pools ---- */
RC raid_init_pools(int argc, char* argv[]) {
    if (S()->state != STATE_DISCOVERED && S()->state != STATE_UNMOUNTED) {
        LOG_ERROR("Not allowed in '%s' (need DISCOVERED or UNMOUNTED)", raid_state_str(S()->state));
        return RC_ERR_INVALID_STATE;
    }
    if (device_get_count() == 0) { LOG_ERROR("No physical disks. Run 'scan' first"); return RC_ERR_INVALID_STATE; }
    if (S()->state == STATE_UNMOUNTED)
        cleanup_pool_session(S());

    uint32_t pairs[MAX_DISKS], pair_count = 0;
    uint64_t pair_sizes[MAX_DISKS];
    bool has_colon = false;
    for (int i = 0; i < argc; i++) { if (strchr(argv[i], ':')) { has_colon = true; break; } }

    if (has_colon) {
        for (int i = 0; i < argc && pair_count < MAX_DISKS; i++) {
            char* colon = strchr(argv[i], ':');
            if (!colon) { LOG_ERROR("Bad format: %s (expected id:mb)", argv[i]); return RC_ERR_INVALID_ARG; }
            *colon = '\0';
            uint32_t di = (uint32_t)atoi(argv[i]);
            uint64_t mb = (uint64_t)atoll(colon + 1);
            *colon = ':';
            if (di >= device_get_count()) { LOG_ERROR("Invalid disk id %u", di); return RC_ERR_INVALID_DISK; }
            if (mb < 1024) { LOG_ERROR("Pool size too small: %llu MB (min 1024)", (unsigned long long)mb); return RC_ERR_INVALID_ARG; }
            DISK_INFO* d = device_get(di);
            if (!d->file_path[0]) {
                char dl = (char)d->drive_letter[0];
                if (dl >= 'A' && dl <= 'Z') { char ds[2] = { dl, 0 }; device_map_drive(di, ds); }
                else { LOG_ERROR("Disk %u has no drive letter", di); return RC_ERR_INVALID_DISK; }
            }
            pairs[pair_count] = di;
            pair_sizes[pair_count] = mb;
            pair_count++;
        }
    } else if (argc > 0) {
        uint32_t first_id = (uint32_t)atoi(argv[0]);
        if (argc == 1 && first_id >= device_get_count()) {
            uint64_t default_size = (uint64_t)atoll(argv[0]);
            if (default_size < 1024) default_size = 1024;
            uint32_t sel_indices[MAX_DISKS], sel_count = 0;
            for (uint32_t i = 0; i < device_get_count(); i++) {
                if (device_is_selected(i)) sel_indices[sel_count++] = i;
            }
            if (sel_count < MIN_DISKS) { LOG_ERROR("Select %d-%d disks first, or use 'init id:mb id:mb ...'", MIN_DISKS, MAX_DISKS); return RC_ERR_INVALID_ARG; }
            for (uint32_t i = 0; i < sel_count && pair_count < MAX_DISKS; i++) {
                uint32_t di = sel_indices[i];
                DISK_INFO* d = device_get(di);
                if (!d->file_path[0]) {
                    char dl = (char)d->drive_letter[0];
                    if (dl >= 'A' && dl <= 'Z') { char ds[2] = { dl, 0 }; device_map_drive(di, ds); }
                }
                pairs[pair_count] = di;
                pair_sizes[pair_count] = default_size;
                pair_count++;
            }
        } else {
            uint64_t default_size = POOL_SIZE_DEFAULT_MB;
            uint32_t ids[MAX_DISKS];
            for (int i = 0; i < argc && (uint32_t)i < MAX_DISKS; i++) {
                ids[i] = (uint32_t)atoi(argv[i]);
                if (ids[i] >= device_get_count()) { LOG_ERROR("Invalid disk id %u", ids[i]); return RC_ERR_INVALID_DISK; }
            }
            device_select(ids, (uint32_t)argc);
            for (int i = 0; i < argc && pair_count < MAX_DISKS; i++) {
                uint32_t di = ids[i];
                DISK_INFO* d = device_get(di);
                if (!d->drive_letter[0]) {
                    LOG_WARN("Disk %u has no drive letter. Use 'mapdrive %u <letter>' before 'init'", di, di);
                }
                if (!d->file_path[0]) {
                    char dl = (char)d->drive_letter[0];
                    if (dl >= 'A' && dl <= 'Z') { char ds[2] = { dl, 0 }; device_map_drive(di, ds); }
                }
                uint64_t mb = (i < S()->disk_count && S()->pool_sizes_mb[i] > 0) ? S()->pool_sizes_mb[i] : default_size;
                if (mb < 1024) mb = 1024;
                pairs[pair_count] = di;
                pair_sizes[pair_count] = mb;
                pair_count++;
            }
        }
    } else {
        uint32_t sel_indices[MAX_DISKS], sel_count = 0;
        for (uint32_t i = 0; i < device_get_count(); i++) {
            if (device_is_selected(i)) sel_indices[sel_count++] = i;
        }
        if (sel_count < MIN_DISKS) { LOG_ERROR("Select %d-%d disks first, or use 'init id:mb id:mb ...'", MIN_DISKS, MAX_DISKS); return RC_ERR_INVALID_ARG; }
        uint64_t default_size = POOL_SIZE_DEFAULT_MB;
        if (default_size < 1024) default_size = 1024;
        for (uint32_t i = 0; i < sel_count && pair_count < MAX_DISKS; i++) {
            uint32_t di = sel_indices[i];
            DISK_INFO* d = device_get(di);
            if (!d->file_path[0]) {
                char dl = (char)d->drive_letter[0];
                if (dl >= 'A' && dl <= 'Z') { char ds[2] = { dl, 0 }; device_map_drive(di, ds); }
            }
            pairs[pair_count] = di;
            pair_sizes[pair_count] = (i < S()->disk_count && S()->pool_sizes_mb[i] > 0) ? S()->pool_sizes_mb[i] : default_size;
            pair_count++;
        }
    }

    if (pair_count < MIN_DISKS || pair_count > MAX_DISKS) { LOG_ERROR("Need %d-%d disks", MIN_DISKS, MAX_DISKS); return RC_ERR_INVALID_ARG; }
    S()->disk_count = 0;
    for (uint32_t i = 0; i < pair_count; i++) {
        uint32_t di = pairs[i];
        DISK_INFO* disk = device_get(di);
        if (!disk->drive_letter[0]) { char model_a[MAX_MODEL_LEN] = {0}; wcstombs(model_a, disk->model, MAX_MODEL_LEN - 1); LOG_WARN("Skip %s (no drive letter)", model_a); continue; }
        uint64_t size_bytes = pair_sizes[i] * 1024ULL * 1024ULL;
        LOG_INFO("Creating pool file on %ls: %llu MB ...", disk->drive_letter, (unsigned long long)pair_sizes[i]);
        if (!pool_file_create(disk, size_bytes)) {
            LOG_ERROR("Failed to create pool on disk %u, rolling back %u pool(s)", di, S()->disk_count);
            for (uint32_t j = 0; j < S()->disk_count; j++) {
                pool_file_close(S()->disks[j]);
                pool_file_delete(S()->disks[j]);
            }
            S()->disk_count = 0;
            return RC_ERR_ROLLBACK;
        }
        S()->pool_sizes_mb[S()->disk_count] = pair_sizes[i];
        S()->disks[S()->disk_count++] = disk;
    }
    if (S()->disk_count >= MIN_DISKS) {
        S()->state = STATE_INITIALIZED;
        LOG_OK("Pool files ready for %u disks", S()->disk_count);
        return RC_OK;
    }
    LOG_ERROR("Failed to create pool files");
    return RC_ERR_IO;
}

/* ---- Create / Mirror ---- */
RC raid_create(void) {
    RC rc; if ((rc = require(STATE_INITIALIZED)) != RC_OK) return rc;
    if (S()->disk_count < MIN_DISKS) { LOG_ERROR("Not enough disks (need %d)", MIN_DISKS); return RC_ERR_INVALID_STATE; }
    if (!volume_create(&S()->volume, S()->disks, S()->disk_count)) return RC_ERR_IO;
    S()->volume_valid = true;
    S()->state = STATE_MOUNTED;
    return RC_OK;
}

RC raid_mirror(void) {
    RC rc; if ((rc = require(STATE_INITIALIZED)) != RC_OK) return rc;
    if (S()->disk_count < MIN_MIRROR_DISKS) { LOG_ERROR("Not enough disks (need %d)", MIN_MIRROR_DISKS); return RC_ERR_INVALID_STATE; }
    if (!volume_mirror(&S()->volume, S()->disks, S()->disk_count)) return RC_ERR_IO;
    S()->volume_valid = true;
    S()->state = STATE_MOUNTED;
    return RC_OK;
}

RC raid_expand(int argc, char* argv[]) {
    RC rc; if ((rc = require(STATE_MOUNTED)) != RC_OK) return rc;
    if (!volume_expand(&S()->volume, S()->physical_disks, S()->physical_count, argc, argv))
        return RC_ERR_IO;
    return RC_OK;
}

RC raid_rebuild(int argc, char* argv[]) {
    if (S()->state != STATE_MOUNTED && S()->state != STATE_DEGRADED) {
        LOG_ERROR("Not allowed in '%s' (need MOUNTED or DEGRADED)", raid_state_str(S()->state));
        return RC_ERR_INVALID_STATE;
    }
    if (argc < 2) { LOG_ERROR("Usage: rebuild <disk_idx> <new_disk_id> <pool_mb>"); return RC_ERR_INVALID_ARG; }
    uint32_t replace_idx = (uint32_t)atoi(argv[0]);
    uint32_t new_disk_id = (uint32_t)atoi(argv[1]);
    uint64_t pool_mb = (argc >= 3) ? (uint64_t)atoll(argv[2]) : 1024;
    if (pool_mb < 1024) pool_mb = 1024;
    if (!volume_rebuild(&S()->volume, S()->physical_disks, S()->physical_count,
                        replace_idx, new_disk_id, pool_mb))
        return RC_ERR_IO;
    return RC_OK;
}

/* ---- Mount ---- */
RC raid_mount(char drive_letter) {
    RC rc; if ((rc = require(STATE_MOUNTED)) != RC_OK) return rc;
    if (!S()->volume_valid) { LOG_ERROR("Volume not created"); return RC_ERR_INVALID_STATE; }
    if (S()->mounted) { LOG_WARN("Already mounted at %c:", S()->volume.mount_point[0]); return RC_ERR_ALREADY; }
    if (!volume_mount(&S()->volume, drive_letter)) { LOG_ERROR("Mount failed at %c:", drive_letter); return RC_ERR_MOUNT; }
    S()->mounted = true;
    return RC_OK;
}

RC raid_unmount(void) {
    RC rc; if ((rc = require(STATE_MOUNTED)) != RC_OK) return rc;
    volume_unmount(&S()->volume, &S()->cache_on, &S()->flush_thread, &S()->mounted);
    S()->state = STATE_UNMOUNTED;
    S()->volume_valid = false;
    LOG_OK("Unmounted (pool files preserved for 'load')");
    return RC_OK;
}

RC raid_load(const wchar_t* drive_root) {
    if (S()->state != STATE_UNMOUNTED && S()->state != STATE_DISCOVERED) {
        LOG_ERROR("Not allowed in '%s' (need UNMOUNTED or DISCOVERED)", raid_state_str(S()->state));
        return RC_ERR_INVALID_STATE;
    }
    wchar_t root[4];
    if (drive_root) {
        wcscpy_s(root, 4, drive_root);
    } else {
        wcscpy_s(root, 4, L"C:\\");
    }
    LOG_INFO("Loading volume from %ls%ls...", root, CONFIG_DIR);
    uint32_t loaded = 0;
    if (!volume_load(&S()->volume, S()->loaded_disks, &loaded,
                     S()->physical_disks, S()->physical_count, root)) {
        LOG_ERROR("Failed to load volume");
        return RC_ERR_METADATA;
    }
    S()->disk_count = loaded;
    for (uint32_t i = 0; i < loaded; i++)
        S()->disks[i] = &S()->loaded_disks[i];
    S()->volume_valid = true;
    S()->state = STATE_MOUNTED;
    return RC_OK;
}

RC raid_destroy(void) {
    if (S()->state != STATE_MOUNTED && S()->state != STATE_UNMOUNTED) {
        LOG_ERROR("Not allowed in '%s' (need MOUNTED or UNMOUNTED)", raid_state_str(S()->state));
        return RC_ERR_INVALID_STATE;
    }
    event_bus_publish(EVENT_VOLUME_DESTROYED, "destroy");
    volume_destroy(&S()->volume, S()->disks, S()->disk_count,
                   &S()->cache_on, &S()->flush_thread, &S()->mounted, &S()->state);
    S()->volume_valid = false;
    LOG_OK("Volume destroyed (all pool files, superblocks, journals deleted)");
    return RC_OK;
}

RC raid_purge(void) {
    if (S()->state != STATE_INITIALIZED && S()->state != STATE_MOUNTED && S()->state != STATE_UNMOUNTED) {
        LOG_ERROR("Not allowed in '%s' (need INITIALIZED/MOUNTED/UNMOUNTED)", raid_state_str(S()->state));
        return RC_ERR_INVALID_STATE;
    }
    if (S()->mounted) {
        volume_unmount(&S()->volume, &S()->cache_on, &S()->flush_thread, &S()->mounted);
    }
    cleanup_pool_session(S());
    S()->state = STATE_DISCOVERED;
    S()->volume_valid = false;
    S()->cache_on = false;
    S()->mounted = false;
    LOG_OK("Pool files and superblock deleted");
    return RC_OK;
}

/* ---- Cache ---- */
RC raid_cache(int argc, char* argv[]) {
    RC rc; if ((rc = require(STATE_MOUNTED)) != RC_OK) return rc;
    if (argc > 0 && strcmp(argv[0], "wt") == 0) {
        if (!S()->cache_on) { LOG_ERROR("Enable cache first ('cache <sizeMB>')"); return RC_ERR_INVALID_STATE; }
        S()->volume.cache.write_through = !S()->volume.cache.write_through;
        LOG_OK("Write-through cache %s", S()->volume.cache.write_through ? "ON" : "OFF");
        return RC_OK;
    }
    if (argc > 0 && strcmp(argv[0], "off") == 0) {
        if (!S()->cache_on) { LOG_WARN("Cache already off"); return RC_ERR_ALREADY; }
        if (S()->flush_thread) {
            S()->volume.cache.running = 0;
            WaitForSingleObject(S()->flush_thread, INFINITE);
            CloseHandle(S()->flush_thread);
            S()->flush_thread = NULL;
        }
        cache_flush_all(&S()->volume.cache, &S()->volume);
        cache_destroy(&S()->volume.cache);
        S()->volume.cache_enabled = false;
        S()->cache_on = false;
        event_bus_publish(EVENT_CACHE_CHANGED, "off");
        LOG_OK("Write-back cache disabled and flushed");
        return RC_OK;
    }
    if (S()->cache_on) { LOG_WARN("Cache already on. Use 'cache off' first to reinit"); return RC_ERR_ALREADY; }
    uint32_t size_mb = CACHE_DEFAULT_MB;
    if (argc > 0) size_mb = (uint32_t)atoi(argv[0]);
    if (size_mb < 256) size_mb = 256;
    LOG_INFO("Initializing %u MB RAM write-back cache...", size_mb);
    if (!cache_init(&S()->volume.cache, (uint64_t)size_mb * 1024ULL * 1024ULL)) {
        LOG_ERROR("Cache init failed"); return RC_ERR_CACHE;
    }
    S()->volume.cache_enabled = true;
    S()->cache_on = true;
    S()->cache_mb = size_mb;
    S()->flush_thread = (HANDLE)_beginthreadex(NULL, 0, cache_flush_thread, &S()->volume, 0, NULL);
    if (S()->flush_thread) LOG_OK("Background flush thread started (1s interval)");
    event_bus_publish(EVENT_CACHE_CHANGED, "on");
    LOG_OK("Write-back cache enabled: %u MB (block size=%u KB)", size_mb, CACHE_BLOCK_SIZE / 1024);
    return RC_OK;
}

/* ---- Info / Status / Test ---- */
RC raid_info(void) {
    LOG_INFO("=== RAIDTEST Status ===  State: %s", raid_state_str(S()->state));
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
    if (!S()->volume_valid) { LOG_INFO("No virtual volume."); return RC_OK; }
    STRIPE_VOLUME* vol = &S()->volume;
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
    LOG_INFO("Cache: %s (%u MB, dirty=%.1f%%)", vol->cache_enabled ? "ON" : "OFF", S()->cache_mb, dirty_ratio);
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
    return RC_OK;
}

RC raid_status(void) {
    if (S()->state != STATE_MOUNTED && S()->state != STATE_DEGRADED) {
        LOG_ERROR("Not allowed in '%s' (need MOUNTED or DEGRADED)", raid_state_str(S()->state));
        return RC_ERR_INVALID_STATE;
    }
    STRIPE_VOLUME* vol = &S()->volume;
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now); QueryPerformanceFrequency(&freq);
    double elapsed = (double)(now.QuadPart - vol->start_time.QuadPart) / freq.QuadPart;
    system("cls");
    printf("\n");
    printf("  ========== RAIDTEST LIVE STATUS ==========\n");
    printf("  State:    %s\n", raid_state_str(S()->state));
    printf("  RAID:     %s\n", vol->raid_level == RAID_LEVEL_MIRROR ? "Mirror (RAID1)" : "Stripe (RAID0)");
    printf("  Virtual size: %.1f GB\n", (double)vol->virtual_total_bytes / (1024.0*1024.0*1024.0));
    printf("  Runtime: %.0f s\n", elapsed);
    printf("  Written: %llu MB  (%.0f MB/s)\n", (unsigned long long)(vol->bytes_written / (1024*1024)), elapsed > 0 ? (vol->bytes_written / (1024.0*1024.0)) / elapsed : 0);
    printf("  Read:    %llu MB  (%.0f MB/s)\n", (unsigned long long)(vol->bytes_read / (1024*1024)), elapsed > 0 ? (vol->bytes_read / (1024.0*1024.0)) / elapsed : 0);
    printf("  Cache:   %s  %u MB\n", vol->cache_enabled ? "ON" : "OFF", S()->cache_mb);
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
    return RC_OK;
}

RC raid_map(void) {
    RC rc; if ((rc = require(STATE_MOUNTED)) != RC_OK) return rc;
    uint64_t dump_size = 64ULL * 1024 * 1024;
    if (dump_size > S()->volume.virtual_total_bytes) dump_size = S()->volume.virtual_total_bytes;
    stripe_volume_dump_mapping(&S()->volume, 0, dump_size);
    return RC_OK;
}

RC raid_test(void) {
    RC rc; if ((rc = require(STATE_MOUNTED)) != RC_OK) return rc;
    stripe_volume_verify_io(&S()->volume);
    return RC_OK;
}

RC raid_random(int argc, char** args) {
    RC rc; if ((rc = require(STATE_MOUNTED)) != RC_OK) return rc;
    uint32_t ops = 100;
    uint32_t max_kb = 64;
    if (argc > 0) ops = (uint32_t)atol(args[0]);
    if (argc > 1) max_kb = (uint32_t)atol(args[1]);
    if (ops < 1) ops = 1;
    if (max_kb < 4) max_kb = 4;
    if (max_kb > 1024) max_kb = 1024;
    stripe_volume_random_test(&S()->volume, ops, max_kb);
    return RC_OK;
}

RC raid_benchfs(int argc, char* argv[]) {
    RC rc; if ((rc = require(STATE_MOUNTED)) != RC_OK) return rc;
    uint32_t size_mb = 512, block_kb = 1024;
    if (argc > 0) size_mb = (uint32_t)atoi(argv[0]);
    if (argc > 1) block_kb = (uint32_t)atoi(argv[1]);
    if (size_mb < 64) size_mb = 64;
    if (size_mb > 4096) size_mb = 4096;
    if (block_kb < 4) block_kb = 4;
    if (block_kb > 8192) block_kb = 8192;
    if (block_kb > size_mb * 1024) block_kb = size_mb * 1024;
    bench_volume(&S()->volume, size_mb, block_kb);
    return RC_OK;
}

RC raid_check(void) {
    if (S()->state != STATE_MOUNTED && S()->state != STATE_DEGRADED) {
        LOG_ERROR("Not allowed in '%s' (need MOUNTED or DEGRADED)", raid_state_str(S()->state));
        return RC_ERR_INVALID_STATE;
    }
    STRIPE_VOLUME* vol = &S()->volume;
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
        if (superblock_read_raw(root, &sb)) {
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
    return all_ok ? RC_OK : RC_ERR_IO;
}

RC raid_simulate(int argc, char* argv[]) {
    if (S()->state != STATE_MOUNTED) {
        LOG_ERROR("Not allowed in '%s' (need MOUNTED)", raid_state_str(S()->state));
        return RC_ERR_INVALID_STATE;
    }
    if (argc < 2) { LOG_ERROR("Usage: simulate <disk_idx> <mode>  (mode: fail, healthy, disconnect)"); return RC_ERR_INVALID_ARG; }
    uint32_t idx = (uint32_t)atoi(argv[0]);
    if (idx >= S()->volume.disk_count) { LOG_ERROR("Invalid disk index %u", idx); return RC_ERR_INVALID_ARG; }
    DISK_INFO* d = S()->volume.disks[idx];
    char mode = argv[1][0];
    switch (mode) {
        case 'f':
            InterlockedExchange(&d->healthy, 0); d->faulty = true;
            InterlockedDecrement(&S()->volume.healthy_count);
            S()->state = STATE_DEGRADED;
            LOG_WARN("Simulated: disk %u -> FAILED", idx);
            event_bus_publish(EVENT_ERROR, "simulate: disk failed");
            break;
        case 'h':
            InterlockedExchange(&d->healthy, 1); d->faulty = false;
            InterlockedIncrement(&S()->volume.healthy_count);
            if (S()->volume.healthy_count >= S()->volume.disk_count) S()->state = STATE_MOUNTED;
            LOG_OK("Simulated: disk %u -> HEALTHY", idx);
            break;
        case 'd':
            pool_file_close(d);
            InterlockedExchange(&d->healthy, 0); d->faulty = true;
            InterlockedDecrement(&S()->volume.healthy_count);
            S()->state = STATE_DEGRADED;
            LOG_WARN("Simulated: disk %u -> DISCONNECTED", idx);
            event_bus_publish(EVENT_ERROR, "simulate: disk disconnected");
            break;
        default:
            LOG_ERROR("Unknown mode '%c' (f= fail, h= healthy, d= disconnect)", mode);
            return RC_ERR_INVALID_ARG;
    }
    return RC_OK;
}

RC raid_metadata(int argc, char* argv[]) {
    wchar_t root[4] = L"C:\\";
    if (argc > 0 && argv[0][0] >= 'A' && argv[0][0] <= 'Z') root[0] = (wchar_t)argv[0][0];
    SUPERBLOCK sb;
    memset(&sb, 0, sizeof(sb));
    if (!metadata_read(root, &sb)) {
        LOG_ERROR("No valid superblock on %ls", root);
        return RC_ERR_NOT_FOUND;
    }
    char report[4096];
    metadata_dump(&sb, report, sizeof(report));
    printf("%s\n", report);
    return RC_OK;
}

RC raid_planner(void) {
    uint32_t n = device_get_count();
    if (n == 0) { printf("  No physical disks. Use 'scan' first.\n"); return RC_OK; }
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
    return RC_OK;
}

RC raid_events(void) {
    if (!S()->appdata_path[0]) { LOG_INFO("No appdata path. No event log."); return RC_OK; }
    wchar_t path[MAX_DRIVE_PATH];
    StringCchPrintfW(path, MAX_DRIVE_PATH, L"%ls\\%ls", S()->appdata_path, L"events.log");
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { LOG_INFO("No events logged yet."); return RC_OK; }
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
    return RC_OK;
}

/* ---- Config ---- */
RC raid_config_save(void) {
    APP_CONFIG* cfg = &S()->config;
    cfg->disk_count = 0;
    for (uint32_t i = 0; i < device_get_count(); i++) {
        DISK_INFO* d = device_get(i);
        if (d && d->selected && cfg->disk_count < MAX_DISKS) {
            cfg->disks[cfg->disk_count].disk_id = i;
            cfg->disks[cfg->disk_count].drive_letter = (char)d->drive_letter[0];
            cfg->disks[cfg->disk_count].pool_mb = d->pool_bytes / (1024 * 1024);
            cfg->disk_count++;
        }
    }
    cfg->cache_mb = S()->cache_mb;
    cfg->mount_letter = S()->volume.mount_point[0] ? S()->volume.mount_point[0] : 'G';
    cfg->auto_bench = true;
    config_save(cfg);
    return RC_OK;
}

RC raid_config_load(void) {
    config_load(&S()->config);
    LOG_OK("Config loaded. Use 'scan' + 'select' + 'create' to restore");
    return RC_OK;
}

/* ---- Wizard / Quick ---- */
RC raid_wizard(void) {
    wizard_run(S());
    return RC_OK;
}

RC raid_quick(void) {
    RC rc;
    rc = raid_scan(); if (rc != RC_OK) return rc;
    if (device_get_count() < MIN_DISKS) { LOG_ERROR("Need at least %d disks", MIN_DISKS); return RC_ERR_INVALID_STATE; }
    printf("\n  Enter disk selections and pool sizes (e.g. 0:51200 3:102400 4:51200):\n  > ");
    fflush(stdout);
    char input[256];
    if (!fgets(input, sizeof(input), stdin)) return RC_ERR_INVALID_ARG;
    char* args[16]; int argc = 0;
    char* ctx = NULL; char* tok = strtok_s(input, " \t\r\n", &ctx);
    while (tok && argc < 16) { args[argc++] = tok; tok = strtok_s(NULL, " \t\r\n", &ctx); }
    if (argc < MIN_DISKS) { LOG_ERROR("Need %d-%d disks", MIN_DISKS, MAX_DISKS); return RC_ERR_INVALID_ARG; }
    rc = raid_init_pools(argc, args); if (rc != RC_OK) return rc;
    rc = raid_create(); if (rc != RC_OK) return rc;
    uint64_t total_pool = 0;
    for (uint32_t i = 0; i < S()->disk_count; i++) total_pool += S()->pool_sizes_mb[i];
    uint64_t total_gb = total_pool / 1024; if (total_gb == 0) total_gb = 1;
    uint32_t max_cache = (uint32_t)min_u64(total_gb * 1024, 4096);
    uint32_t cache_mb = max_cache < CACHE_DEFAULT_MB ? max_cache : CACHE_DEFAULT_MB;
    char cache_arg[16]; snprintf(cache_arg, 16, "%u", cache_mb);
    char* cache_argv[] = { cache_arg };
    rc = raid_cache(1, cache_argv); if (rc != RC_OK) LOG_WARN("Cache setup failed (rc=%d)", rc);
    char mount_letter = S()->config.mount_letter ? S()->config.mount_letter : 'G';
    rc = raid_mount(mount_letter); if (rc != RC_OK) LOG_WARN("Mount failed (rc=%d)", rc);
    raid_config_save();
    LOG_OK("Quick setup complete! Volume mounted at %c:. Type 'exit' to quit.", mount_letter);
    return RC_OK;
}
