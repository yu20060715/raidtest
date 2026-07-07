# GUI Audit — gui.cpp (1777 lines)

## Navigation Tree

```
gui_run() [main loop]
 └── RenderMainUI() [called every frame]
      ├── Menu Bar
      │   ├── File
      │   │   ├── Refresh           → start_worker(W_REFRESH)
      │   │   ├── Settings          → g_gui.show_settings = true
      │   │   ├── Export Diagnostic → start_worker(W_EXPORT)
      │   │   └── Exit              → PostQuitMessage(0)
      │   ├── Actions
      │   │   ├── Scan              → start_worker(W_SCAN)
      │   │   ├── Create            → start_worker(W_CREATE)        [disabled if !can_create]
      │   │   ├── Mirror            → start_worker(W_MIRROR)        [disabled if !can_create]
      │   │   ├── Restore           → g_gui.show_restore_wizard = true
      │   │   ├── Mount             → start_worker(W_MOUNT)         [disabled if !can_mount]
      │   │   ├── Unmount           → start_worker(W_UNMOUNT)       [disabled if !can_umount]
      │   │   ├── Destroy           → g_gui.show_confirm_destroy = true [disabled if !can_destroy]
      │   │   ├── Benchmark         → g_gui.show_bench = true       [disabled if !can_bench]
      │   │   └── Rebuild           → g_gui.show_rebuild_wizard = true
      │   └── View
      │       ├── Settings          → g_gui.show_settings = true
      │       └── About             → g_gui.show_about = true
      │
      ├── ShowModeTabs()
      │   ├── [Beginner]            → g_gui.mode = MODE_BEGINNER
      │   ├── [Advanced]            → g_gui.mode = MODE_ADVANCED
      │   └── [Developer]           → g_gui.mode = MODE_DEVELOPER
      │
      ├── ShowToolbar()
      │   ├── Scan     → start_worker(W_SCAN)          [disabled if busy]
      │   ├── Create   → start_worker(W_CREATE)        [disabled if !create_ok]
      │   ├── Mirror   → start_worker(W_MIRROR)        [disabled if !create_ok]
      │   ├── Mount    → start_worker(W_MOUNT)         [disabled if !mountable]
      │   ├── Unmount  → start_worker(W_UNMOUNT)       [disabled if !unmountable]
      │   ├── Destroy  → g_gui.show_confirm_destroy=true [disabled if !destroyable]
      │   └── Bench    → g_gui.show_bench=true         [disabled if !benchable]
      │
      ├── [MODE_BEGINNER]
      │   └── ShowBeginnerPanel()
      │       ├── Quick Setup      → start_worker(W_QUICK_SETUP)     [disabled if busy]
      │       ├── Scan Disks       → start_worker(W_SCAN)            [disabled if busy]
      │       ├── Restore Volume   → start_worker(W_LOAD_CONFIG)     [disabled if busy]
      │       ├── Health Check     → start_worker(W_CHECK)           [disabled if busy]
      │       ├── Unmount          → start_worker(W_UNMOUNT)         [if mounted & !busy]
      │       └── Benchmark        → g_gui.show_bench=true           [if mounted & !busy]
      │   └── ShowHealthDashboard()  [separate window]
      │
      ├── [MODE_ADVANCED | MODE_DEVELOPER]
      │   ├── Left panel:
      │   │   ├── ShowDiskList()
      │   │   │   └── [checkbox per row] → device_select(&idx, 1)
      │   │   └── ShowPlanner()
      │   └── Right panel:
      │       ├── ShowVolumeInfo()
      │       └── [MODE_DEVELOPER] ? ShowPerformanceDashboard() : ShowPerformancePanel()
      │
      ├── ShowEventLog()  [bottom, all modes]
      ├── ShowStatusBar() [bottom, all modes]
      │
      ├── ShowWelcomeWizard()   [first run only]
      ├── ShowRestoreWizard()   [if g_gui.show_restore_wizard]
      ├── ShowRebuildWizard()   [if g_gui.show_rebuild_wizard]
      ├── render_toasts()       [overlay, always]
      ├── ShowSettings()        [if g_gui.show_settings]
      ├── ShowAbout()           [if g_gui.show_about]
      ├── ShowConfirmDestroy()  [if g_gui.show_confirm_destroy]
      ├── ShowConfirmPurge()    [if g_gui.show_confirm_purge — NEVER SET from GUI]
      ├── ShowBenchmark()       [if g_gui.show_bench]
      └── ShowExportDialog()    [if g_gui.show_export_dialog]
```

