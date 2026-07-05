# RAIDTEST v3 — Architecture

## Overview

RAIDTEST v3 is a Windows software RAID engine implementing RAID0 (stripe) and
RAID1 (mirror) with write-back cache, journaling, and WinFsp-based mount.

## Component Map (Sprint 5 — Manager Architecture)

```
                        CLI (cmd_handler.c)
                              │  (thin wrappers)
                              ▼
                       Raid Service (raid_service.h/c)
                              │
            ┌─────────────────┼─────────────────┐
            ▼                 ▼                  ▼
     Device Manager      Volume Manager     Planner Engine
     (device_manager)    (volume_manager)   (planner_engine)
            │                 │                  │
            └─────────────────┼──────────────────┘
                              ▼
                    Metadata Manager (metadata_manager)
                              │
            ┌─────────────────┼─────────────────┐
            ▼                 ▼                  ▼
     disk_scanner.c     stripe_engine.c     superblock.c
     pool_io.c          mirror_engine.c
     bench_io.c         ram_cache.c
     fuse_bridge.c      journal.c
                         pool_io.c

     ─── Event Bus (event_bus) ───
     Logger subscribes → writes file
     CLI subscribes → displays events
     GUI (future) subscribes → live updates

     ─── UI Model (ui_model) ───
     Read-only queries for GUI

+-------------+   +-----------+   +----------+
|   daemon    |-->|  cleanup  |-->|  config  |
| (auto-run)  |   | (teardown)|   | (JSON)   |
+-------------+   +-----------+   +----------+

+-------------+   +-----------+
|  bench_io   |   |  logger   |
| (benchmark) |   | (log file)|
+-------------+   +-----------+
```

### Layer Descriptions

| Layer | File(s) | Responsibility |
|-------|---------|----------------|
| **CLI** | `cmd_handler.c/h` | Command parsing, dispatch to Raid Service (thin layer) |
| **Raid Service** | `raid_service.c/h` | **Single API for GUI/CLI** — state checking, orchestrates managers |
| **Device Manager** | `device_manager.c/h` | Centralized disk operations (scan, find, status, capacity) |
| **Volume Manager** | `volume_manager.c/h` | Volume lifecycle (create, mirror, load, mount, destroy, expand, rebuild) |
| **Metadata Manager** | `metadata_manager.c/h` | Superblock read/write/validate/dump, delegates to superblock.c |
| **Planner Engine** | `planner_engine.c/h` | Capacity calculation (pure math, no CLI/IO dependency) |
| **UI Model** | `ui_model.c/h` | Read-only state queries for GUI (disk summary, volume info, health) |
| **Event Bus** | `event_bus.c/h` | Publish/subscribe for system events (13 event types) |
| **Engine** | `stripe_engine.c`, `mirror_engine.c` | Striping/mirroring logic, phase mapping, I/O |
| **Cache** | `ram_cache.c/h` | Write-back cache with dirty tracking, flush thread |
| **Metadata** | `superblock.c/h` | Persistent volume metadata (v4 format, UUID, serial) |
| **Journal** | `journal.c/h` | Write-ahead journal for crash recovery |
| **Storage** | `pool_io.c/h` | Pool file create/open/close/delete |
| **Discovery** | `disk_scanner.c/h` | Physical disk enumeration (STORAGE_QUERY) |
| **Benchmark** | `bench_io.c/h` | Disk speed measurement |
| **Mount** | `fuse_bridge.c` | WinFsp filesystem mount point |
| **Config** | `config.c/h` | JSON config save/load |
| **Logging** | `logger.c/h` | Console & file logging |
| **Cleanup** | `cleanup.c/h` | Session/volume/pool teardown, orphan cleanup |
| **Daemon** | `daemon.c/h` | Auto-daemon thread (run loop, state polling) |
| **GUI** | `gui.c`, `wizard.c` | Win32 dialog-based GUI (inactive, Sprint 6+) |
| **Defrag** | `defrag/` | Disk defragmentation helper (inactive) |
| **BenchFS** | `benchfs/` | Filesystem benchmark helper (inactive) |

## Data Flow

### Write Path
```
cmd_write → stripe_engine_write → ram_cache_store → (async) pool_io_write
                                                          ↓
                                                     journal_append
```

### Read Path
```
cmd_read → stripe_engine_read → ram_cache_lookup (miss → pool_io_read)
```

## State Machine

```
DISCONNECTED ──→ DISCOVERED ──→ INITIALIZED ──→ MOUNTED ←→ DEGRADED
      ↑                ↑              ↑              │          │
      │                └── UNMOUNTED ←┘              │          │
      │                      │                       │          │
      └──────────────────────┴─────── RECOVERING ←───┘          │
                                          ↑                     │
                                          └─────────────────────┘
```

States enforced in `cmd_handler.c` via `require_state()` gate at the top of
each command function. See `RAID_STATE` enum in `common.h`.

| State | Meaning |
|-------|---------|
| `DISCONNECTED` | No disks scanned; fresh start or post-error |
| `DISCOVERED` | Physical disks enumerated via `scan` |
| `INITIALIZED` | Pool files created, volume built |
| `MOUNTED` | Volume mounted and I/O-ready |
| `UNMOUNTED` | Volume unmounted but pool files preserved |
| `DEGRADED` | Mirror member missing; read-only |
| `RECOVERING` | Rebuild in progress |

## Pool File Layout

Each physical disk in the volume carries a copy of all pool files under
`<drive>:\RAIDTEST\`:

```
<drive>:\RAIDTEST\
  ├── superblock.dat      — v4 metadata (one per disk, same content)
  ├── stripe_pool.dat     — data pool (one per disk, disjoined extents)
  ├── mirror_pool.dat     — mirror copy (RAID1 only)
  └── journal.dat         — write-ahead journal (one per pool)
```

Superblock is stored per-disk for redundancy; all copies are scanned on `load`
and the highest-generation copy is elected.

## Build

- `build.bat` — Dual-target: clang-cl (MSVC ABI) + MinGW GCC
- No runtime dependencies beyond Windows and WinFsp
