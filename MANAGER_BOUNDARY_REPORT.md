# MANAGER BOUNDARY AUDIT REPORT
## Sprint 10C -- Architecture Cleanup
### Date: 2026-07-06

---

## Executive Summary

Six manager modules were audited for public API usage and boundary violations.
- **Total public functions declared**: 38
- **Unused public functions**: 7 (18%)
- **Bypassed API sites**: 18+ instances across 4 callers
- **Fully clean managers**: planner_engine, event_bus
- **Managers needing attention**: device_manager, metadata_manager, volume_manager, ui_model

---

## 1. device_manager (device_manager.h / device_manager.c)

### A) Public Functions Declared

| # | Function | Callers (excluding own .c) | Status |
|---|----------|---------------------------|--------|
| 1 | device_refresh() | raid_service.c:98 | OK |
| 2 | device_bench_all_selected(uint32_t) | raid_service.c:152 | OK |
| 3 | device_get_count() | raid_service.c (15 call sites) | OK |
| 4 | device_get(uint32_t) | raid_service.c (11 call sites) | OK |
| 5 | device_select(uint32_t*, uint32_t) | raid_service.c:125,225 | OK |
| 6 | device_is_selected(uint32_t) | raid_service.c:204,246 | OK |
| 7 | device_map_drive(uint32_t, const char*) | raid_service.c (5 call sites) | OK |
| 8 | device_print_list() | raid_service.c:111,154 | OK |
| 9 | device_cleanup() | raid_service.c:87,97 | OK |

### B) Unused Public API
**None.** All 9 public functions have external callers.

### C) Bypassed API (Direct Low-Level Calls)

The following callers skip device_manager and call disk_scanner / disk_io functions directly:

| Caller | Direct Call | Should Use |
|--------|------------|------------|
| daemon.c:100 | disk_scan_all(...) | device_refresh() |
| daemon.c:110 | disk_map_drive(...) | device_map_drive() |
| wizard.c:39 | disk_scan_free(...) | device_cleanup() |
| wizard.c:43 | disk_scan_all(...) | device_refresh() |
| wizard.c:46 | disk_print_list(...) | device_print_list() |
| wizard.c:65 | disk_select(...) | device_select() |
| wizard.c:75 | disk_map_drive(...) | device_map_drive() |
| wizard.c:86 | disk_print_list(...) | device_print_list() |
| main.c:16 | disk_scan_all(...) | device_refresh() |
| cleanup.c:47 | disk_scan_free(...) | device_cleanup() |

**Severity: HIGH.** The entire wizard flow bypasses device_manager entirely. Daemon and main also skip it for initial scanning.

---

## 2. metadata_manager (metadata_manager.h / metadata_manager.c)

### A) Public Functions Declared

| # | Function | Callers (excluding own .c) | Status |
|---|----------|---------------------------|--------|
| 1 | metadata_read(const wchar_t*, SUPERBLOCK*) | raid_service.c:697 | OK |
| 2 | metadata_write(STRIPE_VOLUME*) | volume_manager.c:33,56,180,218 | OK |
| 3 | metadata_upgrade(SUPERBLOCK*) | (none) | UNUSED |
| 4 | metadata_dump(const SUPERBLOCK*, char*, size_t) | raid_service.c:702 | OK |
| 5 | metadata_load_volume(...) | volume_manager.c:66 | OK |

### B) Unused Public API

| Function | Notes |
|----------|-------|
| metadata_upgrade(SUPERBLOCK*) | Defined at metadata_manager.c:14. Implementation returns false immediately (no-op). Has zero callers anywhere in the codebase. Should be removed or implemented. |

### C) Bypassed API

| Caller | Direct Call | Should Use |
|--------|------------|------------|
| raid_service.c:634 | superblock_read_raw(root, &sb) directly | metadata_read(root, &sb) |

raid_service.c in the raid_check() health-verification path calls superblock_read_raw() directly rather than going through metadata_read(). While metadata_read() is itself a thin wrapper, the boundary is still violated.

**Severity: LOW** (one bypass site, and metadata_read is used elsewhere).

---

## 3. volume_manager (volume_manager.h / volume_manager.c)

### A) Public Functions Declared