---

## Panel-by-Panel Analysis

### 1. ShowModeTabs() — Mode Selector

| Property | Value |
|----------|-------|
| **Line** | 1051–1071 |
| **Called from** | `RenderMainUI()`:1602 (unconditional) |
| **Window title** | `##modetabs` (hidden title bar) |
| **Controls** | 3 buttons: Beginner / Advanced / Developer |
| **User reaches via** | Always visible below menu bar |
| **State** | **Complete, reachable** |
| **Notes** | Active tab is highlighted with blue accent. Uses `ImGuiWindowFlags_NoScrollbar`. |

---

### 2. ShowToolbar() — Toolbar

| Property | Value |
|----------|-------|
| **Line** | 1073–1126 |
| **Called from** | `RenderMainUI()`:1609 (unconditional, inside `##tb` child) |
| **Window title** | `Toolbar` (hidden title bar) |
| **Controls** | 7 buttons: Scan, Create, Mirror, Mount, Unmount, Destroy, Bench |
| **User reaches via** | Always visible below mode tabs |
| **State** | **Complete, reachable** |
| **Button wiring** | |
| | Scan → `start_worker(W_SCAN, NULL)` |
| | Create → builds arg string → `start_worker(W_CREATE, p)` |
| | Mirror → `start_worker(W_MIRROR, NULL)` |
| | Mount → `start_worker(W_MOUNT, m)` |
| | Unmount → `start_worker(W_UNMOUNT, NULL)` |
| | Destroy → `g_gui.show_confirm_destroy = true` |
| | Bench → `g_gui.show_bench = true`; zeroes bench fields; `start_worker(W_BENCHFS, m)` |
| **Dead buttons?** | None. All 7 buttons are wired. |
| **Notes** | Buttons auto-disable based on state. Disabled state uses `ImGui::BeginDisabled()` / `EndDisabled()` but the Create and Mirror buttons use a non-nested pattern at lines 1088–1096 (`if (!x) BeginDisabled()`, then `if (!x) EndDisabled()`) that is fragile — if an early return happens between them, the disabled state leaks. |

---

### 3. ShowBeginnerPanel() — Quick Actions (Beginner Mode)

| Property | Value |
|----------|-------|
| **Line** | 1497–1533 |
| **Called from** | `RenderMainUI()`:1617 (only in `MODE_BEGINNER`) |
| **Window title** | `Quick Actions` |
| **Controls** | 6 buttons: Quick Setup, Scan Disks, Restore Volume, Health Check, Unmount, Benchmark |
| **User reaches via** | Click "Beginner" mode tab |
| **State** | **Complete, reachable** |
| **Button wiring** | |
| | Quick Setup → `start_worker(W_QUICK_SETUP, NULL)` |
| | Scan Disks → `start_worker(W_SCAN, NULL)` |
| | Restore Volume → `start_worker(W_LOAD_CONFIG, NULL)` |
| | Health Check → `start_worker(W_CHECK, NULL)` |
| | Unmount → `start_worker(W_UNMOUNT, NULL)` [conditional on `g_gui.vol_info.mounted`] |
| | Benchmark → `g_gui.show_bench = true`; zeroes fields; `start_worker(W_BENCHFS, m)` [conditional on `g_gui.vol_info.mounted`] |
| **Sub-panels embedded** | Calls `ShowHealthDashboard()` at line 1532 (after own `ImGui::End()`) |
| **Notes** | Quick Setup uses a wizard-like multi-step in the worker thread. Restore Volume calls `W_LOAD_CONFIG` (JSON config restore), NOT the superblock restore. The Restore Wizard dialog is a separate entry point that offers both options. |

