# SPRINT10C_REPORT.md — Architecture Consistency Report

**Date:** 2026-07-06
**Based on:** Cross-referenced findings from `LOW_LEVEL_CALL_AUDIT.md`, `WORKFLOW_DUPLICATION.md`, `STATE_ACCESS_AUDIT.md`, `MANAGER_BOUNDARY_REPORT.md`
**Method:** Conclusions backed by exact source code line numbers only.

---

## Q1: Is raid_service still acting as a God Service?

**Answer: YES — significant god-service characteristics remain.**

### Evidence

**1. raid_service.c orchestrates ALL lifecycle stages directly (156+ state accesses via `S()`):**
- `raid_init()` (lines 59-83) — initializes config, appdata, event bus, state machine
- `raid_scan()` (lines 95-114) — disk discovery
- `raid_init_pools()` (lines 159-290) — pool file creation (20+ state accesses)
- `raid_create()` / `raid_mirror()` (lines 293-309) — volume creation
- `raid_load()` (lines 355-379) — volume loading from metadata
- `raid_mount()` / `raid_unmount()` (lines 337-353) — mount lifecycle
- `raid_destroy()` / `raid_purge()` (lines 381-406) — teardown
- `raid_cache()` (lines 409-444) — cache management (inlines `volume_cache_enable()` logic)
- `raid_info()` / `raid_status()` / `raid_map()` / `raid_test()` / `raid_random()` / `raid_benchfs()` / `raid_check()` (lines 447-650) — diagnostics and queries
- `raid_simulate()` (lines 652-690) — fault injection
- `raid_config_save()` / `raid_config_load()` (lines 750-773) — persistence
- `raid_quick()` (lines 781-808) — full one-shot setup (create + cache + mount)

**2. raid_service.c calls low-level modules directly, bypassing managers:**

| Line | Direct Call | Bypasses |
|------|-------------|----------|
| 433 | `cache_init()` | `volume_cache_enable()` |
| 439 | `_beginthreadex(..., cache_flush_thread, ...)` | `volume_cache_enable()` |
| 562 | `stripe_volume_dump_mapping()` | No manager wrapper exists |
| 568 | `stripe_volume_verify_io()` | No manager wrapper exists |
| 581 | `stripe_volume_random_test()` | No manager wrapper exists |
| 634 | `superblock_read_raw()` | `metadata_read()` |

**3. raid_service.c duplicates workflows found elsewhere:**
- Cache init (lines 433-439) duplicates `volume_cache_enable()` at `volume_manager.c:114-122`
- Volume create → cache → mount sequence (lines 794-804) has 3 other implementations (wizard.c:113-133, daemon.c:128-151, main.c:31-39)

**4. raid_service.c directly mutates state struct fields (~90 write accesses) instead of using setters:**
- `S()->rt.state = STATE_*` (20+ occurrences)
- `S()->vol.volume_valid = true/false` (7 occurrences)
- `S()->vol.volume.cache.write_through = ...` (line 413)
- `S()->vol.volume.cache_enabled = true` (line 436)
- `S()->vol.volume.cache.flush_thread = ...` (line 439)

**Verdict: YES.** raid_service.c is ~800 lines, handles every lifecycle aspect, calls low-level APIs directly, mutates state fields directly, and contains duplicated workflow logic. It is the single point of control for the entire application.

---

## Q2: Does daemon duplicate service logic?

**Answer: YES — significant duplication with raid_service.**

### Evidence

**1. Daemon bypasses `raid_mount()`:**

| Step | daemon.c | raid_service.c raid_mount() |
|------|----------|---------------------------|
| Mount call | `fuse_mount_volume()` (line 151, 195) | `volume_mount()` → `fuse_mount_volume()` + `EVENT_MOUNT` |
| Event publish | None | `event_bus_publish(EVENT_MOUNT, ...)` inside `volume_mount()` |
| `mounted` flag | `state->rt.mounted = true` (line 152) | `S()->rt.mounted = true` (line 342) |

The daemon loses `EVENT_MOUNT` notifications entirely.

**2. Daemon duplicates the restore-from-config workflow:**

