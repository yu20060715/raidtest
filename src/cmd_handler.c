#include "cmd_handler.h"
#include "raid_service.h"
#include "config.h"

APP_STATE g_state = {0};
CRITICAL_SECTION g_state_cs;

void cleanup_cs(void) { DeleteCriticalSection(&g_state_cs); }

RC cmd_init_all(void) { return raid_init(); }

void cmd_cleanup_all(void) { raid_cleanup(); }

void cmd_print_banner(void) {
    printf("\n");
    printf("  ==============================================\n");
    printf("    RAIDTEST v1.0 RC4 - Asymmetric Stripe Engine\n");
    printf("    Virtual RAID 0 across mixed-speed physical disks\n");
    printf("    Supports: SATA SSD + NVMe SSD + HDD mixed arrays\n");
    printf("  ==============================================\n");
    printf("\n");
}

/* ---- Command wrappers (thin CLI layer) ---- */

static RC cmd_scan(void) { return raid_scan(); }

static RC cmd_mapdrive(int argc, char* argv[]) {
    if (argc < 2) { LOG_ERROR("Usage: mapdrive <disk_id> <drive_letter>"); return RC_ERR_INVALID_ARG; }
    uint32_t disk_id = 0;
    if (!safe_atou32(argv[0], &disk_id)) { LOG_ERROR("Invalid disk id '%s'", argv[0]); return RC_ERR_INVALID_ARG; }
    return raid_mapdrive(disk_id, argv[1]);
}

static RC cmd_bench(int argc, char* argv[]) { return raid_bench(argc, argv); }

static RC cmd_init(int argc, char* argv[]) { return raid_init_pools(argc, argv); }

static RC cmd_select(int argc, char* argv[]) { return raid_select(argc, argv); }

static RC cmd_create(void) { return raid_create(); }

static RC cmd_expand(int argc, char* argv[]) { return raid_expand(argc, argv); }

static RC cmd_mirror(void) { return raid_mirror(); }

static RC cmd_rebuild(int argc, char* argv[]) { return raid_rebuild(argc, argv); }

RC cmd_cache(int argc, char* argv[]) { return raid_cache(argc, argv); }

RC cmd_mount(int argc, char* argv[]) {
    char drive = g_state.cfg.config.mount_letter ? g_state.cfg.config.mount_letter : 'G';
    if (argc > 0 && argv[0][0] >= 'A' && argv[0][0] <= 'Z') drive = argv[0][0];
    return raid_mount(drive);
}

static RC cmd_unmount(void) { return raid_unmount(); }

static RC cmd_load(int argc, char* argv[]) {
    const wchar_t* root = NULL;
    wchar_t buf[4] = L"C:\\";
    if (argc > 0 && argv[0][0] >= 'A' && argv[0][0] <= 'Z') {
        buf[0] = (wchar_t)argv[0][0];
        root = buf;
    }
    return raid_load(root);
}

static RC cmd_purge(void) { return raid_purge(); }

RC cmd_destroy(int argc, char* argv[]) { (void)argc; (void)argv; return raid_destroy(); }

RC cmd_metadata(int argc, char* argv[]) { return raid_metadata(argc, argv); }

RC cmd_check(int argc, char* argv[]) { (void)argc; (void)argv; return raid_check(); }

RC cmd_simulate(int argc, char* argv[]) { return raid_simulate(argc, argv); }

RC cmd_planner(int argc, char* argv[]) { (void)argc; (void)argv; return raid_planner(); }

RC cmd_event_log(int argc, char* argv[]) { (void)argc; (void)argv; return raid_events(); }

static RC cmd_test(void) { return raid_test(); }

static RC cmd_random(int argc, char** args) { return raid_random(argc, args); }

static RC cmd_info(void) { return raid_info(); }

static RC cmd_map(void) { return raid_map(); }

RC cmd_status(void) { return raid_status(); }

static RC cmd_benchfs(int argc, char* argv[]) { return raid_benchfs(argc, argv); }

static RC cmd_wizard(void) { return raid_wizard(); }

static RC cmd_config_save(void) { return raid_config_save(); }

static RC cmd_config_load(void) { return raid_config_load(); }

static RC cmd_quick(void) { return raid_quick(); }

