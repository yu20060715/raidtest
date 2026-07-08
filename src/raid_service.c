#include "raid_service.h"
#include "disk_scanner.h"
#include "bench_io.h"
#include "config.h"
#include "wizard.h"
#include "cleanup.h"
#include "ram_cache.h"
#include "event_bus.h"
#include "stripe_engine.h"
#include "volume_manager.h"
#include "profiler.h"

static APP_STATE* S(void) { return &g_state; }

/* ---- Event log subscriber ---- */
static void event_log_callback(EVENT_TYPE type, const char* data, void* userdata) {
    (void)userdata;
    if (!S()->rt.appdata_path[0]) return;
    wchar_t path[MAX_DRIVE_PATH];
    StringCchPrintfW(path, MAX_DRIVE_PATH, L"%ls\\%ls", S()->rt.appdata_path, L"events.log");
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
    if (!WriteFile(h, buf, (DWORD)len, &written, NULL) || written != (DWORD)len) {
        CloseHandle(h);
        return;
    }
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
    if (S()->rt.state != s) {
        LOG_ERROR("Not allowed in '%s' (need '%s')", raid_state_str(S()->rt.state), raid_state_str(s));
        return RC_ERR_INVALID_STATE;
    }
    return RC_OK;
}

/* ---- Lifecycle ---- */
RC raid_init(void) {
    InitializeCriticalSection(&g_state_cs);
    gs_lock();
    atexit(cleanup_cs);
    config_load(&S()->cfg.config);
    S()->cache.cache_mb = S()->cfg.config.cache_mb;
    S()->rt.state = STATE_DISCONNECTED;
    cleanup_bench_dirs();
    GetEnvironmentVariableW(L"APPDATA", S()->rt.appdata_path, MAX_DRIVE_PATH);
    event_bus_init();
    profiler_init();
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
    gs_unlock();
    return RC_OK;
}

void raid_cleanup(void) {
    gs_lock();
    cleanup_all();
    device_cleanup();
    S()->rt.state = STATE_DISCONNECTED;
    S()->vol.volume_valid = false;
    S()->cache.cache_on = false;
    S()->rt.mounted = false;
    gs_unlock();
}

/* ---- Scan ---- */
RC raid_scan_locked(void) {
    S()->rt.state = STATE_DISCONNECTED;
    device_cleanup();
    if (!device_refresh()) {
        LOG_WARN("No physical disks detected");
        return RC_ERR_NOT_FOUND;
    }
    S()->rt.state = STATE_DISCOVERED;
    uint32_t n = device_get_count();
    LOG_INFO("Found %u physical disk(s). Running quick benchmark (%u MB)...", n, BENCH_SIZE_MB);
    for (uint32_t i = 0; i < n; i++) {
        DISK_INFO* d = device_get(i);
        if (!d || !d->drive_letter[0]) continue;
        printf("  Testing disk %u...\n", i);
        bench_single_disk(d, BENCH_SIZE_MB);
    }
    device_print_list();
    LOG_INFO("Type 'select <id1> <id2> ...' to choose disks, then 'init <id:mb ...>'");
    return RC_OK;
}

RC raid_scan(void) {
    gs_lock();
    RC rc = raid_scan_locked();
    gs_unlock();
    return rc;
}

RC raid_select(int argc, char* argv[]) {
    gs_lock();
    RC rc; if ((rc = require(STATE_DISCOVERED)) != RC_OK) { gs_unlock(); return rc; }
    uint32_t count = (uint32_t)argc;
    if (count == 0) { LOG_ERROR("Usage: select <id1> <id2> ..."); gs_unlock(); return RC_ERR_INVALID_ARG; }
    uint32_t ids[MAX_DISKS];
    for (uint32_t i = 0; i < count && i < MAX_DISKS; i++) {
        if (!safe_atou32(argv[i], &ids[i])) { LOG_ERROR("Invalid disk id '%s'", argv[i]); gs_unlock(); return RC_ERR_INVALID_ARG; }
        if (ids[i] >= device_get_count()) { LOG_ERROR("Invalid disk id %u", ids[i]); gs_unlock(); return RC_ERR_INVALID_DISK; }
    }
    device_select(ids, count);
    LOG_OK("Selected %u disk(s)", count);
    gs_unlock();
    return RC_OK;
}

