# RAIDTEST v3 — Technical Debt Report

Total: **29 items** (3 Critical, 7 High, 9 Medium, 10 Low)

Each debt is scored on:

- **Principal (P)**: Effort to fix (1 = hours, 5 = weeks)
- **Interest (I)**: Cost of NOT fixing (1 = cosmetic, 5 = catastrophic)
- **Priority**: P × I (higher = fix sooner)

---

## 💀 Critical Debt (P × I ≥ 20)

### D1 — Global state is completely unsynchronized

| Field | Value |
|---|---|
| **Files** | `src/raid_service.c` (all 24 functions), `src/fuse_bridge.c` (14 callbacks), `src/gui.cpp` (worker threads) |
| **Description** | `g_state` is accessed by 4 thread types (CLI, FUSE workers, GUI workers, cache flush thread). `gs_lock()`/`gs_unlock()` exist in `common.h:16-17` but are NEVER called in `raid_service.c`, `fuse_bridge.c`, or `gui.cpp` worker threads. Only `cmd_handler.c` CLI dispatch, `daemon.c`, and `main.c` auto-setup use the lock. |
| **Principal** | 5 — Add locks to ~25 functions; audit every return path; verify no re-entrancy |
| **Interest** | 5 — All state machine transitions are data races; state corruption, silent data loss |
| **P × I** | **25** |
| **Long-term Impact** | Makes the architecture fundamentally unsafe for multi-threaded use. Every feature that works via FUSE or GUI is technically broken under concurrency. |
| **Suggested Fix Strategy** | Wrap every `raid_*` function with `gs_lock()`/`gs_unlock()`. Add lock to FUSE callbacks. Ensure unlock on every return path. Document lock ordering. |

---

### D2 — NULL pointer discipline absent (25+ sites)

| Field | Value |
|---|---|
| **Files** | `src/raid_service.c` (8 sites: `device_get()`), `src/stripe_engine.c`, `src/mirror_engine.c`, `src/journal.c`, `src/superblock.c`, `src/cleanup.c` (25+ sites: `vol->disks[i]`) |
| **Description** | `device_get()` returns NULL on invalid index, but none of its 8 call sites check for NULL before dereference. `vol->disks[i]` is accessed without NULL check at 25+ sites across all engine modules. Any partial/corrupted volume configuration causes guaranteed access violation. |
| **Principal** | 4 — Add ~30 NULL guards across 8 files; 1 developer-day |
| **Interest** | 5 — Any partial initialization or error leads to immediate process termination |
| **P × I** | **20** |
| **Long-term Impact** | Every new code path that initializes partial volumes must remember to avoid NULL dereferences. Without defensive guards, bugs in volume initialization cascade into crashes. |
| **Suggested Fix Strategy** | Add `if (!disk) return RC_DISK_NOT_FOUND;` before every `device_get()` dereference and `vol->disks[i]` access. |

---

### D3 — No lock ordering specification

| Field | Value |
|---|---|
| **Files** | Entire project (no document) |
| **Description** | Two critical sections exist (`g_state_cs` in `common.h`, `cache->lock` in `ram_cache.c`). No document defines the order in which they must be acquired. Any future code that nests both without a documented order risks deadlock. |
| **Principal** | 3 — Write LOCK_ORDER.md; verify current code has no nested locking |
| **Interest** | 5 — Deadlock is unrecoverable without process restart |
| **P × I** | **15** |
| **Long-term Impact** | Without a documented lock order, every code change risks introducing deadlock. New contributors have no contract to follow. |
| **Suggested Fix Strategy** | Create LOCK_ORDER.md specifying: always acquire `g_state_cs` before `cache->lock`. Never acquire `cache->lock` while holding `g_state_cs` unless verified. |

---

## 🔴 High Debt (P × I ≥ 12)

### D4 — FUSE flat file table with 64-entry limit

| Field | Value |
|---|---|
| **Files** | `src/fuse_bridge.c` (entire file table section, ~200 lines) |
| **Description** | The FUSE bridge uses a 64-entry flat array of `FUSE_FILE` structs. File lookup is O(n) linear scan (`find_file_by_name`). No directory hierarchy. Exceeding 64 entries overwrites the oldest entry silently. Paths have no hierarchy beyond root. |
| **Principal** | 5 — Replace with hash table + directory support; 5-10 developer-days |
| **Interest** | 4 — 64-file limit is extremely restrictive; overwrite causes silent data corruption |
| **P × I** | **20** |
| **Long-term Impact** | The FUSE filesystem cannot serve as a real storage target. Users hitting the 64-file limit lose data without warning. |
| **Suggested Fix Strategy** | Replace flat array with hash table (keyed by name hash, chaining). Implement proper directory structure (at minimum `readdir` with `.` and `..`). Return `ENOSPC` on table full instead of overwriting. |

