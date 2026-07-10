extern "C" {
#include "gui.h"
#include "raid_service.h"
#include "ui_model.h"
#include "device_manager.h"
#include "event_bus.h"
#include "profiler.h"
#include "cmd_handler.h"
#include "superblock.h"
#include "config.h"
}

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <sys/stat.h>
#include <time.h>
#include <io.h>
#include <fcntl.h>

#define MAX_LOG_LINES 500
#define MAX_LOG_LINE_LEN 256
#define APP_VERSION "v1.0 RC4"

#define MAX_TOASTS 8
#define TOAST_DURATION 5.0
#define PERF_HISTORY 120
#define MAX_HEALTH_CARDS 8

enum GUIMode { MODE_BEGINNER = 0, MODE_ADVANCED, MODE_DEVELOPER };

enum WorkerAction {
    W_NONE = 0,
    W_SCAN, W_CREATE, W_MIRROR, W_MOUNT, W_UNMOUNT, W_DESTROY,
    W_REFRESH, W_BENCHFS, W_EXPORT,
    W_QUICK_SETUP, W_LOAD_SUPERBLOCK, W_LOAD_CONFIG,
    W_REBUILD, W_CHECK,
    W_SIMULATE_FAIL, W_SIMULATE_HEALTHY, W_SIMULATE_DISCONNECT,
    W_WIZARD_SCAN, W_WIZARD_CREATE,
    W_CLEANUP,
    W_CACHE,
    W_PURGE,
    W_BENCH,
    W_TEST,
    W_RANDOM,
    W_CONFIG_SAVE,
    W_CONFIG_LOAD,
    W_EXPAND,
    W_MAP,
    W_METADATA,
};

enum ToastType { TOAST_INFO = 0, TOAST_OK, TOAST_WARN, TOAST_ERROR };

struct Toast {
    char      msg[128];
    double    expire_time;
    ToastType type;
};

struct PerfSample {
    float read_mbs;
    float write_mbs;
    float latency_ms;
};

static struct {
    HWND                    hwnd;
    ID3D11Device*           device;
    ID3D11DeviceContext*    ctx;
    IDXGISwapChain*         swapchain;
    ID3D11RenderTargetView* rtv;

    CRITICAL_SECTION        log_lock;
    char                    log_lines[MAX_LOG_LINES][MAX_LOG_LINE_LEN];
    int                     log_count;
    int                     log_tail;

    volatile LONG           worker_running;
    WorkerAction            worker_action;
    char                    worker_params[256];
    char                    worker_result[512];
    volatile LONG           worker_done;
    char                    progress_text[64];
    volatile float          progress_frac;
    char                    progress_step[64];
    double                  progress_start_time;
    double                  progress_eta;
    volatile LONG           worker_cancel;

    UI_DISK_SUMMARY         disk_summary;
    UI_VOLUME_INFO          vol_info;
    UI_HEALTH_SUMMARY       health;
    double                  last_refresh;
    volatile LONG           refresh_pending;
    int                     selected_disks[8];
    int                     selected_count;
    int                     disk_checked[64];


    int                     pool_size_mb;
    int                     pool_per_disk[8];

    bool                    show_expand_dialog;
    bool                    show_map_dialog;
    bool                    show_metadata_dialog;
    char                    dialog_text[8192];
    int                     expand_disk_ids[8];
    int                     expand_sizes[8];
    int                     expand_count;
    bool                    expand_checks[64];

    bool                    show_about;
    bool                    show_settings;
    bool                    show_confirm_destroy;

    bool                    show_bench;
    bool                    show_bench_raw;
    bool                    show_export_dialog;
    bool                    show_welcome;
    bool                    show_purge_confirm;
    char                    export_result[512];

    char                    bench_read_mbs[32];
    char                    bench_write_mbs[32];
    char                    bench_latency[32];
    bool                    bench_done;
    char                    bench_raw_read[32];
    char                    bench_raw_write[32];
    bool                    bench_raw_done;
    int                     random_ops;
    int                     random_maxkb;
    char                    test_result[256];

    char                    status[128];
    int                     state_value;

    HANDLE                  worker_handle;
    unsigned                worker_thread_id;

    GUIMode                 mode;

    bool                    show_restore_wizard;

    int                     rebuild_failed_idx;
    int                     rebuild_replacement_id;
    int                     rebuild_pool_mb;
    bool                    show_rebuild_wizard;



    APP_CONFIG              settings;

    struct Toast            toasts[MAX_TOASTS];
    int                     toast_count;

    struct PerfSample       perf_history[PERF_HISTORY];
    int                     perf_index;
    double                  perf_last_sample;

    bool                    use_light_theme;
} g_gui = {0};

static double timer_sec(void) {
    LARGE_INTEGER f, n;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&n);
    return (double)n.QuadPart / (double)f.QuadPart;
}

static void toast_push(const char* msg, ToastType type) {
    if (g_gui.toast_count < MAX_TOASTS) {
        int i = g_gui.toast_count++;
        strncpy(g_gui.toasts[i].msg, msg, 127);
        g_gui.toasts[i].msg[127] = 0;
        g_gui.toasts[i].expire_time = ImGui::GetTime() + TOAST_DURATION;
        g_gui.toasts[i].type = type;
    } else {
        memmove(g_gui.toasts, g_gui.toasts + 1, (MAX_TOASTS - 1) * sizeof(struct Toast));
        int i = MAX_TOASTS - 1;
        strncpy(g_gui.toasts[i].msg, msg, 127);
        g_gui.toasts[i].msg[127] = 0;
        g_gui.toasts[i].expire_time = ImGui::GetTime() + TOAST_DURATION;
        g_gui.toasts[i].type = type;
    }
}

static void render_toasts(void) {
    double now = ImGui::GetTime();
    float y = 50.0f;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    for (int i = 0; i < g_gui.toast_count; ) {
        if (now >= g_gui.toasts[i].expire_time) {
            memmove(g_gui.toasts + i, g_gui.toasts + i + 1, (size_t)(g_gui.toast_count - i - 1) * sizeof(struct Toast));
            g_gui.toast_count--;
            continue;
        }
        const char* msg = g_gui.toasts[i].msg;
        ImVec4 c;
        switch (g_gui.toasts[i].type) {
            case TOAST_OK:    c = ImVec4(0.0f,0.8f,0.2f,0.9f); break;
            case TOAST_WARN:  c = ImVec4(1.0f,0.8f,0.0f,0.9f); break;
            case TOAST_ERROR: c = ImVec4(1.0f,0.2f,0.2f,0.9f); break;
            default:          c = ImVec4(0.3f,0.6f,1.0f,0.9f); break;
        }
        ImVec2 sz = ImGui::CalcTextSize(msg);
        float pad = 10.0f;
        float w = sz.x + pad * 2.0f;
        float h = sz.y + pad;
        float x = (1280.0f - w) * 0.5f;
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), ImColor(c), 4.0f);
        dl->AddText(ImVec2(x + pad, y + pad * 0.5f), IM_COL32_WHITE, msg);
        y += h + 6.0f;
        i++;
    }
}

static void gui_log(const char* msg) {
    EnterCriticalSection(&g_gui.log_lock);
    int idx = (g_gui.log_count < MAX_LOG_LINES) ? g_gui.log_count : g_gui.log_tail;
    strncpy(g_gui.log_lines[idx], msg, MAX_LOG_LINE_LEN - 1);
    g_gui.log_lines[idx][MAX_LOG_LINE_LEN - 1] = 0;
    if (g_gui.log_count < MAX_LOG_LINES) g_gui.log_count++;
    else g_gui.log_tail = (g_gui.log_tail + 1) % MAX_LOG_LINES;
    LeaveCriticalSection(&g_gui.log_lock);
}

static void event_cb(EVENT_TYPE type, const char* data, void* userdata) {
    (void)userdata;
    char buf[MAX_LOG_LINE_LEN];
    const char* t = event_type_str(type);
    snprintf(buf, sizeof(buf), "[%s] %s", t ? t : "EVENT", data ? data : "");
    gui_log(buf);
    const char* td = data ? data : "";
    if (strstr(td, "scan") || strstr(td, "Scan")) toast_push(td, TOAST_INFO);
    else if (strstr(td, "error") || strstr(td, "Error") || strstr(td, "FAILED")) toast_push(td, TOAST_ERROR);
    else toast_push(td, TOAST_INFO);
    InterlockedExchange(&g_gui.refresh_pending, 1);
}

static void refresh_ui_model(void) {
    ui_get_disk_summary(&g_gui.disk_summary);
    ui_get_volume_info(&g_gui.vol_info);
    ui_get_health_summary(&g_gui.health);
    g_gui.state_value = g_state.rt.state;
}

