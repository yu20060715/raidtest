# GUI_ARCHITECTURE_REVIEW.md

純 Source Code 分析。不引用任何 Markdown / README / 舊 Audit。

---

## 1. Build Analysis

### build.bat 實際編譯哪些 GUI 檔案

**Line 33 (MinGW 路徑):**
```
g++ -Wall -O2 ... -c src/gui.cpp -o build/gui.o
```

**Line 13 (clang-cl 路徑, 截斷但確認):**
```
clang-cl ... src\gui.cpp ... -Fe:raidtest_winfsp.exe
```

**編譯的 GUI 檔案：**
- `src/gui.cpp` — 是，唯一編譯的 GUI .cpp 檔案
- `src/gui.h` — 是，被 main.c (line 9) 和 gui.cpp (line 2) include
- ImGui 等第三方 — 是 (imgui/*.cpp, imgui/backends/*.cpp)

**未編譯的 GUI 檔案：**
- `src/gui_panels.cpp` — **否**，build.bat 無任何一行提及此檔名
- `src/gui_data.h` — **否**，header only，僅被 gui_panels.cpp include

### GUI 檔案是否加入 Build

| 檔案 | Build 是否編譯 | 行號證據 |
|------|---------------|---------|
| src/gui.cpp | 是 | build.bat:33 `g++ ... -c src/gui.cpp -o build/gui.o` |
| src/gui_panels.cpp | 否 | build.bat 全文無 `gui_panels` |
| src/gui_data.h | N/A (header) | 僅被 gui_panels.cpp include |
| src/gui.h | N/A (header) | 被 main.c:9 和 gui.cpp:2 include |

### Orphan Source Files

**確認：`src/gui_panels.cpp` 和 `src/gui_data.h` 是 Orphan Files。**
- `gui_data.h` 僅被 `gui_panels.cpp` include (gui_panels.cpp:12)
- `gui_panels.cpp` 不被任何檔案 include，也不被 build.bat 編譯
- `gui_panels.cpp` 無 `static` 修飾，定義了 `struct GuiState g_gui` (line 15) 和所有 extern 函式
- 無人連結這些符號

---

## 2. GUI File Inventory

| 檔名 | 行數 | 編譯 | 被 Include | 參與 Runtime |
|------|------|------|-----------|-------------|
| src/gui.h | 2 | 是 | main.c, gui.cpp | 是 — 宣告 `gui_run()` |
| src/gui.cpp | 1765 | 是 | 無 (唯一 .cpp) | 是 — 完整 GUI 實作 |
| src/gui_panels.cpp | 1547 | **否** | 無 | **否 — 完全未連結** |
| src/gui_data.h | 154 | N/A | 僅 gui_panels.cpp | **否 — include chain 無用** |

**Runtime 實際參與的 GUI 程式碼：`gui.cpp` 全部 (1765 行)。**
**gui_panels.cpp + gui_data.h = 1701 行 dead code。**

---

## 3. Duplicate Code Analysis

### 重複函式完整列表

所有 gui.cpp 函式為 `static`，所有 gui_panels.cpp 同名函式為 extern (無 static)。

| 函式名稱 | gui.cpp | gui_panels.cpp | 是否完全相同 | 差異 |
|----------|---------|----------------|-------------|------|
| `timer_sec` | line 140, static | line 17, extern | 幾乎相同 | 完全相同 |
| `toast_push` | line 147, static | line 24, extern | **不同** | gui.cpp:148 無 `EnterCriticalSection(&g_gui.toast_lock)`，gui_panels.cpp:25 有 |
| `render_toasts` | line 164, static | line 43, extern | **不同** | gui.cpp:164 無鎖，gui_panels.cpp:44 有 `EnterCriticalSection(&g_gui.toast_lock)` |
| `gui_log` | line 194, static | line 75, extern | 完全相同 | 邏輯一致 (都有 log_lock) |
| `event_cb` | line 204, static | line 85, extern | 完全相同 | — |
| `refresh_ui_model` | line 217, static | line 98, extern | 完全相同 | — |
| `worker_thread` | line 224, static | line 105, extern | **幾乎相同** | line 447 gui.cpp: `if (device_get(i)->healthy)` 無 NULL-check；gui_panels.cpp:334 有 `if (d && d->healthy)` |
| `start_worker` | line 642, static | line 528, extern | **幾乎相同** | gui_panels.cpp:542-547 有 `if (!g_gui.worker_handle)` 錯誤處理，gui.cpp:655 無 |
| `cancel_worker` | line 658, static | line 550, extern | 完全相同 | — |
| `check_worker_done` | line 663, static | line 555, extern | **幾乎相同** | gui.cpp:668 用 `strncpy`，gui_panels.cpp:560 用 `snprintf` |
| `SetupDarkTheme` | line 683, static | line 575, extern | 完全相同 | — |
| `SetupLightTheme` | line 720, static | line 612, extern | 完全相同 | — |
| `ApplyTheme` | line 728, static | line 620, extern | 完全相同 | — |
| `btn_disabled` | line 807, static | line 625, extern | 完全相同 | — |
| `ShowSettings` | line 809, static | line 627, extern | 完全相同 | — |
| `ShowHealthDashboard` | line 846, static | line 664, extern | 完全相同 | — |
| `ShowPerformanceDashboard` | line 891, static | line 709, extern | 完全相同 | — |
| `ShowAbout` | line 943, static | line 761, extern | **幾乎相同** | gui.cpp:959 "Dear ImGui 1.91"，gui_panels.cpp:777 "Dear ImGui 1.92.8"；年份 2025 vs 2026 |
| `ShowWelcomeWizard` | line 968, static | line 786, extern | 完全相同 | — |
| `ShowModeTabs` | line 1013, static | line 831, extern | 完全相同 | — |
| `ShowToolbar` | line 1035, static | line 853, extern | **幾乎相同** | gui_panels.cpp:905-928 多了 `Restore` 按鈕和 `Cache ON/OFF` 切換 |
| `ShowDiskList` | line 1090, static | line 932, extern | 完全相同 | — |
| `ShowPlanner` | line 1155, static | line 997, extern | 完全相同 | — |
| `ShowVolumeInfo` | line 1196, static | line 1038, extern | 完全相同 | — |
| `ShowPerformancePanel` | line 1261, static | line 1103, extern | 完全相同 | — |
| `ShowEventLog` | line 1301, static | line 1143, extern | 完全相同 | — |
| `ShowStatusBar` | line 1325, static | line 1167, extern | 完全相同 | — |
| `ShowConfirmDestroy` | line 1364, static | line 1206, extern | 完全相同 | — |
| `ShowBenchmark` | line 1387, static | line 1229, extern | **幾乎相同** | gui_panels.cpp:1255 有 `cancel_worker()` 在 cancel 按鈕，gui.cpp:1414 無 |
| `ShowExportDialog` | line 1422, static | line 1265, extern | 完全相同 | — |
| `ShowBeginnerPanel` | line 1443, static | line 1286, extern | **幾乎相同** | gui_panels.cpp:1297 多了 mounted 判斷才顯示 Unmount/Benchmark 按鈕；gui.cpp:1454-1456 多了 Restore Volume / Health Check 按鈕 |
| `ShowRestoreWizard` | line 1481, static | line 1320, extern | 完全相同 | — |
| `ShowRebuildWizard` | line 1508, static | line 1347, extern | 完全相同 | — |
| `ShowSimulationControls` | line 1543, static | line 1382, extern | 完全相同 | — |
| `RenderMainUI` | line 1575, static | line 1414, extern | **幾乎相同** | gui_panels.cpp:1453-1466 多了 `Metadata` CollapsingHeader；gui_panels.cpp:1506-1521 多了 Cache 選單；gui_panels.cpp:1534-1535 多了 Config Save/Load；gui.cpp 無這些 |

### gui.cpp 獨有 (gui_panels.cpp 無對應)

| 函式 | 行號 | 說明 |
|------|------|------|
| `CreateRenderTarget` | 733 | D3D 初始化 |
| `CreateDeviceD3D` | 742 | D3D device/swapchain 建立 |
| `CleanupDeviceD3D` | 769 | D3D 清理 |
| `WndProc` | 778 | Win32 視窗程序 |
| `SetupWindow` | 794 | Win32 視窗建立 |
| `gui_run` | 1677 | **唯一 Entry Point** — 初始化和 Message Loop |

### gui_panels.cpp 獨有 (gui.cpp 無對應)

| 函式 | 行號 | 說明 |
|------|------|------|
| (無) | — | — |

**gui_panels.cpp 沒有 `gui_run()`、沒有 `SetupWindow`、沒有 `CreateDeviceD3D`、沒有 `WndProc`。**
**gui_panels.cpp 無法獨立存在，必須依賴 gui.cpp 的 gui_run() 來驅動。**

---

## 4. Global State Analysis

### 存在兩份 `g_gui`

| 變數 | 檔案 | 行號 | Linkage | 型別 |
|------|------|------|---------|------|
| `g_gui` | gui.cpp | 138 | **`static` (internal)** | `static struct { /* anonymous */ }` |
| `g_gui` | gui_panels.cpp | 15 | `extern` (external, 實際定義處) | `struct GuiState` (gui_data.h:41-115) |

### 差異：匿名 struct vs GuiState

gui.cpp 的匿名 struct (line 61-138) 與 gui_data.h 的 `GuiState` (line 41-115) 幾乎相同，但：

| 欄位 | gui.cpp 匿名 struct | GuiState (gui_data.h) |
|------|-------------------|----------------------|
| `CRITICAL_SECTION toast_lock` | **無** | 有 (line 49) |
| `CRITICAL_SECTION log_lock` | 有 (line 68) | 有 (line 48) |

### Global Ownership Table

| Global Symbol | Owner (Compiled) | Owner (Dead) | Runtime Active |
|--------------|-----------------|-------------|---------------|
| `g_gui` | gui.cpp `static` line 138 | gui_panels.cpp `extern` line 15 | **gui.cpp 的 static g_gui** |
| `g_gui.log_lock` | gui.cpp 初始化(1678) | gui_panels.cpp (無初始化) | **gui.cpp** |
| `g_gui.toast_lock` | **不存在** | gui_panels.cpp (GuiState 有此欄位) | **無 — gui.cpp 沒有此 lock** |

**Runtime 使用 gui.cpp 的 `static g_gui`。gui_panels.cpp 的 `struct GuiState g_gui` 從未被連結。**

---

## 5. Runtime Call Graph

```
main()                                              [main.c:65]
  │
  ├─ cli_main() (if argc>1)                        [main.c:11]
  │
  └─ gui_run()                                     [gui.cpp:1677] ← GUIMode Entry
       │
       ├─ InitializeCriticalSection(&g_gui.log_lock) [1678]
       ├─ SetupWindow()                             [gui.cpp:794]  ← Win32 window
       ├─ CreateDeviceD3D()                         [gui.cpp:742]  ← D3D11 init
       ├─ ImGui_ImplWin32_Init / ImGui_ImplDX11_Init [1701-1702]
       ├─ raid_init()                               [1703]
       ├─ event_bus_subscribe(event_cb)             [1705-1712]
       │
       └─ Message Loop [1720-1748]                 每幀執行：
            │
            ├─ check_worker_done()                  [gui.cpp:663]  ← polling worker
            ├─ refresh_ui_model()                   [gui.cpp:217]  ← if refresh_pending
            ├─ profiler_update_rates()
            ├─ ImGui NewFrame / Render
            │    │
            │    └─ RenderMainUI()                  [gui.cpp:1575]
            │         │
            │         ├─ ShowModeTabs()             [gui.cpp:1013]
            │         ├─ ShowToolbar() (if Advanced/Developer) [gui.cpp:1035]
            │         ├─ [Beginner] ShowBeginnerPanel() [gui.cpp:1443]
            │         │    └─ ShowHealthDashboard() [gui.cpp:846]
            │         ├─ [Advanced/Developer]
            │         │    ├─ ShowDiskList()        [gui.cpp:1090]
            │         │    ├─ ShowPlanner()         [gui.cpp:1155]
            │         │    ├─ ShowVolumeInfo()      [gui.cpp:1196]
            │         │    ├─ [Developer] ShowSimulationControls() [gui.cpp:1543]
            │         │    ├─ [Developer] ShowPerformanceDashboard() [gui.cpp:891]
            │         │    └─ ShowPerformancePanel() [gui.cpp:1261]
            │         ├─ ShowEventLog()             [gui.cpp:1301]
            │         ├─ ShowStatusBar()            [gui.cpp:1325]
            │         ├─ ShowWelcomeWizard()        [gui.cpp:968]
            │         ├─ ShowRestoreWizard()        [gui.cpp:1481]
            │         ├─ ShowRebuildWizard()        [gui.cpp:1508]
            │         ├─ render_toasts()            [gui.cpp:164]
            │         ├─ ShowSettings()             [gui.cpp:809]
            │         ├─ ShowAbout()                [gui.cpp:943]
            │         ├─ ShowConfirmDestroy()       [gui.cpp:1364]
            │         ├─ ShowBenchmark()            [gui.cpp:1387]
            │         └─ ShowExportDialog()         [gui.cpp:1422]
            │
            ├─ ImGui::Render()
            └─ swapchain->Present()

