# RAIDTEST v3 — System Map

## 1. Overview

RAIDTEST v3 is a Windows software RAID implementation using an asymmetric stripe
algorithm (RAID0) and mirroring (RAID1) with write-back caching, write-ahead
journaling, and WinFsp FUSE mount. It supports up to 4 physical disks.

**Version**: v1.0 RC4 (superblock v4)  
**Language**: C (C11) with one C++ file (gui.cpp) for Dear ImGui + DirectX 11  
**Build**: MinGW-w64 (GCC), single `build.bat`  
**Platform**: Windows 10+ (x64)  
**License**: MIT

---

## 2. Source Tree Layout

```
raidv3/
├── src/                    # All source code (65 files)
│   ├── *.c, *.h            # C modules (31 pairs)
│   ├── gui.cpp, gui.h      # C++ GUI (Dear ImGui + DX11)
│   └── test_*.c, test_*.h  # Unit tests (6 test files + runner)
├── tests/                  # Test pool data, scripts
├── stress/                 # Stress test scripts
├── winfsp_headers/         # WinFsp FUSE SDK headers
├── imgui/                  # Dear ImGui library source
├── build/                  # Build artifacts
├── libwinfsp-x64.a         # WinFsp import lib
├── winfsp-x64.dll          # WinFsp runtime DLL
├── build.bat               # Single build script
├── build_asan.bat          # Build with AddressSanitizer
├── build_stress.bat        # Build stress test binaries
└── *.exe                   # Pre-built binaries in root
```

### Build Outputs

| Binary | Description |
|--------|-------------|
| `raidtest_winfsp.exe` | Main application (GUI or CLI) |
| `raidtest_tests.exe` | Unit test runner (39 tests) |
| `test_concurrent.exe` | Concurrent I/O stress test |
| `test_longrun.exe` | Long-running stability test |
| `test_metadata_corrupt.exe` | Metadata corruption recovery test |
| `test_powerfail.exe` | Power-fail / journal recovery test |
| `test_random_io.exe` | Random I/O stress test |

---

## 3. Architecture Layers

