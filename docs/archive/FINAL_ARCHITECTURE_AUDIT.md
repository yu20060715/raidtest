# FINAL ARCHITECTURE AUDIT — RAIDTEST v3

**Audit Date:** 2026-07-06  
**Scope:** Entire `src/` (60 files, 30 headers, 30 sources)  
**Method:** Full source code review — no prior reports trusted.

---

## Executive Summary

The codebase implements a functional asymmetric-stripe RAID0/RAID1 engine with WinFsp FUSE integration and a Dear ImGui GUI. The core I/O path is sound and the stripe-phase algorithm is genuinely novel. However, **one P0 memory corruption bug** exists in the disk scanner's free path. The global state machine is unprotected against concurrent access (P1), and several functional bugs (e.g. the CLI `select` command invoking the wrong function) remain.

**Architecture Score: 62/100**

---

## Remaining P0

### P0-1: Heap corruption in `disk_scan_free()` (src/disk_scanner.c:112-116)

```c
void disk_scan_free(DISK_INFO** disks, uint32_t count) {
    if (!disks) return;
    if (count > 0 && disks[0]) free(disks[0]);  // FAULT: free(&array[0]) — frees into middle of realloc'd block
    free(disks);
}
```

`disk_scan_all()` allocates a contiguous `DISK_INFO[]` via `realloc`, then builds a `DISK_INFO**` pointer array pointing into it. `disk_scan_free()` calls `free(disks[0])`, which attempts to free a pointer into the **middle** of the heap block (the first element of the contiguous array). This is undefined behavior and corrupts the heap.

**Classification:** Confirmed  
**Fix:** Replace with `free(disks[0] ? (void*)disks[0] - offset... )` — actually the contiguous block is never separately freed. The correct pattern is to stash the base pointer or use a different allocation strategy.

---

## Remaining P1

### P1-1: Global state unprotected against concurrent access

`APP_STATE g_state` (cmd_handler.c:11) is accessed by nearly every module. `gs_lock()`/`gs_unlock()` exist but are called in **only 2 places** (daemon.c stdin processing and daemon_run shutdown). All raid_service, device_manager, volume_manager, and cleanup functions read/write `g_state` fields (state, disk_count, mounted, etc.) without acquiring the critical section.

The GUI launches worker threads that call raid_service functions which mutate `g_state.rt.state`, `g_state.vol`, `g_state.disk`, etc. Meanwhile the main thread reads these same fields via `ui_model.c` and the event bus callback.

**Classification:** Confirmed  
**Impact:** Race conditions on state transitions, mount state, volume validity, disk array

### P1-2: State machine naming confusion — STATE_MOUNTED means "volume created"

| Transition | Code path | State set |
|---|---|---|
| `raid_create()` completes | stripe created | `STATE_MOUNTED` |
| `raid_mount()` called | FUSE mounts | still `STATE_MOUNTED` |
| `raid_unmount()` completes | FUSE unmounts | `STATE_UNMOUNTED` |

`raid_mount()` requires `STATE_MOUNTED`, but after `raid_create()` the FUSE mount hasn't happened yet — the volume is just **ready** to mount. The semantic overlap means the state machine cannot distinguish "volume object exists, not mounted" from "volume is actively mounted via FUSE." The `g_state.rt.mounted` bool partially compensates, but the state enum is misleading.

**Classification:** Confirmed  
**Impact:** Confusing API, potential for incorrect state checks

### P1-3: Cache flush thread handle stored in two locations

`RAM_CACHE.flush_thread` and `CacheState.flush_thread` both hold the same HANDLE. `cleanup_volume_cache()` calls `cache_destroy()` which closes `cache->flush_thread` and sets it NULL, but `state->cache.flush_thread` is not updated — it becomes a dangling handle. A subsequent `cleanup_cache()` call would attempt to `WaitForSingleObject`/`CloseHandle` on a stale handle.

**Classification:** Confirmed  
**Impact:** Handle reuse/leak, potential crash on cleanup

### P1-4: CLI `select` command invokes `raid_init_pools` instead of `raid_select`

`cmd_handler.c:215`:
```c
else if (strcmp(args[0], "select") == 0) rc = cmd_init(argc - 1, args + 1);
// cmd_init() calls raid_init_pools(), not raid_select()
```

The `select` command is supposed to mark disks as selected. Instead it runs the entire pool initialization path. `raid_select()` (declared in raid_service.h:15) is **never called** from any code path.

**Classification:** Confirmed  
**Impact:** CLI `select` command behaves incorrectly, marking disks + creating pools instead of just selecting

---

## Remaining P2

### P2-1: Dead enum values
- `STATE_RECOVERING` (5) — never assigned anywhere
- `PHASE_TYPE_MULTI`, `PHASE_TYPE_SINGLE` — defined but unused

