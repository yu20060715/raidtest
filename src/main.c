#include "cmd_handler.h"
#include "config.h"
#include "daemon.h"
#include "wizard.h"
#include "fuse_bridge.h"
#include "pool_io.h"
#include "disk_scanner.h"
#include "ram_cache.h"
#include "cleanup.h"
#include "gui.h"

static void do_restore(char mount_letter) {
    if (g_state.config.disk_count == 0) { LOG_ERROR("No config to restore"); return; }
    LOG_INFO("Restoring from config (%u disks, mount at %c:)...", g_state.config.disk_count, mount_letter);
    if (!g_state.physical_disks) {
        if (!disk_scan_all(&g_state.physical_disks, &g_state.physical_count)) {
            LOG_ERROR("No disks detected"); return;
        }
    }
    char cmd[512]; int pos = 0;
    pos += snprintf(cmd + pos, 512 - (size_t)pos, "select");
    for (uint32_t i = 0; i < g_state.config.disk_count; i++)
        pos += snprintf(cmd + pos, 512 - (size_t)pos, " %u", g_state.config.disks[i].disk_id);
    cmd_process(cmd);

    pos = snprintf(cmd, 512, "init");
    for (uint32_t i = 0; i < g_state.config.disk_count; i++)
        pos += snprintf(cmd + pos, 512 - (size_t)pos, " %u:%llu", g_state.config.disks[i].disk_id,
              (unsigned long long)g_state.config.disks[i].pool_mb);
    cmd_process(cmd);
    cmd_process("create");

    char cache_arg[16]; snprintf(cache_arg, 16, "%u", g_state.config.cache_mb);
    char* cache_av[] = { cache_arg };
    cmd_cache(1, cache_av);

    char mount_arg[2] = { mount_letter, 0 };
    char* mount_av[] = { mount_arg };
    cmd_mount(1, mount_av);
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
                do_restore(g_state.config.mount_letter ? g_state.config.mount_letter : 'G');
            }
            if (g_state.mounted)
                printf("\n  Volume mounted at %c:. Type 'exit' to unmount.\n\n", g_state.volume.mount_point[0]);
        } else if (strcmp(argv[1], "--wizard") == 0) {
            wizard_run(&g_state);
        } else if (strcmp(argv[1], "--daemon") == 0) {
            daemon_start(&g_state);
            return 0;
        } else if (strcmp(argv[1], "--cleanup") == 0) {
            cmd_cleanup_all();
            return 0;
        } else if (strcmp(argv[1], "--quick") == 0) {
            cmd_process("quick");
        } else if (strcmp(argv[1], "--cli") == 0) {
            if (g_state.config.disk_count > 0) {
                do_restore(g_state.config.mount_letter ? g_state.config.mount_letter : 'G');
            } else {
                cmd_process("quick");
            }
        } else {
            char full_cmd[1024] = {0};
            for (int i = 1; i < argc; i++) { strcat_s(full_cmd, 1023, argv[i]); strcat_s(full_cmd, 1023, " "); }
            cmd_process_auto(full_cmd);
        }
    } else {
        if (g_state.config.disk_count > 0) {
            printf("\n  Restoring from saved config...\n");
            do_restore(g_state.config.mount_letter ? g_state.config.mount_letter : 'G');
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