```
┌─────────────────────────────────────────────────────────────────────────┐
│  L7: FRONTEND                                                          │
│  ┌──────────────┐  ┌────────────────────┐  ┌────────────────────────┐  │
│  │  main.c       │  │  gui.cpp / gui.h   │  │  cmd_handler.c/h      │  │
│  │  (entry)      │  │  (ImGui + DX11)    │  │  (CLI dispatch)       │  │
│  └──────┬───────┘  └─────────┬──────────┘  └───────────┬────────────┘  │
│         │                    │                          │               │
│         └────────────────────┼──────────────────────────┘               │
│                              ▼                                          │
│  L6: SERVICE LAYER                                                     │
│  ┌────────────────────────────────────────────────────────────────────┐ │
│  │  raid_service.c/h  —  Unified backend API (30 public functions)   │ │
│  │  wizard.c/h        —  Interactive 8-step CLI wizard                │ │
│  │  daemon.c/h         —  Console daemon + Windows SCM service        │ │
│  └────────────────────────────────────────────────────────────────────┘ │
│                              │                                          │
│                              ▼                                          │
│  L5: MANAGER LAYER                                                     │
│  ┌──────────────┐  ┌──────────────┐  ┌────────────────┐               │
│  │ volume_mgr    │  │ device_mgr   │  │ metadata_mgr   │               │
│  │ .c/h          │  │ .c/h         │  │ .c/h           │               │
│  └──────┬───────┘  └──────┬───────┘  └───────┬────────┘               │
│         │                 │                   │                        │
│         ▼                 ▼                   ▼                        │
│  L4: ENGINE LAYER                                                      │
│  ┌────────────┐  ┌──────────────┐  ┌─────────────────────────────┐    │
│  │stripe_engine│  │mirror_engine │  │planner_engine.c/h           │    │
│  │.c/h (RAID0) │  │.c/h (RAID1)  │  │(capacity calculator)        │    │
│  └──────┬─────┘  └──────┬───────┘  └─────────────────────────────┘    │
│         │               │                                              │
│         ▼               ▼                                              │
│  L3: STORAGE LAYER                                                    │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐               │
│  │ storage_common│  │ ram_cache    │  │ journal      │               │
│  │ .c/h          │  │ .c/h (WBC)  │  │ .c/h (WAL)   │               │
│  │ (async I/O)   │  │              │  │              │               │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘               │
│         │                 │                  │                        │
│         ▼                 ▼                  ▼                        │
│  L2: DISK I/O LAYER                                                   │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐               │
│  │ pool_io      │  │ disk_scanner │  │ bench_io     │               │
│  │ .c/h         │  │ .c/h         │  │ .c/h         │               │
│  │ (pool files)  │  │ (IOCTL scan) │  │ (benchmark)  │               │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘               │
│         │                 │                  │                        │
│         ▼                 ▼                  ▼                        │
│  L1: OS / HARDWARE INTERFACE                                          │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐               │
│  │ WinFsp FUSE  │  │ Windows API  │  │ DirectX 11   │               │
│  │ (fuse_bridge) │  │ (CreateFile,  │  │ (gui.cpp)    │               │
│  │              │  │  IOCTL, etc)  │  │              │               │
│  └──────────────┘  └──────────────┘  └──────────────┘               │
└─────────────────────────────────────────────────────────────────────────┘

L0: CROSS-CUTTING INFRASTRUCTURE
┌─────────────────────────────────────────────────────────────────────────┐
│  event_bus.c/h  │  config.c/h  │  logger.c/h  │  profiler.c/h         │
│  (pub/sub)       │  (JSON)      │  (thread-safe)│  (perf tracking)     │
│  cleanup.c/h     │  crc32.c/h   │  uuid.c/h    │  superblock.c/h      │
│  (resource mgmt) │  (checksum)  │  (UUID gen)  │  (on-disk format)    │
│  ui_model.c/h    │  raid_query.c/h                                   │
│  (GUI data model)│  (state queries)                                   │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 4. Module Reference

### 4.1 Frontend Layer

| Module | File(s) | Role |
|--------|---------|------|
| **Entry Point** | `main.c` | Argument parsing, mode selection (GUI/CLI/daemon/service/auto), lifecycle |
| **GUI** | `gui.cpp`, `gui.h` | Dear ImGui + DirectX 11 frontend. Three-mode panel (Beginner/Advanced/Developer). Calls `raid_*()` via worker thread. 1700+ lines. |
| **CLI Dispatcher** | `cmd_handler.c`, `cmd_handler.h` | 31-command CLI parser. Defines `APP_STATE` (global `g_state`). Calls `raid_*()`. |
| **UI Model** | `ui_model.c`, `ui_model.h` | Read-only state snapshots for GUI rendering (disks, volume, health). |

### 4.2 Service Layer

| Module | File(s) | Role |
|--------|---------|------|
| **Raid Service** | `raid_service.c`, `raid_service.h` | Unified backend API — 30 public functions. Initializes subsystems, dispatches to managers/engines. All CLI and GUI commands funnel through here. |
| **Wizard** | `wizard.c`, `wizard.h` | Interactive 8-step CLI setup wizard (scan → select → init → create → cache → mount). |
| **Daemon** | `daemon.c`, `daemon.h` | Console daemon loop + Windows Service Control Manager (SCM) registration. |

### 4.3 Manager Layer

| Module | File(s) | Role |
|--------|---------|------|
| **Volume Manager** | `volume_manager.c`, `volume_manager.h` | Volume lifecycle: create, mirror, load, mount, unmount, destroy, expand, rebuild. Orchestrates metadata, pool_io, fuse, journal, cache. |
| **Device Manager** | `device_manager.c`, `device_manager.h` | Disk list abstraction: refresh, bench, select, map drive letters, query health/capacity/speed. |
| **Metadata Manager** | `metadata_manager.c`, `metadata_manager.h` | Superblock read/write/validate/upgrade/dump. Restores volume state from on-disk metadata. |

### 4.4 Engine Layer

| Module | File(s) | Role |
|--------|---------|------|
| **Stripe Engine** | `stripe_engine.c`, `stripe_engine.h` | Asymmetric RAID0 LBA mapping. Arbitrary disk sizes → phased I/O mapping. Async OVERLAPPED I/O. |
| **Mirror Engine** | `mirror_engine.c`, `mirror_engine.h` | RAID1 mirror write (all disks), degraded read (skip faulty), rebuild (copy from good to replacement). |
| **Planner Engine** | `planner_engine.c`, `planner_engine.h` | Pure calculation: RAID0/1/10 capacity and efficiency from disk list. No I/O. |

### 4.5 Storage Layer

| Module | File(s) | Role |
|--------|---------|------|
| **Storage Common** | `storage_common.c`, `storage_common.h` | Async OVERLAPPED ReadFile/WriteFile helpers, sector alignment, handle management. |
| **RAM Cache** | `ram_cache.c`, `ram_cache.h` | Write-back cache: block-based (64 KB), dirty/valid bitmaps, flush thread, write-through mode. |
| **Journal (WAL)** | `journal.c`, `journal.h` | Write-ahead journal for crash recovery. Records pending I/O for replay after failure. |

### 4.6 Disk I/O Layer

| Module | File(s) | Role |
|--------|---------|------|
| **Pool I/O** | `pool_io.c`, `pool_io.h` | Pool file management: create, open, close, delete `stripe_pool.dat` on each disk. |
| **Disk Scanner** | `disk_scanner.c`, `disk_scanner.h` | Physical disk enumeration via `IOCTL_STORAGE_QUERY_PROPERTY`, `GetLogicalDrives`, volume discovery. |
| **Bench I/O** | `bench_io.c`, `bench_io.h` | Sequential read/write benchmark with sector-aligned I/O and timing. |

### 4.7 FUSE / Mount Layer

| Module | File(s) | Role |
|--------|---------|------|
| **FUSE Bridge** | `fuse_bridge.c`, `fuse_bridge.h` | WinFsp FUSE filesystem callbacks (getattr, read, write, create, rename, etc.). Flat 64-entry file table. Delegates I/O to stripe/mirror engine. |

### 4.8 Cross-Cutting Infrastructure

| Module | File(s) | Role |
|--------|---------|------|
| **Event Bus** | `event_bus.c`, `event_bus.h` | Publish/subscribe event system. 13 event types (DISK_FOUND, VOLUME_CREATED, MOUNT, ERROR, etc.). |
| **Config** | `config.c`, `config.h` | JSON save/load for disk selection, cache size, mount letter, theme. |
| **Logger** | `logger.c`, `logger.h` | Thread-safe file + console logging. Levels: DEBUG, INFO, OK, WARN, ERROR. |
| **Profiler** | `profiler.c`, `profiler.h` | I/O throughput/latency tracking. |
| **Cleanup** | `cleanup.c`, `cleanup.h` | Graceful resource release: close handles, free memory, delete critical sections. |
| **Superblock** | `superblock.c`, `superblock.h` | On-disk v4 format: magic, version, gen count, UUID, stripe unit, disk layout, CRC32 checksum. |
| **CRC32** | `crc32.c`, `crc32.h` | CRC32-Castagnoli checksum for superblock integrity. |
| **UUID** | `uuid.c`, `uuid.h` | UUID v4 generation for volume identification. |
| **Raid Query** | `raid_query.c`, `raid_query.h` | State query helpers for info/status commands. |
| **Common** | `common.h` | Shared types: `STRIPE_VOLUME`, `DISK_INFO`, `RAM_CACHE`, `MAPPING_PHASE`, `RAID_STATE`, `RC` error codes, macros. |

---

## 5. Data Flow

### 5.1 Write Path (FUSE → Cache → Journal → Disk)

```
FUSE callback (fuse_bridge.c)
    │
    ▼