Callbacks (from worker thread or event bus):
  ├─ worker_thread()                                [gui.cpp:224] ← _beginthreadex
  │    └─ calls raid_*, toast_push, refresh_ui_model
  ├─ event_cb()                                     [gui.cpp:204] ← event_bus callback
  │    └─ calls toast_push, gui_log
  └─ WndProc()                                      [gui.cpp:778] ← Win32 window proc
```

**標記：哪些函式真正執行、哪些永遠不會執行**

- **gui.cpp 所有函式**：全部會執行（透過 gui_run → message loop → RenderMainUI）
- **gui_panels.cpp 所有函式**：**永遠不會執行** — 未被編譯，未被連結

---

## 6. Dead GUI Analysis

### Orphan Code (整個檔案從未被編譯)

| 函式 | 檔案 | 行號 | Call Site |
|------|------|------|-----------|
| `timer_sec` | gui_panels.cpp | 17 | 無 — 無人連結 |
| `toast_push` | gui_panels.cpp | 24 | 無 |
| `render_toasts` | gui_panels.cpp | 43 | 無 |
| `gui_log` | gui_panels.cpp | 75 | 無 |
| `event_cb` | gui_panels.cpp | 85 | 無 |
| `refresh_ui_model` | gui_panels.cpp | 98 | 無 |
| `worker_thread` | gui_panels.cpp | 105 | 無 |
| `start_worker` | gui_panels.cpp | 528 | 無 |
| `cancel_worker` | gui_panels.cpp | 550 | 無 |
| `check_worker_done` | gui_panels.cpp | 555 | 無 |
| `SetupDarkTheme` | gui_panels.cpp | 575 | 無 |
| `SetupLightTheme` | gui_panels.cpp | 612 | 無 |
| `ApplyTheme` | gui_panels.cpp | 620 | 無 |
| `btn_disabled` | gui_panels.cpp | 625 | 無 |
| `ShowSettings` | gui_panels.cpp | 627 | 無 |
| `ShowHealthDashboard` | gui_panels.cpp | 664 | 無 |
| `ShowPerformanceDashboard` | gui_panels.cpp | 709 | 無 |
| `ShowAbout` | gui_panels.cpp | 761 | 無 |
| `ShowWelcomeWizard` | gui_panels.cpp | 786 | 無 |
| `ShowModeTabs` | gui_panels.cpp | 831 | 無 |
| `ShowToolbar` | gui_panels.cpp | 853 | 無 |
| `ShowDiskList` | gui_panels.cpp | 932 | 無 |
| `ShowPlanner` | gui_panels.cpp | 997 | 無 |
| `ShowVolumeInfo` | gui_panels.cpp | 1038 | 無 |
| `ShowPerformancePanel` | gui_panels.cpp | 1103 | 無 |
| `ShowEventLog` | gui_panels.cpp | 1143 | 無 |
| `ShowStatusBar` | gui_panels.cpp | 1167 | 無 |
| `ShowConfirmDestroy` | gui_panels.cpp | 1206 | 無 |
| `ShowBenchmark` | gui_panels.cpp | 1229 | 無 |
| `ShowExportDialog` | gui_panels.cpp | 1265 | 無 |
| `ShowBeginnerPanel` | gui_panels.cpp | 1286 | 無 |
| `ShowRestoreWizard` | gui_panels.cpp | 1320 | 無 |
| `ShowRebuildWizard` | gui_panels.cpp | 1347 | 無 |
| `ShowSimulationControls` | gui_panels.cpp | 1382 | 無 |
| `RenderMainUI` | gui_panels.cpp | 1414 | 無 |
| `struct GuiState g_gui` | gui_panels.cpp | 15 | 無 |

**結論：全部 33 個函式 + 1 個 global variable = 1547 行完全 dead code。**

---

## 7. Dependency Graph

### Include 關係

```
main.c
  └─ #include "gui.h"                  [main.c:9]
       └─ 僅宣告 int gui_run(void)