RC raid_mapdrive(uint32_t disk_id, const char* drive_letter) {
    gs_lock();
    RC rc; if ((rc = require(STATE_DISCOVERED)) != RC_OK) { gs_unlock(); return rc; }
    if (device_get_count() == 0) { LOG_ERROR("Run 'scan' first"); gs_unlock(); return RC_ERR_INVALID_STATE; }
    if (!device_map_drive(disk_id, drive_letter)) { LOG_ERROR("Invalid disk id"); gs_unlock(); return RC_ERR_INVALID_DISK; }
    DISK_INFO* d = device_get(disk_id);
    if (!d) { gs_unlock(); return RC_ERR_INVALID_DISK; }
    char model_a[MAX_MODEL_LEN] = {0}; wcstombs(model_a, d->model, MAX_MODEL_LEN - 1);
    char path_a[MAX_DRIVE_PATH] = {0}; wcstombs(path_a, d->file_path, MAX_DRIVE_PATH - 1);
    LOG_OK("Disk %u mapped -> %s  (pool: %s)", disk_id, drive_letter, path_a);
    gs_unlock();
    return RC_OK;
}

RC raid_bench(int argc, char* argv[]) {
    gs_lock();
    RC rc; if ((rc = require(STATE_DISCOVERED)) != RC_OK) { gs_unlock(); return rc; }
    uint32_t size_mb = BENCH_SIZE_MB;
    if (argc > 0) {
        uint32_t tmp = 0;
        if (!safe_atou32(argv[0], &tmp)) { LOG_ERROR("Invalid size '%s'", argv[0]); gs_unlock(); return RC_ERR_INVALID_ARG; }
        size_mb = tmp;
        if (size_mb < 64) size_mb = 64;
        if (size_mb > 2048) size_mb = 2048;
    }
    device_bench_all_selected(size_mb);
    printf("\n");
    device_print_list();
    gs_unlock();
    return RC_OK;
}

/* ---- Init Pools Helpers ---- */
typedef struct {
    uint32_t ids[MAX_DISKS];
    uint64_t sizes_mb[MAX_DISKS];
    uint32_t count;
} POOL_PAIRS;

static RC ensure_drive_mapped(uint32_t di) {
    DISK_INFO* d = device_get(di);
    if (!d) { LOG_ERROR("Disk %u not found", di); return RC_ERR_INVALID_DISK; }
    if (d->file_path[0]) return RC_OK;
    char dl = (char)d->drive_letter[0];
    if (dl >= 'A' && dl <= 'Z') { char ds[2] = { dl, 0 }; device_map_drive(di, ds); return RC_OK; }
    LOG_ERROR("Disk %u has no drive letter", di);
    return RC_ERR_INVALID_DISK;
}

static RC init_pools_from_pairs(int argc, char* argv[], POOL_PAIRS* pp) {
    for (int i = 0; i < argc && pp->count < MAX_DISKS; i++) {
        char* colon = strchr(argv[i], ':');
        if (!colon) { LOG_ERROR("Bad format: %s (expected id:mb)", argv[i]); return RC_ERR_INVALID_ARG; }
        *colon = '\0';
        uint32_t di = 0;
        if (!safe_atou32(argv[i], &di)) { *colon = ':'; LOG_ERROR("Bad disk id in %s", argv[i]); return RC_ERR_INVALID_ARG; }
        *colon = ':';
        uint64_t mb = 0;
        if (!safe_atou64(colon + 1, &mb) || mb == 0) { LOG_ERROR("Bad size in %s (expected id:mb)", argv[i]); return RC_ERR_INVALID_ARG; }
        if (di >= device_get_count()) { LOG_ERROR("Invalid disk id %u", di); return RC_ERR_INVALID_DISK; }
        if (mb < 1024) { LOG_ERROR("Pool size too small: %llu MB (min 1024)", (unsigned long long)mb); return RC_ERR_INVALID_ARG; }
        RC rc; if ((rc = ensure_drive_mapped(di)) != RC_OK) return rc;
        pp->ids[pp->count] = di;
        pp->sizes_mb[pp->count] = mb;
        pp->count++;
    }
    return RC_OK;
}