---

### D5 — Missing OVERLAPPED cancel safety

| Field | Value |
|---|---|
| **Files** | `src/storage_common.c:27-33`, `src/ram_cache.c:143-175` |
| **Description** | Stack-allocated `OVERLAPPED` structures are passed to `WriteFile`/`ReadFile`. If the I/O is pending, the kernel holds a pointer to stack memory that is freed when the function returns. `CancelIoEx` is used in error paths but provides no guarantee that the OVERLAPPED is no longer referenced. |
| **Principal** | 4 — Refactor to heap-allocated OVERLAPPED with explicit completion tracking; 3-5 days |
| **Interest** | 4 — Kernel-mode write-after-free can corrupt arbitrary memory, cause delayed crashes |
| **P × I** | **16** |
| **Long-term Impact** | Windows I/O completion model violation. Any I/O error path risks heap corruption that may go undetected for long periods. |
| **Suggested Fix Strategy** | Heap-allocate OVERLAPPED. Use `WaitForSingleObject` on the event handle before freeing. Never cancel — wait for completion. |

---

### D6 — Journal unbounded growth

| Field | Value |
|---|---|
| **Files** | `src/journal.c`, `src/journal.h` |
| **Description** | The journal file grows linearly with every cache flush cycle. No rotation, truncation, or circular buffering. `journal_recover_all` replays all entries since file creation. The file fills the disk over time. Acknowledged in KNOWN_LIMITATIONS.md. |
| **Principal** | 4 — Implement circular buffer with head pointer and per-entry CRC; 5-8 days |
| **Interest** | 4 — Journal fills disk, then all writes fail; recovery time grows with file size |
| **P × I** | **16** |
| **Long-term Impact** | Disk space exhaustion is guaranteed under sustained write load. Recovery time increases linearly with uptime. |
| **Suggested Fix Strategy** | Implement fixed-size circular buffer: write head pointer at known offset, overwrite oldest entries when full. Add CRC32 per entry for crash detection. |

---

### D7 — FUSE bridge lifecycle race (DeleteCriticalSection while callbacks active)

| Field | Value |
|---|---|
| **Files** | `src/fuse_bridge.c:585-588` |
| **Description** | `fuse_unmount` calls `DeleteCriticalSection(&g_ft_lock)` and frees `g_file_table` without ensuring no FUSE callbacks are in-flight. WinFsp worker threads may still be executing callbacks that hold the lock. |
| **Principal** | 3 — Add reference counting + quiesce; 1-2 developer-days |
| **Interest** | 4 — Use-after-free of critical section; deadlock or crash on unmount during active I/O |
| **P × I** | **12** |
| **Long-term Impact** | Unmounting with active file handles can crash the process. Prevents clean service shutdown. |
| **Suggested Fix Strategy** | Add reference counter: increment on callback entry, decrement on exit. Wait for zero before destroying lock and table. |

---

### D8 — No error propagation across storage layer

| Field | Value |
|---|---|
| **Files** | `src/storage_common.c/h`, `src/stripe_engine.c/h`, `src/mirror_engine.c/h`, `src/ram_cache.c/h`, `src/volume_manager.c/h`, `src/raid_service.c/h` |
| **Description** | I/O functions return `bool`. Callers partially check the return value but errors do not propagate to user-visible state. On I/O failure, the volume continues operating as if the write succeeded. The disk health `volatile LONG disk_state` is written in disk_fail scenarios but errors are not surfaced through the API. |
| **Principal** | 3 — Refactor return types; add error state to volume/disk structures; 5-7 days |
| **Interest** | 4 — Silent data loss on write failure; user sees "all healthy" while writes fail |
| **P × I** | **12** |
| **Long-term Impact** | No accountability in the storage stack. Failures are invisible until data is read back. |
| **Suggested Fix Strategy** | Change storage layer to return `RC` error codes. Thread through stripe/mirror/cache. Accumulate first error in volume status. Surface via `raid_info()` / GUI. |

---

### D9 — Event bus critical section never deleted

| Field | Value |
|---|---|
| **Files** | `src/event_bus.c:27-31`, `src/event_bus.h` |
| **Description** | `InitializeCriticalSection(&g_eb_cs)` is called in `event_bus_init()`. No `DeleteCriticalSection` or `event_bus_cleanup()` function exists. The CS leaks on every subsystem lifecycle. |
| **Principal** | 1 — Add cleanup function; call from `raid_cleanup()`; 2-3 hours |
| **Interest** | 4 — Resource leak; prevents safe re-initialization in long-running processes |
| **P × I** | **4** |
| **Long-term Impact** | Minor resource leak per init cycle. Prevents clean module reload for testing or reconfiguration. |
| **Suggested Fix Strategy** | Add `event_bus_cleanup()` that calls `DeleteCriticalSection` and resets `g_eb_inited = false`. Call from `raid_cleanup()`. |

