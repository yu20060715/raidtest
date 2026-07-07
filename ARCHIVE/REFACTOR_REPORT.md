# Refactoring Report

## Summary

Refactored the RAID codebase to reduce coupling, increase cohesion, split modules, and reduce globals. All 38 tests pass.

## Changes

### 1. Split `raid_service.c` → `raid_service.c` + `raid_query.c`

**Motivation:** `raid_service.c` mixed command dispatch/execution with display/query logic in 1800+ lines.

**What moved to `raid_query.c`:**
- `raid_drives()` (drive list display)
- `raid_config()` (config display)
- `raid_pool()` (pool status)
- `raid_sb`/`raid_journal` display functions
- `raid_bench_print()` 
- `raid_phase_info()` and phase display helpers

**Result:** `raid_service.c` shrank from ~960 to ~420 lines. Query/display logic is now in its own compilation unit.

### 2. Extracted CRC32 → `crc32.c` + `crc32.h`

**Motivation:** CRC32 table was a static array defined inline in `common.h`, forcing recompilation on any change. The table constructor ran at every call site.

**Changes:**
- Moved CRC32 table to `src/crc32.c` (compiled once)
- `crc32.h` exposes `crc32c()` declaration
- Same function signature, zero behavioral change

### 3. Extracted UUID → `uuid.c` + `uuid.h`

**Motivation:** `VOLUME_UUID` struct and 4 inline UUID functions lived in `common.h`, coupling all files to Windows perf-counter implementation details.

**Changes:**
- `uuid.h` declares `VOLUME_UUID` struct and `uuid_generate`, `uuid_to_str`, `uuid_is_zero`, `uuid_eq`
- `uuid.c` provides implementations
- `common.h` now `#include "uuid.h"` instead of defining them inline

### 4. Extracted `raid_init_pools()` argument parsing helpers

**Motivation:** `raid_init_pools()` was ~130 lines with 4 distinct argument-parsing modes in a single if/else chain.

**Changes:**
- Introduced `POOL_PAIRS` struct to pass disk+size arrays
- Extracted 4 static helpers, one per mode:
  - `init_pools_from_pairs()` — `id:mb id:mb ...` pairs
  - `init_pools_all_selected_with_size()` — single size arg
  - `init_pools_from_ids()` — disk ID list
  - `init_pools_from_selected()` — use current selection
- Extracted `ensure_drive_mapped()` to eliminate repeated drive-mapping logic
- Extracted `init_pools_create_files()` for the file-creation loop
- `raid_init_pools()` is now ~50 lines with a clear dispatch

### 5. Build system updates

- Added `src\uuid.c` and `src\crc32.c` to all 3 compiler builds (MSVC, GCC, GCC test)
- Added `src\uuid.c` to all stress-test builds

## What was considered but not done

- **Reducer logger.h coupling:** logger.h is already included only from common.h. Since every .c file uses LOG_* macros, removing it from common.h would just add it to every .c file — no net reduction. The current single-point inclusion is correct.

## Metrics

| Metric | Before | After | Change |
|--------|--------|-------|--------|
| Compilation units (.c) | 9 | 12 | +3 |
| Header files (.h) | 12 | 15 | +3 |
| `raid_service.c` lines | ~960 | ~420 | -540 |
| `common.h` lines | 321 | 260 | -61 |
| Inline function defs in common.h | 13 | 6 | -7 |
| Test pass rate | 38/38 | 38/38 | 0 |

## Verification

```
38 passed, 0 failed
```