| # | Function | Callers (excluding own .c) | Status |
|---|----------|---------------------------|--------|
| 1 | volume_create(STRIPE_VOLUME*, DISK_INFO**, uint32_t) | raid_service.c:296, daemon.c:128, wizard.c:113 | OK |
| 2 | volume_mirror(STRIPE_VOLUME*, DISK_INFO**, uint32_t) | raid_service.c:305 | OK |
| 3 | volume_load(STRIPE_VOLUME*, DISK_INFO*, uint32_t*, DISK_INFO**, uint32_t, const wchar_t*) | raid_service.c:368, daemon.c:68 | OK |
| 4 | volume_mount(STRIPE_VOLUME*, char) | raid_service.c:341 | OK |
| 5 | volume_unmount(STRIPE_VOLUME*, bool*, HANDLE*, bool*) | raid_service.c:348 | OK |
| 6 | volume_destroy(STRIPE_VOLUME*, DISK_INFO**, uint32_t, bool*, HANDLE*, bool*, RAID_STATE*) | raid_service.c:387 | OK |
| 7 | volume_expand(STRIPE_VOLUME*, DISK_INFO**, uint32_t, int, char**) | raid_service.c:313 | OK |
| 8 | volume_rebuild(STRIPE_VOLUME*, DISK_INFO**, uint32_t, uint32_t, uint32_t, uint64_t) | raid_service.c:330 | OK |
| 9 | volume_gen_uuid(STRIPE_VOLUME*) | (none outside own .c) | Internal-Only |
| 10 | volume_cache_enable(STRIPE_VOLUME*, uint64_t, bool*, uint32_t*, HANDLE*) | daemon.c:83,136, wizard.c:125 | OK |
| 11 | volume_create_pool_file(DISK_INFO*, uint64_t) | daemon.c:112, wizard.c:106, raid_service.c:271 | OK |
| 12 | volume_close_pool_file(DISK_INFO*) | raid_service.c:274,678 | OK |
| 13 | volume_delete_pool_file(DISK_INFO*) | raid_service.c:275 | OK |

### B) Unused / Internal-Only Public API

| Function | Notes |
|----------|-------|
| volume_gen_uuid(STRIPE_VOLUME*) | Declared public in header but only called from volume_create() and volume_mirror() within the same file. No external callers. Should be made static or removed from the header. |

### C) Bypassed API

| Caller | Direct Call | Should Use |
|--------|------------|------------|
| raid_service.c:433 | cache_init(&S()->vol.volume.cache, ...) | volume_cache_enable() |
| raid_service.c:420 | cleanup_volume_cache(...) | volume_cache_enable(..., off) pattern |
| daemon.c:151,195 | fuse_mount_volume(...) | volume_mount() |
| wizard.c:133 | fuse_mount_volume(...) | volume_mount() |
| cleanup.c:27 | fuse_unmount_volume(...) | volume_unmount() |
| cleanup.c:31 | stripe_volume_destroy(...) | volume_destroy() / volume_unmount() |
| cleanup.c:38-39 | pool_file_close(...) and pool_file_delete(...) | volume_close_pool_file() / volume_delete_pool_file() |

**Severity: HIGH.** The cache code path in raid_service.c duplicates the logic of volume_cache_enable() (lines 436-439 mirror volume_manager.c:117-120). Cleanup bypasses nearly every volume_manager function.

---

## 4. planner_engine (planner_engine.h / planner_engine.c)

### A) Public Functions Declared

| # | Function | Callers (excluding own .c) | Status |
|---|----------|---------------------------|--------|
| 1 | planner_calculate(PLANNER_DISK*, uint32_t, PLANNER_RESULT*) | raid_service.c:723 | OK |
| 2 | planner_print(const PLANNER_RESULT*, PLANNER_DISK*, uint32_t) | raid_service.c:724 | OK |

### B) Unused Public API
**None.** Both functions are used.

### C) Bypassed API
**None.** All consumers go through the planner_engine API.

**Status: CLEAN.**

---

## 5. ui_model (ui_model.h / ui_model.c)

### A) Public Functions Declared

| # | Function | Callers (excluding own .c) | Status |
|---|----------|---------------------------|--------|
| 1 | ui_get_disk_summary(UI_DISK_SUMMARY*) | (none) | UNUSED |
| 2 | ui_get_volume_info(UI_VOLUME_INFO*) | (none) | UNUSED |
| 3 | ui_get_health_summary(UI_HEALTH_SUMMARY*) | (none) | UNUSED |
| 4 | ui_get_state(void) | (none) | UNUSED |
| 5 | ui_get_state_str(void) | (none) | UNUSED |

### B) Unused Public API

ALL FIVE public functions are dead code. They are declared in ui_model.h, implemented in ui_model.c, but never called from any .c file (including tests, daemon, GUI, CLI, or raid_service).