---

### 4. ShowHealthDashboard() — Health Dashboard

| Property | Value |
|----------|-------|
| **Line** | 880–923 |
| **Called from** | `ShowBeginnerPanel()`:1532 (only in Beginner mode) |
| **Window title** | `Health Dashboard` |
| **Controls** | "Run Full Check" button (only if mounted + state >= 2) |
| **User reaches via** | Only in Beginner mode. The panel is rendered as a SEPARATE window after Quick Actions ends. |
| **State** | **Complete for Beginner mode, but absent from Advanced/Developer mode** |
| **Notes** | Shows disk health cards in a 4-column grid. Each card shows model, status (Online/Offline), capacity, speed. Shows "Temp: N/A  SMART: N/A" — temperature and SMART are placeholders. The "Run Full Check" button → `start_worker(W_CHECK, NULL)`. |

---

### 5. ShowDiskList() — Physical Disks Table

| Property | Value |
|----------|-------|
| **Line** | 1128–1191 |
| **Called from** | `RenderMainUI()`:1621 (only in Advanced/Developer mode) |
| **Window title** | `Physical Disks` |
| **Controls** | 10-column `ImGuiTable`, checkbox per row for selection |
| **User reaches via** | Click "Advanced" or "Developer" mode tab |
| **State** | **Complete, reachable** |
| **Columns** | Model, ID, Serial, Type, Bus, Size, Speed, Status, RAID, Use |
| **Selection mechanism** | `g_gui.disk_checked[]` array (64 entries), checkbox column "Use" |
| **Wiring** | Checkbox → `device_select(&idx, 1)` then `refresh_ui_model()` |
| **Notes** | Limited to 64 disk rows. Infinite scroll not supported (no virtual scrolling). The checkbox column increments `g_gui.selected_count` and populates `g_gui.selected_disks[]` for downstream use by Planner and Create. |

---

### 6. ShowPlanner() — Capacity Planner

| Property | Value |
|----------|-------|
| **Line** | 1193–1232 |
| **Called from** | `RenderMainUI()`:1622 (only in Advanced/Developer mode, immediately after ShowDiskList in the same child window) |
| **Window title** | `Planner` |
| **Controls** | Read-only display of capacity estimates |
| **User reaches via** | Advanced or Developer mode → select 2+ disks |
| **State** | **Complete, reachable** |
| **Data shown** | Selected disks count, total raw capacity, RAID0/1/10 capacities, efficiency percentages |
| **Notes** | Calls `planner_calculate()` with selected disks. Shows "Select 2+ disks" when fewer selected. The planner display is informational only — no interaction. |

---

### 7. ShowVolumeInfo() — Volume Information

| Property | Value |
|----------|-------|
| **Line** | 1234–1291 |
| **Called from** | `RenderMainUI()`:1626 (only in Advanced/Developer mode, right panel) |
| **Window title** | `Volume Info` |
| **Controls** | Read-only display (2-column layout) |
| **User reaches via** | Advanced or Developer mode tab |
| **State** | **Complete, reachable** |
| **Data shown** | State, RAID level, disk count, capacity, used %, mounted status, cache status, bytes written/read, UUID, generation, uptime, health |
| **Notes** | Dual-sourced: uses both `UI_VOLUME_INFO` and `UI_HEALTH_SUMMARY`. Capacity used % computed as `bytes_written / virtual_capacity_bytes` — this is misleading because `bytes_written` is the total bytes written to the volume, not "used space". |

---

### 8. ShowPerformancePanel() — Simple Performance Panel (Advanced mode)