gui.cpp                               ← **this is compiled**
  ├─ #include "gui.h"                  [gui.cpp:2]
  ├─ #include "raid_service.h"         [gui.cpp:3]
  ├─ #include "ui_model.h"             [gui.cpp:4]
  ├─ #include "device_manager.h"       [gui.cpp:5]
  ├─ #include "planner_engine.h"       [gui.cpp:6]
  ├─ #include "event_bus.h"            [gui.cpp:7]
  ├─ #include "profiler.h"             [gui.cpp:8]
  ├─ #include "cmd_handler.h"          [gui.cpp:9]
  ├─ #include "superblock.h"           [gui.cpp:10]
  ├─ #include "config.h"               [gui.cpp:11]
  ├─ #include "imgui.h"                [gui.cpp:14]
  ├─ #include "backends/imgui_impl_win32.h"  [gui.cpp:15]
  └─ #include "backends/imgui_impl_dx11.h"   [gui.cpp:16]

gui_panels.cpp                         ← **NOT compiled, orphan**
  ├─ #include "raid_service.h"         [gui_panels.cpp:2]
  ├─ #include "ui_model.h"             [gui_panels.cpp:3]
  ├─ #include "device_manager.h"       [gui_panels.cpp:4]
  ├─ #include "planner_engine.h"       [gui_panels.cpp:5]
  ├─ #include "event_bus.h"            [gui_panels.cpp:6]
  ├─ #include "profiler.h"             [gui_panels.cpp:7]
  ├─ #include "config.h"               [gui_panels.cpp:8]
  ├─ #include "superblock.h"           [gui_panels.cpp:9]
  └─ #include "gui_data.h"             [gui_panels.cpp:12] ← **only consumer**
       ├─ #include "imgui.h"           [gui_data.h:2]
       ├─ #include "backends/imgui_impl_win32.h"  [gui_data.h:3]
       └─ #include "backends/imgui_impl_dx11.h"   [gui_data.h:4]