| Implementation | File:Lines | Approach |
|----------------|------------|----------|
| daemon fallback | `daemon.c:92-141` | `config_load()` → `disk_scan_all()` → `volume_create_pool_file()` loop → `volume_create()` |
| cmd_handler auto | `cmd_handler.c:174-190` | `config_load()` → `raid_scan()` → tells user to "load" |
| main.c do_restore | `main.c:12-40` | Config-driven CLI replay → `cmd_process("create")` → `cmd_cache()` → `cmd_mount()` |

**3. Daemon duplicates cleanup sequence within itself:**

| Location | Lines | Sequence |
|----------|-------|----------|
| `daemon_run()` | 164-165 | `cleanup_session(state); cleanup_disks(state);` |
| `daemon_main()` | 212-213 | `cleanup_session(state); cleanup_disks(state);` |

**Verdict: YES.** The daemon duplicates mount logic (bypassing event notifications), restore logic (3rd implementation), and cleanup logic (duplicated within itself).

---

## Q3: Does wizard bypass service?

**Answer: YES — the wizard is the worst offender.**

### Evidence

**1. wizard.c bypasses device_manager for EVERY disk operation (6 direct calls):**

| Line | Direct Call | Should Use |
|------|-------------|------------|
| 39 | `disk_scan_free()` | `device_cleanup()` |
| 43 | `disk_scan_all()` | `device_refresh()` |
| 46 | `disk_print_list()` | `device_print_list()` |
| 65 | `disk_select()` | `device_select()` |
| 75 | `disk_map_drive()` | `device_map_drive()` |
| 86 | `disk_print_list(bench_map)` | `device_print_list()` |

**2. wizard.c bypasses volume_manager for mounting:**

| Line | Direct Call | Should Use |
|------|-------------|------------|
| 133 | `fuse_mount_volume()` | `volume_mount()` |

This loses `EVENT_MOUNT` publication and manually sets `state->rt.mounted = true`.

**3. wizard.c calls bench_io directly:**

| Line | Call |
|------|------|
| 83 | `bench_single_disk()` |

No manager wrapper exists for benchmarking, but this is a direct low-level call nonetheless.

**Verdict: YES.** The wizard bypasses TWO managers (device_manager and volume_manager) for 7+ operations. It is the most significant bypass pattern in the codebase.

---

## Q4: Are managers actually used?

**Answer: YES and NO — varies by manager.**

### Used correctly

| Manager | Status |
|---------|--------|
| **event_bus** | All 4 public functions used. 0 bypasses. **CLEAN.** |
| **planner_engine** | Both public functions used. 0 bypasses. **CLEAN.** |
| **volume_manager** | 11 of 13 functions used. 2 minor issues (`volume_gen_uuid` internal-only, BUT `volume_cache_enable()` is **bypassed** by `raid_cache()`). **MOSTLY USED but bypassed.** |
| **metadata_manager** | 4 of 5 functions used. `metadata_upgrade()` is dead code (no-op stub). **NEARLY CLEAN.** |

### Not used

| Manager | Status |
|---------|--------|
| **ui_model** | ALL 5 public functions are dead code. Zero callers anywhere. **ENTIRELY UNUSED.** |

### Bypassed despite being available

| Manager | Bypass Count | Key Bypassers |
|---------|-------------|---------------|
| **device_manager** | 10 sites | wizard.c (6), daemon.c (2), main.c (1), cleanup.c (1) |
| **volume_manager** | 7 sites | cleanup.c (4), raid_service.c (2), daemon.c (2), wizard.c (1) |
| **metadata_manager** | 1 site | raid_service.c:634 |

**Verdict: PARTIAL.** event_bus and planner_engine are properly used. volume_manager and metadata_manager are mostly used but have specific bypasses. device_manager is consistently bypassed by the wizard. ui_model is completely unused.

---

## Q5: Any circular dependency remaining?

**Answer: NO — all circular dependencies have been resolved.**

### Evidence