| Property | Value |
|----------|-------|
| **Line** | 1293–1331 |
| **Called from** | `RenderMainUI()`:1630 (only in Advanced mode — `else` branch of `if (g_gui.mode == MODE_DEVELOPER)`) |
| **Window title** | `Performance` |
| **Controls** | Read-only text display, capacity progress bar |
| **User reaches via** | Advanced mode tab |
| **State** | **Complete, reachable** |
| **Data shown** | Read MB/s, Write MB/s, Avg Read Latency, Avg Write Latency, IOPS (R/W), Queue Depth, Capacity %, Volume Health |
| **Notes** | Calls `profiler_update_rates()` each frame. This panel is the simpler text-only variant — no plots. The capacity percentage uses the same misleading `bytes_written / capacity` formula. |

---

### 9. ShowPerformanceDashboard() — Full Performance Dashboard (Developer mode)

| Property | Value |
|----------|-------|
| **Line** | 925–979 |
| **Called from** | `RenderMainUI()`:1628 (only in Developer mode) |
| **Window title** | `Performance Dashboard` |
| **Controls** | Read-only text + `ImGui::PlotLines` graphs for throughput and latency |
| **User reaches via** | Developer mode tab |
| **State** | **Partially complete, reachable** |
| **Data shown** | R/W throughput plots (120-sample rolling window), IOPS text, Latency plot (120-sample), Cache hit % (shows "N/A") |
| **Notes** | Uses `calloc` for plot data arrays — frees them before exit. Cache hit percentage is hardcoded as "N/A" (line 970: `ImGui::Text("Cache: N/A")`) even though `ram_cache.c` tracks `hit_count`/`miss_count`. The `gui.cpp` performance history struct has a `cache_hit_pct` field (line 61) but it's set to a nonsensical formula at line 939-940 (`p->avg_iops_read / (p->avg_iops_read + p->avg_iops_write)` — this is IOPS ratio, not cache hit rate). |

---

### 10. ShowEventLog() — Event Log

| Property | Value |
|----------|-------|
| **Line** | 1333–1355 |
| **Called from** | `RenderMainUI()`:1634 (unconditional) |
| **Window title** | `Event Log` |
| **Controls** | Scrollable text display |
| **User reaches via** | Always visible at bottom of main window |
| **State** | **Complete, reachable** |
| **Features** | Color-coded entries (ERROR=red, WARN=yellow, OK=green, INFO=blue, default=white). Auto-scrolls to bottom. 500-line ring buffer. |
| **Notes** | Locking: `EnterCriticalSection(&g_gui.log_lock)` guards the log read. |

---

### 11. ShowStatusBar() — Status Bar

| Property | Value |
|----------|-------|
| **Line** | 1357–1394 |
| **Called from** | `RenderMainUI()`:1635 (unconditional) |
| **Window title** | `StatusBar` (hidden title bar) |
| **Controls** | Status text, worker progress bar, cancel button "X", state indicator, version text |
| **User reaches via** | Always visible at very bottom |
| **State** | **Complete, reachable** |
| **Features** | When worker is running: shows progress text + step + ETA + cancel button. When idle: shows state string + version. |
| **Notes** | The ETA calculation uses elapsed time and progress fraction — reasonably accurate. Cancel button uses `cancel_worker()`. |

---

### 12. ShowWelcomeWizard() — Welcome Dialog

| Property | Value |
|----------|-------|
| **Line** | 1006–1049 |
| **Called from** | `RenderMainUI()`:1636 (unconditional — early return if `!g_gui.show_welcome`) |
| **Window** | `ImGui::OpenPopup("Welcome to RAIDTEST")` |
| **User reaches via** | First run (`g_gui.settings.first_run == true`), set at line 1730 |
| **Controls** | 3 buttons + WinFsp detection text |
| **Button wiring** | |
| | Quick Setup (Recommended) → `start_worker(W_WIZARD_CREATE, NULL)` |
| | Explore Beginner Mode → `start_worker(W_WIZARD_SCAN, NULL)` |
| | Don't show this again → sets `first_run = false`, saves config |
| **State** | **Complete, reachable** |
| **Notes** | Detects WinFsp presence via `GetModuleHandleA("winfsp-x64.dll")`. Shows detection status in dialog. The Wizard Create path (`W_WIZARD_CREATE`) is separate from the toolbar Quick Setup path (`W_QUICK_SETUP`) — they diverge at line 637 vs 465 in the worker thread. |