static RC init_pools_all_selected_with_size(uint64_t size_mb, POOL_PAIRS* pp) {
    if (size_mb < 1024) size_mb = 1024;
    uint32_t sel_indices[MAX_DISKS], sel_count = 0;
    for (uint32_t i = 0; i < device_get_count(); i++) {
        if (device_is_selected(i)) sel_indices[sel_count++] = i;
    }
    if (sel_count < MIN_DISKS) { LOG_ERROR("Select %d-%d disks first, or use 'init id:mb id:mb ...'", MIN_DISKS, MAX_DISKS); return RC_ERR_INVALID_ARG; }
    for (uint32_t i = 0; i < sel_count && pp->count < MAX_DISKS; i++) {
        uint32_t di = sel_indices[i];
        ensure_drive_mapped(di);
        pp->ids[pp->count] = di;
        pp->sizes_mb[pp->count] = size_mb;
        pp->count++;
    }
    return RC_OK;
}

static RC init_pools_from_ids(int argc, char* argv[], POOL_PAIRS* pp) {
    uint64_t default_size = POOL_SIZE_DEFAULT_MB;
    uint32_t ids[MAX_DISKS];
    for (int i = 0; i < argc && (uint32_t)i < MAX_DISKS; i++) {
        if (!safe_atou32(argv[i], &ids[i])) { LOG_ERROR("Invalid disk id '%s'", argv[i]); return RC_ERR_INVALID_ARG; }
        if (ids[i] >= device_get_count()) { LOG_ERROR("Invalid disk id %u", ids[i]); return RC_ERR_INVALID_DISK; }
    }
    device_select(ids, (uint32_t)argc);
    for (int i = 0; i < argc && pp->count < MAX_DISKS; i++) {
        uint32_t di = ids[i];
        DISK_INFO* d = device_get(di);
        if (!d) { LOG_ERROR("Disk %u invalid", di); return RC_ERR_INVALID_DISK; }
        if (!d->drive_letter[0]) {
            LOG_WARN("Disk %u has no drive letter. Use 'mapdrive %u <letter>' before 'init'", di, di);
        }
        ensure_drive_mapped(di);
        uint64_t mb = (i < S()->disk.disk_count && S()->disk.pool_sizes_mb[i] > 0) ? S()->disk.pool_sizes_mb[i] : default_size;
        if (mb < 1024) mb = 1024;
        pp->ids[pp->count] = di;
        pp->sizes_mb[pp->count] = mb;
        pp->count++;
    }
    return RC_OK;
}

static RC init_pools_from_selected(POOL_PAIRS* pp) {
    uint32_t sel_indices[MAX_DISKS], sel_count = 0;
    for (uint32_t i = 0; i < device_get_count(); i++) {
        if (device_is_selected(i)) sel_indices[sel_count++] = i;
    }
    if (sel_count < MIN_DISKS) { LOG_ERROR("Select %d-%d disks first, or use 'init id:mb id:mb ...'", MIN_DISKS, MAX_DISKS); return RC_ERR_INVALID_ARG; }
    uint64_t default_size = POOL_SIZE_DEFAULT_MB;
    if (default_size < 1024) default_size = 1024;
    for (uint32_t i = 0; i < sel_count && pp->count < MAX_DISKS; i++) {
        uint32_t di = sel_indices[i];
        ensure_drive_mapped(di);
        pp->ids[pp->count] = di;
        pp->sizes_mb[pp->count] = (i < S()->disk.disk_count && S()->disk.pool_sizes_mb[i] > 0) ? S()->disk.pool_sizes_mb[i] : default_size;
        pp->count++;
    }
    return RC_OK;
}

static RC init_pools_create_files(POOL_PAIRS* pp) {
    for (uint32_t i = 0; i < pp->count; i++) {
        uint32_t di = pp->ids[i];
        DISK_INFO* disk = device_get(di);
        if (!disk) { LOG_ERROR("Disk %u not found", di); return RC_ERR_INVALID_DISK; }
        if (!disk->drive_letter[0]) { char model_a[MAX_MODEL_LEN] = {0}; wcstombs(model_a, disk->model, MAX_MODEL_LEN - 1); LOG_WARN("Skip %s (no drive letter)", model_a); continue; }
        LOG_INFO("Creating pool file on %ls: %llu MB ...", disk->drive_letter, (unsigned long long)pp->sizes_mb[i]);
        if (!volume_create_pool_file(disk, pp->sizes_mb[i])) {
            LOG_ERROR("Failed to create pool on disk %u, rolling back %u pool(s)", di, S()->disk.disk_count);
            for (uint32_t j = 0; j < S()->disk.disk_count; j++) {
                volume_close_pool_file(S()->disk.disks[j]);
                volume_delete_pool_file(S()->disk.disks[j]);
            }
            S()->disk.disk_count = 0;
            return RC_ERR_ROLLBACK;
        }
        S()->disk.pool_sizes_mb[S()->disk.disk_count] = pp->sizes_mb[i];
        S()->disk.disks[S()->disk.disk_count++] = disk;
    }
    return RC_OK;
}

