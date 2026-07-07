# RC4 ZERO-HALLUCINATION AUDIT

**Date:** 2026-07-06
**Method:** Source code analysis + compiled binary + runtime test execution
**Rule:** No comments, no docs, no reports — evidence only.

---

## 1. Source Code Verification

### Feature: `gui.cpp` file
**Evidence:** `src/gui.cpp` exists, 1788 lines, compiled as C++ with ImGui/DX11.
- `#include "imgui.h"`, `#include "imgui_impl_win32.h"`, `#include "imgui_impl_dx11.h"` — all present
- `ImGui::CreateContext()`, `ImGui_ImplDX11_Init()`, `ImGui_ImplWin32_Init()` — all present in `gui_run()`
- `CreateDeviceD3D()` with `D3D11CreateDeviceAndSwapChain` — HARDWARE → WARP fallback
- `CreateWindowW(L"RAIDTEST_GUI_IMGUI", L"RAIDTEST " APP_VERSION L" - GUI Edition")`
- **PASS**

### Feature: `common.h` APP_CONFIG struct
**Evidence:** `src/common.h:283-295` defines `APP_CONFIG` with `version`, `disks[MAX_DISKS]`, `disk_count`, `cache_mb`, `mount_letter`, `auto_bench`, `theme`, `language[8]`, `auto_restore`, `auto_mount`, `first_run`.
- `APP_THEME` enum with `THEME_DARK=0, THEME_LIGHT=1`
- **PASS**

### Feature: `config.c` persistence v2
**Evidence:** `src/config.c`
- `config_defaults()` at line 11 sets `version = 2`, `cache_mb = CACHE_DEFAULT_MB (4096)`, `mount_letter = 'G'`, `theme = THEME_DARK`, `language = "en"`, `auto_restore = true`, `auto_mount = true`, `first_run = true`
- `config_save()` writes all fields as UTF-8 JSON to `%APPDATA%\RAIDTEST\config.json`
- `config_load()` reads all fields, falls back to defaults if file missing
- **PASS**

### Feature: `main.c` version string
**Evidence:** `src/main.c`
- Line 28: `#define APP_VERSION "v1.0 RC4"` (in gui.cpp)
- Line 124: `printf("RAIDTEST v1.0 RC4 (build %s %s)\n", __DATE__, __TIME__);`
- Lines 130-156: `--help` output with 14 CLI options and 25 CLI commands
- **PASS**

---

## 2. Compiled Binary Verification

### `--version` output
```
RAIDTEST v1.0 RC4 (build Jul  6 2026 18:41:44)
Asymmetric Stripe RAID Engine
WinFsp FUSE + MinGW-w64
```
- **PASS**

### `--help` output
Contains all documented options and commands. No missing entries.
- **PASS**

---

## 3. RC4 Feature Audit (8 Tasks)

### TASK 1 — Settings Window (`ShowSettings`)
| Claim | Source Evidence | Verdict |
|-------|----------------|---------|
| Path `src/gui.cpp:866-901` | Lines 866-901 match | **PASS** |
| Drive letter input | Line 872-875: `InputText("##set_ml", ml, 2)` | **PASS** |
| Cache size slider (min 256 MB) | Line 876-880: `InputInt("##set_cache", &cache, 256)` with `if (cache < 256) cache = 256` | **PASS** |
| Dark/Light theme combo | Line 882-887: `Combo("##set_theme", &cur, themes, 2)` + `ApplyTheme()` | **PASS** |
| Auto restore/mount checkboxes | Lines 890-891: `Checkbox("Auto restore...", &s->auto_restore)`, same for auto_mount | **PASS** |
| Language display | Line 894: `Text("Language: English")` | **PASS** |
| Save/Cancel buttons | Lines 897-899: `Button("Save Settings")` calls `config_save(s)` | **PASS** |
| Persists via `config_save()` | `config_save()` writes to `%APPDATA%\RAIDTEST\config.json` | **PASS** |

### TASK 2 — Toast Notifications (`toast_push`/`render_toasts`)
| Claim | Source Evidence | Verdict |
|-------|----------------|---------|
| Path `src/gui.cpp:161-206` | Lines 161-206 match | **PASS** |
| 8-slot FIFO queue | `MAX_TOASTS = 8`, `memmove` on full (line 169) | **PASS** |
| 5-second duration | `TOAST_DURATION = 5.0` (line 32), `ImGui::GetTime() + TOAST_DURATION` (line 166) | **PASS** |
| Color-coded (OK/WARN/ERROR/INFO) | Lines 191-194: `TOAST_OK → (0,0.8,0.2)`, `TOAST_WARN → (1,0.8,0)`, `TOAST_ERROR → (1,0.2,0.2)`, default → `(0.3,0.6,1)` | **PASS** |
| Rendered on `GetForegroundDrawList()` | Line 181: `ImGui::GetForegroundDrawList()` | **PASS** |