---

### D10 — Compile-time constants throughout

| Field | Value |
|---|---|
| **Files** | `src/common.h` (MAX_DISKS=4, MAX_IO_ENTRIES, CACHE_BLOCK_SIZE=65536, MAX_FLUSH_SIZE=4MB) |
| **Description** | Hard-coded limits everywhere: 4-disk max, 64-entry FUSE table, 64KB cache blocks, 4MB flush buffer size. None of these are runtime-configurable. Changing any requires a recompile. |
| **Principal** | 5 — Abstract into config struct; add validation; 5+ days |
| **Interest** | 3 — Requires recompile to tune; impractical for production deployment |
| **P × I** | **15** |
| **Long-term Impact** | Limits are baked into the binary. Cannot adapt to different hardware configurations without building from source. |
| **Suggested Fix Strategy** | Define a `LIMITS` config struct initialized at startup from defaults. Allow override via config file. Add validation against hardware capabilities. |

---

## 🟠 Medium Debt (P × I ≥ 6)

### D11 — FUSE buffer overflow risks (2 sites)

| Files | `src/fuse_bridge.c:99-102`, `src/fuse_bridge.c:270-274` |
| **Principal** | 2 — Add bounds checks; 2-3 hours |
| **Interest** | 4 — Stack buffer overflows: exploitable, process crash |
| **P × I** | **8** |
| **Suggested Fix** | Add length checks before strncpy/wcscpy operations. Verify against buffer size. |

### D12 — Double-checked locking in event_bus_init

| File | `src/event_bus.c:28-32` |
| **Principal** | 1 — Use init-once primitive; 1 hour |
| **Interest** | 3 — CS corruption on concurrent init |
| **P × I** | **3** |
| **Suggested Fix** | Move `InitializeCriticalSection` to `raid_init()` (single-thread context). Remove lazy-init pattern. |

### D13 — Unsigned underflow (3 sites)

| Files | `src/stripe_engine.c:182`, `src/mirror_engine.c:35,103` |
| **Principal** | 3 — Add underflow guards; 3 days |
| **Interest** | 3 — Wrong volume geometry, bounds bypass |
| **P × I** | **9** |
| **Suggested Fix** | Use subtract-before-add pattern for bounds checks. Add explicit underflow guards. |

### D14 — realloc failure leak in disk_scanner

| File | `src/disk_scanner.c:43` |
| **Principal** | 1 — Save original pointer; 1 hour |
| **Interest** | 3 — Memory leak, silent disk omission |
| **P × I** | **3** |
| **Suggested Fix** | Use temporary pointer for realloc return. Free original on failure. |

### D15 — 64→32 bit truncation (3 sites)

| Files | `src/stripe_engine.c:332`, `src/journal.c:109`, `src/ram_cache.c:8` |
| **Principal** | 2 — Use uint64_t consistently; 2 days |
| **Interest** | 3 — Files >4GB cause partial read/truncation |
| **P × I** | **6** |
| **Suggested Fix** | Keep values as `uint64_t` until explicit range checks confirm they fit in 32 bits. |

### D16 — FUSE threads + volume access unsynchronized

| File | `src/fuse_bridge.c` (all callbacks) |
| **Principal** | 3 — Add g_state lock to FUSE callbacks; 3 days |
| **Interest** | 3 — FUSE callbacks run on WinFsp thread pool, race with GUI/CLI |
| **P × I** | **9** |
| **Suggested Fix** | Acquire `g_state_cs` in every FUSE callback that accesses volume data. |

### D17 — Event bus callback vs unsubscribe race

| File | `src/event_bus.c:48-60,62-74` |
| **Principal** | 3 — Reader-writer lock or RCU; 3 days |
| **Interest** | 3 — Use-after-free of subscriber userdata |
| **P × I** | **9** |
| **Suggested Fix** | Copy callback list under lock, release lock, invoke. Or reference-count subscribers. |

### D18 — Cleanup destroys files on all drives

| File | `src/cleanup.c:135-142` |
| **Principal** | 2 — Add mount state check; 2 days |
| **Interest** | 3 — Inadvertent data loss on active volume |
| **P × I** | **6** |
| **Suggested Fix** | Skip drives that are currently mounted as RAIDTEST volumes. Query FUSE mount state before deletion. |

### D19 — config_load swscanf buffer overflow

