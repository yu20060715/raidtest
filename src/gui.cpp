extern "C" {
#include "gui.h"
#include "raid_service.h"
#include "ui_model.h"
#include "device_manager.h"
#include "planner_engine.h"
#include "event_bus.h"
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

#define MAX_LOG_LINES 500
#define MAX_LOG_LINE_LEN 256
#define APP_VERSION "v1.0 RC1"

enum WorkerAction {
    W_NONE = 0,
    W_SCAN,
    W_CREATE,
    W_MIRROR,
    W_MOUNT,
    W_UNMOUNT,
    W_DESTROY,
    W_REFRESH,
    W_BENCH,
    W_EXPORT,
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
    float                   progress_frac;
    volatile LONG           worker_pct;

    UI_DISK_SUMMARY         disk_summary;
    UI_VOLUME_INFO          vol_info;
    UI_HEALTH_SUMMARY       health;
    double                  last_refresh;
    int                     selected_disks[8];
    int                     selected_count;
    int                     disk_checked[64];

    char                    mount_letter;
    int                     cache_mb;

    bool                    show_about;
    bool                    show_confirm_destroy;
    bool                    show_confirm_purge;
    bool                    show_confirm_exit;
    bool                    show_bench;
    bool                    show_export_dialog;
    char                    export_result[512];

    char                    bench_read_mbs[32];
    char                    bench_write_mbs[32];
    char                    bench_latency[32];
    bool                    bench_done;

    char                    status[128];
    int                     state_value;

    HANDLE                  worker_handle;
    unsigned                worker_thread_id;
} g_gui = {0};

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
}

static void refresh_ui_model(void) {
    ui_get_disk_summary(&g_gui.disk_summary);
    ui_get_volume_info(&g_gui.vol_info);
    ui_get_health_summary(&g_gui.health);
    if (g_gui.vol_info.mounted) g_gui.state_value = 3;
    else if (g_gui.vol_info.exists) g_gui.state_value = 2;
    else if (g_gui.disk_summary.count > 0) g_gui.state_value = 1;
    else g_gui.state_value = 0;
}

