# RAIDTEST v3 — Architecture Review

---

## 1. Module Dependency Graph

### Layer Diagram

```
Layer 0 (Entry)          main.c
                             │
Layer 1 (Frontend)       ┌───┴───┬──────────┬────────┐
                         │       │          │        │
                     gui.cpp  cmd_handler  daemon  wizard
                         │       │          │        │
                         └───┬───┴──────────┴────────┘
                             │
Layer 2 (Service)        raid_service.c/h
                             │
              ┌──────────────┼──────────────┬──────────────┐
              │              │              │              │
Layer 3   volume        device        metadata      planner
(Managers) manager      manager       manager       engine
              │              │              │              │
              │         event_bus ──────────┤              │
              │              │              │              │
              └──────────────┼──────────────┘              │
                             │                             │
                    ui_model.c/h                            │
                             │                             │
Layer 4       ┌──────────────┼──────────────────┐          │
(Engines)     │              │                  │          │
          stripe        mirror            ram_cache        │
          engine        engine          ────╮              │
              │              │          ╱   │              │
              └──────────────┼─────────╯    │              │
                             │              │              │
                        journal            │              │
                             │              │              │
                    storage_common          │              │
                             │              │              │
                      superblock            │              │
                             │              │              │
Layer 5       ┌──────────────┼──────┬───────┘              │
(Infra)       │              │      │                      │
           pool_io      disk_scanner  bench_io             │
                             │                             │
                    ┌────────┴────────┐                    │
                    │                 │                    │
                config            logger                   │
                    │                 │                    │
               profiler          cleanup                   │
                    │                                      │
               fuse_bridge                                 │
                    │                                      │
                    └──────────────────────────────────────┘
```

### Directional Arc Table

| Source | Target(s) | Type |
|---|---|---|
| `main.c` | `raid_service`, `cmd_handler`, `daemon`, `wizard`, `gui` | Call |
| `gui.cpp` | `raid_service`, `ui_model` | Call |
| `cmd_handler.c` | `raid_service` | Call |
| `daemon.c` | `raid_service`, `logger` | Call |
| `wizard.c` | `raid_service` | Call |
| `raid_service.c` | `device_manager`, `volume_manager`, `metadata_manager`, `planner_engine`, `event_bus`, `disk_scanner`, `bench_io`, `journal`, `ram_cache`, `config`, `cleanup`, `pool_io`, `stripe_engine`, `mirror_engine`, `logger` | Call |
| `volume_manager.c` | `metadata_manager`, `pool_io`, `fuse_bridge`, `journal`, `mirror_engine`, `ram_cache`, `cleanup`, `event_bus`, `stripe_engine` | Call |
| `device_manager.c` | `disk_scanner`, `bench_io`, `pool_io`, `event_bus` | Call |
| `metadata_manager.c` | `superblock`, `event_bus` | Call |
| `planner_engine.c` | None (pure calculation) | — |
| `ui_model.c` | `cmd_handler` (reads `g_state`) | Call |
| `event_bus.c` | None | — |
| `stripe_engine.c` | `pool_io`, `storage_common`, `ram_cache` (read/write), `ram_cache.h` include | Call + Include |
| `mirror_engine.c` | `pool_io`, `storage_common`, `ram_cache` (write_through check) | Call |
| `ram_cache.c` | `stripe_engine` (map_lba), `journal` | Call |
| `journal.c` | `storage_common`, `pool_io` | Call |
| `storage_common.c` | `pool_io` | Call |
| `superblock.c` | `storage_common`, `pool_io` | Call |
| `pool_io.c` | Win32 `CreateFile`, `ReadFile`, `WriteFile`, `CloseHandle` | System |
| `disk_scanner.c` | Win32 `DeviceIoControl`, `GetDriveTypeW`, etc. | System |
| `bench_io.c` | Win32 `CreateFile`, `ReadFile`, `WriteFile` | System |
| `config.c` | Win32 file I/O | System |
| `logger.c` | Win32 file I/O | System |
| `profiler.c` | None | — |
| `cleanup.c` | Win32 `FindFirstFile`, `DeleteFile`, `RemoveDirectory` | System |
| `fuse_bridge.c` | WinFsp FUSE API | System |

### Circular Dependency

**Call-level cycle exists between `ram_cache` and `stripe_engine`:**

```
ram_cache.c:cache_flush_all() → stripe_volume_map_lba() [in stripe_engine.c]
stripe_engine.c:stripe_volume_read/write() → cache_read/cache_write() [in ram_cache.c]
```