| File | `src/config.c:86` |
| **Principal** | 1 — Limit field width; 1 hour |
| **Interest** | 3 — Crash on crafted config file |
| **P × I** | **3** |
| **Suggested Fix** | Use `swscanf(value, L"%7[^\"]", lang)` to limit to 7 characters. Validate before use. |

---

## 🟢 Low Debt (P × I < 6)

### D20 — TOCTOU on mirror healthy_count

| Files | `src/mirror_engine.c:59,69,83,91` |
| **Principal** | 2 — Atomic check-and-decrement |
| **Interest** | 2 — Race in degraded mode |
| **P × I** | **4** |
| **Suggested Fix** | Use `InterlockedCompareExchange` to safely decrement only if > 0. |

### D21 — wizard depends on cmd_handler.h for types

| File | `src/wizard.h` |
| **Principal** | 1 — Extract APP_STATE to state.h |
| **Interest** | 2 — Fragile dependency |
| **P × I** | **2** |
| **Suggested Fix** | Create `state.h` with APP_STATE typedef. Remove cmd_handler.h include from wizard.h. |

### D22 — cleanup.h depends on cmd_handler.h

| File | `src/cleanup.h` |
| **Principal** | 1 — Extract APP_STATE to state.h |
| **Interest** | 2 — Same as D21 |
| **P × I** | **2** |
| **Suggested Fix** | Same as D21. |

### D23 — gui.cpp includes cmd_handler.h unnecessarily

| File | `src/gui.cpp:9` |
| **Principal** | 1 — Move S() macro to common.h |
| **Interest** | 2 — Include circle risk, wrong module boundary |
| **P × I** | **2** |
| **Suggested Fix** | Move `S()` macro from `cmd_handler.h` to `common.h`. Remove `#include "cmd_handler.h"` from `gui.cpp`. |

### D24 — Profiler slot 0 ambiguity

| File | `src/profiler.c:36-38` |
| **Principal** | 1 — Change return convention |
| **Interest** | 2 — Misreported metrics on slot 0 overwrite |
| **P × I** | **2** |
| **Suggested Fix** | Return `bool` with slot index via `int* out` parameter. |

### D25 — IOPS calculation uses wrong counter

| File | `src/profiler.c:115-118` |
| **Principal** | 1 — Use read_ops, not read_bytes |
| **Interest** | 2 — Wrong IOPS reporting |
| **P × I** | **2** |
| **Suggested Fix** | Use `current_read_ops - last_read_ops` instead of byte delta. |

### D26 — DO_BATCH macro is 47 lines

| File | `src/bench_io.c:62-108` |
| **Principal** | 2 — Refactor to function |
| **Interest** | 2 — Unmaintainable, debugging nightmare |
| **P × I** | **4** |
| **Suggested Fix** | Convert to static function with explicit parameters. |

### D27 — No journal lock

| File | `src/journal.c:35-78` |
| **Principal** | 2 — Add CRITICAL_SECTION |
| **Interest** | 2 — Concurrent journal corruption |
| **P × I** | **4** |
| **Suggested Fix** | Add `g_journal_cs`; acquire before every file operation. |

### D28 — Unnecessary abstraction: metadata_manager

| File | `src/metadata_manager.c` |
| **Principal** | 1 — Inline into volume_manager |
| **Interest** | 2 — Indirection without value |
| **P × I** | **2** |
| **Suggested Fix** | Remove metadata_manager.c/h; call superblock functions directly from volume_manager. |

### D29 — created_time always 0

| File | `src/volume_manager.c:11-14` |
| **Principal** | 1 — Call GetSystemTimeAsFileTime |
| **Interest** | 1 — Cosmetic; wrong timestamp |
| **P × I** | **1** |
| **Suggested Fix** | Initialize `created_time` with `GetSystemTimeAsFileTime()` after UUID generation. |

---

## Debt Summary

| Priority | Count | Total P×I | Key Items |
|---|---|---|---|
| 💀 Critical (P×I ≥ 20) | 3 | 60 | Thread safety, NULL discipline, lock ordering |
| 🔴 High (P×I ≥ 12) | 7 | 95 | FUSE table, OVERLAPPED, journal unbounded, lifecycle race, error propagation, event_bus cleanup, compile-time limits |
| 🟠 Medium (P×I ≥ 6) | 9 | 52 | Buffer overflows, double-checked locking, underflows, realloc leaks, truncation, FUSE thread safety, event_bus race, cleanup, config overflow |
| 🟢 Low (P×I < 6) | 10 | 28 | TOCTOU, header deps, profiler bugs, macro hygiene, journal lock, abstractions, timestamps |
| **Total** | **29** | **235** | |

**Estimated Payoff Time**: ~15-25 developer-days to eliminate all critical and high debts.
