# RAIDTEST GUI 優化（方案 B：Dear ImGui 淺色固定內嵌版）

## 最終 UI Layout

```
┌──────────────────────────────────────────────────┐
│ [File] [Actions] [View]             [B|A|D tabs]  │
├──────────────────────────────────────────────────┤
│ [Scan][Create][Mirror][Mount][Unmount][Destroy]    │
├───────────────────────┬──────────────────────────┤
│ ID  Model       Use  │ State: INITIALIZED        │
│  0  SKHynix..  [☐]  │ RAID:  RAID0              │
│  1  Acer_SSD.. [☐]  │ Disks: 2                  │
│  2  CT1000P3.. [☐]  │ Capacity: 1024 GB         │
│  3  CT500MX5.. [☐]  │ Cache: ON (512 MB)        │
│                       │ Health: 2/2 healthy       │
│                       │ Planner: R0=1024 R1=512   │
├───────────────────────┴──────────────────────────┤
│ Event Log (固定高度 170px，無邊框)                │
├──────────────────────────────────────────────────┤
│ Status: Ready                     RAIDTEST v1.0   │
└──────────────────────────────────────────────────┘
```

## 已完成

- [x] 清除根目錄多餘 exe 檔案
- [x] 建立本進度檔案
- [x] `gui_run()` — 固定視窗 1280x800，非最大化（SW_SHOWNORMAL）
- [x] `ApplyTheme()` — 淺色主題設為預設（SetupLightTheme）
- [x] `RenderMainUI()` — 重排 layout，所有面板內嵌
- [x] `ShowVolumeInfoContent()` — 移除獨立 Begin/End，改為內嵌區塊
- [x] `ShowEventLogContent(height)` — 移除獨立 Begin/End，改為固定高度參數
- [x] `ShowHealthSummary()` — 取代舊 Health Dashboard，小巧卡片
- [x] `ShowPlannerContent()` — 取代舊 Planner，緊湊兩列
- [x] `ShowPerformanceContent()` — 取代舊 PerformancePanel，單行
- [x] `ShowBeginnerContent()` — 取代舊 BeginnerPanel，只留按鈕
- [x] `ShowSimulationControls()` — 移除獨立 Begin/End
- [x] `ShowModeTabs()` — 保留現狀（已改過）
- [x] Menu Bar — 所有模式開放 File/Actions/View
- [x] 移除未使用函式（ShowPerformanceDashboard、SetupDarkTheme）
- [x] 編譯測試 — 通過（僅 imgui 內部警告）

## 修改範圍限制

- 只改 `src/gui.cpp`
- 不動任何後端 RAID 核心（`raid_service.h/c`, `stripe_engine.*`, `mirror_engine.*`, `volume_manager.*` 等）
- 不動 `main.c`, `cmd_handler.c`, 任何 `.h` 檔案