---

### 13. ShowRestoreWizard() — Restore Volume Dialog

| Property | Value |
|----------|-------|
| **Line** | 1535–1560 |
| **Called from** | `RenderMainUI()`:1637 (unconditional — early return if `!g_gui.show_restore_wizard`) |
| **Window** | `ImGui::OpenPopup("Restore Volume")` |
| **User reaches via** | Actions → Restore in menu bar (line 1661 sets `g_gui.show_restore_wizard = true`) |
| **Controls** | 3 buttons |
| **Button wiring** | |
| | From Superblock (auto-detect) → `start_worker(W_LOAD_SUPERBLOCK, NULL)` |
| | From Saved Config → `start_worker(W_LOAD_CONFIG, NULL)` |
| | Cancel → closes dialog |
| **State** | **Complete, reachable** |
| **Notes** | Note: The Beginner panel's "Restore Volume" button calls `W_LOAD_CONFIG` directly (line 1508), bypassing this dialog. This dialog is only reachable through the menu bar. |

---

### 14. ShowRebuildWizard() — Rebuild RAID Dialog

| Property | Value |
|----------|-------|
| **Line** | 1562–1591 |
| **Called from** | `RenderMainUI()`:1638 (unconditional — early return if `!g_gui.show_rebuild_wizard`) |
| **Window** | `ImGui::OpenPopup("Rebuild RAID")` |
| **User reaches via** | Actions → Rebuild in menu bar (line 1676) |
| **Controls** | Failed disk index display, replacement disk ID input, pool size input, Start Rebuild, Cancel |
| **Button wiring** | Start Rebuild → `start_worker(W_REBUILD, NULL)` |
| **State** | **Complete, reachable** |
| **Notes** | The rebuild parameters are set via `ImGui::InputInt` bound to `g_gui.rebuild_replacement_id` and `g_gui.rebuild_pool_mb`. Pool size defaults to and enforces minimum 1024 MB. However: the **failed disk index** (`g_gui.rebuild_failed_idx`) is never set by any GUI element — it would need to be populated programmatically. |

---

### 15. ShowSettings() — Settings Dialog

| Property | Value |
|----------|-------|
| **Line** | 843–878 |
| **Called from** | `RenderMainUI()`:1640 (conditional on `g_gui.show_settings`) |
| **Window** | `ImGui::Begin("Settings", ...)` with `AlwaysAutoResize` |
| **User reaches via** | File → Settings or View → Settings in menu bar (lines 1649, 1680) |
| **Controls** | Drive letter input, cache size input (combo), theme dropdown (Dark/Light), auto-restore checkbox, auto-mount checkbox, Save Settings, Cancel |
| **Button wiring** | Save Settings → `config_save(s)` |
| **State** | **Complete, reachable** |
| **Notes** | Works on a COPY of the config (`g_gui.settings`) — changes are only persisted when "Save Settings" is clicked. The cache size uses `ImGui::InputInt` with step 256, not a slider or combo. The mount letter input is limited to 2 chars (uppercase letters only). Auto-restore and auto-mount are checkboxes. |

---

### 16. ShowAbout() — About Dialog

| Property | Value |
|----------|-------|
| **Line** | 981–1004 |
| **Called from** | `RenderMainUI()`:1641 (conditional on `g_gui.show_about`) |
| **Window** | `ImGui::Begin("About RAIDTEST", ...)` with `AlwaysAutoResize` |
| **User reaches via** | View → About in menu bar (line 1681) |
| **Controls** | Read-only text display |
| **State** | **Complete, reachable** |
| **Notes** | Shows version, build date/time, compiler info, architecture, library versions (Dear ImGui 1.91, WinFsp 2.1, DirectX 11, MinGW-w64), license. No close button — relies on clicking outside or pressing Escape. |

---

### 17. ShowConfirmDestroy() — Destroy Confirmation