static unsigned int __stdcall worker_thread(void* arg) {
    (void)arg;
    WorkerAction act = g_gui.worker_action;
    char* params = g_gui.worker_params;
    char* result = g_gui.worker_result;
    result[0] = 0;

    g_gui.progress_frac = 0.0f;
    g_gui.worker_pct = 0;

    switch (act) {
    case W_SCAN: {
        strncpy(g_gui.progress_text, "Scanning disks...", 63);
        RC rc = raid_scan();
        g_gui.progress_frac = 1.0f;
        snprintf(result, 511, "Scan: %s (%u disks found)", rc == RC_OK ? "OK" : "FAILED", g_gui.disk_summary.count);
        refresh_ui_model();
        break;
    }
    case W_CREATE: {
        char* args[16] = {0};
        int argc = 0;
        char buf[256]; snprintf(buf, 256, "%s", params);
        char* tok = strtok(buf, " ");
        while (tok && argc < 16) { args[argc++] = tok; tok = strtok(NULL, " "); }
        strncpy(g_gui.progress_text, "Initializing pools...", 63);
        g_gui.progress_frac = 0.3f;
        if (argc > 0) raid_init_pools(argc, args);
        strncpy(g_gui.progress_text, "Creating volume...", 63);
        g_gui.progress_frac = 0.7f;
        RC rc = raid_create();
        g_gui.progress_frac = 1.0f;
        snprintf(result, 511, "Create: %s %s",
            rc == RC_OK ? "OK" : "FAILED",
            rc == RC_OK ? "? Volume is ready for mount" : "");
        refresh_ui_model();
        break;
    }
    case W_MIRROR: {
        strncpy(g_gui.progress_text, "Creating mirror...", 63);
        RC rc = raid_mirror();
        g_gui.progress_frac = 1.0f;
        snprintf(result, 511, "Mirror: %s", rc == RC_OK ? "OK" : "FAILED");
        refresh_ui_model();
        break;
    }
    case W_MOUNT: {
        strncpy(g_gui.progress_text, "Mounting volume...", 63);
        g_gui.progress_frac = 0.5f;
        RC rc = raid_mount(params[0] ? params[0] : 'G');
        g_gui.progress_frac = 1.0f;
        snprintf(result, 511, "Mount: %s %s at %c:",
            rc == RC_OK ? "OK" : "FAILED",
            rc == RC_OK ? "? Volume mounted" : "",
            params[0] ? params[0] : 'G');
        refresh_ui_model();
        break;
    }
    case W_UNMOUNT: {
        strncpy(g_gui.progress_text, "Unmounting...", 63);
        g_gui.progress_frac = 0.5f;
        RC rc = raid_unmount();
        g_gui.progress_frac = 1.0f;
        snprintf(result, 511, "Unmount: %s", rc == RC_OK ? "OK ? Volume unmounted" : "FAILED");
        refresh_ui_model();
        break;
    }
    case W_DESTROY: {
        strncpy(g_gui.progress_text, "Destroying volume...", 63);
        g_gui.progress_frac = 0.5f;
        RC rc = raid_destroy();
        g_gui.progress_frac = 1.0f;
        snprintf(result, 511, "Destroy: %s", rc == RC_OK ? "OK ? Volume destroyed" : "FAILED");
        refresh_ui_model();
        break;
    }
    case W_REFRESH: {
        refresh_ui_model();
        snprintf(result, 511, "Refreshed ? %u disk(s), state=%s",
            g_gui.disk_summary.count, ui_get_state_str());
        break;
    }
    case W_BENCH: {
        strncpy(g_gui.progress_text, "Benchmarking...", 63);
        g_gui.progress_frac = 0.0f;
        char drv[4] = "G:\0";
        drv[0] = params[0] ? params[0] : 'G';
        char path[MAX_PATH];
        snprintf(path, MAX_PATH, "%s\\raidtest_bench.tmp", drv);

        HANDLE hf = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
            CREATE_ALWAYS, FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL);
        if (hf == INVALID_HANDLE_VALUE) {
            snprintf(result, 511, "Bench FAILED ? cannot create test file on %s", drv);
            g_gui.progress_frac = 1.0f;
            break;
        }

        uint64_t total = 256ULL * 1024 * 1024;
        uint32_t blk = 1024 * 1024;
        void* buf = _aligned_malloc(blk, 4096);
        if (!buf) { CloseHandle(hf); snprintf(result, 511, "Bench FAILED ? alloc error"); g_gui.progress_frac = 1.0f; break; }
        memset(buf, 0xAA, blk);

        LARGE_INTEGER freq, start, end;
        QueryPerformanceFrequency(&freq);

        // Write
        QueryPerformanceCounter(&start);
        for (uint64_t off = 0; off < total; off += blk) {
            DWORD wrote;
            WriteFile(hf, buf, blk, &wrote, NULL);
            g_gui.progress_frac = (float)(off + blk) / (float)(total * 2);
        }
        QueryPerformanceCounter(&end);
        double write_sec = (double)(end.QuadPart - start.QuadPart) / (double)freq.QuadPart;
        double write_mbs = (total / (1024.0 * 1024.0)) / write_sec;

        // Read
        SetFilePointer(hf, 0, NULL, FILE_BEGIN);
        QueryPerformanceCounter(&start);
        for (uint64_t off = 0; off < total; off += blk) {
            DWORD got;
            ReadFile(hf, buf, blk, &got, NULL);
            g_gui.progress_frac = 0.5f + (float)(off + blk) / (float)(total * 2);
        }
        QueryPerformanceCounter(&end);
        double read_sec = (double)(end.QuadPart - start.QuadPart) / (double)freq.QuadPart;
        double read_mbs = (total / (1024.0 * 1024.0)) / read_sec;
        double avg_lat = ((write_sec + read_sec) * 1000.0) / (total / blk * 2);

        _aligned_free(buf);
        CloseHandle(hf);
        DeleteFileA(path);

        snprintf(g_gui.bench_read_mbs, 31, "%.1f", read_mbs);
        snprintf(g_gui.bench_write_mbs, 31, "%.1f", write_mbs);
        snprintf(g_gui.bench_latency, 31, "%.2f", avg_lat);
        g_gui.bench_done = true;
        g_gui.progress_frac = 1.0f;
        snprintf(result, 511, "Bench: R=%.1f W=%.1f MB/s  Lat=%.2f ms", read_mbs, write_mbs, avg_lat);
        break;
    }
    case W_EXPORT: {
        strncpy(g_gui.progress_text, "Exporting diagnostics...", 63);
        g_gui.progress_frac = 0.0f;

        char tmpdir[MAX_PATH];
        GetTempPathA(MAX_PATH, tmpdir);
        char dir[MAX_PATH];
        snprintf(dir, MAX_PATH, "%sraidtest_export", tmpdir);
        CreateDirectoryA(dir, NULL);

        // metadata
        char mpath[MAX_PATH];
        snprintf(mpath, MAX_PATH, "%s\\metadata.txt", dir);
        FILE* f = fopen(mpath, "w");
        if (f) {
            fprintf(f, "RAIDTEST %s ? Export Diagnostic\n", APP_VERSION);
            fprintf(f, "Date: %s\n", __DATE__);
            fprintf(f, "Time: %s\n\n", __TIME__);
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
            fprintf(f, "Bytes Written: %llu\n", vi->bytes_written);
            fprintf(f, "Bytes Read: %llu\n\n", vi->bytes_read);

            fprintf(f, "=== Disk Summary ===\n");
            UI_DISK_SUMMARY* ds = &g_gui.disk_summary;
            fprintf(f, "Count: %u\n", ds->count);
            fprintf(f, "Selected: %u\n", ds->selected_count);
            fprintf(f, "Total Capacity: %llu MB\n", ds->total_capacity_mb);

            fprintf(f, "\n=== Health ===\n");
            UI_HEALTH_SUMMARY* h = &g_gui.health;
            fprintf(f, "Healthy: %u/%u\n", h->healthy_count, h->total_count);
            fprintf(f, "Degraded: %s\n", h->degraded ? "Yes" : "No");
            fprintf(f, "State: %s\n", ui_get_state_str());

            uint32_t dc = device_get_count();
            for (uint32_t i = 0; i < dc; i++) {
                DISK_INFO* d = device_get(i);
                if (!d) continue;
                char modelA[128] = {0}; wcstombs(modelA, d->model, 127);
                fprintf(f, "\nDisk %u: %s [%s]\n", i, modelA, disk_type_str(d->type));
                fprintf(f, "  Serial: %s\n", d->serial_number);
                fprintf(f, "  Size: %.0f GB\n", (double)d->total_bytes / 1e9);
                fprintf(f, "  Speed: %u MB/s\n", d->benchmarked ? d->bench_write_mbs : d->write_speed_mbs);
                fprintf(f, "  Healthy: %s\n", d->healthy ? "Yes" : "No");
            }
            fclose(f);
        }

        // event log
        char epath[MAX_PATH];
        snprintf(epath, MAX_PATH, "%s\\event.log", dir);
        f = fopen(epath, "w");
        if (f) {
            EnterCriticalSection(&g_gui.log_lock);
            int n = g_gui.log_count;
            int start = n < MAX_LOG_LINES ? 0 : g_gui.log_tail;
            int end = start + n;
            for (int i = start; i < end; i++)
                fprintf(f, "%s\n", g_gui.log_lines[i % MAX_LOG_LINES]);
            LeaveCriticalSection(&g_gui.log_lock);
            fclose(f);
        }

        // system info
        char spath[MAX_PATH];
        snprintf(spath, MAX_PATH, "%s\\system.txt", dir);
        f = fopen(spath, "w");
        if (f) {
            SYSTEM_INFO si; GetSystemInfo(&si);
            MEMORYSTATUSEX ms; ms.dwLength = sizeof(ms); GlobalMemoryStatusEx(&ms);
            fprintf(f, "=== System Info ===\n");
            fprintf(f, "OS: Windows (build %lu)\n", GetVersion());
            fprintf(f, "Processors: %lu\n", si.dwNumberOfProcessors);
            fprintf(f, "RAM: %llu MB total, %llu MB free\n",
                ms.ullTotalPhys / 1048576, ms.ullAvailPhys / 1048576);
            fprintf(f, "Page Size: %lu bytes\n", si.dwPageSize);
            fclose(f);
        }

        g_gui.progress_frac = 1.0f;
        snprintf(result, 511, "Export saved to: %s", dir);
        snprintf(g_gui.export_result, 511, "Diagnostics exported to:\n%s\n\nFiles:\n  metadata.txt\n  event.log\n  system.txt", dir);
        break;
    }
    default:
        snprintf(result, 511, "Unknown action");
    }
    g_gui.progress_frac = 1.0f;
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
    if (params) strncpy(g_gui.worker_params, params, 255);
    else g_gui.worker_params[0] = 0;
    g_gui.worker_handle = (HANDLE)_beginthreadex(NULL, 0, worker_thread, NULL, 0, &g_gui.worker_thread_id);
}

