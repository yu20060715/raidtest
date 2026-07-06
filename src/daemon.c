#include "daemon.h"
#include "disk_scanner.h"
#include "bench_io.h"
#include "fuse_bridge.h"
#include "config.h"
#include "volume_manager.h"
#include "cmd_handler.h"
#include "cleanup.h"

/* ---- Shared stop event ---- */
static HANDLE g_daemon_stop = NULL;
static HANDLE g_service_stop = NULL; /* distinct from g_daemon_stop for service */

/* ---- Console daemon ctrl handler ---- */
static BOOL WINAPI daemon_ctrl_handler(DWORD ctrl) {
    if (ctrl == CTRL_C_EVENT || ctrl == CTRL_BREAK_EVENT || ctrl == CTRL_CLOSE_EVENT) {
        if (g_daemon_stop) SetEvent(g_daemon_stop);
        return TRUE;
    }
    return FALSE;
}

/* ---- Service control handler ---- */
static SERVICE_STATUS        g_svc_status;
static SERVICE_STATUS_HANDLE g_svc_handle = NULL;

static void WINAPI service_ctrl(DWORD ctrl) {
    switch (ctrl) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            g_svc_status.dwCurrentState = SERVICE_STOP_PENDING;
            g_svc_status.dwCheckPoint++;
            SetServiceStatus(g_svc_handle, &g_svc_status);
            if (g_service_stop) SetEvent(g_service_stop);
            break;
        case SERVICE_CONTROL_INTERROGATE:
            SetServiceStatus(g_svc_handle, &g_svc_status);
            break;
    }
}

/* ---- Stdin processing (daemon only) ---- */
static void daemon_process_stdin(HANDLE hStdin) {
    DWORD avail = 0;
    if (!PeekNamedPipe(hStdin, NULL, 0, NULL, &avail, NULL) || avail == 0)
        return;
    char buf[1024];
    DWORD got = 0;
    DWORD to_read = (DWORD)(sizeof(buf) - 1);
    if (avail < to_read) to_read = avail;
    if (!ReadFile(hStdin, buf, to_read, &got, NULL) || got == 0)
        return;
    buf[got] = 0;
    char* nl = strchr(buf, '\n');
    if (nl) *nl = 0;
    nl = strchr(buf, '\r');
    if (nl) *nl = 0;
    if (buf[0] == 0) return;
    gs_lock();
    cmd_process(buf);
    gs_unlock();
}

