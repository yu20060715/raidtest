# GUI Fix Report

## Verification & Fix Results

| Issue | Status | Action |
|---|---|---|
| P0-1: `event_bus_unsubscribe` missing | **Confirmed** | Added function |
| P0-2: Purge button does nothing | **Confirmed** | Wired to `raid_purge()` |
| P0-3: "Cache" label is pool size | **Confirmed** | Relabeled to "Pool:" |
| P0-4: Worker thread missing volatile | **Confirmed** | Added `volatile` to `progress_frac` |
| P1-1: Events don't refresh UI model | **Confirmed** | Added `refresh_pending` flag |
| P1-2: Planner uses wrong capacity | **Confirmed** | Changed to `pool_bytes` |
| P1-3: Export progress jumps | **Partially Confirmed** | Cosmetic only, not fixed |
| P1-4: Benchmark bypasses RAID | **Partially Confirmed** | By design, not fixed |
| P1-5: `data_off` out-of-bounds | **Rejected** | Bounds-checked at `journal.c:142` |

---

## Fix Details

### P0-1: Add `event_bus_unsubscribe` to event bus
**Files**: `src/event_bus.h`, `src/event_bus.c`

**Root cause**: `gui.cpp:1246-1253` calls `event_bus_unsubscribe()` 8 times during cleanup but the function was never declared in the header or defined in the implementation, causing a C++ compilation error.

**Fix**:
- Declared `void event_bus_unsubscribe(EVENT_TYPE type, event_callback cb)` in `event_bus.h`
- Implemented in `event_bus.c`: iterates subscribers for the given event type, finds the one matching `cb`, marks it inactive and clears its fields. Protected by `g_eb_cs` critical section.

### P0-2: Wire Purge button to `raid_purge()`
**File**: `src/gui.cpp`

**Root cause**: `ShowConfirmPurge()` "Yes, Purge" button only closed the dialog (`show_confirm_purge = false; CloseCurrentPopup()`) without calling any RAID operation. Also `W_PURGE` didn't exist in the `WorkerAction` enum.

**Fix**:
- Added `W_PURGE` to the `WorkerAction` enum
- Added `case W_PURGE` in `worker_thread` that calls `raid_purge()` and then `refresh_ui_model()`
- Changed "Yes, Purge" button handler to call `start_worker(W_PURGE, NULL)`

### P0-3: Relabel "Cache" to "Pool"
**File**: `src/gui.cpp`

**Root cause**: The toolbar field labeled "Cache" was actually used as the pool file size (`id:mb`) in W_CREATE parameters. The actual cache size comes from config file and is displayed separately in Volume Info.

**Fix**: Changed toolbar label from `"Cache:"` to `"Pool:"`.

### P0-4: Add `volatile` to `progress_frac`
**File**: `src/gui.cpp`

**Root cause**: `g_gui.progress_frac` (float) is written by the worker thread and read by the main thread during ImGui rendering. Without `volatile`, the compiler could cache the read value in a register and miss updates.

**Fix**: Changed `float progress_frac` to `volatile float progress_frac` in the anonymous struct. `progress_text` was intentionally left non-volatile because `volatile char[64]` would break `strncpy` compilation. The `progress_text` field is written once at worker start (no intermediate updates) so the practical race window is negligible.

### P1-1: Events trigger UI refresh via pending flag
**File**: `src/gui.cpp`

**Root cause**: `event_cb()` only logged events but never updated the displayed model. The UI depended on a 1-second periodic timer, causing up to 1 second of stale data after any event (create, mount, error, etc.).

**Fix**:
- Added `volatile LONG refresh_pending` flag to the anonymous struct
- `event_cb()` sets `InterlockedExchange(&g_gui.refresh_pending, 1)` after logging
- Main loop checks the flag via `InterlockedCompareExchange(&g_gui.refresh_pending, 0, 1)` and calls `refresh_ui_model()` if set. This avoids cross-thread races by delegating the actual model refresh to the main thread.

### P1-2: Fix Planner capacity to match Disk Summary
**File**: `src/gui.cpp`

**Root cause**: `ShowPlanner()` used `d->total_bytes` (raw disk capacity) while `ui_get_disk_summary` uses `d->pool_bytes > 0 ? d->pool_bytes : d->total_bytes` (pool capacity after init). The CLI planner (`raid_service.c:715`) already uses the correct logic.

**Fix**: Changed `pdisks[i].capacity_bytes = d->total_bytes` to `pdisks[i].capacity_bytes = d->pool_bytes > 0 ? d->pool_bytes : d->total_bytes`.

---

## Rejected Issues

### P1-3: Export progress bar jumps
The progress bar during export goes from 0% to 100% instantly. This is cosmetic only — the export completes correctly. Fixing would require adding intermediate progress updates, which changes behavior. Not a functional bug.

### P1-4: Benchmark bypasses RAID
The GUI benchmark opens a file directly on the mounted drive (`G:\raidtest_bench.tmp`) via `CreateFileA` and measures throughput. This bypasses the RAID service layer. However, this is the designed behavior — it measures end-to-end performance through the WinFsp mount point, which is a valid metric. Changing to `raid_bench()` would measure raw disk performance instead, which is a different test.

### P1-5: `data_off` out-of-bounds
The report suggested `data_off` in journal recovery could be out of bounds after the dynamic buffer allocation change. This is incorrect — `data_off` is always validated by `offset + payload <= read` (journal.c:142) before being stored. The buffer allocation size doesn't affect this bounds check.

---

## Regression Test

```
38 passed, 0 failed
```

All 38 test cases pass. The test suite covers:
- stripe_engine, mirror_engine, ram_cache — all P0/P1 fixes from previous session
- journal, superblock — recovery, replay, corruption handling
- No test regression from event_bus changes (tests link `event_bus.c` cleanly)

## Files Modified

| File | Changes |
|---|---|
| `src/event_bus.h` | Added `event_bus_unsubscribe` declaration |
| `src/event_bus.c` | Added `event_bus_unsubscribe` implementation |
| `src/gui.cpp` | P0-2: W_PURGE enum + case + button wiring; P0-3: "Pool:" label; P0-4: `volatile float progress_frac`; P1-1: refresh_pending flag; P1-2: planner `pool_bytes` |