This is NOT an include cycle (no `#include` loops), but it is a bidirectional call dependency. The modules cannot be tested in isolation, and their lifecycle management is coupled: `stripe_engine` calls `cache_*` for write-back buffering, but `cache_flush_all` calls back into `stripe_engine` for LBA mapping during flush.

---

## 2. Thread Model

### Thread Inventory (7 Types)

| ID | Thread | Created At | Creates | Accesses | Synchronization |
|---|---|---|---|---|---|
| T1 | **Main Thread** | `main.c:main()` | GUI window, worker threads | `g_state`, UI model | `gs_lock()` in CLI dispatch path only |
| T2 | **Cache Flush Thread** | `raid_service.c:442` (`_beginthreadex`) | — | `vol->cache`, `vol->disks[]`, journal file | `cache->lock` only; `vol->disks[]` UNPROTECTED |
| T3 | **Daemon Console Thread** | `daemon.c` (`CreateThread`) | — | `g_state` | `gs_lock()` |
| T4 | **Service Main Thread** | `daemon.c` (SCM entry) | — | `g_state` | `gs_lock()` |
| T5 | **FUSE Worker Threads** | WinFsp internal thread pool | — | `vol->disks[]`, `vol->cache`, `g_file_table` | `g_ft_lock` only; `vol` state UNPROTECTED |
| T6 | **GUI Worker Threads** | `gui.cpp` (`_beginthreadex`) | — | `g_state`, `vol` via `raid_*` functions | NONE |
| T7 | **GUI Render Thread** | `gui.cpp` (main loop) | — | UI model (read-only) | NONE (read-only) |

### Thread Safety Analysis

| Thread Pair | Shared Resource | Protected? | Risk |
|---|---|---|---|
| T2 ↔ T5 | `vol->disks[]`, `vol->cache` | No | Concurrent read/write of disk pointers and cache state |
| T5 ↔ T6 | `g_state` via `raid_*` | No | State machine transition during FUSE callback |
| T1 ↔ T6 | `g_state` via `raid_*` | No | CLI command races with long-running GUI operation |
| T2 ↔ T6 | `vol`, `vol->cache` | No (partial: cache has its own lock) | Concurrent flush and write-back from two paths |
| T1 ↔ T2 | `g_state` via raid_cache() and flush | No | Cache configuration changed while flush is running |
| T3/T4 ↔ T1/T5/T6 | `g_state` | No | Daemon service operations race with all other threads |

### Critical Finding

**4 of 7 thread types (T1 GUI path, T2, T5, T6) have zero synchronization on `g_state`.** The lock is only used by:
- T1 CLI path (`cmd_handler.c` CLI dispatch loop)
- T3 (daemon console)
- T4 (service main)

---

## 3. Lock Hierarchy

### Lock Inventory

| Lock | Type | Location | Protects |
|---|---|---|---|
| `g_state_cs` | `CRITICAL_SECTION` | `common.h:15` | `g_state` (APP_STATE global singleton) |
| `cache->lock` | `CRITICAL_SECTION` | `ram_cache.c:32` | `cache->buffer`, `dirty_map`, `valid_map`, `hit/miss_count` |
| `g_eb_cs` | `CRITICAL_SECTION` | `event_bus.c:12` | Subscriber array (`g_subs`) |
| `g_ft_lock` | `CRITICAL_SECTION` | `fuse_bridge.c:43` | `g_file_table` array and `g_file_count` |

### Current Acquisition Points

| Lock | Acquired In | Released In |
|---|---|---|
| `g_state_cs` | `cmd_handler.c:212`, `main.c:12,81`, `daemon.c:59,147,165,187,215` | Corresponding `gs_unlock()` |
| `cache->lock` | `ram_cache.c:56 (cache_write)`, `:72 (cache_read)`, `:101 (cache_flush_all)` | Same functions |
| `g_eb_cs` | `event_bus.c:36 (subscribe)`, `:50 (unsubscribe)`, `:64 (publish)` | Same functions |
| `g_ft_lock` | `fuse_bridge.c:55 (find_free_slot)`, `:138 (find_file_by_name)`, `:146 (find_file_by_name)` | Same functions |

### Lock Ordering Analysis

**No nesting of different locks currently exists.** Each critical section is acquired and released within a single function scope. However:

1. **Potential for deadlock exists** if future code nests `g_state_cs` and `cache->lock` in different orders.
2. **`journal` has no lock** (D6 bug). Adding one creates another lock that could be nested.
3. **No documented order.** If `cache_flush_all` (holds `cache->lock`) calls into a function that needs `g_state_cs`, deadlock occurs if another thread holds `g_state_cs` and calls `cache_write` (needs `cache->lock`).

