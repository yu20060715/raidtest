#include "cmd_handler.h"
#include "raid_service.h"
#include "config.h"
#include "daemon.h"
#include "wizard.h"
#include "fuse_bridge.h"
#include "disk_scanner.h"
#include "cleanup.h"
#include "gui.h"

static void do_restore(char mount_letter) {
    gs_lock();
    if (g_state.cfg.config.disk_count == 0) { LOG_ERROR("No config to restore"); gs_unlock(); return; }
    LOG_INFO("Restoring from config (%u disks, mount at %c:)...", g_state.cfg.config.disk_count, mount_letter);

    RC rc;
    rc = raid_scan();
    if (rc != RC_OK) { LOG_ERROR("No disks detected"); gs_unlock(); return; }

    char id_size_buf[256];
    int pos = 0;
    for (uint32_t i = 0; i < g_state.cfg.config.disk_count; i++) {
        DISK_CONFIG* dc = &g_state.cfg.config.disks[i];
        if (dc->disk_id >= device_get_count()) {
            LOG_ERROR("Disk %u from config not found", dc->disk_id);
            gs_unlock(); return;
        }
        char dl_str[2] = { dc->drive_letter, 0 };
        raid_mapdrive(dc->disk_id, dl_str);
        pos += snprintf(id_size_buf + pos, (size_t)(256 - pos), "%s%u:%llu",
                        i > 0 ? " " : "",
                        dc->disk_id, (unsigned long long)dc->pool_mb);
        if (pos >= 256) { LOG_ERROR("Config too large"); gs_unlock(); return; }
    }

    char* args[MAX_DISKS];
    int argc = 0;
    char* ctx = NULL;
    char* tok = strtok_s(id_size_buf, " ", &ctx);
    while (tok && argc < MAX_DISKS) { args[argc++] = tok; tok = strtok_s(NULL, " ", &ctx); }
    if (argc < MIN_DISKS) { LOG_ERROR("Need at least %d disks", MIN_DISKS); gs_unlock(); return; }

    rc = raid_init_pools(argc, args);
    if (rc != RC_OK) { LOG_ERROR("Failed to initialize pool files"); gs_unlock(); return; }

    rc = raid_create();
    if (rc != RC_OK) { LOG_ERROR("Failed to create volume"); gs_unlock(); return; }

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
    gs_unlock();
}

static int cli_main(int argc, char* argv[]) {
    cmd_init_all();

    if (argc > 1) {
        if (strcmp(argv[1], "--auto") == 0) {
            if (argc > 2) {
                char first = argv[2][0];
                if (first >= 'A' && first <= 'Z' && argv[2][1] == '\0') {
                    do_restore(first);
                } else {
                    cmd_process_auto(argv[2]);
                }
            } else {
                do_restore(g_state.cfg.config.mount_letter ? g_state.cfg.config.mount_letter : 'G');
            }
            if (g_state.rt.mounted)
                printf("\n  Volume mounted at %c:. Type 'exit' to unmount.\n\n", g_state.vol.volume.mount_point[0]);
        } else if (strcmp(argv[1], "--wizard") == 0) {
            gs_lock(); wizard_run(&g_state); gs_unlock();
        } else if (strcmp(argv[1], "--daemon") == 0) {
            daemon_start(&g_state);
            return 0;
        } else if (strcmp(argv[1], "--cleanup") == 0) {
            cmd_cleanup_all();
            return 0;
        } else if (strcmp(argv[1], "--quick") == 0) {
            cmd_process("quick");
        } else if (strcmp(argv[1], "--cli") == 0) {
            if (g_state.cfg.config.disk_count > 0) {
                do_restore(g_state.cfg.config.mount_letter ? g_state.cfg.config.mount_letter : 'G');
            } else {
                cmd_process("quick");
            }
        } else {
            char full_cmd[1024] = {0};
            for (int i = 1; i < argc; i++) { strcat_s(full_cmd, 1023, argv[i]); strcat_s(full_cmd, 1023, " "); }
            cmd_process_auto(full_cmd);
        }
    } else {
        if (g_state.cfg.config.disk_count > 0) {
            printf("\n  Restoring from saved config...\n");
            do_restore(g_state.cfg.config.mount_letter ? g_state.cfg.config.mount_letter : 'G');
        } else {
            printf("\n  No config found. Entering quick setup...\n");
            cmd_process("quick");
        }
    }

    LOG_OK("Goodbye!");
    cleanup_session(&g_state);
    cleanup_disks(&g_state);
    return 0;
}

int main(int argc, char* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    log_init();

    if (argc > 1) {
        if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
            printf("RAIDTEST v1.0 RC2 (build %s %s)\n", __DATE__, __TIME__);
            printf("Asymmetric Stripe RAID 0 Engine\n");
            printf("WinFsp FUSE + MinGW-w64\n");
            log_cleanup();
            return 0;
        }
        if (strcmp(argv[1], "--service") == 0) {
            SERVICE_TABLE_ENTRYA tbl[] = {
                { "RAIDTEST", service_main },
                { NULL, NULL }
            };
            log_cleanup();
            if (!StartServiceCtrlDispatcherA(tbl))
                return (int)GetLastError();
            return 0;
        }
        if (strcmp(argv[1], "--install-service") == 0) {
            bool ok = service_install();
            log_cleanup();
            return ok ? 0 : 1;
        }
        if (strcmp(argv[1], "--uninstall-service") == 0) {
            bool ok = service_uninstall();
            log_cleanup();
            return ok ? 0 : 1;
        }
        int rc = cli_main(argc, argv);
        log_cleanup();
        return rc;
    }

    int rc = gui_run();
    log_cleanup();
    return rc;
}