/* ---- Load volume from superblock (single metadata source) ---- */
static bool daemon_load_volume(APP_STATE* state) {
    /* Scan all drives for superblock (metadata is on disk, not in JSON) */
    uint32_t loaded_count = 0;
    if (volume_load(&state->vol.volume, state->disk.loaded_disks, &loaded_count,
                    state->disk.physical_disks, state->disk.physical_count, NULL)) {
        state->disk.disk_count = loaded_count;
        for (uint32_t i = 0; i < loaded_count; i++)
            state->disk.disks[i] = &state->disk.loaded_disks[i];
        state->vol.volume_valid = true;

        /* If superblock had cache feature, init cache */
        if (state->vol.volume.generation > 0) {
            uint64_t cache_size = (uint64_t)CACHE_DEFAULT_MB * 1024ULL * 1024ULL;
            APP_CONFIG cfg;
            config_defaults(&cfg);
            config_load(&cfg); /* Try JSON for cache size override only */
            if (cfg.cache_mb > 0) cache_size = (uint64_t)cfg.cache_mb * 1024ULL * 1024ULL;

            if (volume_cache_enable(&state->vol.volume, cache_size,
                                    &state->cache.cache_on, &state->cache.cache_mb, &state->cache.flush_thread)) {
                LOG_OK("Cache enabled: %u MB", state->cache.cache_mb);
            }
        }
        return true;
    }

    /* Fallback: try JSON-based restore for legacy configs */
    APP_CONFIG cfg;
    config_defaults(&cfg);
    config_load(&cfg);
    if (cfg.disk_count == 0) {
        LOG_ERROR("No RAID volume found. Run wizard first or create volume manually.");
        return false;
    }

    if (!disk_scan_all(&state->disk.physical_disks, &state->disk.physical_count)) {
        LOG_ERROR("No disks detected");
        return false;
    }

    for (uint32_t i = 0; i < state->disk.physical_count; i++) {
        for (uint32_t j = 0; j < cfg.disk_count; j++) {
            char dl = (char)state->disk.physical_disks[i]->drive_letter[0];
            if (dl == cfg.disks[j].drive_letter) {
                if (state->disk.disk_count < MAX_DISKS) {
                    disk_map_drive((char[]){dl, 0}, state->disk.physical_disks[i]);
                    state->disk.pool_sizes_mb[state->disk.disk_count] = cfg.disks[j].pool_mb;
                    if (!volume_create_pool_file(state->disk.physical_disks[i], cfg.disks[j].pool_mb)) {
                        LOG_ERROR("Invalid pool size %llu MB from config", (unsigned long long)cfg.disks[j].pool_mb);
                        continue;
                    }
                    state->disk.disks[state->disk.disk_count++] = state->disk.physical_disks[i];
                }
                break;
            }
        }
    }

    if (state->disk.disk_count < MIN_DISKS) {
        LOG_ERROR("Not enough disks from config");
        return false;
    }

    if (!volume_create(&state->vol.volume, state->disk.disks, state->disk.disk_count)) {
        LOG_ERROR("Failed to create volume from config");
        return false;
    }
    state->vol.volume_valid = true;

    if (cfg.cache_mb > 0) {
        uint64_t cache_size = (uint64_t)cfg.cache_mb * 1024ULL * 1024ULL;
        if (volume_cache_enable(&state->vol.volume, cache_size,
                                &state->cache.cache_on, &state->cache.cache_mb, &state->cache.flush_thread)) {
            LOG_OK("Cache enabled: %u MB", cfg.cache_mb);
        }
    }
    LOG_WARN("Loaded from JSON config (legacy) ??superblock not found");
    return true;
}

/* ---- Core daemon/service logic ---- */
static DWORD daemon_run(APP_STATE* state, HANDLE stop_ev) {
    if (!daemon_load_volume(state)) return 1;

    /* Mount if valid */
    if (state->vol.volume_valid && state->vol.volume.mount_point[0]) {
        if (fuse_mount_volume(&state->vol.volume, state->vol.volume.mount_point[0])) {
            state->rt.mounted = true;
            LOG_OK("Daemon: mounted at %c:", state->vol.volume.mount_point[0]);
        }
    }

    LOG_OK("RAIDTEST daemon running. Send 'exit' or press Ctrl+C to stop.");

    /* Wait for stop signal */
    WaitForSingleObject(stop_ev, INFINITE);

    /* Graceful shutdown */
    gs_lock();
    cleanup_session(state);
    cleanup_disks(state);
    gs_unlock();

    if (stop_ev) CloseHandle(stop_ev);
    LOG_OK("Daemon stopped.");
    return 0;
}

/* ---- Console daemon thread ---- */
static DWORD WINAPI daemon_main(LPVOID arg) {
    log_init();
    APP_STATE* state = (APP_STATE*)arg;

    g_daemon_stop = CreateEvent(NULL, TRUE, FALSE, NULL);
    SetConsoleCtrlHandler(daemon_ctrl_handler, TRUE);
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    bool have_stdin = (hStdin != INVALID_HANDLE_VALUE && hStdin != NULL);
    HANDLE stop_ev = g_daemon_stop;

    /* Load volume from superblock (single metadata source) */
    if (!daemon_load_volume(state)) { log_cleanup(); return 1; }

    /* Apply runtime prefs from JSON (mount letter, cache size override) */
    APP_CONFIG cfg;
    config_defaults(&cfg);
    config_load(&cfg);

    /* Mount if a mount point is configured */
    char mount = cfg.mount_letter ? cfg.mount_letter : state->vol.volume.mount_point[0];
    if (mount && state->vol.volume_valid) {
        if (fuse_mount_volume(&state->vol.volume, mount)) {
            state->rt.mounted = true;
            LOG_OK("Daemon: mounted at %c:", mount);
        }
    }

    LOG_OK("RAIDTEST daemon running. Send 'exit' or press Ctrl+C to stop.");

    HANDLE wait_handles[2] = { stop_ev, hStdin };
    DWORD wait_count = have_stdin ? 2 : 1;

    while (WaitForMultipleObjects(wait_count, wait_handles, FALSE, 100) != WAIT_OBJECT_0) {
        if (have_stdin)
            daemon_process_stdin(hStdin);
    }

    gs_lock();
    cleanup_session(state);
    cleanup_disks(state);
    gs_unlock();

    if (g_daemon_stop) CloseHandle(g_daemon_stop);
    g_daemon_stop = NULL;
    LOG_OK("Daemon stopped.");
    log_cleanup();
    return 0;
}