**Classification:** Confirmed

### P2-2: Duplicate phase computation logic (~80 lines)
`stripe_volume_create()` (stripe_engine.c:66-189) and `stripe_volume_expand()` (stripe_engine.c:193-314) contain near-identical phase computation loops. Only the disk index offset differs.

**Classification:** Confirmed

### P2-3: Duplicate `get_filetime_now()` function
Defined identically in `journal.c:5-9` and `superblock.c:38-42`.

**Classification:** Confirmed

### P2-4: Duplicate async I/O completion pattern
The CreateEvent -> overlapped WriteFile/ReadFile -> GetOverlappedResult -> cleanup-on-failure pattern is replicated in:
- `stripe_volume_read()` (stripe_engine.c:437-472)
- `stripe_volume_write()` (stripe_engine.c:566-602)
- `cache_flush_all()` (ram_cache.c:143-182)
- write-through path in `stripe_volume_write()` (stripe_engine.c:503-549)

**Classification:** Confirmed

### P2-5: Functional circular dependency stripe_engine ↔ ram_cache
- `stripe_engine.c` includes `ram_cache.h` — uses `cache_read`/`cache_write`
- `ram_cache.c` includes `stripe_engine.h` — uses `stripe_volume_map_lba` in `cache_flush_all`

Not a header circular dependency (both headers only include `common.h`), but the functional dependency at the .c level couples two otherwise independent subsystems.

**Classification:** Confirmed

### P2-6: Mirror read marks disks unhealthy on transient error
`mirror_volume_read()` calls `stripe_read_raw()` on the first healthy disk. If a transient I/O error occurs (e.g. OS page cache pressure), the disk is permanently marked unhealthy via `InterlockedExchange(&disk->healthy, 0)` and `healthy_count` is decremented. No retry logic.

**Classification:** Confirmed

### P2-7: `system("cls")` in raid_status()
`raid_status()` (raid_service.c:524) calls `system("cls")` — a shell invocation that is a security concern and creates a console dependency.

**Classification:** Confirmed

### P2-8: DO_BATCH macro in bench_io.c is unsafe
The large `DO_BATCH` macro at bench_io.c:62-108 creates local variables (`ii`, `bb`, `nn`, `t1`, `t2`) that shadow external scope, uses `SetFilePointerEx` with zeroed LARGE_INTEGER on every call, and has complex control flow inside a macro.

**Classification:** Confirmed

### P2-9: FUSE bridge 64-file limit
`g_open_files[64]` — creation of the 65th file returns `-ENOSPC`. No dynamic growth.

**Classification:** Confirmed

### P2-10: `volume_expand()` unused parameter
`volume_expand()` (volume_manager.c:138-192) declares `uint32_t physical_count` but immediately `(void)physical_count;`.

**Classification:** Confirmed

### P2-11: Write-back cache dirty bitmap expansion overflow at 256 blocks
`MAX_FLUSH_BATCH` is 256. The batch bit iteration in `cache_flush_all()` checks `b + run < cache->block_count && run < max_batch`. With a 4096 MB cache and 64 KB blocks = 65536 blocks. The bitmap indexing `(b + run) / 8` is safe, but `run` is capped to 256, so 256 blocks = 16 MB per batch max. This is not a bug but a performance limitation — large caches will flush slowly.

**Classification:** Partially Confirmed (design limitation, not bug)

---

## Remaining P3

### P3-1: Flat directory structure
All 60 files in a single `src/` directory. No layering visible at the filesystem level.

**Classification:** Confirmed

### P3-2: Duplicate bench directory cleanup
`cleanup_scan_all_drives()` (cleanup.c:74-111) and `cleanup_bench_dirs()` (cleanup.c:114-132) iterate the same drive letters and delete the same bench directory.

**Classification:** Confirmed

### P3-3: API inconsistency — RC vs bool return
- `raid_service.h` functions return `RC` enum
- `volume_manager.h` functions return `bool`
- Callers must handle both patterns

**Classification:** Confirmed

### P3-4: `volume_unmount()` / `volume_destroy()` parameter explosion
`volume_unmount()` takes 4 separate pointer parameters for state fields instead of a struct. `volume_destroy()` takes 7 parameters.

**Classification:** Confirmed

### P3-5: `config_load()` always returns true
Even when the config file doesn't exist, `config_load()` returns `true` (after loading defaults). Callers cannot distinguish "loaded from file" from "no config found."

**Classification:** Confirmed

### P3-6: Journal recovery partial write success
`journal_data()` writes to all disks and returns `any_ok`. If only 1 of N disks receives the journal data, recovery may silently proceed with partial data.