| Property | Value |
|----------|-------|
| **Line** | 1396–1417 |
| **Called from** | `RenderMainUI()`:1642 (conditional on `g_gui.show_confirm_destroy`) |
| **Window** | `ImGui::OpenPopup("Confirm Destroy")` |
| **User reaches via** | Click Destroy in toolbar (line 1114), or Actions → Destroy in menu (line 1667) |
| **Controls** | "Yes, Destroy" → `start_worker(W_DESTROY, NULL)`, "Cancel" → closes |
| **State** | **Complete, reachable** |
| **Notes** | Modal dialog with explicit warning: "This will delete ALL data" and "CANNOT be undone". |

---

### 18. ShowConfirmPurge() — Purge Confirmation

| Property | Value |
|----------|-------|
| **Line** | 1419–1439 |
| **Called from** | `RenderMainUI()`:1643 (conditional on `g_gui.show_confirm_purge`) |
| **Window** | `ImGui::OpenPopup("Confirm Purge")` |
| **User reaches via** | **UNREACHABLE from GUI** — no button or menu item sets `g_gui.show_confirm_purge = true` |
| **Controls** | "Yes, Purge" → `start_worker(W_PURGE, NULL)`, "Cancel" → closes |
| **State** | **DEAD CODE — unreachable from GUI** |
| **Notes** | The purge action is only available through the CLI (`cmd_handler.c` "purge" command). The GUI has no button or menu item that would trigger this dialog. Could only be reached via the Developer Console (which is itself dead code). |

---

### 19. ShowBenchmark() — Benchmark Results Dialog

| Property | Value |
|----------|-------|
| **Line** | 1441–1474 |
| **Called from** | `RenderMainUI()`:1644 (unconditional — early return if `!g_gui.show_bench`) |
| **Window** | `ImGui::OpenPopup("Benchmark Results")` |
| **User reaches via** | Toolbar Bench button (line 1118), Beginner Benchmark button (line 1515-1519), Actions → Benchmark menu (line 1670-1675) |
| **Controls** | If done: results text, "Run Again" → `start_worker(W_BENCHFS, m)`, "Close". If in progress: progress bar, "Cancel". |
| **State** | **Complete, reachable** |
| **Notes** | Shows benchmark results (read MB/s, write MB/s, latency ms) after completion. The bench worker stores results in `g_gui.bench_read_mbs[]` etc. at lines 353-356. |

---

### 20. ShowExportDialog() — Export Diagnostic Dialog

| Property | Value |
|----------|-------|
| **Line** | 1476–1495 |
| **Called from** | `RenderMainUI()`:1645 (unconditional — early return if `!g_gui.show_export_dialog`) |
| **Window** | `ImGui::OpenPopup("Export Diagnostic")` |
| **User reaches via** | File → Export Diagnostic in menu bar (line 1651) |
| **Controls** | If done: result path text, "Close". If in progress: progress bar. |
| **State** | **Complete, reachable** |
| **Notes** | The export is done in the worker thread (`W_EXPORT` case, lines 364-453). Creates a timestamped folder with metadata.txt, event.log, system.txt. Uses PowerShell `Compress-Archive` for ZIP creation (requires PowerShell availability). |

---

### 21. render_toasts() — Toast Notifications

| Property | Value |
|----------|-------|
| **Line** | 178–206 |
| **Called from** | `RenderMainUI()`:1639 (unconditional) |
| **Window** | Uses `ImDrawList` (foreground overlay) — not an ImGui window |
| **Controls** | Read-only display |
| **User reaches via** | Automatically when events fire (via `toast_push()` called from `event_cb`) |
| **State** | **Complete, reachable** |
| **Notes** | Supports up to MAX_TOASTS (8) simultaneous toasts. Color-coded by type (OK=green, WARN=yellow, ERROR=red, INFO=blue). Auto-expires after TOAST_DURATION (5.0 seconds). Uses `memmove` for removal. Hardcoded width calculation based on 1280px window. |

---

### 22. ShowWelcomeWizard() — Welcome Dialog *(duplicated in table above for completeness)*

Also see entry #12.

---

## Summary of Issues

### Unused / Dead Code