### Recommended Lock Order

```
ALWAYS: g_state_cs → g_eb_cs → g_ft_lock → cache_lock → journal_lock
```

Never acquire a lock earlier in this chain while holding a later lock.

---

## 4. Memory Ownership

### Ownership Map

| Resource | Allocator Function | Deallocator Function | Owner Module | Transferable? |
|---|---|---|---|---|
| `g_state` (global) | Compiler (BSS) | N/A | `cmd_handler` | No |
| `g_subs[]` (event bus) | Compiler (BSS) | N/A | `event_bus` | No |
| `g_file_table[]` (FUSE) | `calloc` in `fuse_bridge.c:33` | `free` in `fuse_bridge.c:588` | `fuse_bridge` | No |
| `DISK_INFO disks[]` | `calloc`/`realloc` in `device_refresh` | `free` in `device_cleanup` | `device_manager` | No |
| `cache->buffer` | `VirtualAlloc` in `cache_init:11` | `VirtualFree` in `cache_destroy:47` | `ram_cache` | No |
| `cache->dirty_map` | `malloc` in `cache_init:14` | `free` in `cache_destroy:44` | `ram_cache` | No |
| `cache->valid_map` | `malloc` in `cache_init:17` | `free` in `cache_destroy:45` | `ram_cache` | No |
| `cache->flush_buffer` | `VirtualAlloc` in `cache_init:25` | `VirtualFree` in `cache_destroy:46` | `ram_cache` | No |
| `vol->disks[]` pointers | Set by `volume_create`/`mirror`/`load`/`expand` | N/A (point to external DISK_INFO) | `volume_manager` | No (borrowed from device_manager) |
| Journal file HANDLE | `CreateFileW` in `journal_init` | `CloseHandle` in `journal_cleanup` | `journal` | No |
| Pool file HANDLEs | `CreateFileW` in `pool_create`/`pool_open` | `CloseHandle` in `pool_close` | `pool_io` (returned to caller) | Yes (handed to stripe/mirror engines) |
| Flush thread HANDLE | `_beginthreadex` in `raid_service.c:442` | Never closed (BUG C8) | `raid_service` | No (leaked) |
| GUI worker HANDLE | `_beginthreadex` in `gui.cpp` | `WaitForSingleObject` + `CloseHandle` in `gui.cpp` cleanup | `gui` | No |
| `OVERLAPPED` (I/O) | Stack allocation | Implicit on scope exit | Callee | No (BUG C6 — freed while kernel may reference) |
| Config file handle | `CreateFileW` in `config_load` / `config_save` | `CloseHandle` in same functions | `config` | No |

### Ownership Rules

1. **No dynamic ownership transfer** — all resources are owned by their creating module. No module passes ownership of a heap allocation to another module (except pool file HANDLEs).
2. **Borrowed pointers** — `vol->disks[]` points to `DISK_INFO` entries owned by `device_manager`. The volume does not own these pointers; they become dangling if `device_cleanup` is called before `volume_destroy`.
3. **Stack-allocated OVERLAPPED** — the most dangerous pattern. The I/O completion may reference stack memory after the allocating function returns.

---

## 5. Resource Lifecycle

### Lifecycle Table

| Resource | Init | Running | Shutdown | Error Recovery |
|---|---|---|---|---|
| **Global state** | `main()` → `raid_init()` sets state to DISCONNECTED | State transitions in `raid_*` functions | `raid_cleanup()` sets to DISCONNECTED | On any `raid_*` failure, state may be inconsistent |
| **Disk list** | `device_refresh()` allocates `disks[]` | Read-only after refresh | `device_cleanup()` frees array | On realloc failure, partial list (BUG M12) |
| **Cache** | `cache_init()` allocates all buffers, creates CS | `cache_write/read` ops, flush_thread runs | `cache_destroy()` frees all, deletes CS | Init failure leaks `valid_map` (BUG C7) |
| **Journal** | `journal_init()` opens/creates file | `journal_begin/data/commit` appends to file | `journal_cleanup()` closes file | None |
| **FUSE** | `fuse_mount()` inits lock, table | WinFsp callbacks | `fuse_unmount()` destroys lock, frees table | `DeleteCriticalSection` while callbacks active (BUG H6) |
| **Event bus** | `event_bus_init()` creates CS | Subscribe/unsubscribe/publish | NO cleanup function (BUG D9) | CS never deleted |
| **Pool files** | `pool_create()` opens with R/W + create | `pool_read/write` | `pool_close()` closes handle | On write error, no propagation (BUG D8) |
| **Superblock** | Written on `volume_create`/`mirror`/`expand` | Read on load/mount | Written on unmount | CRC check on every read |