**Classification:** Confirmed

### P3-7: `progress_frac` volatile but not atomically accessed
The GUI worker thread writes `g_gui.progress_frac` (a `volatile float`), the main thread reads it. On x86 this is likely fine, but `volatile float` does not guarantee atomicity on all architectures.

**Classification:** Confirmed

### P3-8: `metadata_upgrade()` always returns false for v1-v3
Superblock version upgrade is handled in `try_read_superblock_from_drive()`, not in `metadata_upgrade()`. The function `metadata_upgrade()` is never actually used for upgrades.

**Classification:** Confirmed

### P3-9: `raid_events()` returns RC_OK on error
If `malloc` fails in `raid_events()`, the function returns `RC_OK`.

**Classification:** Confirmed

---

## Design Choices (Intentional)

The following are **not** bugs — they are deliberate architectural decisions:

1. **Asymmetric stripe engine** — Speed-weighted ratio-based disk striping is the project's core innovation. The phase computation algorithm allocates more I/O to faster disks within each stripe phase.

2. **Single flat src/ directory** — Simplifies the MinGW build system. No subdirectory layering.

3. **Global `APP_STATE g_state`** — Singleton state structure avoids passing context through every function. Consistent with embedded/firmware style.

4. **Write-back cache with journal** — Dirtiest-first flush strategy with write-ahead journaling for crash recovery.

5. **Superblock metadata** — Volume configuration stored as superblock.dat on each member disk. Serial-number-based matching enables disk reassignment across drive letters.

6. **Direct Windows API** — No abstraction layers for OS primitives. `CRITICAL_SECTION`, `OVERLAPPED`, `CreateFileW`, etc. used directly.

7. **CLI + GUI dual mode** — `main.c` dispatches to `gui_run()` (no args) or `cli_main()` (args present). The same raid_service layer serves both.

8. **Cache flush thread** — Background thread with adaptive sleep based on dirty ratio (10ms at >75%, up to 1s at <10%).

9. **Disk type detection by model substring** — Heuristic classification (NVMe, SATA SSD, HDD) for speed estimation before benchmarking.

10. **Benford-like disk speed estimation** — Default speeds assigned by type, overridden after 512MB benchmark.

---

## Overall Architecture Score

| Category | Score | Notes |
|---|---|---|
| Architecture/Layering | 6/10 | Flat src/ OK, but no clear interface boundaries |
| Thread Safety | 4/10 | Global state unprotected, handle duplication |
| State Machine | 6/10 | Functional but misleading STATE_MOUNTED semantics |
| Memory Safety | 5/10 | P0 heap corruption in disk_scan_free |
| Resource Lifetime | 6/10 | Handle duplication, double-close potential |
| Error Propagation | 6/10 | Mixed RC/bool, partial success masked |
| Recovery | 7/10 | Journal and superblock recovery sound |
| API Consistency | 5/10 | Mixed return types, parameter explosion |
| Manager Boundaries | 6/10 | Cross-layer g_state access blurs boundaries |
| Integration | 8/10 | CLI + GUI + WinFsp + Service all functional |
| **Overall** | **62/100** | |

---

## Enterprise Readiness

| Requirement | Status |
|---|---|
| Crash recovery | ✓ Journal replay + superblock orphan recovery |
| Data integrity | ✓ CRC32 checksums on journal entries and superblock |
| Hot rebuild | ✓ Mirror rebuild from healthy source |
| Health monitoring | ✓ Degraded state, disk error counting, health summary |
| Installation | ✓ SCM service install/uninstall |
| Config persistence | ✓ JSON config + superblock metadata |
| Diagnostics | ✓ Event log, metadata dump, export |
| Logging | ✓ Thread-safe logger with levels |
| **Blockers** | P0 heap corruption must be fixed before any production use |

---

## Recommended Next Sprint

### Sprint Priority (in order)

1. **P0-1 (Critical):** Fix `disk_scan_free()` heap corruption — change to store base pointer or use a different allocation pattern
2. **P1-4 (Functional):** Fix `select` command — wire to `raid_select()` instead of `raid_init_pools()`
3. **P1-1 (Safety):** Add `gs_lock()`/`gs_unlock()` guards around all `g_state` mutations
4. **P1-3 (Bug):** Eliminate duplicate cache thread handle — single source of truth
5. **P2-1 (Cleanup):** Remove dead enum values, unreachable code
6. **P2-2 (Maintainability):** Extract shared phase-computation into helper function
7. **P2-6 (Reliability):** Add retry logic for transient I/O errors in mirror read
8. **P2-7 (Security):** Replace `system("cls")` with console API clear
9. **P3 audit sweep** — all P3 items as backlog