RC raid_init_pools_locked(int argc, char* argv[]) {
    if (S()->rt.state != STATE_DISCOVERED && S()->rt.state != STATE_UNMOUNTED) {
        LOG_ERROR("Not allowed in '%s' (need DISCOVERED or UNMOUNTED)", raid_state_str(S()->rt.state));
        return RC_ERR_INVALID_STATE;
    }
    if (device_get_count() == 0) { LOG_ERROR("No physical disks. Run 'scan' first"); return RC_ERR_INVALID_STATE; }
    if (S()->rt.state == STATE_UNMOUNTED)
        cleanup_pool_session(S());

    POOL_PAIRS pp = {0};
    RC rc;
    bool has_colon = false;
    for (int i = 0; i < argc; i++) { if (strchr(argv[i], ':')) { has_colon = true; break; } }

    if (has_colon) {
        rc = init_pools_from_pairs(argc, argv, &pp);
    } else if (argc == 1) {
        uint32_t first_id = 0;
        if (!safe_atou32(argv[0], &first_id)) first_id = UINT32_MAX;
        if (first_id >= device_get_count()) {
            uint64_t size = 0;
            if (!safe_atou64(argv[0], &size)) { LOG_ERROR("Invalid size '%s'", argv[0]); return RC_ERR_INVALID_ARG; }
            rc = init_pools_all_selected_with_size(size, &pp);
        } else {
            rc = init_pools_from_ids(argc, argv, &pp);
        }
    } else if (argc > 1) {
        rc = init_pools_from_ids(argc, argv, &pp);
    } else {
        rc = init_pools_from_selected(&pp);
    }
    if (rc != RC_OK) return rc;

    if (pp.count < MIN_DISKS || pp.count > MAX_DISKS) { LOG_ERROR("Need %d-%d disks", MIN_DISKS, MAX_DISKS); return RC_ERR_INVALID_ARG; }
    S()->disk.disk_count = 0;
    rc = init_pools_create_files(&pp);
    if (rc != RC_OK) return rc;

    if (S()->disk.disk_count >= MIN_DISKS) {
        S()->rt.state = STATE_INITIALIZED;
        LOG_OK("Pool files ready for %u disks", S()->disk.disk_count);
        return RC_OK;
    }
    LOG_ERROR("Failed to create pool files");
    return RC_ERR_IO;
}

RC raid_init_pools(int argc, char* argv[]) {
    gs_lock();
    RC rc = raid_init_pools_locked(argc, argv);
    gs_unlock();
    return rc;
}

/* ---- Create / Mirror ---- */
RC raid_create_locked(void) {
    RC rc; if ((rc = require(STATE_INITIALIZED)) != RC_OK) return rc;
    if (S()->disk.disk_count < MIN_DISKS) { LOG_ERROR("Not enough disks (need %d)", MIN_DISKS); return RC_ERR_INVALID_STATE; }
    if (!volume_create(&S()->vol.volume, S()->disk.disks, S()->disk.disk_count)) return RC_ERR_IO;
    S()->vol.volume_valid = true;
    S()->rt.state = STATE_MOUNTED;
    S()->cache.cache_on = false;
    S()->cache.flush_thread = NULL;
    return RC_OK;
}

RC raid_create(void) {
    gs_lock();
    RC rc = raid_create_locked();
    gs_unlock();
    return rc;
}

RC raid_mirror(void) {
    gs_lock();
    RC rc; if ((rc = require(STATE_INITIALIZED)) != RC_OK) { gs_unlock(); return rc; }
    if (S()->disk.disk_count < MIN_MIRROR_DISKS) { LOG_ERROR("Not enough disks (need %d)", MIN_MIRROR_DISKS); gs_unlock(); return RC_ERR_INVALID_STATE; }
    if (!volume_mirror(&S()->vol.volume, S()->disk.disks, S()->disk.disk_count)) { gs_unlock(); return RC_ERR_IO; }
    S()->vol.volume_valid = true;
    S()->rt.state = STATE_MOUNTED;
    S()->cache.cache_on = false;
    S()->cache.flush_thread = NULL;
    gs_unlock();
    return RC_OK;
}