/* ---- Help (English) ---- */
static void cmd_help(void) {
    cmd_print_banner();
    printf("  COMMANDS:\n");
    printf("    scan                         Scan physical disks + auto-benchmark\n");
    printf("    mapdrive <id> <letter>       Assign drive letter to a disk\n");
    printf("    select <id1> <id2> ...       Select disks for RAID\n");
    printf("    bench [sizeMB]               Benchmark selected disks (default 512 MB)\n");
    printf("    init [id...|id:mb...]         Create pool files (e.g. init 0 2, init 0:25600 3:51200)\n");
    printf("    create                       Create RAID0 stripe volume\n");
    printf("    mirror                       Create RAID1 mirror volume\n");
    printf("    rebuild <idx> <disk> [MB]    Replace failed mirror disk and rebuild\n");
    printf("    cache [sizeMB]               Enable RAM write-back cache (default %u MB)\n", CACHE_DEFAULT_MB);
    printf("    cache wt                     Toggle write-through mode (only if cache enabled)\n");
    printf("    cache off                    Disable cache and flush to disk\n");
    printf("    mount [drive letter]         Mount volume to a drive letter via WinFsp\n");
    printf("    unmount                      Unmount volume (pool files preserved for 'load')\n");
    printf("    load                         Restore volume from on-disk superblock\n");
    printf("    purge                        Delete pool files + superblock (irreversible)\n");
    printf("    test                         Run I/O verification (write + read + verify)\n");
    printf("    benchfs [sizeMB] [blockKB]   Filesystem-level benchmark (like CrystalDiskMark)\n");
    printf("    cleanup                      Release all RAID resources\n");
    printf("    wizard                       Guided 8-step setup wizard\n");
    printf("    config-save                  Save current configuration to JSON\n");
    printf("    config-load                  Load configuration from JSON\n");
    printf("    quick                        All-in-one: scan + bench + select + init + create + cache + mount\n");
    printf("    info                         Display volume info (capacity, cache, stripe phases)\n");
    printf("    map                          Display LBA to disk mapping table\n");
    printf("    random <ops> [maxKB]         Random I/O stress test (write + read + verify)\n");
    printf("    destroy                      Unmount + delete all pool/superblock/journal files\n");
    printf("    metadata [drive]             Dump superblock contents\n");
    printf("    check                        Full health check: disks + pool files + superblock\n");
    printf("    simulate <idx> <f|h|d>       Simulate disk failure/healthy/disconnect\n");
    printf("    planner                      RAID capacity and efficiency calculator\n");
    printf("    events                       Browse event history log\n");
    printf("    expand <disk_id:mb> ...      Expand volume by adding new disks to stripe\n");
    printf("    status                       Live status dashboard\n");
    printf("    help                         Show this help\n");
    printf("    exit                         Exit the program\n");
    printf("\n");
    printf("  QUICK START:\n");
    printf("    quick               ->  scan + bench + select + init + cache + mount (all-in-one)\n");
    printf("    scan; select...     ->  manual step-by-step\n");
    printf("    (no args)           ->  auto-restore from config, or quick mode\n");
    printf("\n");
}

static const char* rc_str(RC rc) {
    switch (rc) {
        case RC_OK:                return "OK";
        case RC_ERR_GENERIC:       return "GENERIC";
        case RC_ERR_INVALID_STATE: return "INVALID_STATE";
        case RC_ERR_INVALID_ARG:   return "INVALID_ARG";
        case RC_ERR_INVALID_DISK:  return "INVALID_DISK";
        case RC_ERR_IO:            return "IO_ERROR";
        case RC_ERR_METADATA:      return "METADATA_ERROR";
        case RC_ERR_CACHE:         return "CACHE_ERROR";
        case RC_ERR_MOUNT:         return "MOUNT_ERROR";
        case RC_ERR_NOT_FOUND:     return "NOT_FOUND";
        case RC_ERR_ALREADY:       return "ALREADY";
        case RC_ERR_NO_SPACE:      return "NO_SPACE";
        case RC_ERR_ROLLBACK:      return "ROLLBACK";
        default:                   return "UNKNOWN";
    }
}

/* ---- Command dispatcher (unchanged) ---- */

void do_restore(char mount_letter) {
    if (g_state.cfg.config.disk_count == 0) { LOG_ERROR("No config to restore"); return; }
    LOG_INFO("Restoring from config (%u disks, mount at %c:)...", g_state.cfg.config.disk_count, mount_letter);
    RC rc;
    rc = raid_scan();
    if (rc != RC_OK) { LOG_ERROR("No disks detected"); return; }
    char id_size_buf[256];
    int pos = 0;
    for (uint32_t i = 0; i < g_state.cfg.config.disk_count; i++) {
        DISK_CONFIG* dc = &g_state.cfg.config.disks[i];
        if (dc->disk_id >= device_get_count()) {
            LOG_ERROR("Disk %u from config not found", dc->disk_id);
            return;
        }
        char dl_str[2] = { dc->drive_letter, 0 };
        raid_mapdrive(dc->disk_id, dl_str);
        pos += snprintf(id_size_buf + pos, (size_t)(256 - pos), "%s%u:%llu",
                        i > 0 ? " " : "",
                        dc->disk_id, (unsigned long long)dc->pool_mb);
        if (pos >= 256) { LOG_ERROR("Config too large"); return; }
    }
    char* args[MAX_DISKS];
    int argc = 0;
    char* ctx = NULL;
    char* tok = strtok_s(id_size_buf, " ", &ctx);
    while (tok && argc < MAX_DISKS) { args[argc++] = tok; tok = strtok_s(NULL, " ", &ctx); }
    if (argc < MIN_DISKS) { LOG_ERROR("Need at least %d disks", MIN_DISKS); return; }
    rc = raid_init_pools(argc, args);
    if (rc != RC_OK) { LOG_ERROR("Failed to initialize pool files"); return; }
    rc = raid_create();
    if (rc != RC_OK) { LOG_ERROR("Failed to create volume"); return; }
    if (g_state.cfg.config.cache_mb > 0) {
        char cache_str[16];
        snprintf(cache_str, 16, "%u", g_state.cfg.config.cache_mb);
        char* cache_av[] = { cache_str };
        raid_cache(1, cache_av);
    }
    rc = raid_mount(mount_letter);
    if (rc != RC_OK && rc != RC_ERR_ALREADY) {
        LOG_WARN("Mount failed (rc=%d)", rc);
    }
}