```

### 注意事項

- `gui.cpp` **不 include `gui_data.h`**。gui.cpp 自己定義所有型別 (enum, struct, anonymous struct g_gui)。
- `gui_panels.cpp` **不 include `gui.h`**。gui_panels.cpp 沒有 `gui_run()`。
- `gui_data.h` 僅被 `gui_panels.cpp` include。`gui_data.h` 定義了 `GuiState`，但這個型別在 Runtime 從未被使用。
- `gui.cpp` 和 `gui_panels.cpp` 的 include 幾乎相同 (gui_panels.cpp 多了 gui_data.h，少了 cmd_handler.h)。

---

## 8. Architecture Problems

### P1 — Duplicate Implementation (CRITICAL)

- **Evidence**: 33 組函式在 gui.cpp (static) 和 gui_panels.cpp (extern) 各有一份
- **Risk**: 任何修改必須同步兩份，否則行為分歧
- **Impact**: Bug。例如 gui.cpp toast 無 thread-safe lock，gui_panels.cpp 有；gui.cpp start_worker 無 error handling，gui_panels.cpp 有

### P2 — gui_panels.cpp 是 Orphan Dead Code (CRITICAL)

- **Evidence**: build.bat 不編譯 gui_panels.cpp；無任何檔案 include 或連結它
- **Risk**: 開發者可能誤改 gui_panels.cpp 以為在改 active code
- **Impact**: 浪費維護精力，約 1700 行無用程式碼

### P3 — 兩份 Global State (CRITICAL)

- **Evidence**: gui.cpp:138 `static struct { ... } g_gui` vs gui_panels.cpp:15 `struct GuiState g_gui`
- **Risk**: 混淆變數歸屬
- **Impact**: 開發者可能以為 gui.cpp 使用 GuiState，但實際是匿名 struct

### P4 — gui.cpp 無 `CRITICAL_SECTION toast_lock` (BUG)

- **Evidence**: gui.cpp 匿名 struct (line 61-138) 無 `toast_lock` 欄位；gui.cpp:147 `toast_push` 未上鎖
- **Risk**: 當 worker_thread (不同 thread) 呼叫 `toast_push` 時，與主 thread 的 `render_toasts` 競爭
- **Impact**: toast 資料損毀、crash

### P5 — Bug Divergence

- **Evidence**: gui.cpp `start_worker` (line 642) 無 `if (!g_gui.worker_handle)` 錯誤處理；gui_panels.cpp (line 542) 有
- **Evidence**: gui.cpp `ShowBeginnerPanel` (line 1443) 無 mounted 判斷就顯示 Unmount 按鈕；gui_panels.cpp (line 1297) 有
- **Risk**: gui_panels.cpp 裡修復的 bug 在 gui.cpp 仍存在
- **Impact**: Runtime 行為不一致

### P6 — Build Inconsistency

- **Evidence**: gui_panels.cpp 存在於 src/ 目錄但不加入 build
- **Risk**: 重構時以為檔案有作用
- **Impact**: 垃圾檔案累積

### P7 — `gui.h` 缺乏完整介面宣告

- **Evidence**: gui.h (line 1-2) 只宣告 `int gui_run(void)`，無任何 panel/worker/theme 函式宣告
- **Risk**: 其他檔案無法呼叫這些函式（雖然它們都是 static）
- **Impact**: 無法外部呼叫，限制了測試和擴展

---

## 9. Decision Analysis

### 方案A：保留 gui.cpp，刪除 gui_panels.cpp

| 項目 | 評估 |
|------|------|
| 修改檔案數 | 2 (刪除 gui_panels.cpp, gui_data.h) |
| 風險 | 極低 — 此二檔案無任何 Call Site，Build 不參考 |
| 維護性 | 維持現狀，但需在 gui.cpp 內修 bug |
| 工作量 | 5 分鐘 |
| 是否適合大學專題 | 是 — 消除困惑，專注單一檔案 |
| 影響 Build | 無 — 這兩個檔案本來就沒加入 Build |
| 影響 Tests | 無 — test build 不含任何 GUI |

### 方案B：完成 gui_panels.cpp 模組化

| 項目 | 評估 |
|------|------|
| 修改檔案數 | 4+ (gui.cpp, gui_panels.cpp, gui_data.h, build.bat 或其他) |
| 風險 | 中高 — 需確保 33 組函式差異被正確合併，需要實際 Build 和 Test |
| 維護性 | 高 — 分離關注點，模組化設計 |
| 工作量 | 大 — 數小時到數天，需逐函式比對差異、處理分歧 bug、確認 toast_lock 等 |
| 是否適合大學專題 | 可，但有風險 — 需完整測試 |
| 影響 Build | 需修改 build.bat 加入 gui_panels.cpp |
| 影響 Tests | 需確保 GUI 可正確 build |

### 方案比較摘要

| 構面 | 方案A (刪除) | 方案B (模組化) |
|------|------------|--------------|
| 立即風險 | 最低 | 中高 |
| 長期維護 | 單一檔案 1765 行 | 多檔案分散 |
| Bug 修復速度 | 快速 | 較慢 (需注意差異) |
| 心靈負擔 | 無 | 需理解兩套實作差異 |
| 技術債 | 保留原始 debt | 改善架構 |
| 需 Build 驗證 | 否 | 是 |

### 推薦方案

**方案A：保留 gui.cpp，刪除 gui_panels.cpp 和 gui_data.h。**

理由：
1. 這兩個檔案是 100% dead code — 無任何 Call Site，Build 不編譯
2. gui_panels.cpp 無法獨立執行 (缺少 gui_run, SetupWindow, CreateDeviceD3D, WndProc)
3. 33 組函式有細微差異，合併需耗時比對且風險高
4. 方案A 不影響 Build、不影響 Tests、5 分鐘可完成
5. 大學專題層級，單一 1765 行的 gui.cpp 雖大但仍可管理
6. 未來若要重構，應從 gui.cpp 內部模組化開始，而非復活 gui_panels.cpp

---

## 10. Final Recommendation

### 目前 GUI 是否應立即重構？

**否。不應立即重構。**

理由：
- 兩個 bug 需要立即修復 (P0 等級)：
  1. **B9** — `gui.cpp:1143` `device_select(&idx, 1)` 每次 checkbox 點擊取代所有選取
  2. **B14** — `gui.cpp:663` `check_worker_done` polling 無 event-based notification

### 是否應先修 Bug？

**是。先修 Bug，再談重構。**
- B1-B14 中的 P0 項目應優先修正 (見 REFACTOR_BACKLOG.md)
- `gui.cpp` 的 toast 缺 `CRITICAL_SECTION toast_lock` 也應修正

### 是否應保留 gui_panels.cpp？

**否。應立即刪除。**
- 1547 行 100% dead code，無任何 Call Site
- `gui_data.h` 也應刪除 (僅被 gui_panels.cpp include)
- 保留只會誤導未來開發者

### 是否應刪除 gui_panels.cpp？

**是。立即刪除。**
- Source Evidence: build.bat 無任何 `gui_panels` 引用
- Source Evidence: 無任何檔案 `#include "gui_panels"` 或 `#include "gui_data.h"` (除了 gui_panels.cpp 自己)
- Source Evidence: gui_panels.cpp 無 `gui_run()`，無法獨立存在