static unsigned int __stdcall worker_thread(void* arg) {
    (void)arg;
    WorkerAction act = g_gui.worker_action;
    char* params = g_gui.worker_params;
    char* result = g_gui.worker_result;
    result[0] = 0;
    g_gui.progress_frac = 0.0f;
    g_gui.progress_eta = 0;
    g_gui.progress_step[0] = 0;
    g_gui.progress_start_time = timer_sec();
    InterlockedExchange(&g_gui.worker_cancel, 0);

    switch (act) {
    case W_SCAN: {
        strncpy(g_gui.progress_text, "Scanning disks...", 63);
        strncpy(g_gui.progress_step, "Step 1/1: Scanning physical disks", 63);
        RC rc = raid_scan();
        g_gui.progress_frac = 1.0f;
        snprintf(result, 511, "Scan: %s (%u disks found)", rc == RC_OK ? "OK" : "FAILED", g_gui.disk_summary.count);
        refresh_ui_model();
        if (rc == RC_OK) toast_push("Scan complete", TOAST_OK);
        else toast_push("Scan failed", TOAST_ERROR);
        break;
    }
    case W_CREATE: {
        g_gui.progress_frac = 0.0f;
        strncpy(g_gui.progress_text, "Creating volume...", 63);
        char* args[16] = {0}; int argc = 0;
        char buf[256]; snprintf(buf, 256, "%s", params);
        char* tok = strtok(buf, " ");
        while (tok && argc < 16) { args[argc++] = tok; tok = strtok(NULL, " "); }
        strncpy(g_gui.progress_step, "Step 1/2: Initializing pools...", 63);
        g_gui.progress_frac = 0.3f;
        if (argc > 0) raid_init_pools(argc, args);
        if (InterlockedCompareExchange(&g_gui.worker_cancel, 1, 1) == 1) { strncpy(result, "Cancelled", 511); break; }
        strncpy(g_gui.progress_step, "Step 2/2: Creating stripe volume...", 63);
        g_gui.progress_frac = 0.7f;
        RC rc = raid_create();
        g_gui.progress_frac = 1.0f;
        snprintf(result, 511, "Create: %s %s", rc == RC_OK ? "OK" : "FAILED", rc == RC_OK ? "- Volume is ready" : "");
        refresh_ui_model();
        if (rc == RC_OK) toast_push("Volume created", TOAST_OK);
        else toast_push("Volume creation failed", TOAST_ERROR);
        break;
    }
    case W_MIRROR: {
        g_gui.progress_frac = 0.0f;
        strncpy(g_gui.progress_text, "Creating mirror...", 63);
        char* args[16] = {0}; int argc = 0;
        char buf[256]; snprintf(buf, 256, "%s", params);
        char* tok = strtok(buf, " ");
        while (tok && argc < 16) { args[argc++] = tok; tok = strtok(NULL, " "); }
        strncpy(g_gui.progress_step, "Step 1/2: Initializing pools...", 63);
        g_gui.progress_frac = 0.3f;
        if (argc > 0) raid_init_pools(argc, args);
        if (InterlockedCompareExchange(&g_gui.worker_cancel, 1, 1) == 1) { strncpy(result, "Cancelled", 511); break; }
        strncpy(g_gui.progress_step, "Step 2/2: Creating RAID1 mirror...", 63);
        g_gui.progress_frac = 0.7f;
        RC rc = raid_mirror();
        g_gui.progress_frac = 1.0f;
        snprintf(result, 511, "Mirror: %s", rc == RC_OK ? "OK" : "FAILED");
        refresh_ui_model();
        if (rc == RC_OK) toast_push("Mirror created", TOAST_OK);
        else toast_push("Mirror failed", TOAST_ERROR);
        break;
    }
    case W_MOUNT: {
        strncpy(g_gui.progress_text, "Mounting...", 63);
        strncpy(g_gui.progress_step, "Step 1/1: Mounting volume...", 63);
        g_gui.progress_frac = 0.5f;
        RC rc = raid_mount(params[0] ? params[0] : 'G');
        g_gui.progress_frac = 1.0f;
        snprintf(result, 511, "Mount: %s %s at %c:", rc == RC_OK ? "OK" : "FAILED", rc == RC_OK ? "- Volume mounted" : "", params[0] ? params[0] : 'G');
        refresh_ui_model();
        if (rc == RC_OK) toast_push("Volume mounted", TOAST_OK);
        else toast_push("Mount failed", TOAST_ERROR);
        break;
    }
    case W_UNMOUNT: {
        strncpy(g_gui.progress_text, "Unmounting...", 63);
        strncpy(g_gui.progress_step, "Step 1/1: Unmounting volume...", 63);
        g_gui.progress_frac = 0.5f;
        RC rc = raid_unmount();
        g_gui.progress_frac = 1.0f;
        snprintf(result, 511, "Unmount: %s", rc == RC_OK ? "OK - Volume unmounted" : "FAILED");
        refresh_ui_model();
        if (rc == RC_OK) toast_push("Volume unmounted", TOAST_OK);
        else toast_push("Unmount failed", TOAST_ERROR);
        break;
    }
    case W_DESTROY: {
        strncpy(g_gui.progress_text, "Destroying...", 63);
        strncpy(g_gui.progress_step, "Step 1/1: Destroying volume...", 63);
        g_gui.progress_frac = 0.5f;
        RC rc = raid_destroy();
        g_gui.progress_frac = 1.0f;
        snprintf(result, 511, "Destroy: %s", rc == RC_OK ? "OK - Volume destroyed" : "FAILED");
        refresh_ui_model();
        if (rc == RC_OK) toast_push("Volume destroyed", TOAST_OK);
        else toast_push("Destroy failed", TOAST_ERROR);
        break;
    }
    case W_REFRESH: {
        refresh_ui_model();
        snprintf(result, 511, "Refreshed - %u disk(s), state=%s", g_gui.disk_summary.count, ui_get_state_str());
        break;
    }
    case W_BENCHFS: {
        strncpy(g_gui.progress_text, "Benchmarking...", 63);
        g_gui.progress_frac = 0.0f;
        char* args[2] = {0}; int argc = 0;
        if (params[0]) { char* tok = strtok(params, " "); while (tok && argc < 2) { args[argc++] = tok; tok = strtok(NULL, " "); } }
        for (int step = 0; step < 4; step++) {
            snprintf(g_gui.progress_step, 63, "Step %d/4: Running benchmark pass...", step + 1);
            g_gui.progress_frac = (step + 1) * 0.25f;
            if (InterlockedCompareExchange(&g_gui.worker_cancel, 1, 1) == 1) { strncpy(result, "Benchmark cancelled", 511); goto bench_done; }
            Sleep(250);
        }
        { RC rc = raid_benchfs(argc, args);
        g_gui.progress_frac = 1.0f;
        if (rc == RC_OK) {
            IO_PROFILER* p = profiler_get();
            snprintf(g_gui.bench_read_mbs, 31, "%.1f", p->avg_read_mbs);
            snprintf(g_gui.bench_write_mbs, 31, "%.1f", p->avg_write_mbs);
            snprintf(g_gui.bench_latency, 31, "%.2f", (p->avg_read_latency_ms + p->avg_write_latency_ms) / 2.0);
            g_gui.bench_done = true;
            snprintf(result, 511, "Bench: R=%.1f W=%.1f MB/s", p->avg_read_mbs, p->avg_write_mbs);
            toast_push("Benchmark complete", TOAST_OK);
        } else { snprintf(result, 511, "Benchmark FAILED"); toast_push("Benchmark failed", TOAST_ERROR); } }
        bench_done:;
        break;
    }
    case W_EXPORT: {
        strncpy(g_gui.progress_text, "Exporting diagnostics...", 63);
        char tmpdir[MAX_PATH]; GetTempPathA(MAX_PATH, tmpdir);
        char dir[MAX_PATH]; snprintf(dir, MAX_PATH, "%sraidtest_export", tmpdir);
        CreateDirectoryA(dir, NULL);
        char tstamp[64];
        { time_t t = time(NULL); struct tm lt; localtime_s(&lt, &t); strftime(tstamp, 64, "%Y%m%d_%H%M%S", &lt); }
        char outdir[MAX_PATH]; snprintf(outdir, MAX_PATH, "%s\\raidtest_%s", dir, tstamp);
        CreateDirectoryA(outdir, NULL);

        strncpy(g_gui.progress_step, "Exporting metadata...", 63);
        g_gui.progress_frac = 0.1f;
        char mpath[MAX_PATH]; snprintf(mpath, MAX_PATH, "%s\\metadata.txt", outdir);
        FILE* f = fopen(mpath, "w");
        if (f) {
            fprintf(f, "RAIDTEST %s - Diagnostic Export\n", APP_VERSION);
            fprintf(f, "Date: %s %s\n\n", __DATE__, __TIME__);
            fprintf(f, "=== Volume Info ===\n");
            UI_VOLUME_INFO* vi = &g_gui.vol_info;
            fprintf(f, "Exists: %s\n", vi->exists ? "Yes" : "No");
            fprintf(f, "RAID Level: %u\n", vi->raid_level);
            fprintf(f, "Disks: %u\n", vi->disk_count);
            fprintf(f, "Capacity: %llu bytes\n", vi->virtual_capacity_bytes);
            fprintf(f, "UUID: %s\n", vi->uuid_str);
            fprintf(f, "Generation: %llu\n", vi->generation);
            fprintf(f, "Mounted: %s\n", vi->mounted ? "Yes" : "No");
            fprintf(f, "Cache: %s (%u MB)\n", vi->cache_enabled ? "ON" : "OFF", vi->cache_mb);
            fprintf(f, "Written: %llu\n", vi->bytes_written);
            fprintf(f, "Read: %llu\n\n", vi->bytes_read);
            fprintf(f, "=== Disk Summary ===\n");
            UI_DISK_SUMMARY* ds = &g_gui.disk_summary;
            fprintf(f, "Count: %u, Selected: %u, Capacity: %llu MB\n", ds->count, ds->selected_count, ds->total_capacity_mb);
            fprintf(f, "\n=== Health ===\n");
            UI_HEALTH_SUMMARY* h = &g_gui.health;
            fprintf(f, "Healthy: %u/%u, Degraded: %s, State: %s\n", h->healthy_count, h->total_count, h->degraded ? "Yes" : "No", ui_get_state_str());
            uint32_t dc = device_get_count();
            for (uint32_t i = 0; i < dc; i++) {
                DISK_INFO* d = device_get(i); if (!d) continue;
                char modelA[128] = {0}; wcstombs(modelA, d->model, 127);
                fprintf(f, "\nDisk %u: %s [%s]\n", i, modelA, disk_type_str(d->type));
                fprintf(f, "  Serial: %s\n", d->serial_number);
                fprintf(f, "  Size: %.0f GB\n", (double)d->total_bytes / 1e9);
                fprintf(f, "  Speed: %u MB/s\n", d->benchmarked ? d->bench_write_mbs : d->write_speed_mbs);
                fprintf(f, "  Healthy: %s\n", d->healthy ? "Yes" : "No");
            }
            fclose(f);
        }
        g_gui.progress_frac = 0.3f;

        strncpy(g_gui.progress_step, "Exporting event log...", 63);
        char epath[MAX_PATH]; snprintf(epath, MAX_PATH, "%s\\event.log", outdir);
        f = fopen(epath, "w");
        if (f) {
            EnterCriticalSection(&g_gui.log_lock);
            int n = g_gui.log_count, start = n < MAX_LOG_LINES ? 0 : g_gui.log_tail;
            for (int i = 0; i < n; i++) fprintf(f, "%s\n", g_gui.log_lines[(start + i) % MAX_LOG_LINES]);
            LeaveCriticalSection(&g_gui.log_lock);
            fclose(f);
        }
        g_gui.progress_frac = 0.5f;

        strncpy(g_gui.progress_step, "Exporting system info...", 63);
        char spath[MAX_PATH]; snprintf(spath, MAX_PATH, "%s\\system.txt", dir);
        f = fopen(spath, "wb");
        if (f) {
            SYSTEM_INFO si; GetSystemInfo(&si);
            MEMORYSTATUSEX ms; ms.dwLength = sizeof(ms); GlobalMemoryStatusEx(&ms);
            fprintf(f, "=== System Info ===\n");
            fprintf(f, "OS: Windows (build %lu)\n", GetVersion());
            fprintf(f, "Processors: %lu\n", si.dwNumberOfProcessors);
            fprintf(f, "RAM: %llu MB total, %llu MB free\n", ms.ullTotalPhys / 1048576, ms.ullAvailPhys / 1048576);
            fclose(f);
        }

        strncpy(g_gui.progress_step, "Creating ZIP archive...", 63);
        g_gui.progress_frac = 0.8f;
        /* Copy system.txt to outdir */
        { char sysdest[MAX_PATH]; snprintf(sysdest, MAX_PATH, "%s\\system.txt", outdir);
          CopyFileA(spath, sysdest, FALSE); }

        char zipcmd[1024]; snprintf(zipcmd, 1024,
            "powershell -NoProfile -Command \"Compress-Archive -Path '%s\\*' -DestinationPath '%s\\raidtest_%s.zip' -Force\"",
            outdir, dir, tstamp);
        system(zipcmd);

        g_gui.progress_frac = 1.0f;
        snprintf(result, 511, "Export saved to: %s\\raidtest_%s.zip", dir, tstamp);
        snprintf(g_gui.export_result, 511, "Diagnostics exported to:\n%s\\raidtest_%s.zip\n\nContents:\n  metadata.txt\n  event.log\n  system.txt", dir, tstamp);
        toast_push("Export complete", TOAST_OK);
        break;
    }
    case W_QUICK_SETUP: {
        strncpy(g_gui.progress_text, "Quick Setup...", 63);
        strncpy(g_gui.progress_step, "Step 1/5: Scanning disks...", 63);
        g_gui.progress_frac = 0.0f;
        RC rc = raid_scan();
        if (rc != RC_OK) { snprintf(result, 511, "Quick Setup FAILED - no disks found"); break; }
        g_gui.progress_frac = 0.2f;
        if (InterlockedCompareExchange(&g_gui.worker_cancel, 1, 1) == 1) { strncpy(result, "Cancelled", 511); break; }
        uint32_t count = device_get_count();
        uint32_t sel_count = 0, sel_ids[MAX_DISKS];
        for (uint32_t i = 0; i < count && sel_count < MAX_DISKS; i++) {
            if (device_get(i)->healthy) sel_ids[sel_count++] = i;
        }
        if (sel_count < MIN_DISKS) { snprintf(result, 511, "Quick Setup FAILED - need %d healthy disks", MIN_DISKS); break; }
        device_select(sel_ids, sel_count);
        strncpy(g_gui.progress_step, "Step 2/5: Creating pool files...", 63);
        int pool_mb = g_gui.pool_size_mb ? g_gui.pool_size_mb : 51200;
        if (pool_mb < 1024) pool_mb = 1024;
        char init_buf[256] = {0}; int pos = 0;
        for (uint32_t i = 0; i < sel_count; i++)
            pos += snprintf(init_buf + pos, 255 - pos, "%s%u:%d", i > 0 ? " " : "", sel_ids[i], pool_mb);
        char* init_args[16]; int init_argc = 0;
        char* tok = strtok(init_buf, " ");
        while (tok && init_argc < 16) { init_args[init_argc++] = tok; tok = strtok(NULL, " "); }
        rc = raid_init_pools(init_argc, init_args);
        if (rc != RC_OK) { snprintf(result, 511, "Quick Setup FAILED - pool creation"); break; }
        g_gui.progress_frac = 0.4f;
        if (InterlockedCompareExchange(&g_gui.worker_cancel, 1, 1) == 1) { strncpy(result, "Cancelled", 511); break; }
        strncpy(g_gui.progress_step, "Step 3/5: Creating volume...", 63);
        g_gui.progress_frac = 0.6f;
        rc = raid_create();
        if (rc != RC_OK) { snprintf(result, 511, "Quick Setup FAILED - volume creation"); break; }
        if (InterlockedCompareExchange(&g_gui.worker_cancel, 1, 1) == 1) { strncpy(result, "Cancelled", 511); break; }
        strncpy(g_gui.progress_step, "Step 4/5: Enabling cache...", 63);
        g_gui.progress_frac = 0.8f;
        { char cache_str[16]; snprintf(cache_str, 16, "%u", CACHE_DEFAULT_MB);
          char* cache_av[] = { cache_str }; raid_cache(1, cache_av); }
        strncpy(g_gui.progress_step, "Step 5/5: Mounting...", 63);
        g_gui.progress_frac = 0.9f;
        { char mnt = g_gui.settings.mount_letter ? g_gui.settings.mount_letter : 'G';
          rc = raid_mount(mnt); }
        g_gui.progress_frac = 1.0f;
        if (rc != RC_OK) { snprintf(result, 511, "Quick Setup: volume created but mount failed"); break; }
        snprintf(result, 511, "Quick Setup complete! Volume mounted at %c: (%u disks)", g_gui.settings.mount_letter ? g_gui.settings.mount_letter : 'G', sel_count);
        refresh_ui_model();
        toast_push("Quick Setup complete", TOAST_OK);
        break;
    }
    case W_LOAD_SUPERBLOCK: {
        strncpy(g_gui.progress_text, "Restoring...", 63);
        strncpy(g_gui.progress_step, "Step 1/2: Scanning...", 63);
        g_gui.progress_frac = 0.3f;
        raid_scan();
        strncpy(g_gui.progress_step, "Step 2/2: Loading superblock...", 63);
        g_gui.progress_frac = 0.6f;
        RC rc = raid_load(NULL);
        g_gui.progress_frac = 1.0f;
        snprintf(result, 511, "Restore from superblock: %s", rc == RC_OK ? "OK" : "FAILED");
        refresh_ui_model();
        if (rc == RC_OK) toast_push("Restore completed", TOAST_OK);
        else toast_push("Restore failed", TOAST_ERROR);
        break;
    }
    case W_LOAD_CONFIG: {
        strncpy(g_gui.progress_text, "Restoring from config...", 63);
        strncpy(g_gui.progress_step, "Step 1/3: Scanning...", 63);
        g_gui.progress_frac = 0.2f;
        raid_scan();
        strncpy(g_gui.progress_step, "Step 2/3: Loading config...", 63);
        g_gui.progress_frac = 0.4f;
        APP_CONFIG cfg; config_defaults(&cfg); config_load(&cfg);
        if (cfg.disk_count == 0) { snprintf(result, 511, "Restore FAILED - no config found"); g_gui.progress_frac = 1.0f; break; }
        char init_buf[256] = {0}; int pos = 0;
        for (uint32_t i = 0; i < cfg.disk_count; i++) {
            char dl_str[2] = { cfg.disks[i].drive_letter, 0 };
            raid_mapdrive(cfg.disks[i].disk_id, dl_str);
            pos += snprintf(init_buf + pos, 255 - pos, "%s%u:%llu", i > 0 ? " " : "", cfg.disks[i].disk_id, (unsigned long long)cfg.disks[i].pool_mb);
        }
        char* iargs[16]; int iargc = 0;
        char* tok = strtok(init_buf, " ");
        while (tok && iargc < 16) { iargs[iargc++] = tok; tok = strtok(NULL, " "); }
        strncpy(g_gui.progress_step, "Step 3/3: Recreating volume...", 63);
        g_gui.progress_frac = 0.6f;
        if (iargc >= MIN_DISKS) raid_init_pools(iargc, iargs);
        g_gui.progress_frac = 0.8f;
        RC rc = raid_create();
        g_gui.progress_frac = 1.0f;
        snprintf(result, 511, "Restore from config: %s", rc == RC_OK ? "OK" : "FAILED");
        refresh_ui_model();
        if (rc == RC_OK) toast_push("Restore completed", TOAST_OK);
        else toast_push("Restore failed", TOAST_ERROR);
        break;
    }
    case W_REBUILD: {
        strncpy(g_gui.progress_text, "Rebuilding...", 63);
        strncpy(g_gui.progress_step, "Rebuilding... (copying data to replacement disk)", 63);
        g_gui.progress_frac = 0.1f;
        { char rbuf[128]; snprintf(rbuf, 127, "%d %d %d", g_gui.rebuild_failed_idx, g_gui.rebuild_replacement_id, g_gui.rebuild_pool_mb);
          char* rargs[3] = {0}; int rargc = 0;
          char* tok = strtok(rbuf, " "); while (tok && rargc < 3) { rargs[rargc++] = tok; tok = strtok(NULL, " "); }
          RC rc = raid_rebuild(rargc, rargs);
          g_gui.progress_frac = 1.0f;
          snprintf(result, 511, "Rebuild: %s", rc == RC_OK ? "OK" : "FAILED");
          refresh_ui_model();
          if (rc == RC_OK) toast_push("Rebuild completed", TOAST_OK);
          else toast_push("Rebuild failed", TOAST_ERROR); }
        break;
    }
    case W_CHECK: {
        strncpy(g_gui.progress_text, "Checking health...", 63);
        strncpy(g_gui.progress_step, "Step 1/1: Running health check...", 63);
        RC rc = raid_check();
        g_gui.progress_frac = 1.0f;
        snprintf(result, 511, "Health check: %s", rc == RC_OK ? "OK" : "WARN");
        refresh_ui_model();
        if (rc == RC_OK) toast_push("Health check OK", TOAST_OK);
        else toast_push("Health check found issues", TOAST_WARN);
        break;
    }
    case W_WIZARD_SCAN: {
        strncpy(g_gui.progress_text, "Scanning...", 63);
        strncpy(g_gui.progress_step, "Step 1/1: Scanning...", 63);
        raid_scan();
        refresh_ui_model();
        snprintf(result, 511, "Scan complete: %u disk(s)", g_gui.disk_summary.count);
        break;
    }
    case W_WIZARD_CREATE: {
        strncpy(g_gui.progress_text, "Setting up...", 63);
        strncpy(g_gui.progress_step, "Step 1/4: Scanning...", 63);
        g_gui.progress_frac = 0.0f;
        RC rc = raid_scan();
        if (rc != RC_OK) { snprintf(result, 511, "Setup FAILED"); break; }
        uint32_t cnt = device_get_count(), scnt = 0, sids[MAX_DISKS];
        for (uint32_t i = 0; i < cnt && scnt < MAX_DISKS; i++) { if (device_get(i)->healthy) sids[scnt++] = i; }
        if (scnt < MIN_DISKS) { snprintf(result, 511, "Need %d+ healthy disks", MIN_DISKS); break; }
        device_select(sids, scnt);
        strncpy(g_gui.progress_step, "Step 2/4: Creating pool files...", 63);
        g_gui.progress_frac = 0.3f;
        char ib[256] = {0}; int pos = 0;
        for (uint32_t i = 0; i < scnt; i++) pos += snprintf(ib + pos, 255 - pos, "%s%u:51200", i > 0 ? " " : "", sids[i]);
        char* ia[16]; int ic = 0; char* tok = strtok(ib, " "); while (tok && ic < 16) { ia[ic++] = tok; tok = strtok(NULL, " "); }
        rc = raid_init_pools(ic, ia); if (rc != RC_OK) { snprintf(result, 511, "Pool creation failed"); break; }
        strncpy(g_gui.progress_step, "Step 3/4: Creating volume...", 63);
        g_gui.progress_frac = 0.6f;
        rc = raid_create(); if (rc != RC_OK) { snprintf(result, 511, "Volume creation failed"); break; }
        strncpy(g_gui.progress_step, "Step 4/4: Mounting...", 63);
        g_gui.progress_frac = 0.8f;
        { char cache_str[16]; snprintf(cache_str, 16, "%u", CACHE_DEFAULT_MB);
          char* cav[] = { cache_str }; raid_cache(1, cav); }
        rc = raid_mount('G');
        g_gui.progress_frac = 1.0f;
        snprintf(result, 511, "All done! Volume ready at G:");
        refresh_ui_model();
        toast_push("Quick Setup complete", TOAST_OK);
        break;
    }
    case W_SIMULATE_FAIL: {
        strncpy(g_gui.progress_text, "Simulating failure...", 63);
        strncpy(g_gui.progress_step, "Step 1/1: Injecting disk fault...", 63);
        char mode_f[] = "f";
        char* sargv[] = { params, mode_f };
        RC rc = raid_simulate(2, sargv);
        g_gui.progress_frac = 1.0f;
        snprintf(result, 511, "Simulate: disk %s -> FAILED", params);
        refresh_ui_model();
        if (rc == RC_OK) toast_push("Simulated disk failure - state now DEGRADED", TOAST_WARN);
        else toast_push("Simulation failed", TOAST_ERROR);
        break;
    }
    case W_SIMULATE_HEALTHY: {
        strncpy(g_gui.progress_text, "Restoring health...", 63);
        strncpy(g_gui.progress_step, "Step 1/1: Restoring disk health...", 63);
        char mode_h[] = "h";
        char* sargv[] = { params, mode_h };
        RC rc = raid_simulate(2, sargv);
        g_gui.progress_frac = 1.0f;
        snprintf(result, 511, "Simulate: disk %s -> HEALTHY", params);
        refresh_ui_model();
        if (rc == RC_OK) toast_push("Disk restored to healthy", TOAST_OK);
        else toast_push("Restore failed", TOAST_ERROR);
        break;
    }
    case W_SIMULATE_DISCONNECT: {
        strncpy(g_gui.progress_text, "Simulating disconnect...", 63);
        strncpy(g_gui.progress_step, "Step 1/1: Disconnecting disk...", 63);
        char mode_d[] = "d";
        char* sargv[] = { params, mode_d };
        RC rc = raid_simulate(2, sargv);
        g_gui.progress_frac = 1.0f;
        snprintf(result, 511, "Simulate: disk %s -> DISCONNECTED", params);
        refresh_ui_model();
        if (rc == RC_OK) toast_push("Simulated disk disconnect", TOAST_WARN);
        else toast_push("Simulation failed", TOAST_ERROR);
        break;
    }
    case W_CLEANUP: {
        strncpy(g_gui.progress_text, "Cleaning up...", 63);
        strncpy(g_gui.progress_step, "Releasing all RAID resources...", 63);
        raid_cleanup();
        g_gui.progress_frac = 1.0f;
        snprintf(result, 511, "Cleanup complete - all resources released");
        refresh_ui_model();
        toast_push("Cleanup complete", TOAST_OK);
        break;
    }
    case W_CACHE: {
        strncpy(g_gui.progress_text, "Cache...", 63);
        g_gui.progress_frac = 0.5f;
        if (!params || params[0] == 0) { snprintf(result, 511, "Cache: no params"); break; }
        char buf[256]; strncpy(buf, params, 255); buf[255] = 0;
        char* av[2]; int ac = 0;
        char* tok = strtok(buf, " ");
        while (tok && ac < 2) { av[ac++] = tok; tok = strtok(NULL, " "); }
        RC rc = raid_cache(ac, av);
        g_gui.progress_frac = 1.0f;
        snprintf(result, 511, "Cache: %s", rc == RC_OK ? "OK" : "FAILED");
        refresh_ui_model();
        break;
    }
    case W_PURGE: {
        strncpy(g_gui.progress_text, "Purging...", 63);
        strncpy(g_gui.progress_step, "Removing all pool files and superblock...", 63);
        g_gui.progress_frac = 0.5f;
        RC rc = raid_purge();
        g_gui.progress_frac = 1.0f;
        snprintf(result, 511, "Purge: %s - All pool files and superblock removed", rc == RC_OK ? "OK" : "FAILED");
        refresh_ui_model();
        if (rc == RC_OK) toast_push("Purge complete", TOAST_OK);
        else toast_push("Purge failed", TOAST_ERROR);
        break;
    }
    case W_BENCH: {
        strncpy(g_gui.progress_text, "Raw Bench...", 63);
        g_gui.progress_frac = 0.0f;
        char* args[2] = {0}; int argc = 0;
        if (params[0]) { char* tok = strtok(params, " "); while (tok && argc < 2) { args[argc++] = tok; tok = strtok(NULL, " "); } }
        for (int step = 0; step < 4; step++) {
            snprintf(g_gui.progress_step, 63, "Step %d/4: Benchmarking raw disks...", step + 1);
            g_gui.progress_frac = (step + 1) * 0.25f;
            if (InterlockedCompareExchange(&g_gui.worker_cancel, 1, 1) == 1) { strncpy(result, "Bench cancelled", 511); goto raw_bench_done; }
            Sleep(250);
        }
        { RC rc = raid_bench(argc, args);
        g_gui.progress_frac = 1.0f;
        if (rc == RC_OK) {
            uint32_t dc = device_get_count();
            float tot_r = 0, tot_w = 0; int n = 0;
            for (uint32_t i = 0; i < dc; i++) {
                DISK_INFO* d = device_get(i);
                if (d && d->selected && d->benchmarked) {
                    tot_r += (float)d->bench_read_mbs;
                    tot_w += (float)d->bench_write_mbs;
                    n++;
                }
            }
            if (n > 0) {
                snprintf(g_gui.bench_raw_read, 31, "%.1f", tot_r / n);
                snprintf(g_gui.bench_raw_write, 31, "%.1f", tot_w / n);
            } else {
                strncpy(g_gui.bench_raw_read, "N/A", 31);
                strncpy(g_gui.bench_raw_write, "N/A", 31);
            }
            g_gui.bench_raw_done = true;
            g_gui.show_bench_raw = true;
            snprintf(result, 511, "Raw Bench: R=%.1f W=%.1f MB/s (avg)", tot_r / (n > 0 ? n : 1), tot_w / (n > 0 ? n : 1));
            toast_push("Raw bench complete", TOAST_OK);
        } else { snprintf(result, 511, "Raw Bench FAILED"); toast_push("Raw bench failed", TOAST_ERROR); } }
        raw_bench_done:;
        break;
    }
    case W_TEST: {
        strncpy(g_gui.progress_text, "Running I/O verification...", 63);
        strncpy(g_gui.progress_step, "Writing + reading + verifying data on volume...", 63);
        g_gui.progress_frac = 0.5f;
        RC rc = raid_test();
        g_gui.progress_frac = 1.0f;
        snprintf(g_gui.test_result, 255, "I/O verification: %s - Volume data integrity %s", rc == RC_OK ? "PASS" : "FAIL", rc == RC_OK ? "OK" : "CHECK FAILED");
        snprintf(result, 511, "%s", g_gui.test_result);
        refresh_ui_model();
        if (rc == RC_OK) toast_push("I/O verification passed", TOAST_OK);
        else toast_push("I/O verification FAILED - data corruption detected", TOAST_ERROR);
        break;
    }
    case W_RANDOM: {
        strncpy(g_gui.progress_text, "Random I/O stress test...", 63);
        strncpy(g_gui.progress_step, "Running random I/O operations...", 63);
        g_gui.progress_frac = 0.0f;
        char pbuf[64]; snprintf(pbuf, 63, "%d %d", g_gui.random_ops ? g_gui.random_ops : 100, g_gui.random_maxkb ? g_gui.random_maxkb : 64);
        char* av[2]; int ac = 0;
        char* tok = strtok(pbuf, " "); while (tok && ac < 2) { av[ac++] = tok; tok = strtok(NULL, " "); }
        for (int step = 0; step < 5; step++) {
            snprintf(g_gui.progress_step, 63, "Random I/O pass %d/5...", step + 1);
            g_gui.progress_frac = (step + 1) * 0.2f;
            if (InterlockedCompareExchange(&g_gui.worker_cancel, 1, 1) == 1) { strncpy(result, "Random test cancelled", 511); goto random_done; }
            Sleep(200);
        }
        { RC rc = raid_random(ac, av);
        g_gui.progress_frac = 1.0f;
        snprintf(result, 511, "Random I/O test: %s (%d ops, %d KB max)", rc == RC_OK ? "PASS" : "FAIL", g_gui.random_ops ? g_gui.random_ops : 100, g_gui.random_maxkb ? g_gui.random_maxkb : 64);
        refresh_ui_model();
        if (rc == RC_OK) toast_push("Random I/O test passed", TOAST_OK);
        else toast_push("Random I/O test FAILED", TOAST_ERROR); }
        random_done:;
        break;
    }
    case W_CONFIG_SAVE: {
        strncpy(g_gui.progress_text, "Saving config...", 63);
        strncpy(g_gui.progress_step, "Saving current configuration to file...", 63);
        g_gui.progress_frac = 0.5f;
        RC rc = raid_config_save();
        g_gui.progress_frac = 1.0f;
        snprintf(result, 511, "Config save: %s", rc == RC_OK ? "OK - configuration saved" : "FAILED");
        if (rc == RC_OK) toast_push("Configuration saved", TOAST_OK);
        else toast_push("Config save failed", TOAST_ERROR);
        break;
    }
    case W_CONFIG_LOAD: {
        strncpy(g_gui.progress_text, "Loading config...", 63);
        strncpy(g_gui.progress_step, "Loading saved configuration...", 63);
        g_gui.progress_frac = 0.5f;
        RC rc = raid_config_load();
        g_gui.progress_frac = 1.0f;
        refresh_ui_model();
        snprintf(result, 511, "Config load: %s", rc == RC_OK ? "OK - configuration restored" : "FAILED");
        if (rc == RC_OK) toast_push("Configuration loaded", TOAST_OK);
        else toast_push("Config load failed", TOAST_ERROR);
        break;
    }
    case W_EXPAND: {
        strncpy(g_gui.progress_text, "Expanding volume...", 63);
        strncpy(g_gui.progress_step, "Adding new disks to stripe...", 63);
        g_gui.progress_frac = 0.3f;
        char* av[8]; int ac = 0;
        char buf[256]; strncpy(buf, params, 255); buf[255] = 0;
        char* tok = strtok(buf, " ");
        while (tok && ac < 8) { av[ac++] = tok; tok = strtok(NULL, " "); }
        g_gui.progress_frac = 0.6f;
        RC rc = raid_expand(ac, av);
        g_gui.progress_frac = 1.0f;
        snprintf(result, 511, "Expand: %s - Added %d disk(s) to stripe", rc == RC_OK ? "OK" : "FAILED", ac);
        refresh_ui_model();
        if (rc == RC_OK) toast_push("Volume expanded", TOAST_OK);
        else toast_push("Expand failed", TOAST_ERROR);
        break;
    }
    case W_MAP: {
        strncpy(g_gui.progress_text, "Reading LBA map...", 63);
        strncpy(g_gui.progress_step, "Dumping LBA-to-disk mapping...", 63);
        g_gui.progress_frac = 0.5f;
        int pipe_fds[2];
        int saved = _dup(_fileno(stdout));
        _pipe(pipe_fds, 65536, _O_BINARY);
        _dup2(pipe_fds[1], _fileno(stdout));
        _close(pipe_fds[1]);
        RC rc = raid_map();
        _dup2(saved, _fileno(stdout));
        _close(saved);
        setvbuf(stdout, NULL, _IONBF, 0);
        int n = _read(pipe_fds[0], g_gui.dialog_text, 8191);
        if (n < 0) n = 0;
        g_gui.dialog_text[n] = 0;
        _close(pipe_fds[0]);
        g_gui.progress_frac = 1.0f;
        snprintf(result, 511, "Map: %s (%d bytes)", rc == RC_OK ? "OK" : "FAILED", n);
        g_gui.show_map_dialog = true;
        break;
    }
    case W_METADATA: {
        strncpy(g_gui.progress_text, "Reading metadata...", 63);
        strncpy(g_gui.progress_step, "Dumping superblock contents...", 63);
        g_gui.progress_frac = 0.5f;
        int pipe_fds[2];
        int saved = _dup(_fileno(stdout));
        _pipe(pipe_fds, 65536, _O_BINARY);
        _dup2(pipe_fds[1], _fileno(stdout));
        _close(pipe_fds[1]);
        char mnt[2] = { g_gui.settings.mount_letter ? g_gui.settings.mount_letter : 'G', 0 };
        char* mav[] = { mnt };
        RC rc = raid_metadata(1, mav);
        _dup2(saved, _fileno(stdout));
        _close(saved);
        setvbuf(stdout, NULL, _IONBF, 0);
        int n = _read(pipe_fds[0], g_gui.dialog_text, 8191);
        if (n < 0) n = 0;
        g_gui.dialog_text[n] = 0;
        _close(pipe_fds[0]);
        g_gui.progress_frac = 1.0f;
        snprintf(result, 511, "Metadata: %s (%d bytes)", rc == RC_OK ? "OK" : "FAILED", n);
        g_gui.show_metadata_dialog = true;
        break;
    }
    default:
        snprintf(result, 511, "Unknown action");
    }
    g_gui.progress_frac = 1.0f;
    g_gui.progress_eta = 0;
    InterlockedExchange(&g_gui.worker_done, 1);
    return 0;
}

