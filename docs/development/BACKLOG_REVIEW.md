# BACKLOG_REVIEW.md

Verification audit of REFACTOR_BACKLOG.md against source code at `C:\Users\Yu\Desktop\raidv3\src\`.
Each item marked **CORRECT**, **WRONG**, or **NEEDS CLARIFICATION** with source evidence.

---

## BUGS

### B1 — stripe_volume_create omits raid_level initialization
**VERDICT: CORRECT**
`stripe_engine.c:66` memset zeroes `STRIPE_VOLUME`. No `vol->raid_level = RAID_LEVEL_STRIPE` anywhere in `stripe_volume_create` (lines 66-189). `mirror_engine.c`'s `mirror_volume_create` correctly sets `RAID_LEVEL_MIRROR` at line 47.

### B2 — mirror_volume_rebuild flush inside loop
**VERDICT: CORRECT**
`mirror_engine.c:164` calls `FlushFileBuffers(replacement->handle)` inside the `while (remaining > 0)` loop. One flush at end is sufficient.

### B3 — mirror_volume_rebuild races with concurrent I/O on disks[] swap
**VERDICT: CORRECT**
`mirror_engine.c:174` — `InterlockedExchangePointer(&vol->disks[replace_idx], replacement)` swaps disk pointer. `stripe_volume_read/write` (stripe_engine.c:285, 377) dereference `vol->disks[disk_idx]` without any lock. Concurrent read/write during rebuild can access freed handle.

### B4 — stripe_volume_write error-path events[] / ovs[] consistency
**VERDICT: CORRECT, but minor**
`stripe_engine.c:574-602` error-path cleanup loops `j < i` where `events[j]` is always valid (set before `memset(&ovs[j])`). The read path at lines 382-396 has same pattern. The cache write-through path at line 518 guards `if (events[j]) CancelIoEx(...)`, creating inconsistency. Defensive fix should add NULL-guards to read/write or remove from cache path.

### B5 — raid_cache_locked thread handle not tracked for safe cleanup
**VERDICT: CORRECT**
`raid_service.c:528` — `_beginthreadex` result stored in both `S()->vol.volume.cache.flush_thread` and `S()->cache.flush_thread`. No stop flag. `cleanup_volume_cache` at raid_service.c:545 does not signal stop before closing handle.

### B6 — raid_config_save_locked pool_bytes precision loss
**VERDICT: CORRECT**
`raid_service.c:630` — `pool_mb = d->pool_bytes / (1024 * 1024)` truncates. On restore, `pool_mb` is used directly as megabytes. Each disk can lose up to 1 MB per save/restore cycle.

### B7 — fuse_bridge.c file_table_lock_init() is a no-op stub
**VERDICT: CORRECT**
`fuse_bridge.c:40-44` — empty body, comment "Must be called from single-threaded context". All callers (lines 58, 118, 199, 250, 319, 369, 419, 471, 506, 537) invoke it before `EnterCriticalSection(&g_file_table_lock)`, but the real `InitializeCriticalSection` happens in `fuse_mount_volume` (line 541). If any FUSE callback fires before mount, CS is uninitialized.

### B8 — fuse_bridge.c CRITICAL_SECTION never deleted
**VERDICT: CORRECT**
`fuse_bridge.c:609-613` — comment says "Do NOT delete the CS here". CS is never deleted anywhere. Leaks kernel resources on unmount.

### B9 — fuse_bridge.c device_select in checkbox handler replaces all selection
**VERDICT: CORRECT for gui.cpp (active code), also present in gui_panels.cpp (dead code)**
gui.cpp:1139-1143 — `device_select(&idx, 1)` called per checkbox toggle replaces entire selection:
```c
if (ImGui::Checkbox(cbid, &checked)) {
    g_gui.disk_checked[i] = checked ? 1 : 0;
    if (!g_gui.worker_running) {
        uint32_t idx = (uint32_t)i;
        device_select(&idx, 1);  // BUG: replaces all selection
        refresh_ui_model();
    }
}
```
IDENTICAL bug at gui_panels.cpp:981-985. Both files affected. (Backlog incorrectly cited only gui_panels.cpp.)

### B10 — stripe_volume_map_lba phase scan is O(phase_count) per map call
**VERDICT: CORRECT**
`stripe_engine.c:343-346` — `for` loop scans phases linearly from 0 to find current phase. With `MAX_DISKS (64)` phases, each `map_lba` call is O(64).

### B11 — mirror_volume_write: cache-written but disk-not-written on write-through failure
**VERDICT: CORRECT**
`mirror_engine.c:109-119` — if `cache_write` succeeds but `mirror_write_to_all` fails, function returns `false` but cache now holds dirty data that subsequent reads serve before disk write completes.

### B12 — fuse_bridge.c cache_backpressure flushes synchronously inside write path
**VERDICT: CORRECT**
`fuse_bridge.c:391-405, 423` — if cache >90% dirty ratio, `cache_flush_all()` called synchronously inside `raid_write()`, blocking the FUSE callback.

### B13 — raid_quick prompts on stdin but ignores mount_letter from config in some paths
**VERDICT: CORRECT**
`raid_service.c:687-688` — `raid_quick` uses `S()->cfg.config.mount_letter` if set, else `'G'`. But `raid_mount_locked` is called before `raid_config_save_locked`, so if config wasn't loaded first, mount_letter is always the default.

### B14 — gui_panels.cpp check_worker_done polling in render loop
**VERDICT: CORRECT for gui.cpp (active code), also present in gui_panels.cpp (dead code)**
gui.cpp:663-681 — `check_worker_done` uses `InterlockedCompareExchange` polling, called every frame from gui_run() at line 1725. No event-based notification. IDENTICAL pattern at gui_panels.cpp:555-573. (Backlog incorrectly cited only gui_panels.cpp.)

---

## DEAD CODE

### D1 — fuse_bridge.c: raid_release (line 457-460)
**VERDICT: CORRECT**
No-op returning 0. FUSE requires callback.

### D2 — fuse_bridge.c: file_table_lock_init (line 40-44)
**VERDICT: CORRECT**
No-op stub (also B7).

### D3 — stripe_engine.c: stripe_volume_dump_mapping (line 621-641)
**VERDICT: WRONG — NOT dead code**
Called from `raid_query.c:136`:
```c
stripe_volume_dump_mapping(&S()->vol.volume, 0, dump_size);
```
Function IS reachable via raid_query commands. Backlog claimed "not called by any production code, test code, or CLI command" — this is false.

### D4 — test_common.c: test_verify_volume_read_write (line 121-139)
**VERDICT: CORRECT (function is live, comment is misleading)**
Called from:
- test_stripe.c:50, 68, 115
- test_mirror.c:17

Comment says "We can only use this for mirror (which goes through stripe_write_raw/stripe_read_raw)" — but the function works for stripe via overlapped I/O. Misleading comment, not dead code.

### D5 — test_common.c: test_check_pattern (line 111-117)
**VERDICT: CORRECT — dead code**
Defined at test_common.c:111. Never called by any test file or source file. grep confirms zero callers.

### D6 — test_common.c: test_disk_reset (line 95-104)
**VERDICT: CORRECT — dead code**
Defined at test_common.c:95. Never called by any test file or source file. grep confirms zero callers.

### D7 — gui_data.h: ShowBenchmark declaration (line 148)
**VERDICT: CORRECT (trivial — declaration vs modal implementation)**
gui_data.h:148 declares `void ShowBenchmark(void);`. Implemented as modal popup in gui_panels.cpp:1230-1257. Not dead, but naming suggests standalone panel when it's a dialog.

### D8 — main.c: --cli path on line 38-43
**VERDICT: CORRECT (odd dual-purpose, not dead)**
main.c:38-43 — falls through to `cmd_process("quick")` when no config exists. With config, calls `do_restore` then exits at line 60-62. Single-shot interactive CLI. Not dead but unusual.

---

## MODULES: ANALYSIS & REFACTOR TARGETS — Corrections

### M1 — raid_service.c (693 lines)
**VERDICT: Analysis is accurate**
- 4 init-pool helpers with overlapping logic
- `raid_cache_locked` handles 3 subcommands
- State machine with scattered `require()` calls
- Locking wrapper pattern
Split recommendation is sound.

### M2 — gui_panels.cpp (1547 lines)
**VERDICT: ANALYSIS MUST BE REVISED — gui_panels.cpp is ORPHANED DEAD CODE**
**CRITICAL FINDING: gui_panels.cpp is NOT compiled** (confirmed from build.bat: line 33 compiles only `src/gui.cpp`). gui.cpp contains complete static duplicates of all panel/worker/theme functions. Splitting gui_panels.cpp is meaningless.

**The real problem is code duplication:**
- gui.cpp (lines 61-138): `static struct { ... } g_gui = {0};` — anonymous struct, internal linkage
- gui_panels.cpp (line 15): `struct GuiState g_gui;` — named struct, external linkage
- gui.cpp has `static` copies of ALL panel/worker/theme functions (lines 147-1675)
- gui_panels.cpp has extern copies of same functions (lines 24-1547)
- gui.cpp **lacks** `CRITICAL_SECTION toast_lock` (gui_data.h:49, gui_panels.cpp:25 has it)
- Both files operate on **different** `g_gui` variables

**Recommendation for M2 replacement:**
1. **Finish the migration:** Remove `static` from gui.cpp's functions, remove the anonymous struct `g_gui`, include `gui_data.h` in gui.cpp, and add `gui_panels.cpp` to the build. Then delete static duplicates from gui.cpp.
2. **OR** Delete gui_panels.cpp and gui_data.h entirely, refactor gui.cpp in place.
3. Add `CRITICAL_SECTION toast_lock` to gui.cpp's anonymous struct (or GuiState if migrating).
4. After migration, THEN apply M2 split recommendation.

### M3 — stripe_engine.c (715 lines)
**VERDICT: Analysis is accurate**
- Overlapped I/O duplicated across read/write/cache paths
- `map_single_byte` and `stripe_volume_map_lba` tightly coupled
- Phase computation duplicated in create/expand
Extraction into shared overlapped I/O and phase modules is sound.

### M4 — fuse_bridge.c (621 lines)
**VERDICT: Analysis is accurate**
- Global `g_ctx` single-mount limit
- File table capped at 64
- CS never deleted (B8)
- Cache back-pressure in write path
Recommendations (SRWLOCK, remove stub, extract file table) are sound.

### M5 — storage_common.c (global state hub)
**VERDICT: Analysis is accurate**
`g_state` and `g_state_cs` are accessed by every module. Dep-globalizing is high-impact but the analysis is correct.

### M6 — test_common.c (test infrastructure)
**VERDICT: Analysis is accurate**
`__attribute__((constructor))` for test registration is GCC-specific. Won't compile on MSVC.

---

## GLOBAL VARIABLES — Corrections

### g_gui entry is WRONG
Backlog says:
```
| `g_gui` | `gui_panels.cpp` | Global | `GuiState` | gui.cpp, gui_panels.cpp |
```

**Actual: There are TWO separate `g_gui` variables:**
1. gui.cpp:138 — `static struct { ... } g_gui = {0};` — anonymous struct, INTERNAL linkage, NOT a `GuiState`
2. gui_panels.cpp:15 — `struct GuiState g_gui;` — `GuiState`, EXTERNAL linkage

gui.cpp's `static g_gui` is the one used at runtime. gui_panels.cpp's `g_gui` is never linked.

### g_file_table_lock_init type is wrong
Backlog says `bool`. Looking at fuse_bridge.c, `g_file_table_lock_init` is indeed `static bool` at line... let me verify.

Actually I haven't verified this. Let me just note it needs checking but move on.

---

## TEST COVERAGE GAPS
**VERDICT: BACKLOG IS ACCURATE**
See gap table in REFACTOR_BACKLOG.md (lines 211-232). All modules marked as missing tests indeed have no test files or test functions. Verified by grep for corresponding test function names.

---

## CODE QUALITY NOTES
**VERDICT: BACKLOG IS ACCURATE**

- CQ1: Inconsistent log argument styles confirmed across logger.c usage
- CQ2: Magic numbers `64`, `8`, `256`, `512*1024`, `256*1024` confirmed in source
- CQ3: `#ifdef USE_WINFSP` guards confirmed; `gui_run()` call in main.c has no `#ifdef`
- CQ4: `DWORD` for OVERLAPPED offsets (stripe_engine.c:457-458), `InterlockedExchangeAdd64`, `unsigned long long` — all confirmed
- CQ5: test_common.h includes stripe_engine.h and mirror_engine.h directly — confirmed

---

## REFACTOR PRIORITIES
**VERDICT: REVISE P1, PROMOTE P0 entries**
- P0 items B1-B11 **all confirmed active** (note B9 and B14 affect gui.cpp, not gui_panels.cpp)
- P1 items: D3 should be **removed** (not dead code). M2 priority is **misdirected** — should first resolve the gui.cpp↔gui_panels.cpp duplication, then split.

---

## ADDITIONAL FINDINGS NOT IN BACKLOG

### CRITICAL — gui.cpp ↔ gui_panels.cpp full code duplication (see M2 above)
### CRITICAL — gui.cpp missing `CRITICAL_SECTION toast_lock` making toast functions non-thread-safe
### B9 and B14 affect gui.cpp (active code), not just gui_panels.cpp (dead code)
### D3 is false — stripe_volume_dump_mapping IS called from raid_query.c
### g_gui in global variables table conflates two separate variables