RC raid_expand(int argc, char* argv[]) {
    gs_lock();
    RC rc; if ((rc = require(STATE_MOUNTED)) != RC_OK) { gs_unlock(); return rc; }
    if (!volume_expand(&S()->vol.volume, S()->disk.physical_disks, S()->disk.physical_count, argc, argv)) {
        gs_unlock(); return RC_ERR_IO;
    }
    gs_unlock();
    return RC_OK;
}

RC raid_rebuild(int argc, char* argv[]) {
    gs_lock();
    if (S()->rt.state != STATE_MOUNTED && S()->rt.state != STATE_DEGRADED) {
        LOG_ERROR("Not allowed in '%s' (need MOUNTED or DEGRADED)", raid_state_str(S()->rt.state));
        gs_unlock(); return RC_ERR_INVALID_STATE;
    }
    if (argc < 2) { LOG_ERROR("Usage: rebuild <disk_idx> <new_disk_id> <pool_mb>"); gs_unlock(); return RC_ERR_INVALID_ARG; }
    uint32_t replace_idx = 0, new_disk_id = 0;
    if (!safe_atou32(argv[0], &replace_idx)) { LOG_ERROR("Invalid disk index '%s'", argv[0]); gs_unlock(); return RC_ERR_INVALID_ARG; }
    if (!safe_atou32(argv[1], &new_disk_id)) { LOG_ERROR("Invalid disk id '%s'", argv[1]); gs_unlock(); return RC_ERR_INVALID_ARG; }
    uint64_t pool_mb = 1024;
    if (argc >= 3 && !safe_atou64(argv[2], &pool_mb)) { LOG_ERROR("Invalid pool size '%s'", argv[2]); gs_unlock(); return RC_ERR_INVALID_ARG; }
    if (pool_mb < 1024) pool_mb = 1024;
    if (!volume_rebuild(&S()->vol.volume, S()->disk.physical_disks, S()->disk.physical_count,
                        replace_idx, new_disk_id, pool_mb)) {
        gs_unlock(); return RC_ERR_IO;
    }
    gs_unlock();
    return RC_OK;
}

/* ---- Mount ---- */
RC raid_mount_locked(char drive_letter) {
    RC rc; if ((rc = require(STATE_MOUNTED)) != RC_OK) return rc;
    if (!S()->vol.volume_valid) { LOG_ERROR("Volume not created"); return RC_ERR_INVALID_STATE; }
    if (S()->rt.mounted) { LOG_WARN("Already mounted at %c:", S()->vol.volume.mount_point[0]); return RC_ERR_ALREADY; }
    if (!volume_mount(&S()->vol.volume, drive_letter)) { LOG_ERROR("Mount failed at %c:", drive_letter); return RC_ERR_MOUNT; }
    S()->rt.mounted = true;
    return RC_OK;
}

RC raid_mount(char drive_letter) {
    gs_lock();
    RC rc = raid_mount_locked(drive_letter);
    gs_unlock();
    return rc;
}

RC raid_unmount(void) {
    gs_lock();
    RC rc; if ((rc = require(STATE_MOUNTED)) != RC_OK) { gs_unlock(); return rc; }
    volume_unmount(&S()->vol.volume, &S()->cache.cache_on, &S()->cache.flush_thread, &S()->rt.mounted);
    S()->rt.state = STATE_UNMOUNTED;
    S()->vol.volume_valid = false;
    LOG_OK("Unmounted (pool files preserved for 'load')");
    gs_unlock();
    return RC_OK;
}

RC raid_load(const wchar_t* drive_root) {
    gs_lock();
    if (S()->rt.state != STATE_UNMOUNTED && S()->rt.state != STATE_DISCOVERED) {
        LOG_ERROR("Not allowed in '%s' (need UNMOUNTED or DISCOVERED)", raid_state_str(S()->rt.state));
        gs_unlock(); return RC_ERR_INVALID_STATE;
    }
    wchar_t root[4];
    if (drive_root) {
        wcscpy_s(root, 4, drive_root);
    } else {
        wcscpy_s(root, 4, L"C:\\");
    }
    LOG_INFO("Loading volume from %ls%ls...", root, CONFIG_DIR);
    uint32_t loaded = 0;
    if (!volume_load(&S()->vol.volume, S()->disk.loaded_disks, &loaded,
                     S()->disk.physical_disks, S()->disk.physical_count, root)) {
        LOG_ERROR("Failed to load volume");
        gs_unlock(); return RC_ERR_METADATA;
    }
    S()->disk.disk_count = loaded;
    for (uint32_t i = 0; i < loaded; i++)
        S()->disk.disks[i] = &S()->disk.loaded_disks[i];
    S()->vol.volume_valid = true;
    S()->rt.state = STATE_MOUNTED;
    gs_unlock();
    return RC_OK;
}