static void start_worker(WorkerAction action, const char* params) {
    if (InterlockedCompareExchange(&g_gui.worker_running, 1, 0) != 0) {
        gui_log("Busy: wait for current operation to finish");
        return;
    }
    g_gui.worker_action = action;
    g_gui.worker_result[0] = 0;
    g_gui.worker_done = 0;
    g_gui.progress_frac = 0.0f;
    g_gui.progress_text[0] = 0;
    g_gui.progress_step[0] = 0;
    if (params) strncpy(g_gui.worker_params, params, 255);
    else g_gui.worker_params[0] = 0;
    g_gui.worker_handle = (HANDLE)_beginthreadex(NULL, 0, worker_thread, NULL, 0, &g_gui.worker_thread_id);
}

static void cancel_worker(void) {
    InterlockedExchange(&g_gui.worker_cancel, 1);
    gui_log("Cancelling operation...");
}

static void check_worker_done(void) {
    if (InterlockedCompareExchange(&g_gui.worker_done, 1, 1) == 1) {
        bool failed = (strstr(g_gui.worker_result, "FAILED") != NULL);
        bool cancelled = (strstr(g_gui.worker_result, "Cancelled") != NULL);
        gui_log(g_gui.worker_result);
        strncpy(g_gui.status, g_gui.worker_result, sizeof(g_gui.status) - 1);
        if (failed && !cancelled)
            MessageBoxA(g_gui.hwnd, g_gui.worker_result, "Operation Failed", MB_ICONERROR | MB_OK);
        if (g_gui.worker_handle) {
            WaitForSingleObject(g_gui.worker_handle, INFINITE);
            CloseHandle(g_gui.worker_handle);
            g_gui.worker_handle = NULL;
        }
        InterlockedExchange(&g_gui.worker_running, 0);
        InterlockedExchange(&g_gui.worker_done, 0);
        g_gui.progress_frac = 0.0f;
        refresh_ui_model();
    }
}