| Panel | Lines | Problem |
|-------|-------|---------|
| `ShowConfirmPurge()` | 1419–1439 | **DEAD CODE** — no GUI element sets `g_gui.show_confirm_purge = true`. Purge is CLI-only. |
| Console fields (`console_input` etc.) | 132–138 | **7 dead struct fields** (~1.5 KB) for a developer console that was planned but never implemented. No function renders or reads them. |
| Developer Console window | — | No function creates a developer console panel despite the mode tab existing. The `MODE_DEVELOPER` tab exists but there is no console UI to type commands into — only `ShowPerformanceDashboard()` is different from Advanced mode. |

### Incomplete Panels

| Panel | Issue |
|-------|-------|
| `ShowPerformanceDashboard()` | Cache hit % shows "N/A" (line 970) even though `ram_cache.c` tracks `hit_count`/`miss_count`. The `cache_hit_pct` field is computed from a nonsensical IOPS formula (`avg_iops_read / (avg_iops_read + avg_iops_write)`). |
| `ShowHealthDashboard()` | Temperature and SMART fields show "N/A" (line 917). These are permanent placeholders. |
| Developer Console | **Entirely missing.** The `MODE_DEVELOPER` tab exists, the console state fields exist in `g_gui`, and `MODE_DEVELOPER` is supposed to show "a CLI console and diagnostics" (per welcome text line 1028), but no console UI is rendered. Developer mode is identical to Advanced mode except it shows `ShowPerformanceDashboard()` instead of `ShowPerformancePanel()`. |
| `ShowVolumeInfo()` | "Used" percentage is computed as `bytes_written / virtual_capacity_bytes` — this is not "used space" but total bytes written over the volume's lifetime (could exceed 100% due to repeated writes). |
| `ShowRebuildWizard()` | `g_gui.rebuild_failed_idx` is never set by any GUI code. The wizard always shows "Failed disk index: 0" unless set programmatically. |
| `ShowExportDialog()` | ZIP creation uses `system("powershell ... Compress-Archive ...")` — fragile (requires PowerShell, path may contain spaces or special chars). |

### Panels That Should Be Merged

| Panels | Reason |
|--------|--------|
| `ShowPerformanceDashboard()` + `ShowPerformancePanel()` | Both show similar performance data. Dashboard has plots (Developer mode), Panel is text-only (Advanced mode). Could be unified with a "Show plots" toggle. |
| `ShowVolumeInfo()` + `ShowHealthDashboard()` | Both show health status with overlapping information. Health info is duplicated between them. |
| `ShowPlanner()` + `ShowDiskList()` | Planner is already rendered directly below Disk List in the same child window. They are logically related (planner uses selected disks). Could be merged into a single "Storage" panel with tabs or sections. |

### Panels That Should Be Split

| Panel | Lines | Reason |
|-------|-------|--------|
| `worker_thread()` | 241–674 (433 lines) | Monolithic switch statement with 19 cases. Each case could be its own function. The W_QUICK_SETUP case alone is 50 lines. |
| `gui.cpp` (entire file) | 1777 | Every panel, dialog, utility, and the main loop are in one file. Should be split into: `gui_panels.cpp` (UI panels), `gui_dialogs.cpp` (modals), `gui_worker.cpp` (background thread), `gui_main.cpp` (entry + loop), `gui_render.cpp` (D3D setup). |

### Duplicated Logic / Controls

| Duplication | Occurrences |
|-------------|-------------|
| "Bench" button wiring | **3 times**: toolbar (line 1118-1123), Beginner panel (line 1515-1520), menu bar (line 1670-1675) — same 6 lines of setup duplicated |
| "Unmount" button wiring | **3 times**: toolbar (line 1110), Beginner panel (line 1513), menu bar (line 1665) |
| "Scan" button wiring | **3 times**: toolbar (line 1086), Beginner panel (line 1506), menu bar (line 1657) |
| `g_gui.vol_info` health display | **3 times**: ShowVolumeInfo (lines 1281-1286), ShowHealthDashboard (lines 885-890), ShowPerformancePanel (line 1329) — same "healthy count / total count" pattern |
| cr32-based journal CRC | `test_journal.c` uses manual CRC (line 233) — this duplicates `crc32()` from `crc32.c` |

