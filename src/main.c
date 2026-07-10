#include "cmd_handler.h"
#include "raid_service.h"
#include "config.h"
#include "daemon.h"
#include "wizard.h"
#include "fuse_bridge.h"
#include "disk_scanner.h"
#include "device_manager.h"
#include "volume_manager.h"
#include "cleanup.h"
#include "gui.h"

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
        } else if (strcmp(argv[1], "--mount") == 0) {
            char mount_letter = argc > 2 ? (char)toupper(argv[2][0]) : 'R';
            if (mount_letter < 'A' || mount_letter > 'Z') mount_letter = 'R';
            int timeout_sec = 300;
            if (argc > 3) timeout_sec = atoi(argv[3]);
            if (timeout_sec < 1) timeout_sec = 300;
            device_refresh();
            uint32_t loaded_count = 0;
            if (volume_load(&g_state.vol.volume, g_state.disk.loaded_disks, &loaded_count,
                            g_state.disk.physical_disks, g_state.disk.physical_count, NULL)) {
                g_state.disk.disk_count = loaded_count;
                for (uint32_t i = 0; i < loaded_count; i++)
                    g_state.disk.disks[i] = &g_state.disk.loaded_disks[i];
                g_state.vol.volume_valid = true;
                if (g_state.vol.volume.generation > 0) {
                    uint64_t cache_size = (uint64_t)CACHE_DEFAULT_MB * 1024ULL * 1024ULL;
                    volume_cache_enable(&g_state.vol.volume, cache_size,
                                        &g_state.cache.cache_on, &g_state.cache.cache_mb, &g_state.cache.flush_thread);
                }
                if (g_state.vol.volume.raid_level == RAID_LEVEL_STRIPE) {
                    if (!stripe_volume_workers_init(&g_state.vol.volume)) {
                        LOG_ERROR("Failed to start per-disk workers");
                    }
                }
                g_state.rt.state = STATE_MOUNTED;
                RC rc = raid_mount(mount_letter);
                if (rc == RC_OK) {
                    LOG_OK("Mounted %c: for %d seconds. Ctrl+C to unmount early.", mount_letter, timeout_sec);
                    Sleep((DWORD)timeout_sec * 1000);
                }
            } else {
                LOG_ERROR("No RAID volume found on any drive. Run quick setup first.");
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
            printf("RAIDTEST v1.0 (build %s %s)\n", __DATE__, __TIME__);
            printf("Asymmetric Stripe RAID Engine\n");
            printf("WinFsp FUSE + MinGW-w64\n");
            log_cleanup();
            return 0;
        }
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "/?") == 0) {
            printf("RAIDTEST v1.0 - Asymmetric Stripe RAID Engine\n");
            printf("Usage: raidtest_winfsp.exe [OPTIONS]\n");
            printf("\n");
            printf("Options:\n");
            printf("  (no args)           Launch GUI\n");
            printf("  --help, -h          Show this help\n");
            printf("  --version, -v       Show version\n");
            printf("  --cli               Force CLI mode (interactive)\n");
            printf("  --auto [letter]     Auto-restore from saved config\n");
            printf("  --quick             Quick all-in-one setup (scan + create + mount)\n");
            printf("  --wizard            Guided setup wizard\n");
            printf("  --daemon            Run as console daemon\n");
            printf("  --service           Run as Windows service (SCM)\n");
            printf("  --install-service   Register Windows service\n");
            printf("  --uninstall-service Unregister Windows service\n");
            printf("  --cleanup           Release resources and exit\n");
            printf("\n");
            printf("Commands (in CLI mode):\n");
            printf("  scan, select, init, create, mirror, mount, unmount, destroy\n");
            printf("  cache, expand, rebuild, check, info, status, map, planner\n");
            printf("  bench, benchfs, test, random, simulate, metadata, events\n");
            printf("  load, purge, config-save, config-load, wizard, quick, cleanup, help\n");
            printf("  mapdrive <id> <letter>, exit\n");
            printf("\n");
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