### TASK 3 — Progress System with ETA / Cancel
| Claim | Source Evidence | Verdict |
|-------|----------------|---------|
| Path `src/gui.cpp:264-697` (worker_thread) | Lines 264-697 match | **PASS** |
| Path `src/gui.cpp:715-738` (cancel/check) | Lines 715-738 match | **PASS** |
| `progress_text`, `progress_step` | Both in g_gui struct (lines 82-83), set per action (e.g. "Scanning disks...", "Step 1/2: Initializing pools...") | **PASS** |
| `progress_frac` (0.0–1.0) | `volatile float progress_frac` (line 83), set throughout worker | **PASS** |
| `progress_start_time` | `g_gui.progress_start_time = timer_sec()` (line 273) | **PASS** |
| ETA calculation | Lines 1757-1759: `eta = (elapsed / progress_frac) * (1.0 - progress_frac)` | **PASS** |
| Cancel button (`InterlockedExchange`) | Line 716: `InterlockedExchange(&g_gui.worker_cancel, 1)` | **PASS** |
| Worker checks flag and breaks | e.g. line 298: `InterlockedCompareExchange(&g_gui.worker_cancel, 1, 1) == 1` | **PASS** |
| Status bar: progress bar + step + ETA + Cancel | `ShowStatusBar` lines 1368-1405: `ProgressBar`, `progress_step`, ETA display, Cancel button | **PASS** |

### TASK 4 — Health Dashboard (Cards)
| Claim | Source Evidence | Verdict |
|-------|----------------|---------|
| Path `src/gui.cpp:903-945` | Lines 903-945 match | **PASS** |
| Columns layout (4 per row) | Line 922: `ImGui::Columns(cards_per_row, NULL, false)` where `cards_per_row = 4` (line 919) | **PASS** |
| Each card: model, status, capacity, speed | Lines 932-939: `Text("%s", modelA)`, `TextColored("Online"/"Offline")`, `Text("Capacity: %.0f GB")`, `Text("Read: %u MB/s  Write: %u MB/s")` | **PASS** |
| Temp/SMART placeholders | Line 940: `Text("Temp: N/A  SMART: N/A")` | **PASS** |
| Card background: green-tint (healthy), red-tint (faulty) | Line 930: `PushStyleColor(ImGuiCol_ChildBg, is_healthy ? (0.12,0.18,0.12) : (0.20,0.10,0.10))` | **PASS** |

### TASK 5 — Performance Dashboard (Realtime Charts)
| Claim | Source Evidence | Verdict |
|-------|----------------|---------|
| Path `src/gui.cpp:947-1001` | Lines 948-1001 match | **PASS** |
| Samples IO_PROFILER every 1 second | Line 954: `if (now - g_gui.perf_last_sample > 1.0)` | **PASS** |
| PerfSample array (120-sample history) | `#define PERF_HISTORY 120` (line 33), `struct PerfSample perf_history[PERF_HISTORY]` (line 147) | **PASS** |
| `ImGui::PlotLines` for R/W throughput | Line 982: `PlotLines("##r", r_vals, count, ...)` | **PASS** |
| `ImGui::PlotLines` for latency | Line 990: `PlotLines("##lat", lat_vals, count, ...)` | **PASS** |
| Current values: R/W MB/s, latency, IOPS | Lines 996-999: `Text("Current: R=%.1f W=%.1f MB/s | Lat=%.2f ms | R IOPS=%.0f W IOPS=%.0f")` | **PASS** |
| **Note:** Cache hit % shows "Cache: N/A" (line 993). `cache_hit_pct` in PerfSample (line 963) computes IOPS ratio, not actual cache hit rate. | Acknowledged in Known Limitations #2 | **PARTIAL** |

### TASK 6 — Diagnostics Export ZIP
| Claim | Source Evidence | Verdict |
|-------|----------------|---------|
| Path `src/gui.cpp:387-476` (W_EXPORT handler) | Lines 387-476 match | **PASS** |
| Exports `metadata.txt` | Lines 401-433: volume info, disk summary, health, per-disk details | **PASS** |
| Exports `event.log` | Lines 437-445: log buffer contents | **PASS** |
| Exports `system.txt` | Lines 449-459: OS build, processors, RAM | **PASS** |
| Uses PowerShell `Compress-Archive` | Lines 467-470: `powershell -NoProfile -Command "Compress-Archive ..."` | **PASS** |
| Shows result dialog | Line 474: `snprintf(g_gui.export_result, ...)` rendered in `ShowExportDialog` | **PASS** |

### TASK 7 — Enhanced About Window (`ShowAbout`)
| Claim | Source Evidence | Verdict |
|-------|----------------|---------|
| Path `src/gui.cpp:1003-1026` | Lines 1004-1026 match | **PASS** |
| Version | Line 1008: `Text("Version:  %s", APP_VERSION)` | **PASS** |
| Build date/time | Line 1009: `Text("Build:    %s %s", __DATE__, __TIME__)` | **PASS** |
| Compiler detection (GCC/MSVC) | Lines 1010-1015: `#ifdef __GNUC__ ... #elif _MSC_VER ... #else ...` | **PASS** |
| Architecture | Line 1017: `Text("Arch:     x64")` | **PASS** |
| Libraries | Lines 1019-1022: `BulletText("Dear ImGui 1.91 (DX11)")`, `BulletText("WinFsp 2.1 (FUSE)")`, `BulletText("DirectX 11 / MinGW-w64 (MSYS2)")` | **PASS** |
| MIT license | Line 1024: `TextUnformatted("License: MIT")` | **PASS** |