### Lifecycle Ordering (Init)

```
main()
  → raid_init()
    → event_bus_init()        (no ordering dependency)
    → config_load()            (no ordering dependency)
    → logger_init()            (no ordering dependency)
  → cmd_handler_run() / gui_run()
    → raid_scan()              (requires event_bus)
      → device_refresh()
        → disk_scanner_init()
    → raid_init_pools()
      → pool_create()
    → raid_create() / raid_mirror()
      → cache_init()           (requires VirtualAlloc)
      → stripe_volume_create() / mirror_volume_create()
        → journal_init()
    → raid_mount()
      → fuse_mount()
```

### Lifecycle Ordering (Shutdown)

```
raid_cleanup()
  → cache_destroy()
    → WaitForSingleObject(flush_thread)
    → VirtualFree(buffer, flush_buffer)
    → free(dirty_map, valid_map)
    → DeleteCriticalSection(&cache->lock)
  → journal_cleanup()
    → CloseHandle(journal_file)
  → cleanup_all()
    → cleanup_scan_all_drives()   (BUG M14: scans ALL drives)
  → event_bus (NO CLEANUP)        (BUG D9)
  → config (NO CLEANUP)
  → logger (NO CLEANUP)
```

---

## 6. Execution Flows

### 6.1 Read Path (RAID0 — Stripe)

```
FUSE callback: raid_read (fuse_bridge.c:211)
  → stripe_volume_read (stripe_engine.c:468)
    → cache_read (ram_cache.c:68)
      ├── HIT:  memcpy from cache->buffer → return
      └── MISS: cache->miss_count++
    → stripe_volume_map_lba (stripe_engine.c:321)
      → find_segment_for_lba()
        → find_phase_for_lba()
          → compute_cycle → find_in_phase → apply_mapping
      → generate IO_MAPPING_ENTRY array
    → stripe_read_raw (stripe_engine.c:426) [or inline read]
      → pool_read (storage_common.c:18)
        → SetFilePointer + ReadFile (OVERLAPPED, sync wait)
    → cache_write (ram_cache.c:52) [populate cache]
      → memcpy into cache->buffer
      → set dirty_map + valid_map bits
```

### 6.2 Read Path (RAID1 — Mirror)

```
FUSE callback: raid_read (fuse_bridge.c:211)
  → mirror_volume_read (mirror_engine.c:30)
    → for each disk in vol->disks[]:
      → check disk->state == HEALTHY
      → pool_read (storage_common.c:18) on first healthy disk
      → on success: return data
      → on failure: InterlockedDecrement(&vol->healthy_count)
    → if no healthy disk: return RC_MIRROR_ALL_DEAD
```

### 6.3 Write Path (Write-Back Cache)

```
FUSE callback: raid_write (fuse_bridge.c:270)
  → stripe_volume_write (stripe_engine.c:494)
    → if cache.write_through: stripe_write_raw (immediate)
    → cache_write (ram_cache.c:52)
      → memcpy into cache->buffer
      → set dirty_map bits
      → return (deferred flush)
```

### 6.4 Write Path (Write-Through)

```
FUSE callback: raid_write (fuse_bridge.c:270)
  → stripe_volume_write (stripe_engine.c:494)
    → cache_write (ram_cache.c:52) [marks dirty + valid]
    → stripe_volume_map_lba (stripe_engine.c:321)
    → stripe_write_raw
      → pool_write (storage_common.c:30)
        → SetFilePointer + WriteFile (OVERLAPPED, sync wait)
```

### 6.5 Cache Flush Path

```
cache_flush_thread (ram_cache.c:210) [background, adaptive sleep]
  → cache_flush_all (ram_cache.c:86)
    → for each dirty block:
      → EnterCriticalSection(&cache->lock)
      → find consecutive dirty run
      → memcpy into cache->flush_buffer  (BUG H2: shared buffer)
      → clear dirty bits
      → LeaveCriticalSection(&cache->lock)
    → journal_begin (journal.c:35)
      → SetFilePointer + WriteFile(JOURNAL_MAGIC + gen)
    → journal_data (journal.c:56) [per batch]
      → SetFilePointer + WriteFile(offset, length, data_crc)
    → stripe_volume_map_lba (stripe_engine.c:321)
    → for each mapping entry:
      → SetFilePointer + WriteFile (disk writes via OVERLAPPED)
    → journal_commit (journal.c:78)
      → SetFilePointer + WriteFile(COMMIT_MAGIC + gen)
```