These were presumably intended for the GUI (gui.cpp) but are not referenced there either (verified via grep on *.cpp and *.h).

**Severity: HIGH.** Entire module is dead code.

### C) Bypassed API
N/A -- no callers exist to bypass.

---

## 6. event_bus (event_bus.h / event_bus.c)

### A) Public Functions Declared

| # | Function | Callers (excluding own .c) | Status |
|---|----------|---------------------------|--------|
| 1 | event_bus_init(void) | raid_service.c:67 | OK |
| 2 | event_bus_subscribe(EVENT_TYPE, event_callback, void*) | raid_service.c:69-81 (13 calls) | OK |
| 3 | event_bus_publish(EVENT_TYPE, const char*) | device_manager.c:24, volume_manager.c (8 calls), metadata_manager.c:10, raid_service.c (5 calls) | OK |
| 4 | event_type_str(EVENT_TYPE) | raid_service.c:30 | OK |

### B) Unused Public API
**None.** All 4 functions have external callers.

### C) Bypassed API
**None.** All event publication goes through event_bus_publish().

**Status: CLEAN.**

---

## Consolidated Findings

### Unused Public API (7 functions)

| Manager | Function | Recommendation |
|---------|----------|---------------|
| metadata_manager | metadata_upgrade() | Remove (no-op stub with zero callers) |
| volume_manager | volume_gen_uuid() | Make static; remove from header |
| ui_model | ui_get_disk_summary() | Remove entire module or implement callers |
| ui_model | ui_get_volume_info() | Remove entire module or implement callers |
| ui_model | ui_get_health_summary() | Remove entire module or implement callers |
| ui_model | ui_get_state() | Remove entire module or implement callers |
| ui_model | ui_get_state_str() | Remove entire module or implement callers |

### Bypassed API (18+ sites)

| Manager | Bypass Location | Direct Call | Fix |
|---------|----------------|------------|-----|
| device_manager | wizard.c (6 sites) | disk_scan_all, disk_scan_free, disk_print_list, disk_select, disk_map_drive | Route through device_manager |
| device_manager | daemon.c:100,110 | disk_scan_all, disk_map_drive | Use device_refresh(), device_map_drive() |
| device_manager | main.c:16 | disk_scan_all | Use device_refresh() |
| device_manager | cleanup.c:47 | disk_scan_free | Use device_cleanup() |
| metadata_manager | raid_service.c:634 | superblock_read_raw | Use metadata_read() |
| volume_manager | raid_service.c:433-439 | cache_init + manual setup | Use volume_cache_enable() |
| volume_manager | cleanup.c:27 | fuse_unmount_volume | Use volume_unmount() |
| volume_manager | cleanup.c:31 | stripe_volume_destroy | Use volume_unmount() or volume_destroy() |
| volume_manager | cleanup.c:38-39 | pool_file_close/delete | Use volume_close/delete_pool_file() |
| volume_manager | daemon.c:151,195 | fuse_mount_volume | Use volume_mount() |
| volume_manager | wizard.c:133 | fuse_mount_volume | Use volume_mount() |

### Fully Compliant Managers
- **planner_engine** -- all API used, no bypasses
- **event_bus** -- all API used, no bypasses

---

## Recommendations by Priority

1. **P0 - Remove dead code**: Delete ui_model.h, ui_model.c, and metadata_upgrade(). These represent ~180 lines of dead code that complicate maintenance and confuse static analysis.

2. **P0 - Fix wizard bypass**: The wizard is a major onboarding path. Route all disk operations through device_manager API instead of calling disk_scanner/disk_io directly.

3. **P1 - Fix cleanup bypass**: cleanup.c bypasses volume_manager for cache/volume/pool cleanup. Route through volume_unmount(), volume_close_pool_file(), and volume_delete_pool_file().

4. **P1 - Fix raid_cache bypass**: raid_service.c:raid_cache() duplicates volume_cache_enable() logic. Replace lines 433-439 with a call to volume_cache_enable().

5. **P2 - Fix daemon bypass**: Replace disk_scan_all() and disk_map_drive() calls in daemon.c:100,110 with device_refresh() and device_map_drive().

6. **P2 - Fix main.c bypass**: Replace disk_scan_all() in main.c:16 with device_refresh().

7. **P3 - Encapsulate volume_gen_uuid()**: Make it static and remove from the header since it is only used internally.

8. **P3 - Fix metadata bypass**: Replace superblock_read_raw() in raid_service.c:634 with metadata_read().

---

*End of Report*
