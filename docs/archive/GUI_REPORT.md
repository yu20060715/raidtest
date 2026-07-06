# GUI Review Report

Based on source code at `C:\Users\Yu\Desktop\raidv3\src\`.

Reviewed files: `gui.cpp`, `gui.h`, `ui_model.c`, `ui_model.h`, `event_bus.c`, `event_bus.h`, `device_manager.c`, `raid_service.c`, `volume_manager.c`, `planner_engine.c`, `cmd_handler.h`.

---

## Critical (P0)

### P0-1: `event_bus_unsubscribe` called but does not exist
**File**: `gui.cpp:1246-1253`

`gui_run()` calls `event_bus_unsubscribe()` for 8 event types during cleanup. This function is **never declared or defined** anywhere. It does not exist in `event_bus.h` or `event_bus.c`.

**Impact**: `gui.cpp` cannot compile as C++ (g++ rejects it). The main GUI binary (`raidtest_winfsp.exe`) has never been linkable. Only the test suite (which does not include `gui.cpp`) has ever been built.

**Evidence**: `event_bus.h` declares only `event_bus_init`, `event_bus_subscribe`, `event_bus_publish`, `event_type_str`. No `event_bus_unsubscribe`.

### P0-2: "Purge" dialog "Yes, Purge" button does nothing
**File**: `gui.cpp:1000-1006`

```cpp
if (ImGui::Button("Yes, Purge", ImVec2(120, 0))) {
    g_gui.show_confirm_purge = false;
    ImGui::CloseCurrentPopup();
}
```

The button closes the dialog but never calls `start_worker(W_PURGE, NULL)` or `raid_purge()`. The entire Purge flow is dead. A `W_PURGE` action does not even exist in the `WorkerAction` enum.

**Impact**: Users can never purge metadata through the GUI.

### P0-3: "Cache" label in toolbar is actually pool size
**File**: `gui.cpp:624-628`

```cpp
ImGui::TextUnformatted("Cache:");
ImGui::SameLine();
ImGui::SetNextItemWidth(55);
ImGui::InputInt("##cache", &g_gui.cache_mb, 0);
```

The label says "Cache" but the value is passed to `W_CREATE` as the **pool size** argument in `id:mb` format (`gui.cpp:558`):

```cpp
pos += snprintf(p + pos, 255 - (size_t)pos, "%s%d:%d",
    pos ? " " : "", g_gui.selected_disks[i],
    g_gui.cache_mb ? g_gui.cache_mb : 1024);