/* ---- Service entry point (called by SCM) ---- */
void WINAPI service_main(DWORD argc, LPTSTR* argv) {
    log_init();

    g_svc_handle = RegisterServiceCtrlHandlerA("RAIDTEST", service_ctrl);
    if (!g_svc_handle) { LOG_ERROR("RegisterServiceCtrlHandler failed"); return; }

    g_svc_status.dwServiceType             = SERVICE_WIN32_OWN_PROCESS;
    g_svc_status.dwCurrentState            = SERVICE_RUNNING;
    g_svc_status.dwControlsAccepted        = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    g_svc_status.dwWin32ExitCode           = NO_ERROR;
    g_svc_status.dwServiceSpecificExitCode = 0;
    g_svc_status.dwCheckPoint              = 0;
    g_svc_status.dwWaitHint                = 30000;
    SetServiceStatus(g_svc_handle, &g_svc_status);

    g_service_stop = CreateEvent(NULL, TRUE, FALSE, NULL);

    APP_STATE state = {0};
    cmd_init_all();
    daemon_run(&state, g_service_stop);
    g_service_stop = NULL;

    g_svc_status.dwCurrentState = SERVICE_STOPPED;
    g_svc_status.dwCheckPoint = 0;
    SetServiceStatus(g_svc_handle, &g_svc_status);

    log_cleanup();
}

/* ---- SCM registration ---- */
bool service_install(void) {
    wchar_t path[MAX_DRIVE_PATH];
    GetModuleFileNameW(NULL, path, MAX_DRIVE_PATH);

    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
    if (!scm) {
        LOG_ERROR("OpenSCManager failed (%lu)", GetLastError());
        return false;
    }

    /* Append --service so SCM starts with the right entry point */
    wchar_t cmdline[MAX_DRIVE_PATH + 32];
    StringCchPrintfW(cmdline, MAX_DRIVE_PATH + 32, L"\"%s\" --service", path);

    SC_HANDLE svc = CreateServiceW(scm, L"RAIDTEST", L"RAIDTEST v3",
        SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL, cmdline, NULL, NULL, NULL, NULL, NULL);

    if (!svc) {
        LOG_ERROR("CreateService failed (%lu)", GetLastError());
        CloseServiceHandle(scm);
        return false;
    }

    /* Set description */
    SERVICE_DESCRIPTIONW desc = { L"Asymmetric Stripe RAID 0 Engine (RAIDTEST v3)" };
    ChangeServiceConfig2W(svc, SERVICE_CONFIG_DESCRIPTION, &desc);

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    LOG_OK("Service 'RAIDTEST' installed successfully");
    return true;
}

bool service_uninstall(void) {
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scm) {
        LOG_ERROR("OpenSCManager failed (%lu)", GetLastError());
        return false;
    }

    SC_HANDLE svc = OpenServiceW(scm, L"RAIDTEST", SERVICE_ALL_ACCESS);
    if (!svc) {
        LOG_ERROR("Service 'RAIDTEST' not found (%lu)", GetLastError());
        CloseServiceHandle(scm);
        return false;
    }

    if (!DeleteService(svc)) {
        LOG_ERROR("DeleteService failed (%lu)", GetLastError());
    } else {
        LOG_OK("Service 'RAIDTEST' uninstalled successfully");
    }

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return true;
}

/* ---- Console daemon entry point ---- */
bool daemon_start(APP_STATE* state) {
    if (!state) return false;
    HANDLE thread = CreateThread(NULL, 0, daemon_main, state, 0, NULL);
    if (!thread) return false;
    CloseHandle(thread);
    return true;
}