### State/Reachability Inconsistencies

| Issue | Detail |
|-------|--------|
| Beginner "Restore Volume" bypasses dialog | Beginner panel button directly calls `W_LOAD_CONFIG`. Menu Actions → Restore opens `ShowRestoreWizard()` dialog. Two different paths with different UX. |
| No "Health Check" button in Advanced/Developer | The "Run Full Check" button in `ShowHealthDashboard()` is only visible in Beginner mode. There is no health check button in Advanced/Developer UI — only via menu or toolbar which have no health check button. |
| Purge is GUI-dead | `ShowConfirmPurge()` is fully implemented but unreachable from any GUI element. |
| Create button never re-enabled after pool init | After the user initializes pools, `g_gui.state_value` may not update to reflect `STATE_INITIALIZED` until a refresh happens. The Create button in toolbar might remain disabled if the user doesn't manually refresh. |

### Worker Action Completeness

| WorkerAction | Used by | Reachable? |
|-------------|---------|------------|
| `W_NONE` | — | Internal only |
| `W_SCAN` | Toolbar, Beginner, Menu | Yes |
| `W_CREATE` | Toolbar, Menu | Yes |
| `W_MIRROR` | Toolbar, Menu | Yes |
| `W_MOUNT` | Toolbar, Menu | Yes |
| `W_UNMOUNT` | Toolbar, Beginner, Menu | Yes |
| `W_DESTROY` | Toolbar (via confirm dialog), Menu (via confirm dialog) | Yes |
| `W_REFRESH` | Menu | Yes |
| `W_BENCHFS` | Toolbar, Beginner, Menu (via benchmark dialog) | Yes |
| `W_EXPORT` | Menu | Yes |
| `W_PURGE` | ShowConfirmPurge() | **No** (purge dialog unreachable) |
| `W_QUICK_SETUP` | Beginner Panel | Yes |
| `W_LOAD_SUPERBLOCK` | Restore Wizard | Yes |
| `W_LOAD_CONFIG` | Beginner Panel, Restore Wizard | Yes |
| `W_REBUILD` | Rebuild Wizard | Yes |
| `W_CACHE_ENABLE` | — | **No** — no GUI element triggers this |
| `W_CACHE_DISABLE` | — | **No** — no GUI element triggers this |
| `W_CACHE_FLUSH` | — | **No** — no GUI element triggers this |
| `W_CHECK` | Beginner Panel | Yes |
| `W_WIZARD_SCAN` | Welcome Wizard | Yes |
| `W_WIZARD_CREATE` | Welcome Wizard | Yes |

**3 unreachable worker actions** (`W_CACHE_ENABLE`, `W_CACHE_DISABLE`, `W_CACHE_FLUSH`) — cache control is not exposed in the GUI at all. Users cannot enable/disable/flush the write-back cache from the GUI.

---

## Final Count

| Category | Count | Details |
|----------|-------|---------|
| Total panels/dialogs | 20 | 12 panels + 8 dialogs/popups |
| Complete + reachable | 17 | All panels except those listed below |
| Dead code (GUI-unreachable) | 1 | `ShowConfirmPurge()` |
| Incomplete | 5 | Developer Console (missing), Cache hit % (N/A), Rebuild failed_idx (unset), Volume Info used% (wrong formula), Export ZIP (fragile) |
| Unreachable worker actions | 3 | W_CACHE_ENABLE, W_CACHE_DISABLE, W_CACHE_FLUSH |
| Dead struct fields | 7 | Console I/O ring buffer fields |
| Should-be-merged | 3 | Performance panels, Volume Info + Health Dashboard, Planner + Disk List |
| Should-be-split | 2 | `worker_thread()` (433 lines), entire `gui.cpp` (1777 lines) |
| Duplicated button wiring | 3 | Bench, Unmount, Scan each wired 3 times identically |