static void SetupLightTheme(void) {
    ImGui::StyleColorsLight();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0.0f; s.ChildRounding = 0.0f;
    s.FrameRounding = 3.0f; s.PopupRounding = 3.0f;
    s.ScrollbarRounding = 3.0f; s.GrabRounding = 3.0f; s.TabRounding = 3.0f;
}

static void ApplyTheme(void) {
    SetupLightTheme();
}

static bool CreateRenderTarget(void) {
    ID3D11Texture2D* back = NULL;
    g_gui.swapchain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (!back) return false;
    g_gui.device->CreateRenderTargetView(back, NULL, &g_gui.rtv);
    back->Release();
    return g_gui.rtv != NULL;
}

static bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd = {0};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1; sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL featureLevel;
    D3D_FEATURE_LEVEL flvl[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE,
        NULL, createFlags, flvl, 2, D3D11_SDK_VERSION,
        &sd, &g_gui.swapchain, &g_gui.device, &featureLevel, &g_gui.ctx);
    if (hr != S_OK) {
        hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_WARP,
            NULL, createFlags, flvl, 2, D3D11_SDK_VERSION,
            &sd, &g_gui.swapchain, &g_gui.device, &featureLevel, &g_gui.ctx);
        if (hr != S_OK) return false;
    }
    return CreateRenderTarget();
}