```

The cache size is actually stored in `g_state.cache.cache_mb` (set from config at init in `raid_service.c:62`). The GUI's "Cache" field has no effect on actual cache size.

**Impact**: Users who set cache size in the GUI will actually set pool file size. If they set e.g. 256 MB expecting a 256 MB cache, they'll get a 256 MB pool (below the 1024 MB minimum enforced by `raid_init_pools`), causing pool creation to fail silently.

### P0-4: Worker thread writes `g_gui.progress_frac` / `g_gui.progress_text` without synchronization
**File**: `gui.cpp` globals, `worker_thread()`, main loop

The worker thread:
```cpp
g_gui.progress_frac = 0.0f;           // line 127
strncpy(g_gui.progress_text, "Scanning disks...", 63);  // line 132
```

The main thread reads these for the progress bar (`gui.cpp:937`):
```cpp
ImGui::ProgressBar(g_gui.progress_frac, ImVec2(120, 16), "");
```

Neither `progress_frac` nor `progress_text` is `volatile` or protected by a critical section. `progress_frac` is 4 bytes (float) — on x64 it is naturally aligned, so torn reads are not a concern, but the compiler may optimize the read into a register and never re-read the updated value.

---

## High (P1)

### P1-1: Event callbacks do not refresh UI model
**File**: `gui.cpp:102-108`

```cpp
static void event_cb(EVENT_TYPE type, const char* data, void* userdata) {
    char buf[MAX_LOG_LINE_LEN];
    snprintf(buf, sizeof(buf), "[%s] %s", t ? t : "EVENT", data ? data : "");
    gui_log(buf);
}
```

Events from `EVENT_DISK_FOUND`, `EVENT_VOLUME_CREATED`, `EVENT_MOUNT`, `EVENT_ERROR`, etc. are only logged. They never call `refresh_ui_model()`. The 1-second periodic refresh in the main loop (`gui.cpp:1225-1228`) is the only mechanism that updates the model after an event. Between the event and the next tick, the UI shows stale data.

### P1-2: `total_capacity_mb` in `UI_DISK_SUMMARY` mixes pool bytes and total bytes
**File**: `ui_model.c:13`

```cpp
uint64_t mb = (d->pool_bytes > 0 ? d->pool_bytes : d->total_bytes) / (1024 * 1024);
```

`pool_bytes` is the configured pool file size (set during init). `total_bytes` is the physical disk capacity. For a pre-init disk, `pool_bytes` is 0, so `total_bytes` is used. For a post-init disk, `pool_bytes` is used. This means the displayed total capacity changes after pool initialization, which is correct but may be confusing (the display shows pool capacity, not disk capacity).

More importantly, the **Planner** panel (`gui.cpp:736`) reads `d->total_bytes` directly:
```cpp
pdisks[i].capacity_bytes = d->total_bytes;
```

This is inconsistent with `ui_get_disk_summary` which uses `pool_bytes` when available. The planner ignores pool sizing and always uses raw disk capacity.

### P1-3: "Export" progress bar depends on worker progress, but W_EXPORT never updates it
**File**: `gui.cpp:263-355`

The `W_EXPORT` worker sets `g_gui.progress_frac = 0.0f` at start and `1.0f` at end, with no intermediate updates. The export dialog shows a progress bar that jumps from 0% to 100% instantly. The progress bar serves no purpose for this action.

### P1-4: Benchmark bypasses all service/manager layers
**File**: `gui.cpp:202-261`

`W_BENCH` opens the mounted drive's root directory directly (`%s\\raidtest_bench.tmp`) with `CreateFileA` and does raw ReadFile/WriteFile with `FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH`. It does not use `raid_bench()`, `device_bench_all_selected()`, or any code path in `bench_io.c`. The result is a filesystem-level benchmark of the WinFsp mount point, not a RAID volume benchmark.

This is architecturally inconsistent but not necessarily a bug — it measures the FUSE + filesystem path, which is a valid end-to-end test. However, results will differ significantly from `bench_io.c`'s direct disk benchmark.

### P1-5: `data_off` in journal recovery could be out of bounds after dynamic buffer change
**File**: `journal.c` (post-fix from earlier session)

The `data_off` field stores an offset into the `raw` buffer. With the dynamic allocation fix, `raw` is allocated based on `file_size.QuadPart`. If `file_size.QuadPart` is very large (e.g., a corrupt journal file), this could allocate excessive memory or fail.

Not directly a GUI issue, but the GUI triggers journal recovery via `raid_create` / `raid_mirror` → `journal_recover_all`.

---

## Medium (P2)

### P2-1: `progress_frac` and `worker_pct` are redundant
**File**: `gui.cpp` globals, worker thread

The `g_gui` struct contains both `progress_frac` (float) and `worker_pct` (volatile LONG). Only `progress_frac` is actually used. `worker_pct` is initialized to 0 at worker start but never written again. Dead field.

### P2-2: `disk_checked[64]` and `selected_disks[8]` arrays are not reset on scan
**File**: `gui.cpp:66-67`

When the user clicks "Scan", the underlying device list is rebuilt (`device_refresh()` calls `disk_scan_free` + `disk_scan_all`), which changes the disk count and ordering. But `g_gui.disk_checked[]` and `g_gui.selected_disks[]` are not cleared. Old indices may reference incorrect disks or be out of bounds after a re-scan.

### P2-3: Volume Info "Used" display formula is incorrect
**File**: `gui.cpp:793`

```cpp
double used_pct = (double)vi->bytes_written / (double)vi->virtual_capacity_bytes * 100.0;
```

`bytes_written` is the **lifetime** total bytes written since volume creation (`vol->bytes_written` in `stripe_engine.c`). This includes overwrites and does not account for deleted/modified files. The progress bar "Used" is a meaningless metric for actual volume usage, but it is displayed as capacity utilization.

### P2-4: Selected disk tracking recalculated every frame in ShowDiskList
**File**: `gui.cpp:661`, `gui.cpp:717-718`

```cpp
g_gui.selected_count = 0;  // Reset each frame
...
if (checked && g_gui.selected_count < 8)
    g_gui.selected_disks[g_gui.selected_count++] = (int)i;