RC raid_destroy(void) {
    gs_lock();
    if (S()->rt.state != STATE_MOUNTED && S()->rt.state != STATE_UNMOUNTED) {
        LOG_ERROR("Not allowed in '%s' (need MOUNTED or UNMOUNTED)", raid_state_str(S()->rt.state));
        gs_unlock(); return RC_ERR_INVALID_STATE;
    }
    event_bus_publish(EVENT_VOLUME_DESTROYED, "destroy");
    volume_destroy(&S()->vol.volume, S()->disk.disks, S()->disk.disk_count,
                   &S()->cache.cache_on, &S()->cache.flush_thread, &S()->rt.mounted, &S()->rt.state);
    S()->vol.volume_valid = false;
    LOG_OK("Volume destroyed (all pool files, superblocks, journals deleted)");
    gs_unlock();
    return RC_OK;
}

RC raid_purge(void) {
    gs_lock();
    if (S()->rt.state != STATE_INITIALIZED && S()->rt.state != STATE_UNMOUNTED) {
        LOG_ERROR("Not allowed in '%s' (need INITIALIZED or UNMOUNTED). Use 'unmount' first.", raid_state_str(S()->rt.state));
        gs_unlock(); return RC_ERR_INVALID_STATE;
    }
    cleanup_pool_session(S());
    S()->rt.state = STATE_DISCOVERED;
    S()->vol.volume_valid = false;
    S()->cache.cache_on = false;
    S()->rt.mounted = false;
    LOG_OK("Pool files and superblock deleted");
    gs_unlock();
    return RC_OK;
}

/* ---- Cache ---- */
RC raid_cache_locked(int argc, char* argv[]) {
    RC rc; if ((rc = require(STATE_MOUNTED)) != RC_OK) return rc;
    if (argc > 0 && strcmp(argv[0], "wt") == 0) {
        if (!S()->cache.cache_on) { LOG_ERROR("Enable cache first ('cache <sizeMB>')"); return RC_ERR_INVALID_STATE; }
        S()->vol.volume.cache.write_through = !S()->vol.volume.cache.write_through;
        LOG_OK("Write-through cache %s", S()->vol.volume.cache.write_through ? "ON" : "OFF");
        return RC_OK;
    }
    if (argc > 0 && strcmp(argv[0], "off") == 0) {
        if (!S()->cache.cache_on) { LOG_WARN("Cache already off"); return RC_ERR_ALREADY; }
        cleanup_volume_cache(&S()->vol.volume);
        S()->cache.flush_thread = NULL;
        S()->cache.cache_on = false;
        event_bus_publish(EVENT_CACHE_CHANGED, "off");
        LOG_OK("Write-back cache disabled and flushed");
        return RC_OK;
    }
    if (S()->cache.cache_on) { LOG_WARN("Cache already on. Use 'cache off' first to reinit"); return RC_ERR_ALREADY; }
    uint32_t size_mb = CACHE_DEFAULT_MB;
    if (argc > 0) {
        if (!safe_atou32(argv[0], &size_mb)) { LOG_ERROR("Invalid cache size '%s'", argv[0]); return RC_ERR_INVALID_ARG; }
    }
    if (size_mb < 256) size_mb = 256;
    LOG_INFO("Initializing %u MB RAM write-back cache...", size_mb);
    if (!cache_init(&S()->vol.volume.cache, (uint64_t)size_mb * 1024ULL * 1024ULL)) {
        LOG_ERROR("Cache init failed"); return RC_ERR_CACHE;
    }
    S()->vol.volume.cache_enabled = true;
    S()->cache.cache_on = true;
    S()->cache.cache_mb = size_mb;
    S()->vol.volume.cache.flush_thread = S()->cache.flush_thread = (HANDLE)_beginthreadex(NULL, 0, cache_flush_thread, &S()->vol.volume, 0, NULL);
    if (S()->cache.flush_thread) LOG_OK("Background flush thread started (1s interval)");
    event_bus_publish(EVENT_CACHE_CHANGED, "on");
    LOG_OK("Write-back cache enabled: %u MB (block size=%u KB)", size_mb, CACHE_BLOCK_SIZE / 1024);
    return RC_OK;
}