stripe_engine_write() or mirror_engine_write()
    │  Splits request into per-disk I/Os based on LBA mapping
    ▼
ram_cache_write()
    │  Writes to cache buffer, marks dirty
    │  If write-through: bypasses cache, goes directly to journal
    ▼
journal_write()
    │  Records entry in WAL file
    ▼
storage_common_write()
    │  Async OVERLAPPED WriteFile on each disk
    ▼
Cache flush thread (periodic):
    ram_cache_flush() → journal_commit() → storage_common_write()
    │  Reads dirty blocks from cache, commits journal, writes to disk
    ▼
Disk pool files (stripe_pool.dat)
```

### 5.2 Read Path (FUSE → Cache → Disk)

```
FUSE callback (fuse_bridge.c)
    │
    ▼
stripe_engine_read() or mirror_engine_read()
    │  Splits request into per-disk I/Os
    ▼
ram_cache_read()
    │  Check valid_map → hit: return from cache, miss: read from disk
    │  On miss, reads into cache buffer, marks valid
    ▼
storage_common_read()
    │  Async OVERLAPPED ReadFile on each disk
    ▼
Disk pool files
```

### 5.3 Mount Flow

```
raid_mount(letter)
    │
    ▼
volume_mount()
    │
    ├── fuse_mount(letter)  → WinFsp FUSE filesystem registration
    │
    ├── ram_cache_init()     → Allocate cache buffer, start flush thread
    │
    └── journal_recover()    → Replay any uncommitted WAL entries