static void auto_restore_or_quick(void) {
    config_load(&g_state.cfg.config);
    if (g_state.cfg.config.disk_count > 0) {
        raid_scan();
        do_restore(g_state.cfg.config.mount_letter ? g_state.cfg.config.mount_letter : 'G');
    } else {
        raid_quick();
    }
}

void cmd_process_auto(const char* cmd_str) {
    if (!cmd_str || cmd_str[0] == '\0' || cmd_str[0] == '\n') {
        auto_restore_or_quick();
        return;
    }
    cmd_process(cmd_str);
}

bool cmd_process(const char* input) {
    char buf[512];
    strncpy_s(buf, sizeof(buf), input, _TRUNCATE);
    char* args[32]; int argc = 0;
    char* ctx = NULL;
    char* tok = strtok_s(buf, " \t\r\n", &ctx);
    while (tok && argc < 32) { args[argc++] = tok; tok = strtok_s(NULL, " \t\r\n", &ctx); }
    if (argc == 0) return true;

    RC rc = RC_OK;
    if (strcmp(args[0], "exit") == 0 || strcmp(args[0], "quit") == 0) { return false; }
    else if (strcmp(args[0], "help") == 0) { cmd_help(); }
    else if (strcmp(args[0], "scan") == 0) rc = cmd_scan();
    else if (strcmp(args[0], "mapdrive") == 0) rc = cmd_mapdrive(argc - 1, args + 1);
    else if (strcmp(args[0], "bench") == 0) rc = cmd_bench(argc - 1, args + 1);
    else if (strcmp(args[0], "select") == 0) rc = cmd_select(argc - 1, args + 1);
    else if (strcmp(args[0], "init") == 0) rc = cmd_init(argc - 1, args + 1);
    else if (strcmp(args[0], "create") == 0) rc = cmd_create();
    else if (strcmp(args[0], "mirror") == 0) rc = cmd_mirror();
    else if (strcmp(args[0], "expand") == 0) rc = cmd_expand(argc - 1, args + 1);
    else if (strcmp(args[0], "rebuild") == 0) rc = cmd_rebuild(argc - 1, args + 1);
    else if (strcmp(args[0], "cache") == 0) rc = cmd_cache(argc - 1, args + 1);
    else if (strcmp(args[0], "mount") == 0) rc = cmd_mount(argc - 1, args + 1);
    else if (strcmp(args[0], "unmount") == 0) rc = cmd_unmount();
    else if (strcmp(args[0], "load") == 0) rc = cmd_load(argc - 1, args + 1);
    else if (strcmp(args[0], "purge") == 0) rc = cmd_purge();
    else if (strcmp(args[0], "destroy") == 0) rc = cmd_destroy(argc - 1, args + 1);
    else if (strcmp(args[0], "test") == 0) rc = cmd_test();
    else if (strcmp(args[0], "random") == 0) rc = cmd_random(argc - 1, args + 1);
    else if (strcmp(args[0], "benchfs") == 0) rc = cmd_benchfs(argc - 1, args + 1);
    else if (strcmp(args[0], "check") == 0) rc = cmd_check(argc - 1, args + 1);
    else if (strcmp(args[0], "info") == 0) rc = cmd_info();
    else if (strcmp(args[0], "map") == 0) rc = cmd_map();
    else if (strcmp(args[0], "status") == 0) rc = cmd_status();
    else if (strcmp(args[0], "metadata") == 0) rc = cmd_metadata(argc - 1, args + 1);
    else if (strcmp(args[0], "simulate") == 0) rc = cmd_simulate(argc - 1, args + 1);
    else if (strcmp(args[0], "planner") == 0) rc = cmd_planner(argc - 1, args + 1);
    else if (strcmp(args[0], "events") == 0) rc = cmd_event_log(argc - 1, args + 1);
    else if (strcmp(args[0], "config-save") == 0) rc = cmd_config_save();
    else if (strcmp(args[0], "config-load") == 0) rc = cmd_config_load();
    else if (strcmp(args[0], "wizard") == 0) rc = cmd_wizard();
    else if (strcmp(args[0], "quick") == 0) rc = cmd_quick();
    else if (strcmp(args[0], "cleanup") == 0) { raid_cleanup(); LOG_OK("Cleanup complete"); }
    else { LOG_WARN("Unknown command: %s  (try 'help')", args[0]); }

    if (rc != RC_OK && rc != RC_ERR_ROLLBACK)
        LOG_WARN("Command failed: %s", rc_str(rc));
    return true;
}