```

`selected_count` and `selected_disks` are rebuilt from the checkbox state every frame. This works but is fragile — any code path that reads `selected_count` before `ShowDiskList` runs will see stale values.

### P2-5: `worker_params` uses fixed 256-byte buffer with no length parameter
**File**: `gui.cpp:54`

```cpp
char worker_params[256];
```

In `start_worker`, it copies with `strncpy(g_gui.worker_params, params, 255)` (line 374). For W_CREATE, the param string is built as:
```cpp
pos += snprintf(p + pos, 255 - (size_t)pos, "%s%d:%d", ...);
```
With up to 4 disks, this is safe. But the limit is not enforced at the format site; it relies on the 255-cap in `snprintf`.

---

## Low (P3)

### P3-1: `APP_VERSION` hardcoded as `"v1.0 RC2"`
**File**: `gui.cpp:25`

The version string is hardcoded. The window title always shows `RAIDTEST v1.0 RC2 – GUI Edition` regardless of the actual build.

### P3-2: About dialog shows `"Git Commit: SCR1"` as placeholder
**File**: `gui.cpp:966`

```cpp
ImGui::Text("Git Commit: %s", "SCR1");
```

Hardcoded placeholder, not an actual git hash.

### P3-3: Status bar state string and g_gui.state_value are duplicated
**File**: `gui.cpp:114`, `gui.cpp:941-942`

`refresh_ui_model()` writes `g_gui.state_value` (line 114-117) but the StatusBar and most Show* panels call `ui_get_state_str()` directly. `g_gui.state_value` is only used for button enable/disable logic. This duplication is harmless but unnecessary — button logic could call `ui_get_state()` directly.

### P3-4: `W_EXPORT` export file path uses `%sraidtest_export` (no backslash)
**File**: `gui.cpp:270`

```cpp
char dir[MAX_PATH];
snprintf(dir, MAX_PATH, "%sraidtest_export", tmpdir);
```

`GetTempPathA` returns a path with a trailing backslash (e.g., `C:\Users\...\AppData\Local\Temp\`), so this produces `C:\Users\...\Temp\raidtest_export` which is correct. But the convention relies on the trailing backslash guaranteed by `GetTempPathA`. If the temp path behavior changes, the path would be malformed.

---

## Dead UI / Stub

### D1: Purge dialog — button does nothing
See P0-2.

### D2: `worker_pct` is never used
See P2-1.

### D3: `event_cb` registers for `EVENT_DISK_FOUND, VOLUME_CREATED, VOLUME_DESTROYED, MOUNT, UNMOUNT, ERROR, METADATA_UPDATED, CACHE_CHANGED` but never for `EVENT_DISK_REMOVED, DISK_BENCHED, VOLUME_LOADED, REBUILD, EXPAND`

These events are logged by `raid_service.c`'s `event_log_callback` to the file-based event log, but the GUI does not subscribe to them. Disk removal (unplug), bench completion, or volume load events are never shown in the GUI event log window.

### D4: `W_REFRESH` action does nothing on its own that the periodic timer doesn't already do
```cpp
case W_REFRESH: {
    refresh_ui_model();
    ...
```
The 1-second timer in the main loop already calls `refresh_ui_model()`. The Refresh button simply triggers the same function immediately. Not dead, but its marginal value is near zero.

---

## Bypass / Integration Problems

### B1: Benchmark bypasses entire RAID path
See P1-4. The bench writes directly to `G:\raidtest_bench.tmp` via `CreateFileA`. It tests the WinFsp mount point performance, not the RAID volume.

### B2: Checkbox-based disk selection bypasses `device_select` for display, but uses it for actual selection
**File**: `gui.cpp:709-713`

```cpp
if (ImGui::Checkbox(cbid, &checked)) {
    g_gui.disk_checked[i] = checked ? 1 : 0;
    if (!g_gui.worker_running) {
        uint32_t idx = (uint32_t)i;
        device_select(&idx, 1);  // Selects just this disk, deselects all others
        refresh_ui_model();
    }
}
```

Each checkbox click calls `device_select(&idx, 1)` which first **deselects all disks** (`disk_scanner.c:130`) and then selects only the clicked one. This means:
- Clicking checkbox on disk 3 → disk 3 selected, disks 0-2 deselected
- But the GUI then rebuilds `selected_count` from the checkbox state (`gui.cpp:717-718`), which may show multiple checkboxes still checked
- Result: the checkboxes and `device_is_selected()` state can be inconsistent if the user clicks checkboxes rapidly without waiting for `refresh_ui_model()`

### B3: Planner reads `d->total_bytes` while Disk Summary reads `pool_bytes`
See P1-2. If pools have been allocated, the Planner shows wrong capacity.

### B4: Worker thread uses main-thread data `g_gui.disk_summary.count` without synchronization
See P0-1 and P0-4. The scan result line uses `g_gui.disk_summary.count` which was populated by `refresh_ui_model()` — which is called in the worker itself. So this specific instance is safe (confirmed by reading the code flow), but sets a dangerous pattern.

---

## Recommendation for Fix Order

1. **P0-1**: Add `event_bus_unsubscribe()` function (event_bus.h + event_bus.c) or remove the cleanup calls from gui.cpp. This blocks GUI compilation.
2. **P0-2**: Implement Purge or remove the button. Simplest: call `start_worker(W_PURGE, NULL)` after adding `W_PURGE` to the enum, or just remove the Purge UI entirely.
3. **P0-3**: Relabel "Cache" to "Pool Size (MB)" in the toolbar, or split cache size and pool size controls.
4. **P0-4**: Add `volatile` qualifier to `progress_frac` and `progress_text` in the struct. Or use `InterlockedExchange` for the float (cast to/from LONG).
5. **P1-1**: Call `refresh_ui_model()` at the end of `event_cb()`.
6. **P1-2**: Planner should use `pool_bytes` when available, consistent with Disk Summary.
7. **P2-2**: Clear `disk_checked` and `selected_count` after Scan.
8. **D3**: Subscribe to remaining event types for complete event log coverage.