```

### 5.4 State Transitions

```
                    ┌──────────────┐
                    │ DISCONNECTED │  ← Initial state (no config)
                    └──────┬───────┘
                           │ raid_scan()
                           ▼
                    ┌──────────────┐
                    │  DISCOVERED  │  ← Disks found, not initialized
                    └──────┬───────┘
                           │ raid_init_pools()
                           ▼
                    ┌───────────────┐
                    │ INITIALIZED   │  ← Pool files created on disks
                    └───────┬───────┘
                            │ raid_create() / raid_mirror()
                            ▼
                    ┌───────────────┐
                    │ (no state)    │  ← Volume object created in memory
                    └───────┬───────┘
                            │ raid_mount()
                            ▼
                    ┌──────────────┐
                    │   MOUNTED    │  ← FUSE active, cache running
                    └──────┬───────┘
                     ▲     │     ▲
                     │     │     │
               raid_ │     │     │ disk failure (RAID1)
               unmount│     │     │
                     │     │     │
                     │     ▼     │
                     │  ┌────────────┐
                     │  │  DEGRADED  │  ← RAID1 with failed disk(s)
                     │  └──────┬─────┘
                     │         │ raid_rebuild()
                     │         ▼
                     │  ┌──────────────┐
                     │  │  RECOVERING  │  ← Rebuild in progress
                     │  └──────┬───────┘
                     │         │ rebuild complete
                     │         ▼
                     │  ┌────────────┐
                     └──│  MOUNTED   │  ← Back to healthy
                        └────────────┘
```

---

## 6. State Machine

The system state is tracked in `g_state.rt.state` (type `RAID_STATE`):

| State | Value | Meaning | Transitions |
|-------|-------|---------|-------------|
| `DISCONNECTED` | 0 | No config, no disks scanned | → `DISCOVERED` (scan) |
| `DISCOVERED` | 1 | Disks found, none initialized | → `INITIALIZED` (init), → `DISCONNECTED` (purge) |
| `INITIALIZED` | 2 | Pool files created | → `MOUNTED` (create + mount), → `DISCONNECTED` (destroy) |
| `MOUNTED` | 3 | FUSE active, accepting I/O | → `DEGRADED` (disk fail), → `DISCONNECTED` (unmount + destroy) |
| `DEGRADED` | 4 | RAID1 with failed disk | → `RECOVERING` (rebuild start), → `MOUNTED` (if all fail, treat as...), → `DISCONNECTED` |
| `RECOVERING` | 5 | Rebuild in progress | → `MOUNTED` (rebuild done), → `DEGRADED` (rebuild fail) |
| `UNMOUNTED` | 6 | Volume exists but not mounted | → `MOUNTED` (mount), → `DISCONNECTED` (destroy) |

Note: `UNMOUNTED` is declared but appears unused in the main code paths
(no `raid_unmount()` → `UNMOUNTED` transition is visible; unmount typically
proceeds directly to destroy or cleanup).

---

## 7. Thread Model

| Thread | Created In | Role | Locks Held |
|--------|-----------|------|------------|
| **Main / CLI** | `main.c` | Argument dispatch, CLI REPL loop, GUI main loop | `g_state_cs` (via `gs_lock()`/`gs_unlock()`) |
| **FUSE Worker** | WinFsp (internal pool) | Services FUSE callbacks (read, write, getattr, etc.) | `g_state_cs`, `g_file_table_lock` |
| **Cache Flush** | `ram_cache.c` (at init) | Periodically flushes dirty cache blocks to disk | `cache->lock`, `g_journal_cs` |
| **GUI Worker** | `gui.cpp` (per action) | Runs `raid_*()` calls off the UI thread | `g_state_cs` (calls `raid_*`) |
| **Logger** | Main thread | Serialized via `g_log_lock` (leaf lock) | `g_log_lock` only |

---

## 8. Locking Architecture

### Lock Order (MUST be enforced)

```
g_state_cs  →  cache->lock  →  g_journal_cs  →  g_eb_cs
                                                     ↓
                                              g_file_table_lock
                                                     ↓
                                              g_log_lock
                                                     ↓
                                              g_gui.log_lock