static void check_worker_done(void) {
    if (InterlockedCompareExchange(&g_gui.worker_done, 1, 1) == 1) {
        bool failed = (strstr(g_gui.worker_result, "FAILED") != NULL);
        gui_log(g_gui.worker_result);
        strncpy(g_gui.status, g_gui.worker_result, sizeof(g_gui.status) - 1);
        g_gui.status[sizeof(g_gui.status) - 1] = 0;
        if (failed) {
            MessageBoxA(g_gui.hwnd, g_gui.worker_result, "Operation Failed",
                        MB_ICONERROR | MB_OK);
        }
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

static void SetupDarkTheme(void) {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0.0f;
    s.ChildRounding = 0.0f;
    s.FrameRounding = 3.0f;
    s.PopupRounding = 3.0f;
    s.ScrollbarRounding = 3.0f;
    s.GrabRounding = 3.0f;
    s.TabRounding = 3.0f;
    s.WindowBorderSize = 0.0f;
    s.ChildBorderSize = 0.0f;
    s.FrameBorderSize = 0.0f;
    s.PopupBorderSize = 1.0f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]          = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    c[ImGuiCol_ChildBg]           = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    c[ImGuiCol_PopupBg]           = ImVec4(0.10f, 0.10f, 0.12f, 0.94f);
    c[ImGuiCol_FrameBg]           = ImVec4(0.15f, 0.15f, 0.18f, 1.00f);
    c[ImGuiCol_FrameBgHovered]    = ImVec4(0.20f, 0.20f, 0.25f, 1.00f);
    c[ImGuiCol_FrameBgActive]     = ImVec4(0.25f, 0.25f, 0.30f, 1.00f);
    c[ImGuiCol_TitleBg]           = ImVec4(0.06f, 0.06f, 0.08f, 1.00f);
    c[ImGuiCol_TitleBgActive]     = ImVec4(0.10f, 0.10f, 0.14f, 1.00f);
    c[ImGuiCol_MenuBarBg]         = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    c[ImGuiCol_Header]            = ImVec4(0.20f, 0.20f, 0.28f, 1.00f);
    c[ImGuiCol_HeaderHovered]     = ImVec4(0.28f, 0.28f, 0.38f, 1.00f);
    c[ImGuiCol_HeaderActive]      = ImVec4(0.35f, 0.35f, 0.45f, 1.00f);
    c[ImGuiCol_Button]            = ImVec4(0.18f, 0.18f, 0.22f, 1.00f);
    c[ImGuiCol_ButtonHovered]     = ImVec4(0.28f, 0.30f, 0.40f, 1.00f);
    c[ImGuiCol_ButtonActive]      = ImVec4(0.35f, 0.38f, 0.50f, 1.00f);
    c[ImGuiCol_Tab]               = ImVec4(0.12f, 0.12f, 0.16f, 1.00f);
    c[ImGuiCol_TabHovered]        = ImVec4(0.22f, 0.24f, 0.34f, 1.00f);
    c[ImGuiCol_TabSelected]       = ImVec4(0.18f, 0.22f, 0.30f, 1.00f);
    c[ImGuiCol_ScrollbarGrab]     = ImVec4(0.30f, 0.30f, 0.40f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.40f, 0.50f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.50f, 0.50f, 0.60f, 1.00f);
    c[ImGuiCol_CheckMark]         = ImVec4(0.30f, 0.70f, 0.40f, 1.00f);
    c[ImGuiCol_SliderGrab]        = ImVec4(0.30f, 0.50f, 0.80f, 1.00f);
    c[ImGuiCol_SliderGrabActive]  = ImVec4(0.40f, 0.60f, 0.90f, 1.00f);
    c[ImGuiCol_PlotHistogram]     = ImVec4(0.35f, 0.60f, 0.90f, 1.00f);
    c[ImGuiCol_TextSelectedBg]    = ImVec4(0.25f, 0.40f, 0.65f, 1.00f);
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
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
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
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
    switch (msg) {
    case WM_SIZE:
        if (g_gui.device && wParam != SIZE_MINIMIZED) {
            g_gui.ctx->OMSetRenderTargets(0, NULL, NULL);
            g_gui.rtv->Release(); g_gui.rtv = NULL;
            g_gui.swapchain->ResizeBuffers(0,
                (UINT)LOWORD(lParam), (UINT)HIWORD(lParam),
                DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
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
        L"RAIDTEST " APP_VERSION L" ? GUI Edition",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        r.right - r.left, r.bottom - r.top, NULL, NULL, hInst, NULL);
    return g_gui.hwnd != NULL;
}

static bool btn_disabled(void) {
    return g_gui.worker_running != 0;
}

static void ShowToolbar(void) {
    ImGui::Begin("Toolbar", NULL,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
    {
        float w = ImGui::GetContentRegionAvail().x;
        float bw = w * 0.075f;
        bool busy = btn_disabled();

        bool busy_grp = busy;
        bool no_vol_grp = g_gui.state_value == 1 && !busy;
        bool has_sel_grp = g_gui.selected_count >= 2 && !busy;
        bool mountable = g_gui.state_value >= 2 && !g_gui.vol_info.mounted && !busy;
        bool unmountable = g_gui.vol_info.mounted && !busy;
        bool destroyable = g_gui.state_value >= 2 && !busy;
        bool benchable = g_gui.vol_info.mounted && !busy;

        if (busy_grp) ImGui::BeginDisabled();
        if (ImGui::Button("Scan", ImVec2(bw, 28))) start_worker(W_SCAN, NULL);
        if (busy_grp) ImGui::EndDisabled();
        ImGui::SameLine();

        bool create_ok = no_vol_grp && has_sel_grp;
        if (!create_ok) ImGui::BeginDisabled();
        if (ImGui::Button("Create", ImVec2(bw, 28))) {
            char p[256] = {0}; int pos = 0;
            for (int i = 0; i < g_gui.selected_count && i < 4; i++)
                pos += snprintf(p + pos, 255 - (size_t)pos, "%s%d:%d",
                    pos ? " " : "", g_gui.selected_disks[i],
                    g_gui.cache_mb ? g_gui.cache_mb : 1024);
            start_worker(W_CREATE, p);
        }
        if (!create_ok) ImGui::EndDisabled();
        ImGui::SameLine();

        bool mirror_ok = no_vol_grp && has_sel_grp;
        if (!mirror_ok) ImGui::BeginDisabled();
        if (ImGui::Button("Mirror", ImVec2(bw, 28))) start_worker(W_MIRROR, NULL);
        if (!mirror_ok) ImGui::EndDisabled();
        ImGui::SameLine();

        if (!mountable) ImGui::BeginDisabled();
        if (ImGui::Button("Mount", ImVec2(bw, 28))) {
            char m[2] = { g_gui.mount_letter ? g_gui.mount_letter : 'G', 0 };
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
        ImGui::Separator();
        ImGui::SameLine();
        if (!benchable) ImGui::BeginDisabled();
        if (ImGui::Button("Bench", ImVec2(bw, 28))) {
            g_gui.show_bench = true;
            g_gui.bench_done = false;
            g_gui.bench_read_mbs[0] = 0;
            g_gui.bench_write_mbs[0] = 0;
            g_gui.bench_latency[0] = 0;
            char m[2] = { g_gui.mount_letter ? g_gui.mount_letter : 'G', 0 };
            start_worker(W_BENCH, m);
        }
        if (!benchable) ImGui::EndDisabled();
        ImGui::SameLine();
        if (busy) ImGui::BeginDisabled();
        if (ImGui::Button("Export", ImVec2(bw, 28))) {
            start_worker(W_EXPORT, NULL);
            g_gui.show_export_dialog = true;
        }
        if (busy) ImGui::EndDisabled();
        ImGui::SameLine();
        if (busy) ImGui::BeginDisabled();
        if (ImGui::Button("Refresh", ImVec2(bw, 28))) start_worker(W_REFRESH, NULL);
        if (busy) ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::Separator();
        ImGui::SameLine();
        ImGui::TextUnformatted("Drv:");
        ImGui::SameLine();
        char ml[2] = { g_gui.mount_letter ? g_gui.mount_letter : 'G', 0 };
        ImGui::SetNextItemWidth(28);
        if (ImGui::InputText("##ml", ml, 2, ImGuiInputTextFlags_CharsUppercase))
            g_gui.mount_letter = ml[0];
        ImGui::SameLine();
        ImGui::TextUnformatted("Cache:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(55);
        ImGui::InputInt("##cache", &g_gui.cache_mb, 0);
        if (g_gui.cache_mb < 256) g_gui.cache_mb = 256;
        ImGui::SameLine();
        ImGui::TextUnformatted("MB");
    }
    ImGui::End();
}

static void ShowDiskList(void) {
    ImGui::Begin("Physical Disks", NULL, ImGuiWindowFlags_NoCollapse);
    {
        uint32_t count = device_get_count();
        float txt = (float)ImGui::GetContentRegionAvail().y - 4;
        ImGui::Text("%u disk(s) | %u selected | Total: %.1f GB | State: %s",
            g_gui.disk_summary.count, g_gui.disk_summary.selected_count,
            g_gui.disk_summary.total_capacity_mb / 1024.0, ui_get_state_str());

        static ImGuiTableFlags flags = ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable;
        if (ImGui::BeginTable("##disks", 10, flags, ImVec2(0, txt)))
        {
            ImGui::TableSetupColumn("Model",   ImGuiTableColumnFlags_WidthFixed, 170);
            ImGui::TableSetupColumn("ID",      ImGuiTableColumnFlags_WidthFixed, 25);
            ImGui::TableSetupColumn("Serial",  ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableSetupColumn("Type",    ImGuiTableColumnFlags_WidthFixed, 70);
            ImGui::TableSetupColumn("Bus",     ImGuiTableColumnFlags_WidthFixed, 50);
            ImGui::TableSetupColumn("Size",    ImGuiTableColumnFlags_WidthFixed, 65);
            ImGui::TableSetupColumn("Speed",   ImGuiTableColumnFlags_WidthFixed, 45);
            ImGui::TableSetupColumn("Status",  ImGuiTableColumnFlags_WidthFixed, 55);
            ImGui::TableSetupColumn("RAID",    ImGuiTableColumnFlags_WidthFixed, 40);
            ImGui::TableSetupColumn("Use",     ImGuiTableColumnFlags_WidthFixed, 32);
            ImGui::TableHeadersRow();

            g_gui.selected_count = 0;
            for (uint32_t i = 0; i < count && i < 64; i++) {
                DISK_INFO* d = device_get(i);
                if (!d) continue;
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                char modelA[128] = {0};
                wcstombs(modelA, d->model, 127);
                ImGui::TextUnformatted(modelA);

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%u", i);

                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(d->serial_number[0] ? d->serial_number : "-");

                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(disk_type_str(d->type));

                ImGui::TableSetColumnIndex(4);
                const char* bus = "SATA";
                if (d->type == DISK_TYPE_NVME_SSD) bus = "NVMe";
                else if (d->type == DISK_TYPE_RAMDISK) bus = "RAM";
                else if (d->type == DISK_TYPE_FILEBACKED) bus = "FILE";
                ImGui::TextUnformatted(bus);

                ImGui::TableSetColumnIndex(5);
                ImGui::Text("%.0f GB", (double)d->total_bytes / 1e9);

                ImGui::TableSetColumnIndex(6);
                ImGui::Text("%u", d->benchmarked ? d->bench_write_mbs : d->write_speed_mbs);

                ImGui::TableSetColumnIndex(7);
                if (d->healthy)
                    ImGui::TextColored(ImVec4(0,1,0,1), "Online");
                else
                    ImGui::TextColored(ImVec4(1,0,0,1), "Offline");

                ImGui::TableSetColumnIndex(8);
                if (d->selected)
                    ImGui::TextColored(ImVec4(0,1,1,1), "Yes");
                else
                    ImGui::TextDisabled("-");

                ImGui::TableSetColumnIndex(9);
                char cbid[16]; snprintf(cbid, 16, "##ck%u", i);
                bool checked = !!g_gui.disk_checked[i];
                if (ImGui::Checkbox(cbid, &checked)) {
                    g_gui.disk_checked[i] = checked ? 1 : 0;
                    if (!g_gui.worker_running) {
                        uint32_t idx = (uint32_t)i;
                        device_select(&idx, 1);
                        refresh_ui_model();
                    }
                }
                if (checked && g_gui.selected_count < 8)
                    g_gui.selected_disks[g_gui.selected_count++] = (int)i;
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

static void ShowPlanner(void) {
    ImGui::Begin("Planner", NULL, ImGuiWindowFlags_NoCollapse);
    {
        if (g_gui.selected_count >= 2) {
            PLANNER_DISK pdisks[8] = {0};
            for (int i = 0; i < g_gui.selected_count && i < 8; i++) {
                DISK_INFO* d = device_get(g_gui.selected_disks[i]);
                if (!d) continue;
                pdisks[i].disk_index = g_gui.selected_disks[i];
                pdisks[i].capacity_bytes = d->total_bytes;
                pdisks[i].speed_mbs = d->benchmarked ? d->bench_write_mbs : d->write_speed_mbs;
                pdisks[i].selected = true;
                char* sn = pdisks[i].serial;
                snprintf(sn, MAX_SERIAL_LEN, "%s", d->serial_number);
            }
            PLANNER_RESULT pr = {0};
            planner_calculate(pdisks, (uint32_t)g_gui.selected_count, &pr);

            ImGui::Columns(2, NULL, false);
            ImGui::Text("Selected disks:"); ImGui::NextColumn();
            ImGui::Text("%u", pr.selected_count); ImGui::NextColumn();
            ImGui::Text("Total raw:"); ImGui::NextColumn();
            ImGui::Text("%llu MB (%llu GB)", pr.total_raw_mb, pr.total_raw_mb / 1024); ImGui::NextColumn();
            ImGui::Text("RAID0 capacity:"); ImGui::NextColumn();
            ImGui::Text("%llu MB (%llu GB)", pr.raid0_capacity_mb, pr.raid0_capacity_mb / 1024); ImGui::NextColumn();
            ImGui::Text("RAID1 capacity:"); ImGui::NextColumn();
            if (pr.raid1_capacity_mb > 0)
                ImGui::Text("%llu MB (%llu GB)", pr.raid1_capacity_mb, pr.raid1_capacity_mb / 1024);
            else
                ImGui::TextDisabled("N/A (needs 2n)");
            ImGui::NextColumn();
            ImGui::Text("RAID10 capacity:"); ImGui::NextColumn();
            if (pr.raid10_capacity_mb > 0)
                ImGui::Text("%llu MB (%llu GB)", pr.raid10_capacity_mb, pr.raid10_capacity_mb / 1024);
            else
                ImGui::TextDisabled("N/A (needs 2n)");
            ImGui::NextColumn();
            ImGui::Text("Efficiency R0:"); ImGui::NextColumn();
            ImGui::Text("%.1f%%", pr.efficiency_raid0 * 100.0); ImGui::NextColumn();
            ImGui::Text("Efficiency R1:"); ImGui::NextColumn();
            ImGui::Text("%.1f%%", pr.efficiency_raid1 * 100.0);
            ImGui::Columns(1);
        } else {
            ImGui::TextColored(ImVec4(1,1,0,1), "Select 2+ disks (check boxes) to see planner");
        }
    }
    ImGui::End();
}

static void ShowVolumeInfo(void) {
    ImGui::Begin("Volume Info", NULL, ImGuiWindowFlags_NoCollapse);
    {
        UI_VOLUME_INFO* vi = &g_gui.vol_info;
        if (vi->exists) {
            ImGui::Columns(2, NULL, false);
            ImGui::Text("State:"); ImGui::NextColumn();
            ImGui::TextUnformatted(ui_get_state_str()); ImGui::NextColumn();
            ImGui::Text("RAID Level:"); ImGui::NextColumn();
            ImGui::Text("RAID%u", vi->raid_level); ImGui::NextColumn();
            ImGui::Text("Disks:"); ImGui::NextColumn();
            ImGui::Text("%u", vi->disk_count); ImGui::NextColumn();
            ImGui::Text("Capacity:"); ImGui::NextColumn();
            ImGui::Text("%llu GB (%llu bytes)", vi->virtual_capacity_bytes / 1000000000ULL,
                vi->virtual_capacity_bytes); ImGui::NextColumn();
            ImGui::Text("Mounted:"); ImGui::NextColumn();
            if (vi->mounted)
                ImGui::TextColored(ImVec4(0,1,0,1), "Yes");
            else
                ImGui::TextUnformatted("No");
            ImGui::NextColumn();
            ImGui::Text("Cache:"); ImGui::NextColumn();
            ImGui::Text("%s (%u MB)", vi->cache_enabled ? "ON" : "OFF", vi->cache_mb); ImGui::NextColumn();
            ImGui::Text("Written:"); ImGui::NextColumn();
            char wb[32]; snprintf(wb, 32, "%llu MB", vi->bytes_written / 1000000ULL);
            ImGui::TextUnformatted(wb); ImGui::NextColumn();
            ImGui::Text("Read:"); ImGui::NextColumn();
            char rb[32]; snprintf(rb, 32, "%llu MB", vi->bytes_read / 1000000ULL);
            ImGui::TextUnformatted(rb); ImGui::NextColumn();
            ImGui::Text("UUID:"); ImGui::NextColumn();
            ImGui::TextUnformatted(vi->uuid_str); ImGui::NextColumn();
            ImGui::Text("Generation:"); ImGui::NextColumn();
            ImGui::Text("%llu", vi->generation); ImGui::NextColumn();
            if (vi->uptime_seconds > 0) {
                ImGui::Text("Uptime:"); ImGui::NextColumn();
                int h = (int)(vi->uptime_seconds / 3600);
                int m = (int)((vi->uptime_seconds - h * 3600) / 60);
                int s = (int)(vi->uptime_seconds - h * 3600 - m * 60);
                ImGui::Text("%02d:%02d:%02d", h, m, s); ImGui::NextColumn();
            }
            ImGui::Columns(1);

            ImGui::Separator();
            UI_HEALTH_SUMMARY* h = &g_gui.health;
            ImGui::Text("Health:");
            ImGui::SameLine();
            if (h->total_count > 0 && !h->degraded)
                ImGui::TextColored(ImVec4(0,1,0,1), "%u/%u healthy", h->healthy_count, h->total_count);
            else if (h->degraded)
                ImGui::TextColored(ImVec4(1,1,0,1), "%u/%u ? DEGRADED", h->healthy_count, h->total_count);
            else
                ImGui::TextUnformatted("N/A");
        } else {
            ImGui::TextDisabled("No volume ? Scan + Create first");
        }
    }
    ImGui::End();
}

static void ShowEventLog(void) {
    ImGui::Begin("Event Log", NULL, ImGuiWindowFlags_NoCollapse);
    {
        ImGui::BeginChild("##logsc", ImVec2(0, ImGui::GetContentRegionAvail().y), false);
        EnterCriticalSection(&g_gui.log_lock);
        int n = g_gui.log_count;
        int start = n < MAX_LOG_LINES ? 0 : g_gui.log_tail;
        int end = start + n;
        for (int i = start; i < end; i++) {
            int idx = i % MAX_LOG_LINES;
            if (g_gui.log_lines[idx][0] == 0) continue;
            ImVec4 col(1,1,1,1);
            if (strstr(g_gui.log_lines[idx], "ERROR") || strstr(g_gui.log_lines[idx], "FAILED"))
                col = ImVec4(1,0.3f,0.3f,1);
            else if (strstr(g_gui.log_lines[idx], "WARN")) col = ImVec4(1,1,0,1);
            else if (strstr(g_gui.log_lines[idx], "OK")) col = ImVec4(0.3f,1,0.3f,1);
            else if (strstr(g_gui.log_lines[idx], "INFO")) col = ImVec4(0.6f,0.8f,1,1);
            else if (strstr(g_gui.log_lines[idx], "Bench")) col = ImVec4(0.3f,0.8f,1,1);
            ImGui::TextColored(col, "%s", g_gui.log_lines[idx]);
        }
        LeaveCriticalSection(&g_gui.log_lock);
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
    }
    ImGui::End();
}

static void ShowStatusBar(void) {
    ImGui::Begin("StatusBar", NULL,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
    {
        float w = ImGui::GetContentRegionAvail().x;
        ImGui::TextUnformatted(g_gui.status);
        ImGui::SameLine();

        if (g_gui.worker_running) {
            float x = ImGui::GetCursorPosX();
            ImGui::TextColored(ImVec4(1,1,0,1), " [");
            ImGui::SameLine();
            if (g_gui.progress_text[0])
                ImGui::TextUnformatted(g_gui.progress_text);
            else
                ImGui::TextUnformatted("Working");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1,1,0,1), "]");
            ImGui::SameLine(x + 200);
            ImGui::ProgressBar(g_gui.progress_frac, ImVec2(120, 16), "");
        } else {
            float x = ImGui::GetCursorPosX();
            ImGui::SameLine(x + 150);
            const char* s = ui_get_state_str();
            ImGui::TextColored(ImVec4(0,1,0,1), "%s", s);
            ImGui::SameLine(w - 170);
            ImGui::Text("RAIDTEST %s", APP_VERSION);
        }
    }
    ImGui::End();
}

static void ShowAbout(bool* open) {
    if (!ImGui::Begin("About RAIDTEST", open, ImGuiWindowFlags_AlwaysAutoResize))
        return ImGui::End();
    ImGui::TextUnformatted("RAID Prototype");
    ImGui::Separator();
    ImGui::Text("Version:  %s", APP_VERSION);
    ImGui::Text("Build:    %s %s", __DATE__, __TIME__);
    ImGui::Separator();
    ImGui::TextUnformatted("Architecture:");
    ImGui::BulletText("Service Layer  ? raid_service (unified API)");
    ImGui::BulletText("Manager Layer ? device/volume/metadata/planner/event");
    ImGui::BulletText("Engine Layer  ? stripe/mirror/cache/journal");
    ImGui::Separator();
    ImGui::TextUnformatted("Framework: Dear ImGui v1.92.8 + DirectX 11");
    ImGui::TextUnformatted("Backend:   WinFsp FUSE + MinGW-w64");
    ImGui::Separator();
    ImGui::Text("Git Commit: %s", "SCR1");
    ImGui::TextUnformatted("Author:    RAIDTEST Team");
    ImGui::TextUnformatted("License:   MIT");
    ImGui::End();
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
        ImGui::TextUnformatted("Purge will remove ALL metadata from disks.");
        ImGui::TextColored(ImVec4(1,1,0,1), "This may make the volume unrecoverable.");
        ImGui::Separator();
        if (ImGui::Button("Yes, Purge", ImVec2(120, 0))) {
            g_gui.show_confirm_purge = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            g_gui.show_confirm_purge = false;
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
                char m[2] = { g_gui.mount_letter ? g_gui.mount_letter : 'G', 0 };
                start_worker(W_BENCH, m);
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

static void RenderMainUI(void) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);

    ImGui::Begin("Main", NULL,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_MenuBar);

    float toolbar_h = 32;
    float status_h = 26;
    float avail_h = ImGui::GetContentRegionAvail().y;
    float log_h = 170;

    {
        ImGui::BeginChild("##tb", ImVec2(0, toolbar_h), false);
        ShowToolbar();
        ImGui::EndChild();
    }

    {
        float mid_h = avail_h - log_h - status_h - toolbar_h - 18;
        float left_w = ImGui::GetContentRegionAvail().x * 0.55f;

        ImGui::BeginChild("##left", ImVec2(left_w, mid_h), true);
        ShowDiskList();
        ShowPlanner();
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("##right", ImVec2(0, mid_h), true);
        ShowVolumeInfo();
        ImGui::EndChild();
    }

    ShowEventLog();
    ShowStatusBar();

    if (g_gui.show_about) ShowAbout(&g_gui.show_about);
    if (g_gui.show_confirm_destroy) ShowConfirmDestroy();
    if (g_gui.show_confirm_purge) ShowConfirmPurge();
    ShowBenchmark();
    ShowExportDialog();

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Refresh", "F5", false, !g_gui.worker_running)) start_worker(W_REFRESH, NULL);
            ImGui::Separator();
            if (ImGui::MenuItem("Export Diagnostic", NULL, false, !g_gui.worker_running)) start_worker(W_EXPORT, NULL);
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) PostQuitMessage(0);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Actions")) {
            if (ImGui::MenuItem("Scan", NULL, false, !g_gui.worker_running)) start_worker(W_SCAN, NULL);
            bool can_menu_create = g_gui.state_value == 1 && g_gui.selected_count >= 2 && !g_gui.worker_running;
            if (ImGui::MenuItem("Create", NULL, false, can_menu_create)) start_worker(W_CREATE, NULL);
            if (ImGui::MenuItem("Mirror", NULL, false, can_menu_create)) start_worker(W_MIRROR, NULL);
            bool can_menu_mount = g_gui.state_value >= 2 && !g_gui.vol_info.mounted && !g_gui.worker_running;
            if (ImGui::MenuItem("Mount", NULL, false, can_menu_mount)) start_worker(W_MOUNT, NULL);
            bool can_menu_umount = g_gui.vol_info.mounted && !g_gui.worker_running;
            if (ImGui::MenuItem("Unmount", NULL, false, can_menu_umount)) start_worker(W_UNMOUNT, NULL);
            bool can_menu_destroy = g_gui.state_value >= 2 && !g_gui.worker_running;
            if (ImGui::MenuItem("Destroy", NULL, false, can_menu_destroy)) g_gui.show_confirm_destroy = true;
            ImGui::Separator();
            bool can_menu_bench = g_gui.vol_info.mounted && !g_gui.worker_running;
            if (ImGui::MenuItem("Benchmark", NULL, false, can_menu_bench)) {
                g_gui.show_bench = true;
                g_gui.bench_done = false;
                g_gui.bench_read_mbs[0] = 0;
                g_gui.bench_write_mbs[0] = 0;
                g_gui.bench_latency[0] = 0;
                char m[2] = { g_gui.mount_letter ? g_gui.mount_letter : 'G', 0 };
                start_worker(W_BENCH, m);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
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
        CleanupDeviceD3D();
        DestroyWindow(g_gui.hwnd);
        DeleteCriticalSection(&g_gui.log_lock);
        MessageBoxA(NULL, "Failed to create DirectX 11 device.\n\n"
            "Your GPU or driver may not support DirectX 11.",
            "Error", MB_ICONERROR);
        return 1;
    }

    ShowWindow(g_gui.hwnd, SW_SHOWMAXIMIZED);
    UpdateWindow(g_gui.hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    SetupDarkTheme();
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

    gui_log("RAIDTEST " APP_VERSION " ? GUI Edition started");
    snprintf(g_gui.status, sizeof(g_gui.status), "Ready ? %u disk(s) detected",
        g_gui.disk_summary.count);
    g_gui.cache_mb = 1024;
    g_gui.mount_letter = 'G';

    MSG msg = {0};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }

        check_worker_done();

        double now = ImGui::GetTime();
        if (now - g_gui.last_refresh > 1.0) {
            refresh_ui_model();
            g_gui.last_refresh = now;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        RenderMainUI();

        ImGui::Render();
        g_gui.ctx->OMSetRenderTargets(1, &g_gui.rtv, NULL);
        ImVec4 clear = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
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