static void CleanupDeviceD3D(void) {
    if (g_gui.rtv) { g_gui.rtv->Release(); g_gui.rtv = NULL; }
    if (g_gui.swapchain) { g_gui.swapchain->Release(); g_gui.swapchain = NULL; }
    if (g_gui.ctx) { g_gui.ctx->Release(); g_gui.ctx = NULL; }
    if (g_gui.device) { g_gui.device->Release(); g_gui.device = NULL; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_SIZE:
        if (g_gui.device && wParam != SIZE_MINIMIZED) {
            g_gui.ctx->OMSetRenderTargets(0, NULL, NULL);
            g_gui.rtv->Release(); g_gui.rtv = NULL;
            g_gui.swapchain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

static bool SetupWindow(HINSTANCE hInst) {
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, hInst,
        NULL, NULL, NULL, NULL, L"RAIDTEST_GUI_IMGUI", NULL };
    if (!RegisterClassExW(&wc)) return false;
    RECT r = { 0, 0, 1280, 800 };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    g_gui.hwnd = CreateWindowW(L"RAIDTEST_GUI_IMGUI",
        L"RAIDTEST " APP_VERSION L" - GUI Edition",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        r.right - r.left, r.bottom - r.top, NULL, NULL, hInst, NULL);
    return g_gui.hwnd != NULL;
}

static bool btn_disabled(void) { return g_gui.worker_running != 0; }

static void ShowSettings(bool* open) {
    if (!ImGui::Begin("Settings", open, ImGuiWindowFlags_AlwaysAutoResize)) return ImGui::End();
    APP_CONFIG* s = &g_gui.settings;
    ImGui::Text("General"); ImGui::Separator();
    ImGui::Text("Default drive letter:");
    ImGui::SameLine();
    char ml[2] = { s->mount_letter ? s->mount_letter : 'G', 0 };
    ImGui::SetNextItemWidth(30);
    if (ImGui::InputText("##set_ml", ml, 2, ImGuiInputTextFlags_CharsUppercase))
        s->mount_letter = ml[0] ? ml[0] : 'G';
    ImGui::Text("Default cache size (MB):");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    int cache = (int)s->cache_mb;
    if (ImGui::InputInt("##set_cache", &cache, 256)) { if (cache < 256) cache = 256; s->cache_mb = (uint32_t)cache; }
    ImGui::Text("Theme:");
    ImGui::SameLine();
    const char* themes[] = { "Dark", "Light" };
    int cur = s->theme;
    if (ImGui::Combo("##set_theme", &cur, themes, 2)) {
        s->theme = cur; g_gui.use_light_theme = (cur == THEME_LIGHT); ApplyTheme();
    }
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::Text("Startup"); ImGui::Separator();
    ImGui::Checkbox("Auto restore on startup", &s->auto_restore);
    ImGui::Checkbox("Auto mount after restore", &s->auto_mount);
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::Text("About"); ImGui::Separator();
    ImGui::Text("Language: English");
    ImGui::Text("Version: %s", APP_VERSION);
    ImGui::Dummy(ImVec2(0, 8));
    if (ImGui::Button("Save Settings", ImVec2(140, 24))) { config_save(s); g_gui.pool_size_mb = (int)s->pool_mb; gui_log("Settings saved"); toast_push("Settings saved", TOAST_OK); }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 24))) { *open = false; }
    ImGui::End();
}