```

### Lock Table

| Lock | Symbol | Defined In | Protects |
|------|--------|------------|----------|
| State | `g_state_cs` | `common.h:15` | `g_state` (volume, disks, runtime flags) |
| Cache | `cache->lock` | `common.h:217` | RAM cache buffer, dirty/valid maps |
| Journal | `g_journal_cs` | `journal.c:5` | Journal file writes |
| Event Bus | `g_eb_cs` | `event_bus.c:12` | Subscriber list during publish |
| File Table | `g_file_table_lock` | `fuse_bridge.c:37` | FUSE open file table |
| Log | `g_log_lock` | `logger.c:7` | Log file writes |
| GUI Log | `g_gui.log_lock` | `gui.cpp:70` | GUI event log buffer |

---

## 9. Key Data Structures

### STRIPE_VOLUME (common.h:223)
The central volume descriptor. Contains disk pointers, stripe mapping phases,
cache, I/O stats, mount point, RAID level. One instance stored in `g_state.vol.volume`.

### DISK_INFO (common.h:161)
Per-disk descriptor. Device path, drive letter, model, type, capacity,
speed benchmarks, health indicators, pool file path, handle.

### MAPPING_PHASE (common.h:189)
Stripe phase descriptor. Active disks in this phase, their capacity ratios,
virtual ↔ physical LBA translation, cycle size. Up to 4 phases (one per disk).

### RAM_CACHE (common.h:206)
Write-back cache. Buffer, dirty/valid bitmaps, flush thread handle,
write-through flag, hit/miss counters.

### APP_STATE (cmd_handler.h)
Global state struct containing:
- `cfg` — `APP_CONFIG` (persisted JSON settings)
- `rt` — runtime state (`RAID_STATE`, mount status, flags)
- `vol` — `STRIPE_VOLUME` (with embedded `RAM_CACHE` + disks)
- `bench` — benchmark data
- `eb` — event bus
- `disks` — `DISK_INFO` array (8 entries max)

---

## 10. Asymmetric Stripe Algorithm

The RAID0 engine handles disks of unequal sizes using a **phased mapping**:

1. Calculate GCD of all disk capacities
2. For each phase, assign disks that still have remaining capacity
3. Within a phase, I/O is striped across active disks proportional to their
   size ratios
4. When a disk fills, it drops out of subsequent phases

Example (3 disks: 500 GB, 300 GB, 200 GB):
- Phase 0: all 3 disks, ratios 5:3:2, cycle = 10 logical units
- Phase 1: 2 disks (300 GB full), ratios 3:2, cycle = 5 logical units
- Phase 2: 1 disk (200 GB full), single disk, no striping

---

## 11. WinFsp FUSE Integration

| Aspect | Detail |
|--------|--------|
| **Library** | WinFsp v2.0 (FUSE API compatibility layer) |
| **Mount type** | `FUSE_FSCTL` (userspace) |
| **Drive letter** | Configurable (default G:) |
| **File table** | Flat 64-entry array, O(n) lookup |
| **Implemented ops** | `getattr`, `readdir`, `open`, `create`, `read`, `write`, `flush`, `release`, `rename`, `mkdir`, `rmdir`, `unlink`, `statfs` |
| **Threading** | WinFsp dispatches callbacks from internal thread pool |

---

## 12. Superblock Format (v4)

| Field | Size | Description |
|-------|------|-------------|
| Magic | 4 bytes | `0x52444953` ("RDIS") |
| Version | 4 bytes | Superblock version (4) |
| Volume UUID | 16 bytes | Unique volume identifier |
| Generation | 8 bytes | Monotonic generation counter |
| RAID Level | 4 bytes | 0 = stripe, 1 = mirror |
| Disk count | 4 bytes | Number of disks in volume |
| Stripe unit | 4 bytes | Stripe unit size (default 1 MB) |
| Per-disk entries | var | Disk index, capacity, offset |
| CRC32 | 4 bytes | Checksum of entire superblock |

---

## 13. Build System

| Command | Output | Description |
|---------|--------|-------------|
| `build.bat` | `raidtest_winfsp.exe`, `raidtest_tests.exe` | Release build |
| `build_asan.bat` | `raidtest_winfsp.exe`, `raidtest_tests.exe` | AddressSanitizer build |
| `build_stress.bat` | Stress test binaries | Stress test build |

Compiler: `x86_64-w64-mingw32-gcc` / `g++` (MinGW-w64 GCC)  
Linker flags: `-limagehlp -lshlwapi -lwinfsp-x64` (plus DirectX for GUI)  
Test runner: Custom (`test_runner.c`), no external framework

---

## 14. Test Infrastructure

| Test Suite | File | Tests | Description |
|-----------|------|-------|-------------|
| Cache | `test_cache.c` | 10 | RAM cache init, read/write/hit/miss, flush, write-through |
| Journal | `test_journal.c` | 4 | Journal init, write, sync, recover |
| Mirror | `test_mirror.c` | 4 | Mirror engine read/write, rebuild |
| Stripe | `test_stripe.c` | 4 | Stripe engine init, read/write, mapping |
| Superblock | `test_superblock.c` | 12 | Superblock init, read/write/validate, CRC, corrupt detection |
| Common | `test_common.c` | 5 | Helper functions, boundary checks |
| **Total** | | **39** | All passing (2026-07-07) |

### Integration Tests

| Binary | Purpose |
|--------|---------|
| `test_concurrent.exe` | Concurrent I/O stress (multiple threads) |
| `test_longrun.exe` | Long-duration stability test |
| `test_metadata_corrupt.exe` | Metadata corruption + recovery |
| `test_powerfail.exe` | Power-fail / journal replay |
| `test_random_io.exe` | Random I/O pattern stress |

---

## 15. Known Bug Hotspots (P0/P1)

| ID | File | Issue |
|----|------|-------|
| B1 | `storage_common.c` | OVERLAPPED on stack → async use-after-free |
| B2 | `fuse_bridge.c` | `parent_dir_exists()` no path bounds check |
| B3 | `fuse_bridge.c` | `raid_rename()` no path bounds check |
| B4 | `fuse_bridge.c` | `file_table_lock_init()` double-checked locking race |
| B5 | `fuse_bridge.c` | `DeleteCriticalSection` while callbacks active |
| B6 | `journal.c` | Journal writes lack lock → concurrent corruption |
| B7 | `event_bus.c` | Missing `DeleteCriticalSection` (leak) |
| B8 | `raid_service.c` | 8 `device_get()` call sites without NULL check |
| B9 | multiple | ~25 `vol->disks[i]` accesses without NULL check |

## 16. Configuration File

The app stores persistent configuration at:
`%USERPROFILE%\.config\RAIDTEST\config.json`

Fields: disk configs, cache size, mount letter, auto-bench, theme, language,
auto-restore, auto-mount, first_run flag.
