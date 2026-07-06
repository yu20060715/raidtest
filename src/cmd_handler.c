#include "cmd_handler.h"
#include "raid_service.h"
#include "disk_scanner.h"
#include "bench_io.h"
#include "fuse_bridge.h"
#include "config.h"
#include "wizard.h"
#include "daemon.h"
#include "cleanup.h"

APP_STATE g_state = {0};
CRITICAL_SECTION g_state_cs;

void cleanup_cs(void) { DeleteCriticalSection(&g_state_cs); }

RC cmd_init_all(void) { return raid_init(); }

void cmd_cleanup_all(void) { raid_cleanup(); }

void cmd_print_banner(void) {
    printf("\n");
    printf("  ==============================================\n");
    printf("    RAIDTEST v1.0 RC2 - Asymmetric Stripe Engine\n");
    printf("    Non-撖寧妍敹急瘛瑟 RAID 0 ?撘?\n");
    printf("    ?舀: SATA SSD + NVMe SSD ?芰瘛瑟\n");
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

/* ---- Help (unchanged, still Chinese) ---- */
static void cmd_help(void) {
    cmd_print_banner();
    printf("  COMMANDS:\n");
    printf("    scan                         ???祕擃?SSD\n");
    printf("    mapdrive <id> <driver>       蝬?蝤??唳局??(靘? mapdrive 0 F)\n");
    printf("    select <id1> <id2> ...       蝑? 'init id1 id2 ...' (?豢?+撱箇? pool)\n");
    printf("    bench [sizeMB]               撠銝剔?蝣??祕皜祇?(?身 512MB)\n");
    printf("    init [id...|id:mb...]         撱箇? pool 瑼? (靘? init 0 2, init 0:25600 3:51200)\n");
    printf("    create                       撱箇???蝔望?撣嗅??蝤? (RAID0)\n");
    printf("    mirror                       撱箇??∪??蝤? (RAID1)\n");
    printf("    rebuild <idx> <disk> [MB]    ?湔????∪?蝤?銝阡?撱箄??n");
    printf("    cache [sizeMB]               ? RAM Write-back 敹怠? (?身 %u MB)\n", CACHE_DEFAULT_MB);
    printf("    cache wt                     ?? Write-through 璅∪? (?活頛詨?? Write-back)\n");
    printf("    cache off                    ?敹怠?銝血??蝣n");
    printf("    mount [隞??]                  ????Windows 蝤?璈?(? WinFsp)\n");
    printf("    unmount                      ?貉?蝤?璈?(靽? pool 瑼?嚗??load 頛)\n");
    printf("    load                         敺?superblock ?頛 volume (靽?銝活鞈?)\n");
    printf("    purge                        ?芷 pool 瑼? + superblock (摰皜)\n");
    printf("    test                         ?瑁? IO 撽? (撖怠+霈??瘥?)\n");
    printf("    benchfs [sizeMB] [blockKB]   ?湔撠???霈撖急葫??(??CDM)\n");
    printf("    cleanup                      皜????pool 瑼????冗?身摰n");
    printf("    wizard                       鈭?蝎暸?: 撘?撘身摰???\n");
    printf("    config-save                  ?脣??桀?閮剖???JSON\n");
    printf("    config-load                  頛銝活?脣??身摰n");
    printf("    quick                        敹恍身摰? scan+?貊+init+create+cache+mount\n");
    printf("    info                         憿舐內?蝤?鞈? (??cache dirty ratio)\n");
    printf("    map                          憿舐內 LBA ??銵?(??64MB)\n");
    printf("    random <ops> [maxKB]         ?冽? I/O 憯?皜祈岫 (撖怠+霈??瘥?)\n");
    printf("    destroy                      皜 volume (unmount + ?芷 pool/superblock/journal)\n");
    printf("    metadata [隞??]               憿舐內 superblock ??n");
    printf("    check                        ?亙熒瑼Ｘ: 撽????蝣? superblock\n");
    printf("    simulate <idx> <f|h|d>       璅⊥蝤???/敺拙?/?瑞?\n");
    printf("    planner                      摰寥?閬?: 憿舐內??RAID 蝝?舐摰寥?\n");
    printf("    events                       憿舐內鈭辣?亥?\n");
    printf("    status                       ?單????\n");
    printf("    help                         憿舐內隤芣?\n");
    printf("    exit                         ?ａ?\n");
    printf("\n");
    printf("  QUICK START:\n");
    printf("    quick               ->  scan + bench + select + init + mount (all-in-one)\n");
    printf("    scan; select...     ->  manual step-by-step\n");
    printf("    (no args)           ->  auto-restore from config, or enter quick mode\n");
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

static void auto_restore_or_quick(void) {
    config_load(&g_state.cfg.config);
    if (g_state.cfg.config.disk_count > 0) {
        LOG_INFO("Restoring from saved config...");
        raid_scan();
        for (uint32_t i = 0; i < g_state.cfg.config.disk_count; i++) {
            APP_CONFIG* cfg = &g_state.cfg.config;
            if (!g_state.disk.physical_disks || !cfg || cfg->disk_count == 0) break;
            char dl_str[2] = { cfg->disks[0].drive_letter, 0 };
            raid_mapdrive(cfg->disks[0].disk_id, dl_str);
            break;
        }
        LOG_INFO("Use 'load' to restore volume from superblock.");
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
    gs_lock();
    if (strcmp(args[0], "exit") == 0 || strcmp(args[0], "quit") == 0) { gs_unlock(); return false; }
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
    gs_unlock();
    return true;
}

void cmd_interactive(void) {
    cmd_print_banner();
    LOG_INFO("Type 'help' for commands\n");
    char input[512];
    while (1) {
        printf("raidtest> ");
        fflush(stdout);
        if (!fgets(input, sizeof(input), stdin)) break;
        if (!cmd_process(input)) break;
    }
    LOG_INFO("Shutting down...");
    gs_lock(); raid_cleanup(); gs_unlock();
}