static void ShowAbout(bool* open) {
    if (!ImGui::Begin("About RAIDTEST", open, ImGuiWindowFlags_AlwaysAutoResize)) return ImGui::End();
    ImGui::TextUnformatted("RAIDTEST - Asymmetric Stripe RAID Engine");
    ImGui::Separator();
    ImGui::Text("Version:  %s", APP_VERSION);
    ImGui::Text("Build:    %s %s", __DATE__, __TIME__);
#ifdef __GNUC__
    ImGui::Text("Compiler: GCC %d.%d.%d", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#elif _MSC_VER
    ImGui::Text("Compiler: MSVC %d", _MSC_VER);
#else
    ImGui::Text("Compiler: Unknown");
#endif
    ImGui::Text("Arch:     x64");
    ImGui::Separator();
    ImGui::TextUnformatted("Libraries:");
    ImGui::BulletText("Dear ImGui 1.91 (DX11)");
    ImGui::BulletText("WinFsp 2.1 (FUSE)");
    ImGui::BulletText("DirectX 11 / MinGW-w64 (MSYS2)");
    ImGui::Separator();
    ImGui::TextUnformatted("License: MIT");
    ImGui::TextUnformatted("Copyright (c) 2025 RAIDTEST Contributors");
    ImGui::End();
}

static void ShowWelcomeWizard(void) {
    if (!g_gui.show_welcome) return;
    ImGui::OpenPopup("Welcome to RAIDTEST");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(460, 340));
    if (ImGui::BeginPopupModal("Welcome to RAIDTEST", NULL, ImGuiWindowFlags_NoResize)) {
        ImGui::TextUnformatted("Welcome to RAIDTEST v1.0 RC4");
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::TextWrapped("RAIDTEST creates virtual RAID 0/1/10 volumes across mixed-speed physical disks (SATA SSD, NVMe, HDD).");
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::TextColored(ImVec4(0.2f,0.6f,1.0f,1.0f), "Getting Started:");
        bool winfsp_ok = (GetModuleHandleA("winfsp-x64.dll") != NULL);
        if (winfsp_ok) ImGui::TextColored(ImVec4(0,1,0,1), "[OK] WinFsp detected");
        else {
            ImGui::TextColored(ImVec4(1,1,0,1), "[!] WinFsp not found - mount requires it");
            ImGui::TextWrapped("Download from: https://winfsp.dev/rel/");
        }
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::TextUnformatted("Beginner mode shows Quick Actions for common tasks.");
        ImGui::TextUnformatted("Advanced mode gives full control over disks and cache.");
        ImGui::TextUnformatted("Developer mode shows detailed disk and volume diagnostics.");
        ImGui::Dummy(ImVec2(0, 8));
        if (ImGui::Button("Quick Setup (Recommended)", ImVec2(200, 30))) {
            g_gui.show_welcome = false; g_gui.settings.first_run = false;
            config_save(&g_gui.settings); ImGui::CloseCurrentPopup();
            start_worker(W_WIZARD_CREATE, NULL);
        }
        ImGui::SameLine();
        if (ImGui::Button("Explore Beginner Mode", ImVec2(200, 30))) {
            g_gui.show_welcome = false; g_gui.settings.first_run = false;
            config_save(&g_gui.settings); g_gui.mode = MODE_BEGINNER;
            ImGui::CloseCurrentPopup();
            start_worker(W_WIZARD_SCAN, NULL);
        }
        ImGui::Dummy(ImVec2(0, 4));
        if (ImGui::Button("Don't show this again", ImVec2(180, 22))) {
            g_gui.show_welcome = false; g_gui.settings.first_run = false;
            config_save(&g_gui.settings); ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

static void ShowModeTabs(void) {
    const char* labels[] = { "  Beginner  ", "  Advanced  ", "  Developer  " };
    for (int i = 0; i < 3; i++) {
        if (i > 0) ImGui::SameLine(0, 0);
        bool active = (g_gui.mode == i);
        if (active) {
            ImVec4 ac = ImVec4(0.20f, 0.45f, 0.70f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ac);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ac);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ac);
        }
        if (ImGui::Button(labels[i], ImVec2(0, 22))) g_gui.mode = (GUIMode)i;
        if (active) ImGui::PopStyleColor(3);
        if (i < 2) ImGui::SameLine(0, 0);
    }
}

static void ShowToolbar(void) {
    ImGui::BeginChild("##tb_content", ImVec2(0, 0), false);
    float w = ImGui::GetContentRegionAvail().x;
    float bw = w * 0.075f;
    bool busy = btn_disabled();
    bool create_ok = g_gui.state_value == 1 && g_gui.selected_count >= 2 && !busy;
    bool mountable = g_gui.state_value >= 2 && !g_gui.vol_info.mounted && !busy;
    bool unmountable = g_gui.vol_info.mounted && !busy;
    bool destroyable = g_gui.state_value >= 2 && !busy;
    bool benchable = g_gui.vol_info.mounted && !busy;

    if (ImGui::Button("Scan", ImVec2(bw, 28)) && !busy) start_worker(W_SCAN, NULL);
    ImGui::SameLine();
    if (!create_ok) ImGui::BeginDisabled();
    if (ImGui::Button("Create", ImVec2(bw, 28))) {
        char p[256] = {0}; int pos = 0;
        for (int i = 0; i < g_gui.selected_count && i < 4; i++) {
            int mb = g_gui.pool_per_disk[i] > 0 ? g_gui.pool_per_disk[i] :
                     (g_gui.pool_size_mb ? g_gui.pool_size_mb : 51200);
            pos += snprintf(p + pos, 255 - (size_t)pos, "%s%d:%d", pos ? " " : "",
                g_gui.selected_disks[i], mb);
        }
        start_worker(W_CREATE, p);
    }
    if (!create_ok) ImGui::EndDisabled();
    ImGui::SameLine();
    if (!create_ok) ImGui::BeginDisabled();
    if (ImGui::Button("Mirror", ImVec2(bw, 28))) {
        char p[256] = {0}; int pos = 0;
        for (int i = 0; i < g_gui.selected_count && i < 4; i++) {
            int mb = g_gui.pool_per_disk[i] > 0 ? g_gui.pool_per_disk[i] :
                     (g_gui.pool_size_mb ? g_gui.pool_size_mb : 51200);
            pos += snprintf(p + pos, 255 - (size_t)pos, "%s%d:%d", pos ? " " : "",
                g_gui.selected_disks[i], mb);
        }
        start_worker(W_MIRROR, p);
    }
    if (!create_ok) ImGui::EndDisabled();
    ImGui::SameLine();
    char ml_label[4] = { g_gui.settings.mount_letter ? g_gui.settings.mount_letter : 'R', ':', 0 };
    ImGui::SetNextItemWidth(36);
    if (ImGui::BeginCombo("##ml", ml_label, ImGuiComboFlags_NoArrowButton)) {
        for (char c = 'A'; c <= 'Z'; c++) {
            char lb[2] = { c, 0 };
            bool sel = (c == g_gui.settings.mount_letter);
            if (ImGui::Selectable(lb, sel)) { g_gui.settings.mount_letter = c; }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine(0, 0);
    if (!mountable) ImGui::BeginDisabled();
    if (ImGui::Button("Mount", ImVec2(bw, 28))) {
        char m[2] = { g_gui.settings.mount_letter ? g_gui.settings.mount_letter : 'R', 0 };
        start_worker(W_MOUNT, m);
    }
    if (!mountable) ImGui::EndDisabled();
    ImGui::SameLine();
    if (!unmountable) ImGui::BeginDisabled();
    if (ImGui::Button("Unmount", ImVec2(bw, 28))) start_worker(W_UNMOUNT, NULL);
    if (!unmountable) ImGui::EndDisabled();
    ImGui::SameLine();
    if (!destroyable) ImGui::BeginDisabled();
    if (ImGui::Button("Destroy", ImVec2(bw, 28))) g_gui.show_confirm_destroy = true;
    if (!destroyable) ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Quick", ImVec2(bw, 28)) && !busy) start_worker(W_QUICK_SETUP, NULL);
    ImGui::SameLine();
    if (ImGui::Button("Check", ImVec2(bw, 28)) && !busy) start_worker(W_CHECK, NULL);
    ImGui::SameLine();
    if (!benchable) ImGui::BeginDisabled();
    if (ImGui::Button("Bench", ImVec2(bw, 28))) {
        g_gui.show_bench = true; g_gui.bench_done = false;
        g_gui.bench_read_mbs[0] = 0; g_gui.bench_write_mbs[0] = 0; g_gui.bench_latency[0] = 0;
        start_worker(W_BENCHFS, "512 10240");
    }
    if (!benchable) ImGui::EndDisabled();
    ImGui::EndChild();
}

static void ShowDiskList(void) {
    uint32_t count = device_get_count();
    float txt = (float)ImGui::GetContentRegionAvail().y - 28;
    ImGui::Text("%u disk(s) | %u selected | Total: %.1f GB | State: %s",
        g_gui.disk_summary.count, g_gui.disk_summary.selected_count,
        g_gui.disk_summary.total_capacity_mb / 1024.0, ui_get_state_str());
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 200);
    if (ImGui::SmallButton("All")) {
        for (uint32_t i = 0; i < count && i < 64; i++) {
            DISK_INFO* d = device_get(i);
            if (d && d->healthy) g_gui.disk_checked[i] = 1;
        }
        uint32_t indices[64]; uint32_t n = 0;
        for (uint32_t i = 0; i < count && i < 64; i++) if (g_gui.disk_checked[i]) indices[n++] = i;
        device_select(indices, n); refresh_ui_model();
        int def = g_gui.pool_size_mb ? g_gui.pool_size_mb : 51200;
        for (int si = 0; si < g_gui.selected_count && si < 8; si++) g_gui.pool_per_disk[si] = def;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("None")) {
        memset(g_gui.disk_checked, 0, sizeof(g_gui.disk_checked));
        device_select(NULL, 0); refresh_ui_model();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Sel SATA")) {
        for (uint32_t i = 0; i < count && i < 64; i++) {
            DISK_INFO* d = device_get(i);
            g_gui.disk_checked[i] = (d && (d->type == DISK_TYPE_SATA_SSD)) ? 1 : 0;
        }
        uint32_t indices[64]; uint32_t n = 0;
        for (uint32_t i = 0; i < count && i < 64; i++) if (g_gui.disk_checked[i]) indices[n++] = i;
        device_select(indices, n); refresh_ui_model();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Sel NVMe")) {
        for (uint32_t i = 0; i < count && i < 64; i++) {
            DISK_INFO* d = device_get(i);
            g_gui.disk_checked[i] = (d && d->type == DISK_TYPE_NVME_SSD) ? 1 : 0;
        }
        uint32_t indices[64]; uint32_t n = 0;
        for (uint32_t i = 0; i < count && i < 64; i++) if (g_gui.disk_checked[i]) indices[n++] = i;
        device_select(indices, n); refresh_ui_model();
    }
    static ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable("##disks", 10, flags, ImVec2(0, txt))) {
        ImGui::TableSetupColumn("Model",  ImGuiTableColumnFlags_WidthFixed, 170);
        ImGui::TableSetupColumn("ID",     ImGuiTableColumnFlags_WidthFixed, 25);
        ImGui::TableSetupColumn("Serial", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Type",   ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Bus",    ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("Size",   ImGuiTableColumnFlags_WidthFixed, 65);
        ImGui::TableSetupColumn("Speed",  ImGuiTableColumnFlags_WidthFixed, 45);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 55);
        ImGui::TableSetupColumn("RAID",   ImGuiTableColumnFlags_WidthFixed, 40);
        ImGui::TableSetupColumn("Use",    ImGuiTableColumnFlags_WidthFixed, 32);
        ImGui::TableHeadersRow();
        g_gui.selected_count = 0;
        bool selection_changed = false;
        for (uint32_t i = 0; i < count && i < 64; i++) {
            DISK_INFO* d = device_get(i);
            if (!d) continue;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            char modelA[128] = {0}; wcstombs(modelA, d->model, 127);
            ImGui::TextUnformatted(modelA);
            ImGui::TableSetColumnIndex(1); ImGui::Text("%u", i);
            ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(d->serial_number[0] ? d->serial_number : "-");
            ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(disk_type_str(d->type));
            ImGui::TableSetColumnIndex(4);
            const char* bus = "SATA";
            if (d->type == DISK_TYPE_NVME_SSD) bus = "NVMe";
            else if (d->type == DISK_TYPE_RAMDISK) bus = "RAM";
            else if (d->type == DISK_TYPE_FILEBACKED) bus = "FILE";
            ImGui::TextUnformatted(bus);
            ImGui::TableSetColumnIndex(5); ImGui::Text("%.0f GB", (double)d->total_bytes / 1e9);
            ImGui::TableSetColumnIndex(6); ImGui::Text("%u", d->benchmarked ? d->bench_write_mbs : d->write_speed_mbs);
            ImGui::TableSetColumnIndex(7);
            if (d->healthy) ImGui::TextColored(ImVec4(0,1,0,1), "Online");
            else ImGui::TextColored(ImVec4(1,0,0,1), "Offline");
            ImGui::TableSetColumnIndex(8);
            if (d->selected) ImGui::TextColored(ImVec4(0,1,1,1), "Yes");
            else ImGui::TextDisabled("-");
            ImGui::TableSetColumnIndex(9);
            char cbid[16]; snprintf(cbid, 16, "##ck%u", i);
            bool checked = !!g_gui.disk_checked[i];
            if (ImGui::Checkbox(cbid, &checked)) {
                g_gui.disk_checked[i] = checked ? 1 : 0;
                selection_changed = true;
            }
            if (checked && g_gui.selected_count < 8)
                g_gui.selected_disks[g_gui.selected_count++] = (int)i;
        }
        if (selection_changed && !g_gui.worker_running) {
            uint32_t indices[64];
            uint32_t n = 0;
            for (uint32_t i = 0; i < count && i < 64; i++) {
                if (g_gui.disk_checked[i]) indices[n++] = i;
            }
            device_select(indices, n);
            refresh_ui_model();
            int default_mb = g_gui.pool_size_mb ? g_gui.pool_size_mb : 51200;
            for (int si = 0; si < g_gui.selected_count && si < 8; si++) {
                DISK_INFO* d = device_get(g_gui.selected_disks[si]);
                uint64_t max_bytes = d ? (d->pool_bytes > 0 ? d->pool_bytes : d->total_bytes) : 0;
                int max_mb = max_bytes > 0 ? (int)(max_bytes / 1048576) : default_mb;
                g_gui.pool_per_disk[si] = default_mb < max_mb ? default_mb : max_mb;
            }
        }
        ImGui::EndTable();
    }
}

static void ShowDiskAllocation(void) {
    if (g_gui.selected_count < 1) return;
    ImGui::SeparatorText("Disk Allocation");
    int def = g_gui.pool_size_mb ? g_gui.pool_size_mb : 51200;
    ImGui::SetNextItemWidth(100);
    ImGui::InputInt("##unif_pool", &def, 1024);
    ImGui::SameLine();
    if (ImGui::SmallButton("Set All")) {
        for (int i = 0; i < g_gui.selected_count && i < 8; i++) {
            DISK_INFO* d = device_get(g_gui.selected_disks[i]);
            uint64_t max_bytes = d ? (d->pool_bytes > 0 ? d->pool_bytes : d->total_bytes) : 0;
            int max_mb = max_bytes > 0 ? (int)(max_bytes / 1048576) : def;
            g_gui.pool_per_disk[i] = def < max_mb ? def : max_mb;
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("MB (uniform)");
    for (int i = 0; i < g_gui.selected_count && i < 8; i++) {
        DISK_INFO* d = device_get(g_gui.selected_disks[i]);
        if (!d) continue;
        char modelA[64] = {0}; wcstombs(modelA, d->model, 63);
        uint64_t max_bytes = d->pool_bytes > 0 ? d->pool_bytes : d->total_bytes;
        int max_mb = max_bytes > 0 ? (int)(max_bytes / 1048576) : 0;
        char label[16]; snprintf(label, 16, "##pd%d", i);
        ImGui::SetNextItemWidth(100);
        ImGui::InputInt(label, &g_gui.pool_per_disk[i], 1024);
        if (g_gui.pool_per_disk[i] < 0) g_gui.pool_per_disk[i] = 0;
        if (max_mb > 0 && g_gui.pool_per_disk[i] > max_mb) g_gui.pool_per_disk[i] = max_mb;
        ImGui::SameLine();
        ImGui::TextDisabled("#%d %s (max %d MB)", (int)g_gui.selected_disks[i], modelA, max_mb);
    }
}

static void ShowVolumeInfoContent(void) {
    UI_VOLUME_INFO* vi = &g_gui.vol_info;
    UI_HEALTH_SUMMARY* h = &g_gui.health;
    if (vi->exists) {
        ImGui::Columns(2, NULL, false);
        ImGui::Text("State:"); ImGui::NextColumn();
        {   const char* st = ui_get_state_str();
            ImVec4 s_col = ImVec4(0,0,0,1);
            if (strcmp(st, "DEGRADED") == 0) s_col = ImVec4(1,1,0,1);
            else if (strcmp(st, "RECOVERING") == 0) s_col = ImVec4(0.3f,0.6f,1,1);
            else if (strcmp(st, "MOUNTED") == 0) s_col = ImVec4(0,0.6f,0,1);
            ImGui::TextColored(s_col, "%s", st);
        } ImGui::NextColumn();
        ImGui::Text("RAID Level:"); ImGui::NextColumn();
        ImGui::Text("RAID%u", vi->raid_level); ImGui::NextColumn();
        ImGui::Text("Disks:"); ImGui::NextColumn();
        ImGui::Text("%u", vi->disk_count); ImGui::NextColumn();
        ImGui::Text("Capacity:"); ImGui::NextColumn();
        ImGui::Text("%llu GB", vi->virtual_capacity_bytes / 1000000000ULL); ImGui::NextColumn();
        ImGui::Text("Used:"); ImGui::NextColumn();
        if (vi->mounted && h->total_count > 0) {
            double used_pct = (double)vi->bytes_written / (double)vi->virtual_capacity_bytes * 100.0;
            if (used_pct > 100) used_pct = 100;
            ImGui::Text("%.1f%%", used_pct);
        } else { ImGui::TextUnformatted("N/A"); } ImGui::NextColumn();
        ImGui::Text("Mounted:"); ImGui::NextColumn();
        if (vi->mounted) ImGui::TextColored(ImVec4(0,0.6f,0,1), "Yes");
        else ImGui::TextUnformatted("No");
        ImGui::NextColumn();
        ImGui::Text("Cache:"); ImGui::NextColumn();
        ImGui::Text("%s (%u MB)", vi->cache_enabled ? "ON" : "OFF", vi->cache_mb); ImGui::NextColumn();
        ImGui::Text("Read:"); ImGui::NextColumn();
        char rb[32]; snprintf(rb, 32, "%llu MB", vi->bytes_read / 1000000ULL);
        ImGui::TextUnformatted(rb); ImGui::NextColumn();
        ImGui::Text("Write:"); ImGui::NextColumn();
        char wb[32]; snprintf(wb, 32, "%llu MB", vi->bytes_written / 1000000ULL);
        ImGui::TextUnformatted(wb); ImGui::NextColumn();
        ImGui::Columns(1);
        ImGui::Separator();
        ImGui::Text("Health: "); ImGui::SameLine();
        if (h->total_count > 0 && !h->degraded)
            ImGui::TextColored(ImVec4(0,0.6f,0,1), "%u/%u healthy", h->healthy_count, h->total_count);
        else if (h->degraded)
            ImGui::TextColored(ImVec4(1,1,0,1), "%u/%u DEGRADED", h->healthy_count, h->total_count);
        else ImGui::TextUnformatted("N/A");
    } else {
        ImGui::TextDisabled("No volume - Scan + Create first");
    }
}


static void ShowEventLogContent(float height) {
    ImGui::BeginChild("##logsc", ImVec2(0, height), true);
    EnterCriticalSection(&g_gui.log_lock);
    int n = g_gui.log_count;
    int start = n < MAX_LOG_LINES ? 0 : g_gui.log_tail;
    for (int i = start; i < start + n; i++) {
        int idx = i % MAX_LOG_LINES;
        if (g_gui.log_lines[idx][0] == 0) continue;
        ImVec4 col(0,0,0,1);
        if (strstr(g_gui.log_lines[idx], "ERROR") || strstr(g_gui.log_lines[idx], "FAILED"))
            col = ImVec4(1,0.3f,0.3f,1);
        else if (strstr(g_gui.log_lines[idx], "WARN")) col = ImVec4(1,0.8f,0,1);
        else if (strstr(g_gui.log_lines[idx], "OK")) col = ImVec4(0,0.5f,0,1);
        else if (strstr(g_gui.log_lines[idx], "INFO")) col = ImVec4(0,0.4f,0.7f,1);
        ImGui::TextColored(col, "%s", g_gui.log_lines[idx]);
    }
    LeaveCriticalSection(&g_gui.log_lock);
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

static void ShowStatusBarContent(float height) {
    ImGui::BeginChild("##status", ImVec2(0, height), false);
    float w = ImGui::GetContentRegionAvail().x;
    ImGui::TextUnformatted(g_gui.status);
    ImGui::SameLine();
    if (g_gui.worker_running) {
        float x = ImGui::GetCursorPosX();
        ImGui::TextColored(ImVec4(1,1,0,1), " [");
        ImGui::SameLine();
        if (g_gui.progress_text[0]) ImGui::TextUnformatted(g_gui.progress_text);
        else ImGui::TextUnformatted("Working");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1,1,0,1), "]");
        ImGui::SameLine(x + 200);
        ImGui::ProgressBar(g_gui.progress_frac, ImVec2(120, 16), "");
        if (g_gui.progress_step[0]) {
            ImGui::SameLine(x + 330);
            ImGui::TextUnformatted(g_gui.progress_step);
        }
        if (g_gui.progress_frac > 0.01f && g_gui.progress_eta > 0) {
            ImGui::SameLine(w - 160);
            int etas = (int)g_gui.progress_eta;
            ImGui::Text("ETA: %02d:%02d", etas / 60, etas % 60);
        }
        ImGui::SameLine(w - 50);
        if (ImGui::Button("X", ImVec2(24, 18))) cancel_worker();
    } else {
        float x = ImGui::GetCursorPosX();
        ImGui::SameLine(x + 150);
        const char* s = ui_get_state_str();
        ImGui::TextColored(ImVec4(0,1,0,1), "%s", s);
        ImGui::SameLine(w - 170);
        ImGui::Text("RAIDTEST %s", APP_VERSION);
    }
    ImGui::EndChild();
}

static void ShowConfirmDestroy(void) {
    ImGui::OpenPopup("Confirm Destroy");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Confirm Destroy", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Are you sure you want to DESTROY the volume?");
        ImGui::TextColored(ImVec4(1,1,0,1), "This will delete ALL data on the volume.");
        ImGui::TextUnformatted("This action CANNOT be undone.");
        ImGui::Separator();
        if (ImGui::Button("Yes, Destroy", ImVec2(120, 0))) {
            start_worker(W_DESTROY, NULL);
            g_gui.show_confirm_destroy = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            g_gui.show_confirm_destroy = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

static void ShowConfirmPurge(void) {
    ImGui::OpenPopup("Confirm Purge");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Confirm Purge", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Are you sure you want to PURGE all pool files?");
        ImGui::TextColored(ImVec4(1,0.3f,0,1), "This will remove all pool files AND the superblock.");
        ImGui::TextUnformatted("The disk will be clean with no RAID metadata.");
        ImGui::TextUnformatted("This action CANNOT be undone.");
        ImGui::Separator();
        if (ImGui::Button("Yes, Purge", ImVec2(120, 0))) {
            start_worker(W_PURGE, NULL);
            g_gui.show_purge_confirm = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            g_gui.show_purge_confirm = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

static void ShowRawBenchResults(void) {
    if (!g_gui.show_bench_raw) return;
    ImGui::OpenPopup("Raw Bench Results");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Raw Bench Results", &g_gui.show_bench_raw, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (g_gui.bench_raw_done) {
            ImGui::TextUnformatted("Raw Disk Benchmark Results");
            ImGui::Separator();
            ImGui::Text("Average Read:  %s MB/s", g_gui.bench_raw_read);
            ImGui::Text("Average Write: %s MB/s", g_gui.bench_raw_write);
            ImGui::Separator();
            if (ImGui::Button("Run Again", ImVec2(100, 0))) {
                g_gui.bench_raw_done = false;
                start_worker(W_BENCH, "512");
            }
            ImGui::SameLine();
            if (ImGui::Button("Close", ImVec2(100, 0))) {
                g_gui.show_bench_raw = false;
                ImGui::CloseCurrentPopup();
            }
        } else {
            ImGui::TextUnformatted("Benchmarking in progress...");
            ImGui::ProgressBar(g_gui.progress_frac, ImVec2(250, 0), "");
            if (ImGui::Button("Cancel", ImVec2(100, 0))) {
                g_gui.show_bench_raw = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
}

static void ShowExpandDialog(void) {
    if (!g_gui.show_expand_dialog) return;
    ImGui::OpenPopup("Expand RAID0 Volume");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(480, 360), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Expand RAID0 Volume", &g_gui.show_expand_dialog, ImGuiWindowFlags_NoResize)) {
        ImGui::TextUnformatted("Select disks to add to the stripe volume:");
        ImGui::Separator();
        uint32_t dc = device_get_count();
        g_gui.expand_count = 0;
        if (ImGui::BeginChild("##expand_list", ImVec2(0, 240), true)) {
            for (uint32_t i = 0; i < dc; i++) {
                DISK_INFO* d = device_get(i);
                if (!d) continue;
                char modelA[64] = {0}; wcstombs(modelA, d->model, 63);
                char cbid[24]; snprintf(cbid, 24, "##exp_ck%u", i);
                bool ck = !!g_gui.expand_checks[i];
                ImGui::Checkbox(cbid, &ck);
                g_gui.expand_checks[i] = ck ? 1 : 0;
                ImGui::SameLine();
                ImGui::Text("#%u %s", i, modelA);
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 120);
                int sz = g_gui.expand_sizes[g_gui.expand_count];
                if (sz < 1024) sz = 51200;
                ImGui::SetNextItemWidth(110);
                char szid[24]; snprintf(szid, 24, "##exp_sz%u", i);
                int max_mb = (d->pool_bytes > 0 ? (int)(d->pool_bytes / 1048576) : 102400);
                ImGui::InputInt(szid, &sz, 1024);
                if (sz < 1024) sz = 1024;
                if (sz > max_mb) sz = max_mb;
                int idx = g_gui.expand_count;
                g_gui.expand_sizes[idx] = sz;
                g_gui.expand_disk_ids[idx] = (int)i;
                if (ck) g_gui.expand_count++;
            }
        }
        ImGui::EndChild();
        ImGui::Separator();
        if (ImGui::Button("Expand", ImVec2(120, 0)) && g_gui.expand_count > 0) {
            char p[256] = {0}; int pos = 0;
            int idx = 0;
            for (uint32_t i = 0; i < dc && idx < 8; i++) {
                if (g_gui.expand_checks[i]) {
                    pos += snprintf(p + pos, 255 - (size_t)pos, "%s%u:%d", pos ? " " : "", i, g_gui.expand_sizes[idx]);
                    idx++;
                }
            }
            g_gui.show_expand_dialog = false;
            ImGui::CloseCurrentPopup();
            start_worker(W_EXPAND, p);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            g_gui.show_expand_dialog = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

static void ShowMapDialog(void) {
    if (!g_gui.show_map_dialog) return;
    ImGui::OpenPopup("LBA Mapping");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("LBA Mapping", &g_gui.show_map_dialog, ImGuiWindowFlags_NoResize)) {
        if (g_gui.dialog_text[0]) {
            ImGui::TextUnformatted("LBA-to-disk mapping:");
            ImGui::Separator();
            ImGui::InputTextMultiline("##map_text", g_gui.dialog_text, sizeof(g_gui.dialog_text),
                ImVec2(-1, ImGui::GetContentRegionAvail().y - 36),
                ImGuiInputTextFlags_ReadOnly);
        } else {
            ImGui::TextUnformatted("Mapping data not available.");
        }
        ImGui::Separator();
        if (ImGui::Button("Close", ImVec2(100, 0))) {
            g_gui.show_map_dialog = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

static void ShowMetadataDialog(void) {
    if (!g_gui.show_metadata_dialog) return;
    ImGui::OpenPopup("Superblock Metadata");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Superblock Metadata", &g_gui.show_metadata_dialog, ImGuiWindowFlags_NoResize)) {
        if (g_gui.dialog_text[0]) {
            ImGui::TextUnformatted("Superblock contents:");
            ImGui::Separator();
            ImGui::InputTextMultiline("##meta_text", g_gui.dialog_text, sizeof(g_gui.dialog_text),
                ImVec2(-1, ImGui::GetContentRegionAvail().y - 36),
                ImGuiInputTextFlags_ReadOnly);
        } else {
            ImGui::TextUnformatted("Metadata not available.");
        }
        ImGui::Separator();
        if (ImGui::Button("Close", ImVec2(100, 0))) {
            g_gui.show_metadata_dialog = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

static void ShowBenchmark(void) {
    if (!g_gui.show_bench) return;
    ImGui::OpenPopup("Benchmark Results");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Benchmark Results", &g_gui.show_bench, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (g_gui.bench_done) {
            ImGui::TextUnformatted("Volume Benchmark Results");
            ImGui::Separator();
            ImGui::Text("Read:   %s MB/s", g_gui.bench_read_mbs);
            ImGui::Text("Write:  %s MB/s", g_gui.bench_write_mbs);
            ImGui::Text("Latency: %s ms", g_gui.bench_latency);
            ImGui::Separator();
            if (ImGui::Button("Run Again", ImVec2(100, 0))) {
                g_gui.bench_done = false;
                start_worker(W_BENCHFS, "512 10240");
            }
            ImGui::SameLine();
            if (ImGui::Button("Close", ImVec2(100, 0))) {
                g_gui.show_bench = false;
                ImGui::CloseCurrentPopup();
            }
        } else {
            ImGui::TextUnformatted("Benchmarking in progress...");
            ImGui::ProgressBar(g_gui.progress_frac, ImVec2(250, 0), "");
            if (ImGui::Button("Cancel", ImVec2(100, 0))) {
                g_gui.show_bench = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
}

static void ShowExportDialog(void) {
    if (!g_gui.show_export_dialog) return;
    ImGui::OpenPopup("Export Diagnostic");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Export Diagnostic", &g_gui.show_export_dialog, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (g_gui.export_result[0]) {
            ImGui::TextUnformatted(g_gui.export_result);
            ImGui::Separator();
            if (ImGui::Button("Close", ImVec2(100, 0))) {
                g_gui.show_export_dialog = false;
                ImGui::CloseCurrentPopup();
            }
        } else {
            ImGui::ProgressBar(g_gui.progress_frac, ImVec2(250, 0), "");
            ImGui::TextUnformatted("Exporting...");
        }
        ImGui::EndPopup();
    }
}


static void ShowRestoreWizard(void) {
    if (!g_gui.show_restore_wizard) return;
    ImGui::OpenPopup("Restore Volume");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Restore Volume", &g_gui.show_restore_wizard, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("How would you like to restore?");
        ImGui::Separator();
        if (ImGui::Button("From Superblock (auto-detect)", ImVec2(260, 30))) {
            g_gui.show_restore_wizard = false;
            start_worker(W_LOAD_SUPERBLOCK, NULL);
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::Button("From Saved Config", ImVec2(260, 30))) {
            g_gui.show_restore_wizard = false;
            start_worker(W_LOAD_CONFIG, NULL);
            ImGui::CloseCurrentPopup();
        }
        ImGui::Separator();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            g_gui.show_restore_wizard = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

static void ShowRebuildWizard(void) {
    if (!g_gui.show_rebuild_wizard) return;
    ImGui::OpenPopup("Rebuild RAID");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Rebuild RAID", &g_gui.show_rebuild_wizard, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Failed disk index:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        ImGui::InputInt("##fidx", &g_gui.rebuild_failed_idx);
        if (g_gui.rebuild_failed_idx < 0) g_gui.rebuild_failed_idx = 0;
        ImGui::Text("Replacement disk ID:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        ImGui::InputInt("##rid", &g_gui.rebuild_replacement_id);
        ImGui::Text("Pool size (MB):");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::InputInt("##pmb", &g_gui.rebuild_pool_mb);
        if (g_gui.rebuild_pool_mb < 1024) g_gui.rebuild_pool_mb = 1024;
        ImGui::Separator();
        if (ImGui::Button("Start Rebuild", ImVec2(140, 0))) {
            g_gui.show_rebuild_wizard = false;
            start_worker(W_REBUILD, NULL);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            g_gui.show_rebuild_wizard = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

static void ShowSimulationControls(void) {
    if (!g_gui.vol_info.mounted) {
        ImGui::TextDisabled("Mount a volume, then simulate disk faults here.");
        return;
    }
    static int sim_disk_idx = 0;
    ImGui::Text("Simulate Faults"); ImGui::Separator();
    ImGui::Text("Disk index:"); ImGui::SameLine();
    ImGui::SetNextItemWidth(60);
    ImGui::InputInt("##sim_disk", &sim_disk_idx, 1, 1);
    if (sim_disk_idx < 0) sim_disk_idx = 0;
    if (sim_disk_idx > 3) sim_disk_idx = 3;
    bool busy = btn_disabled();
    if (ImGui::Button("Simulate Fail", ImVec2(120, 26)) && !busy) {
        char p[8]; snprintf(p, sizeof(p), "%d", sim_disk_idx);
        start_worker(W_SIMULATE_FAIL, p);
    }
    ImGui::SameLine();
    if (ImGui::Button("Simulate Healthy", ImVec2(130, 26)) && !busy) {
        char p[8]; snprintf(p, sizeof(p), "%d", sim_disk_idx);
        start_worker(W_SIMULATE_HEALTHY, p);
    }
    ImGui::SameLine();
    if (ImGui::Button("Simulate Disconnect", ImVec2(140, 26)) && !busy) {
        char p[8]; snprintf(p, sizeof(p), "%d", sim_disk_idx);
        start_worker(W_SIMULATE_DISCONNECT, p);
    }
}

static void ShowCacheControls(void) {
    UI_VOLUME_INFO* vi = &g_gui.vol_info;
    ImGui::SeparatorText("> Cache");
    if (!vi->mounted) { ImGui::TextDisabled("Mount volume to configure cache"); return; }
    static int sz = 1024;
    if (vi->cache_enabled) sz = (int)vi->cache_mb;
    ImGui::SetNextItemWidth(100);
    ImGui::InputInt("##cachesz", &sz, 256);
    if (sz < 256) sz = 256;
    ImGui::SameLine();
    ImGui::TextDisabled("MB");
    ImGui::SameLine();
    bool busy = g_gui.worker_running != 0;
    if (vi->cache_enabled) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0.5f,0,0.8f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0,0.6f,0,0.9f));
        if (ImGui::Button(" ON ", ImVec2(40,22)) && !busy) { char p[] = "off"; start_worker(W_CACHE, p); }
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0,1,0,1));
        ImGui::Text("ON (%u MB)", vi->cache_mb);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::SmallButton("WT") && !busy) { char p[] = "wt"; start_worker(W_CACHE, p); }
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f,0.4f,0.4f,0.8f));
        if (ImGui::Button(" OFF ", ImVec2(40,22)) && !busy) { char p[32]; snprintf(p, 32, "%d", sz); start_worker(W_CACHE, p); }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextDisabled("Disabled");
    }
}

static void RenderMainUI(void) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::Begin("Main", NULL,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_MenuBar);

    ShowModeTabs();

    float toolbar_h = 32;
    float log_h = 100;
    float status_h = 26;
    float avail_h = ImGui::GetContentRegionAvail().y;
    float mid_h = avail_h - log_h - status_h - toolbar_h - 18;

    ImGui::BeginChild("##tb", ImVec2(0, toolbar_h), false);
    ShowToolbar();
    ImGui::EndChild();

    float left_w = ImGui::GetContentRegionAvail().x * 0.60f;
    ImGui::BeginChild("##left", ImVec2(left_w, mid_h), true);
    ShowDiskList();
    ShowDiskAllocation();
    ImGui::EndChild();
    ImGui::SameLine();

    float right_w = ImGui::GetContentRegionAvail().x;
    ImGui::BeginChild("##right", ImVec2(right_w, mid_h), true);
    ShowVolumeInfoContent();
    ShowCacheControls();
    if (g_gui.mode == MODE_DEVELOPER) {
        ShowSimulationControls();
    }
    ImGui::EndChild();

    ShowEventLogContent(log_h);
    ShowStatusBarContent(status_h);
    ShowWelcomeWizard();
    ShowRestoreWizard();
    ShowRebuildWizard();
    render_toasts();
    if (g_gui.show_settings) ShowSettings(&g_gui.show_settings);
    if (g_gui.show_about) ShowAbout(&g_gui.show_about);
    if (g_gui.show_confirm_destroy) ShowConfirmDestroy();
    if (g_gui.show_purge_confirm) ShowConfirmPurge();
    ShowExpandDialog();
    ShowMapDialog();
    ShowMetadataDialog();
    ShowBenchmark();
    ShowRawBenchResults();
    ShowExportDialog();
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Refresh", "F5", false, !g_gui.worker_running)) start_worker(W_REFRESH, NULL);
            ImGui::Separator();
            if (ImGui::MenuItem("Config Save", NULL, false, !g_gui.worker_running)) start_worker(W_CONFIG_SAVE, NULL);
            if (ImGui::MenuItem("Config Load", NULL, false, !g_gui.worker_running)) start_worker(W_CONFIG_LOAD, NULL);
            ImGui::Separator();
            if (ImGui::MenuItem("Export Diagnostic", NULL, false, !g_gui.worker_running)) start_worker(W_EXPORT, NULL);
            ImGui::Separator();
            if (ImGui::MenuItem("Cleanup", NULL, false, !g_gui.worker_running)) start_worker(W_CLEANUP, NULL);
            if (ImGui::MenuItem("Purge", NULL, false, !g_gui.worker_running)) g_gui.show_purge_confirm = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Settings", NULL, false)) g_gui.show_settings = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) PostQuitMessage(0);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Actions")) {
            if (ImGui::MenuItem("Scan", NULL, false, !g_gui.worker_running)) start_worker(W_SCAN, NULL);
            bool can_create = g_gui.state_value == 1 && g_gui.selected_count >= 2 && !g_gui.worker_running;
            if (ImGui::MenuItem("Create", NULL, false, can_create)) {
                char p[256] = {0}; int pos = 0;
                for (int i = 0; i < g_gui.selected_count && i < 4; i++) {
                    int mb = g_gui.pool_per_disk[i] > 0 ? g_gui.pool_per_disk[i] :
                             (g_gui.pool_size_mb ? g_gui.pool_size_mb : 51200);
                    pos += snprintf(p + pos, 255 - (size_t)pos, "%s%d:%d", pos ? " " : "",
                        g_gui.selected_disks[i], mb);
                }
                start_worker(W_CREATE, p);
            }
            if (ImGui::MenuItem("Mirror", NULL, false, can_create)) {
                char p[256] = {0}; int pos = 0;
                for (int i = 0; i < g_gui.selected_count && i < 4; i++) {
                    int mb = g_gui.pool_per_disk[i] > 0 ? g_gui.pool_per_disk[i] :
                             (g_gui.pool_size_mb ? g_gui.pool_size_mb : 51200);
                    pos += snprintf(p + pos, 255 - (size_t)pos, "%s%d:%d", pos ? " " : "",
                        g_gui.selected_disks[i], mb);
                }
                start_worker(W_MIRROR, p);
            }
            if (ImGui::MenuItem("Restore", NULL, false, !g_gui.worker_running)) g_gui.show_restore_wizard = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Quick Setup", NULL, false, !g_gui.worker_running)) start_worker(W_QUICK_SETUP, NULL);
            if (ImGui::MenuItem("Health Check", NULL, false, !g_gui.worker_running)) start_worker(W_CHECK, NULL);
            ImGui::Separator();
            bool can_mount = g_gui.state_value >= 2 && !g_gui.vol_info.mounted && !g_gui.worker_running;
            if (ImGui::MenuItem("Mount", NULL, false, can_mount)) {
                char m[2] = { g_gui.settings.mount_letter ? g_gui.settings.mount_letter : 'R', 0 };
                start_worker(W_MOUNT, m);
            }
            bool can_umount = g_gui.vol_info.mounted && !g_gui.worker_running;
            if (ImGui::MenuItem("Unmount", NULL, false, can_umount)) start_worker(W_UNMOUNT, NULL);
            bool can_destroy = g_gui.state_value >= 2 && !g_gui.worker_running;
            if (ImGui::MenuItem("Destroy", NULL, false, can_destroy)) g_gui.show_confirm_destroy = true;
            ImGui::Separator();
            bool can_bench = g_gui.vol_info.mounted && !g_gui.worker_running;
            if (ImGui::MenuItem("Benchmark", NULL, false, can_bench)) {
                g_gui.show_bench = true; g_gui.bench_done = false;
                g_gui.bench_read_mbs[0] = 0; g_gui.bench_write_mbs[0] = 0; g_gui.bench_latency[0] = 0;
                start_worker(W_BENCHFS, "512 10240");
            }
            if (ImGui::MenuItem("Rebuild", NULL, false, !g_gui.worker_running)) g_gui.show_rebuild_wizard = true;
            ImGui::Separator();
            bool can_bench_raw = g_gui.disk_summary.selected_count >= 2 && !g_gui.worker_running;
            if (ImGui::MenuItem("Raw Bench", NULL, false, can_bench_raw)) {
                g_gui.show_bench_raw = true; g_gui.bench_raw_done = false;
                g_gui.bench_raw_read[0] = 0; g_gui.bench_raw_write[0] = 0;
                start_worker(W_BENCH, "512");
            }
            bool can_test = g_gui.vol_info.mounted && !g_gui.worker_running;
            if (ImGui::MenuItem("I/O Test", NULL, false, can_test)) start_worker(W_TEST, NULL);
            if (ImGui::MenuItem("Random Stress", NULL, false, can_test)) start_worker(W_RANDOM, NULL);
            ImGui::Separator();
            bool can_expand = g_gui.vol_info.mounted && !g_gui.worker_running;
            if (ImGui::MenuItem("Expand", NULL, false, can_expand)) {
                memset(g_gui.expand_checks, 0, sizeof(g_gui.expand_checks));
                g_gui.show_expand_dialog = true;
            }
            if (ImGui::MenuItem("Map", NULL, false, can_expand)) start_worker(W_MAP, NULL);
            if (ImGui::MenuItem("Metadata", NULL, false, !g_gui.worker_running)) start_worker(W_METADATA, NULL);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("About")) g_gui.show_about = true;
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
    ImGui::End();
}

extern "C" int gui_run(void) {
    InitializeCriticalSection(&g_gui.log_lock);
    HINSTANCE hInst = GetModuleHandle(NULL);
    if (!SetupWindow(hInst)) {
        MessageBoxA(NULL, "Failed to create window", "Error", MB_ICONERROR);
        DeleteCriticalSection(&g_gui.log_lock);
        return 1;
    }
    if (!CreateDeviceD3D(g_gui.hwnd)) {
        CleanupDeviceD3D(); DestroyWindow(g_gui.hwnd);
        DeleteCriticalSection(&g_gui.log_lock);
        MessageBoxA(NULL, "Failed to create DirectX 11 device.\n\nYour GPU or driver may not support DirectX 11.", "Error", MB_ICONERROR);
        return 1;
    }
    ShowWindow(g_gui.hwnd, SW_SHOWNORMAL);
    UpdateWindow(g_gui.hwnd);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    config_defaults(&g_gui.settings);
    config_load(&g_gui.settings);
    g_gui.use_light_theme = true;
    ApplyTheme();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = NULL;
    ImGui_ImplWin32_Init(g_gui.hwnd);
    ImGui_ImplDX11_Init(g_gui.device, g_gui.ctx);
    raid_init();
    refresh_ui_model();
    event_bus_subscribe(EVENT_DISK_FOUND, event_cb, NULL);
    event_bus_subscribe(EVENT_VOLUME_CREATED, event_cb, NULL);
    event_bus_subscribe(EVENT_VOLUME_DESTROYED, event_cb, NULL);
    event_bus_subscribe(EVENT_MOUNT, event_cb, NULL);
    event_bus_subscribe(EVENT_UNMOUNT, event_cb, NULL);
    event_bus_subscribe(EVENT_ERROR, event_cb, NULL);
    event_bus_subscribe(EVENT_METADATA_UPDATED, event_cb, NULL);
    event_bus_subscribe(EVENT_CACHE_CHANGED, event_cb, NULL);
    g_gui.pool_size_mb = (int)g_gui.settings.pool_mb ? (int)g_gui.settings.pool_mb : 51200;

    gui_log("RAIDTEST " APP_VERSION " - GUI Edition started");
    snprintf(g_gui.status, sizeof(g_gui.status), "Ready - %u disk(s) detected", g_gui.disk_summary.count);
    if (g_gui.settings.first_run) g_gui.show_welcome = true;
    MSG msg = {0};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessage(&msg);
            continue;
        }
        check_worker_done();
        if (InterlockedCompareExchange(&g_gui.refresh_pending, 0, 1) == 1)
            refresh_ui_model();
        double now = ImGui::GetTime();
        if (now - g_gui.last_refresh > 1.0) {
            refresh_ui_model();
            g_gui.last_refresh = now;
        }
        profiler_update_rates(g_gui.vol_info.bytes_read, g_gui.vol_info.bytes_written);
        if (g_gui.progress_frac > 0.0f) {
            double elapsed = timer_sec() - g_gui.progress_start_time;
            g_gui.progress_eta = (elapsed / g_gui.progress_frac) * (1.0 - g_gui.progress_frac);
        }
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        RenderMainUI();
        ImGui::Render();
        g_gui.ctx->OMSetRenderTargets(1, &g_gui.rtv, NULL);
        ImVec4 clear = g_gui.use_light_theme ? ImVec4(0.94f,0.94f,0.96f,1.00f) : ImVec4(0.08f,0.08f,0.10f,1.00f);
        g_gui.ctx->ClearRenderTargetView(g_gui.rtv, (float*)&clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_gui.swapchain->Present(1, 0);
    }
    raid_cleanup();
    event_bus_unsubscribe(EVENT_DISK_FOUND, event_cb);
    event_bus_unsubscribe(EVENT_VOLUME_CREATED, event_cb);
    event_bus_unsubscribe(EVENT_VOLUME_DESTROYED, event_cb);
    event_bus_unsubscribe(EVENT_MOUNT, event_cb);
    event_bus_unsubscribe(EVENT_UNMOUNT, event_cb);
    event_bus_unsubscribe(EVENT_ERROR, event_cb);
    event_bus_unsubscribe(EVENT_METADATA_UPDATED, event_cb);
    event_bus_unsubscribe(EVENT_CACHE_CHANGED, event_cb);
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(g_gui.hwnd);
    DeleteCriticalSection(&g_gui.log_lock);
    return 0;
}