### 6.6 Journal Commit Path

```
journal_begin (journal.c:35)
  → write JOURNAL_MAGIC (0x4F555243)
  → write generation number
  → write range count

journal_data (journal.c:56)
  → write DATA_MAGIC (0x44415441)
  → write offset, length
  → write data payload (full block range)
  → write crc32 of payload

journal_commit (journal.c:78)
  → write COMMIT_MAGIC (0x434F4D4D)
  → write generation number
  → flush file buffers
  → increment generation
```

### 6.7 Journal Recovery Path

```
raid_init() → journal_recover_all (journal.c:98)
  → GetFileSizeEx → read entire journal
  → find highest generation number
  → for each entry at highest gen:
    → read entry type (BEGIN/DATA/COMMIT)
    → if DATA: extract offset + length + data
    → if COMMIT found: replay all DATA entries
    → stripe_volume_map_lba → WriteFile to disks
  → truncate journal file to 0
```

### 6.8 Init Cascade

```
main() → parse CLI flags
  → gui_run() [if --gui] or cmd_handler_run() [if --cli]
    → raid_init()
      → event_bus_init()
      → config_load()
      → logger_init()
      → (no further init — lazy on first command)
    → raid_scan() [first user command typically]
      → disk_scanner_scan()
      → bench_io (optional)
      → state: DISCONNECTED → DISCOVERED
    → raid_init_pools()
      → pool_create() per disk
      → state: DISCOVERED → INITIALIZED
    → raid_create() / raid_mirror()
      → stripe_volume_create() / mirror_volume_create()
        → calculate phase table (stripe)
        → write superblock
      → journal_init()
      → cache_init()
        → VirtualAlloc buffer
        → VirtualAlloc flush_buffer
        → _beginthreadex flush_thread (BUG C8: handle leak)
      → state: INITIALIZED → MOUNTED
    → raid_mount()
      → fuse_mount()
        → InitializeCriticalSection(&g_ft_lock) (BUG C4: double-init race)
        → calloc g_file_table
        → FUSE_FSP_mount
```

### 6.9 Shutdown Cascade

```
raid_cleanup()
  → cache_destroy()
    → cache->running = 0
    → WaitForSingleObject(flush_thread)  [join]
    → CloseHandle(flush_thread)
    → free(dirty_map, valid_map)
    → VirtualFree(buffer, flush_buffer)
    → DeleteCriticalSection(&cache->lock)
  → journal_cleanup()
    → CloseHandle(journal_handle)
  → cleanup_all()
    → cleanup_scan_all_drives()
      → iterate all drive letters A:-Z:
      → delete RAIDTEST\* files on each (BUG M14: no mount check)
  → (event_bus CS never deleted — BUG D9)
```

### 6.10 Write-Back Cache + FUSE Backpressure

```
When cache is full and FUSE write arrives:
  → stripe_volume_write → cache_write returns true (always, no capacity check)
  → [cache is full — data is NOT actually stored in cache]
  → [flush_thread is responsible for making room]
  → flush_thread eventually flushes dirty blocks
  → if cache remains full AND burst persists:
    → FUSE write completes successfully but data is silently dropped
    → [NO backpressure mechanism exists]
```

### 6.11 Degraded Mirror Read

```
mirror_volume_read (mirror_engine.c:30)
  → check vol->healthy_count
  → for each disk in vol->disks[]:
    → if disk->state == HEALTHY:
      → pool_read from this disk
      → on success: return data
      → on failure:
        → InterlockedDecrement(&vol->healthy_count)
        → continue to next disk
    → if disk->state == FAILED:
      → skip
  → if no healthy disk tried:
    → return RC_MIRROR_ALL_DEAD
  → if all healthy disks failed:
    → wait for rebuild or return error
```

### 6.12 Mirror Rebuild

```
raid_rebuild() → mirror_volume_rebuild (mirror_engine.c:150)
  → set state to RECOVERING
  → for each byte range in volume:
    → read from healthy disk via pool_read
    → write to replacement disk via pool_write
    → [no checksum verification during rebuild]
  → replace disk pointer: vol->disks[replace_idx] = replacement (BUG H12: non-atomic)
  → InterlockedIncrement(&vol->healthy_count)
  → set state to MOUNTED
```