- `common.h` defines all shared types (`STRIPE_VOLUME`, `DISK_INFO`, etc.) — no engine includes another engine's header
- `volume_manager.h` includes `common.h` and `stripe_engine.h` — the direction is **high → low** (no reverse include)
- `device_manager.h` includes `common.h` and `disk_scanner.h` — **high → low**
- `metadata_manager.h` includes `common.h` and `superblock.h` — **high → low**
- `mirror_engine.h` includes `common.h` only — no dependency on stripe_engine
- `stripe_engine.h` includes `common.h` only — no dependency on mirror_engine
- `storage_common.h` provides `stripe_read_raw/write_raw` — breaks the former cyclic dependency between stripe_engine and mirror_engine
- `cleanup.h` includes `common.h` only — does not create cycles
- `raid_service.h` includes 6 headers but all directions are **high → low**:
  ```
  raid_service.h → metadata_manager.h → superblock.h
  raid_service.h → volume_manager.h → stripe_engine.h
  raid_service.h → planner_engine.h
  raid_service.h → disk_scanner.h
  raid_service.h → bench_io.h
  ```

**Verdict: NO circular dependencies.** The include graph is a DAG (Directed Acyclic Graph). All arrows point from higher-level abstractions to lower-level implementations.

---

## Q6: Any duplicated lifecycle?

**Answer: YES — three lifecycle patterns are duplicated.**

### Duplicate 1: Volume Setup (Create → Cache → Mount)

Four implementations of the same workflow:

| # | File:Lines | Cache Init | Mount | Events |
|---|------------|------------|-------|--------|
| 1 | `wizard.c:113-133` | `volume_cache_enable()` | `fuse_mount_volume()` direct | No |
| 2 | `raid_service.c:794-804` (raid_quick) | Inline in `raid_cache()` | `volume_mount()` | Yes |
| 3 | `daemon.c:128-151` | `volume_cache_enable()` | `fuse_mount_volume()` direct | No |
| 4 | `main.c:31-39` (do_restore) | Inline via `raid_cache()` | `volume_mount()` | Yes |

**Behavioral inconsistencies:** Cache init goes through either the wrapper or inline code. Mount either publishes events or doesn't. `mounted` flag is either set manually or in `raid_mount()`.

### Duplicate 2: Teardown (Close → Delete Pool Files → Delete Superblock)

Two implementations of the same file-deletion logic:

| # | File:Lines | Method |
|---|------------|--------|
| 1 | `volume_manager.c:99-108` (inside `volume_destroy()`) | `pool_file_close()` + `pool_file_delete()` + `DeleteFileW(sb)` + `RemoveDirectoryW(dir)` |
| 2 | `cleanup.c:38-71` (inside `cleanup_pool_session()`) | Same sequence via `cleanup_pool_files()` + inline superblock deletion |

Nearly identical loops in two places.

### Duplicate 3: Shutdown (cleanup_session + cleanup_disks)

Three occurrences:

| # | File:Lines |
|---|------------|
| 1 | `daemon.c:164-165` |
| 2 | `daemon.c:212-213` |
| 3 | `main.c:91-92` |

**Verdict: YES.** Three lifecycle patterns are duplicated — volume setup (4×), teardown (2×), and shutdown (3×).

---

## Summary of Findings

| Question | Answer |
|----------|--------|
| Q1: God Service? | **YES** — raid_service.c (800 LOC) handles all lifecycle, calls low-level APIs, mutates state fields directly |
| Q2: Daemon duplicates? | **YES** — mount logic bypasses events, restore-from-config has 3 implementations, cleanup duplicated within daemon.c |
| Q3: Wizard bypasses? | **YES** — most severe, bypasses TWO managers (device_manager: 6 sites, volume_manager: 1 site) |
| Q4: Managers used? | **PARTIAL** — event_bus/planner_engine clean; volume_manager bypassed; device_manager ignored by wizard; ui_model dead code |
| Q5: Circular deps? | **NO** — include graph is a DAG, no cycles |
| Q6: Duplicated lifecycle? | **YES** — 3 patterns duplicated across 2-4 implementations each |

### Key Numbers

| Metric | Count |
|--------|-------|
| Direct low-level calls in wrong places | 19 |
| Unused public API functions | 7 |
| Bypassed API sites | 18+ |
| Dead code (ui_model + metadata_upgrade) | ~180 lines |
| Duplicated workflow patterns | 3 (total 9 implementations) |
| Files with stale includes | 6 |
| g_state direct field accesses | ~202 |
| Circular dependencies | 0 |
| Fully clean managers | 2 (event_bus, planner_engine) |