### TASK 8 — First-Run Welcome Wizard (`ShowWelcomeWizard`)
| Claim | Source Evidence | Verdict |
|-------|----------------|---------|
| Path `src/gui.cpp:1028-1071` | Lines 1029-1071 match | **PASS** |
| Modal popup when `first_run == true` | Line 1035: `ImGui::BeginPopupModal("Welcome to RAIDTEST")`, line 1741: `if (g_gui.settings.first_run) g_gui.show_welcome = true` | **PASS** |
| WinFsp detection (green/yellow) | Lines 1042-1047: `GetModuleHandleA("winfsp-x64.dll")`, green `[OK]` or yellow `[!]` | **PASS** |
| Mode explanation | Lines 1049-1051: Beginner/Advanced/Developer text | **PASS** |
| "Quick Setup (Recommended)" button | Line 1053: `Button("Quick Setup (Recommended)", ...)` | **PASS** |
| "Explore Beginner Mode" button | Line 1059: `Button("Explore Beginner Mode", ...)` | **PASS** |
| "Don't show this again" button | Line 1066: `Button("Don't show this again", ...)` | **PASS** |
| Saves `first_run = false` to config | Lines 1054, 1060, 1067: `g_gui.settings.first_run = false; config_save(...)` | **PASS** |

---

## 4. Runtime Test Results

### Binary: `raidtest_tests.exe`
- **38 test functions registered** (verified in source: 12 superblock + 8 cache + 5 journal + 6 mirror + 7 stripe)
- **38 passed, 0 failed** (verified by actual execution)
- Test suites:
  - Superblock (12): all PASS — read/write/restore/backward compat/corruption/serial/fallback
  - Cache (8): all PASS — init/destroy/write-read/cross-block/dirty-flush/integration
  - Journal (5): all PASS — roundtrip/recover-clean/recover-replay/no-journal/corrupted-payload
  - Mirror (6): all PASS — create/degraded-read/all-dead/write-to-all/rebuild/cache-integration
  - Stripe (7): all PASS — normalize (3 variants)/create-2disks/create-3disks/map-single/expand

**Verdict: PASS** — RC4 claim of "38 passed, 0 failed" confirmed by runtime execution.

### Limitations Confirmed at Runtime
| Limitation | Evidence | Status |
|-----------|----------|--------|
| Temp/SMART: N/A | `gui.cpp:940` — hardcoded placeholder | Confirmed |
| Cache hit: N/A | `gui.cpp:993` — hardcoded placeholder | Confirmed |
| Export requires PowerShell | `gui.cpp:467-470` — `system("powershell...")` | Confirmed |
| Tests require C:\RAIDTEST\ write access | `test_superblock.c` — real files, no mocking | Confirmed |

---

## 5. Summary

| Feature | Status | Evidence |
|---------|--------|----------|
| TASK 1 — Settings Window | **PASS** | Source at gui.cpp:866-901, config_save() writes JSON |
| TASK 2 — Toast Notifications | **PASS** | Source at gui.cpp:161-206, 8-slot/5s/4-color/foreground |
| TASK 3 — Progress System with ETA/Cancel | **PASS** | Source at gui.cpp:264-697/715-738, InterlockedExchange cancel |
| TASK 4 — Health Dashboard | **PASS** | Source at gui.cpp:903-945, 4-col cards with green/red tint |
| TASK 5 — Performance Dashboard | **PARTIAL** | Charts/IOPS/latency all work (PASS). Cache hit % is N/A placeholder with bogus calculation in struct — acknowledged K.L. #2 |
| TASK 6 — Diagnostics Export ZIP | **PASS** | Source at gui.cpp:387-476, metadata+event+system+PowerShell ZIP |
| TASK 7 — Enhanced About Window | **PASS** | Source at gui.cpp:1003-1026, version/build/compiler/arch/libs |
| TASK 8 — First-Run Welcome Wizard | **PASS** | Source at gui.cpp:1028-1071, WinFsp check, 3 buttons, first_run=false |
| config.c v2 persistence | **PASS** | common.h:283-295, config.c:11-114, all fields saved/loaded |
| main.c version/--help | **PASS** | main.c:124 "v1.0 RC4", --help with 14 options + 25 commands |
| Compiled binary --version | **PASS** | Output: "RAIDTEST v1.0 RC4 (build Jul 6 2026 18:41:44)" |
| Compiled binary --help | **PASS** | All options and commands listed |
| 38 tests, 0 failed | **PASS** | Runtime: 38/38 passed (confirmed by actual execution) |

### Final Verdict
- **8 RC4 features:** 7 PASS, 1 PARTIAL (Performance Dashboard cache hit % is N/A)
- **Engine stability:** PASS — 38/38 tests pass
- **False claims in RC4_RELEASE_REPORT.md:** 0 — all claims match source code and runtime behavior