RC raid_cache(int argc, char* argv[]) {
    gs_lock();
    RC rc = raid_cache_locked(argc, argv);
    gs_unlock();
    return rc;
}

RC raid_test(void) {
    gs_lock();
    RC rc; if ((rc = require(STATE_MOUNTED)) != RC_OK) { gs_unlock(); return rc; }
    stripe_volume_verify_io(&S()->vol.volume);
    gs_unlock();
    return RC_OK;
}

RC raid_random(int argc, char** args) {
    gs_lock();
    RC rc; if ((rc = require(STATE_MOUNTED)) != RC_OK) { gs_unlock(); return rc; }
    uint32_t ops = 100;
    uint32_t max_kb = 64;
    if (argc > 0) ops = (uint32_t)atol(args[0]);
    if (argc > 1) max_kb = (uint32_t)atol(args[1]);
    if (ops < 1) ops = 1;
    if (max_kb < 4) max_kb = 4;
    if (max_kb > 1024) max_kb = 1024;
    stripe_volume_random_test(&S()->vol.volume, ops, max_kb);
    gs_unlock();
    return RC_OK;
}

RC raid_benchfs(int argc, char* argv[]) {
    gs_lock();
    RC rc; if ((rc = require(STATE_MOUNTED)) != RC_OK) { gs_unlock(); return rc; }
    uint32_t size_mb = 512, block_kb = 1024;
    if (argc > 0 && !safe_atou32(argv[0], &size_mb)) { LOG_ERROR("Invalid size '%s'", argv[0]); gs_unlock(); return RC_ERR_INVALID_ARG; }
    if (argc > 1 && !safe_atou32(argv[1], &block_kb)) { LOG_ERROR("Invalid block size '%s'", argv[1]); gs_unlock(); return RC_ERR_INVALID_ARG; }
    if (size_mb < 64) size_mb = 64;
    if (size_mb > 4096) size_mb = 4096;
    if (block_kb < 4) block_kb = 4;
    if (block_kb > 8192) block_kb = 8192;
    if (block_kb > size_mb * 1024) block_kb = size_mb * 1024;
    bench_volume(&S()->vol.volume, size_mb, block_kb);
    gs_unlock();
    return RC_OK;
}

RC raid_simulate(int argc, char* argv[]) {
    gs_lock();
    if (S()->rt.state != STATE_MOUNTED) {
        LOG_ERROR("Not allowed in '%s' (need MOUNTED)", raid_state_str(S()->rt.state));
        gs_unlock(); return RC_ERR_INVALID_STATE;
    }
    if (argc < 2) { LOG_ERROR("Usage: simulate <disk_idx> <mode>  (mode: fail, healthy, disconnect)"); gs_unlock(); return RC_ERR_INVALID_ARG; }
    uint32_t idx = 0;
    if (!safe_atou32(argv[0], &idx)) { LOG_ERROR("Invalid disk index '%s'", argv[0]); gs_unlock(); return RC_ERR_INVALID_ARG; }
    if (idx >= S()->vol.volume.disk_count) { LOG_ERROR("Invalid disk index %u", idx); gs_unlock(); return RC_ERR_INVALID_ARG; }
    DISK_INFO* d = S()->vol.volume.disks[idx];
    char mode = argv[1][0];
    switch (mode) {
        case 'f':
            if (InterlockedExchange(&d->healthy, 0) == 1) { d->faulty = true; InterlockedDecrement(&S()->vol.volume.healthy_count); }
            S()->rt.state = STATE_DEGRADED;
            LOG_WARN("Simulated: disk %u -> FAILED", idx);
            event_bus_publish(EVENT_ERROR, "simulate: disk failed");
            break;
        case 'h':
            InterlockedExchange(&d->healthy, 1); d->faulty = false;
            InterlockedIncrement(&S()->vol.volume.healthy_count);
            if (S()->vol.volume.healthy_count >= S()->vol.volume.disk_count) S()->rt.state = STATE_MOUNTED;
            LOG_OK("Simulated: disk %u -> HEALTHY", idx);
            break;
        case 'd':
            volume_close_pool_file(d);
            if (InterlockedExchange(&d->healthy, 0) == 1) { d->faulty = true; InterlockedDecrement(&S()->vol.volume.healthy_count); }
            S()->rt.state = STATE_DEGRADED;
            LOG_WARN("Simulated: disk %u -> DISCONNECTED", idx);
            event_bus_publish(EVENT_ERROR, "simulate: disk disconnected");
            break;
        default:
            LOG_ERROR("Unknown mode '%c' (f= fail, h= healthy, d= disconnect)", mode);
            gs_unlock(); return RC_ERR_INVALID_ARG;
    }
    gs_unlock();
    return RC_OK;
}

