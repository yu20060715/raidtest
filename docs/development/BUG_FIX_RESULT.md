# Bug Fix Results

**Date**: 2026-07-07
**Fixed**: B10 (P0), B11 (P0), B12 (P1)
**Status**: All fixes verified, build OK, tests 39/39 passed.

---

## B10 — pool_size_mb derived from cache_mb

### Issue
`gui.cpp:1644` set `g_gui.pool_size_mb = (int)g_gui.settings.cache_mb`, causing pool files to be created with cache size (default 4096 MB) instead of the intended 51200 MB.

### Files Changed
| File | Lines | Change |
|------|-------|--------|
| `src/common.h:259` | +1 | Added `uint32_t pool_mb;` field to `APP_CONFIG` struct |
| `src/config.c:16` | +1 | `config_defaults()`: set `cfg->pool_mb = 51200` |
| `src/config.c:41` | +1 | `config_save()`: write `pool_mb` to JSON |
| `src/config.c:86` | +1 | `config_load()`: parse `pool_mb` from JSON |
| `src/gui.cpp:1644` | 1 | Changed `settings.cache_mb` → `settings.pool_mb` |

### Functions Changed
- `config_defaults()` — new default `pool_mb = 51200`
- `config_save()` — persists `pool_mb` to config file
- `config_load()` — reads `pool_mb` from config file
- `gui_run()` — assigns `g_gui.pool_size_mb` from `settings.pool_mb`

### Structs Changed
- `APP_CONFIG` — added `pool_mb` field

### Reason
Pool size was derived from cache size, causing incorrect pool file sizing. Pool size must be independently configurable.

### Verification
- Build: OK (same pre-existing warnings)
- Tests: 39/39 passed
- Source review: Value flow confirmed correct:
  ```
  config_defaults() → pool_mb = 51200
  config_load()     → reads saved pool_mb
  gui.cpp:1644      → pool_size_mb = settings.pool_mb
  gui.cpp:455       → pool_mb = pool_size_mb (51200)
  raid_init_pools   → "0:51200 1:51200" ✓
  ```

### Remaining Risk
- Existing config files from before this fix lack `pool_mb` field. The `config_load()` parser will skip unrecognized JSON fields (line 108: unrecognized fields log WARN but continue). Default `pool_mb=51200` is already set by `config_defaults()` which runs before load. So old configs will use 51200 — correct behavior.

---

## B11 — cleanup_scan_all_drives destructive scope

### Issue
`cleanup_scan_all_drives()` iterated ALL fixed drives (A:-Z:) and deleted RAIDTEST pool files. The mount-point guard at line 93 was dead code because `cleanup_session()` (called just before) sets `mounted=false`. This destroyed pool files on drives unrelated to the current volume.

### Files Changed
| File | Lines | Change |
|------|-------|--------|
| `src/cleanup.c:88-127` | -40 | Removed `cleanup_scan_all_drives()` static function |
| `src/cleanup.c:109-112` | 1 | Replaced call with `cleanup_pool_session(state)` |

### Functions Changed
- `cleanup_all()` — now calls `cleanup_pool_session()` instead of `cleanup_scan_all_drives()`

### Structs Changed
- None

### Reason
Pool file deletion during cleanup must be restricted to the current volume's member disks only. The global scan could delete pool files from other RAIDTEST volumes or previous installations.

### Verification
- Build: OK (same pre-existing warnings)
- Tests: 39/39 passed
- Source review: `cleanup_pool_session()` iterates `state->disk.disks[]` which contains only the current volume's disks. Pool files, superblocks, and config directories for those disks are cleaned.
- `cleanup_bench_dirs()` (separate function) still scans all drives for bench temp files — safe and preserves existing behavior.

### Remaining Risk
- `cleanup_pool_session()` accesses `state->disk.disks[i]->drive_letter[0]` after `cleanup_pool_files()` has closed pool file handles. This is safe because `drive_letter` is metadata, not a handle.
- If `cleanup_all()` is called when there are no disks (`state->disk.disk_count == 0`), `cleanup_pool_session()` does nothing — safe.
- Cross-volume cleanup is no longer performed. If user has two RAIDTEST volumes on separate disk sets, only the current volume's files are cleaned. This is the desired fix.

---

## B12 — Non-atomic bytes_read in mirror_engine.c

### Issue
`mirror_engine.c:65` used plain `vol->bytes_read += length` instead of `InterlockedExchangeAdd64`. Under concurrent read load from WinFsp's thread pool, the counter could lose updates.

### Files Changed
| File | Lines | Change |
|------|-------|--------|
| `src/mirror_engine.c:65` | 1 | Changed `vol->bytes_read += length` → `InterlockedExchangeAdd64((volatile LONG64*)&vol->bytes_read, length)` |

### Functions Changed
- `mirror_volume_read()` — stats counter now atomic

### Structs Changed
- None

### Reason
`bytes_read` is accessed from multiple threads (WinFsp callback pool). Plain `+=` is not an atomic read-modify-write on x64. The same pattern was already correct at line 41 (cache path) and in `stripe_engine.c` (3 call sites for `bytes_read`, 4 for `bytes_written`).

### Verification
- Build: OK (same pre-existing warnings)
- Tests: 39/39 passed
- Source review: Uses same `InterlockedExchangeAdd64((volatile LONG64*)&vol->bytes_read, length)` pattern as line 41 and all other atomic counter updates in the codebase.
- `bytes_written` counters in mirror_engine were already using `InterlockedExchangeAdd64` (lines 117, 122) — no change needed.

### Remaining Risk
- None. One-line change, identical pattern to 7 other call sites in the codebase.