### 是否建議延後 GUI 重構到最後？

**是。延後到所有 Bug 修完之後。**

重構順序建議：
1. **P0 Bug 修正**：B1-B14 中影響正確性的項目 (特別是 B1, B2, B3, B9, B11)
2. **刪除 dead code**：gui_panels.cpp, gui_data.h
3. **修復 toast_lock 缺失**：在 gui.cpp 匿名 struct 加入 `CRITICAL_SECTION toast_lock`
4. **選擇性內部重構**：如果時間允許，從 gui.cpp 內部進行區域性重構 (如提取 common worker 邏輯)
5. **大型模組化**：如果真有需要，從頭設計新架構，不要復活 gui_panels.cpp

### Source Code Evidence Summary

所有結論僅引用以下 source code：

| 結論 | 證據 |
|------|------|
| gui.cpp 是唯一編譯的 GUI 檔 | build.bat:33 `-c src/gui.cpp` |
| gui_panels.cpp 不編譯 | build.bat 全文無 `gui_panels` |
| gui_panels.cpp 是 orphan | 無任何檔案 include 或連結 |
| 兩份 g_gui | gui.cpp:138 `static struct` vs gui_panels.cpp:15 `struct GuiState` |
| toast 無 lock | gui.cpp line 61-138 無 `toast_lock` 欄位 |
| toast_push 無鎖 | gui.cpp:147 `static void toast_push` 無 EnterCriticalSection |
| gui_run() 僅在 gui.cpp | gui.cpp:1677, gui.h:2 |
| gui.cpp 不 include gui_data.h | gui.cpp:1-16 include 列表 |
| gui_data.h 僅被 gui_panels.cpp include | grep 結果：唯一匹配為 gui_panels.cpp:12 |