/* ---- Config ---- */
RC raid_config_save_locked(void) {
    APP_CONFIG* cfg = &S()->cfg.config;
    cfg->disk_count = 0;
    for (uint32_t i = 0; i < device_get_count(); i++) {
        DISK_INFO* d = device_get(i);
        if (d && d->selected && cfg->disk_count < MAX_DISKS) {
            cfg->disks[cfg->disk_count].disk_id = i;
            cfg->disks[cfg->disk_count].drive_letter = (char)d->drive_letter[0];
            cfg->disks[cfg->disk_count].pool_mb = (d->pool_bytes + (1024 * 1024 - 1)) / (1024 * 1024);
            cfg->disk_count++;
        }
    }
    cfg->cache_mb = S()->cache.cache_mb;
    cfg->mount_letter = S()->vol.volume.mount_point[0] ? S()->vol.volume.mount_point[0] : 'G';
    cfg->auto_bench = true;
    config_save(cfg);
    return RC_OK;
}

RC raid_config_save(void) {
    gs_lock();
    RC rc = raid_config_save_locked();
    gs_unlock();
    return rc;
}

RC raid_config_load(void) {
    gs_lock();
    config_load(&S()->cfg.config);
    LOG_OK("Config loaded. Use 'scan' + 'select' + 'create' to restore");
    gs_unlock();
    return RC_OK;
}

/* ---- Wizard / Quick ---- */
RC raid_wizard(void) {
    gs_lock();
    wizard_run(S());
    gs_unlock();
    return RC_OK;
}

RC raid_quick(void) {
    gs_lock();
    RC rc;
    rc = raid_scan_locked(); if (rc != RC_OK) { gs_unlock(); return rc; }
    if (device_get_count() < MIN_DISKS) { LOG_ERROR("Need at least %d disks", MIN_DISKS); gs_unlock(); return RC_ERR_INVALID_STATE; }
    printf("\n  Enter disk selections and pool sizes (e.g. 0:51200 3:102400 4:51200):\n  > ");
    fflush(stdout);
    char input[256];
    if (!fgets(input, sizeof(input), stdin)) { gs_unlock(); return RC_ERR_INVALID_ARG; }
    char* args[16]; int argc = 0;
    char* ctx = NULL; char* tok = strtok_s(input, " \t\r\n", &ctx);
    while (tok && argc < 16) { args[argc++] = tok; tok = strtok_s(NULL, " \t\r\n", &ctx); }
    if (argc < MIN_DISKS) { LOG_ERROR("Need %d-%d disks", MIN_DISKS, MAX_DISKS); gs_unlock(); return RC_ERR_INVALID_ARG; }
    rc = raid_init_pools_locked(argc, args); if (rc != RC_OK) { gs_unlock(); return rc; }
    rc = raid_create_locked(); if (rc != RC_OK) { gs_unlock(); return rc; }
    uint64_t total_pool = 0;
    for (uint32_t i = 0; i < S()->disk.disk_count; i++) total_pool += S()->disk.pool_sizes_mb[i];
    uint64_t total_gb = total_pool / 1024; if (total_gb == 0) total_gb = 1;
    uint32_t max_cache = (uint32_t)min_u64(total_gb * 1024, 4096);
    uint32_t cache_mb = max_cache < CACHE_DEFAULT_MB ? max_cache : CACHE_DEFAULT_MB;
    char cache_arg[16]; snprintf(cache_arg, 16, "%u", cache_mb);
    char* cache_argv[] = { cache_arg };
    rc = raid_cache_locked(1, cache_argv); if (rc != RC_OK) LOG_WARN("Cache setup failed (rc=%d)", rc);
    char mount_letter = S()->cfg.config.mount_letter ? S()->cfg.config.mount_letter : 'G';
    rc = raid_mount_locked(mount_letter); if (rc != RC_OK) LOG_WARN("Mount failed (rc=%d)", rc);
    raid_config_save_locked();
    LOG_OK("Quick setup complete! Volume mounted at %c:. Type 'exit' to quit.", mount_letter);
    gs_unlock();
    return RC_OK;
}
