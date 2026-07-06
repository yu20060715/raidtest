# Dead Code Audit — Sprint 10B

## Overview

Systematic audit of all dead code (functions defined but never called) across the codebase. Each function was verified by grepping all `.c` and `.h` files for callers outside the file where it is defined.

## Removed (15 functions)

All confirmed unreachable with no plausible near-term use case.

| File | Function | Reason |
|---|---|---|
| `device_manager.c` / `.h` | `device_bench` | Manual per-disk bench — only `device_bench_all_selected` is used |
| | `device_find_serial` | Serial matching lives in `superblock.c:match_disk_by_serial` |
| | `device_find_drive` | Drive lookup done inline in `raid_service.c` |
| | `device_get_all` | Raw pointer array — all consumers iterate via `device_get()` |
| | `device_selected_count` | Selection count tracked in UI layer |
| | `device_has_drive_letter` | Redundant — callers check `drive_letter[0]` directly |
| | `device_capacity` | Capacity read directly from `DISK_INFO` by callers |
| | `device_speed` | Speed read directly from `DISK_INFO` by callers |
| | `device_health` | Health checked via `vol->healthy_count` or direct `DISK_INFO` fields |
| `metadata_manager.c` / `.h` | `metadata_validate` | Superblock validation duplicates `superblock_read()` internals |
| | `metadata_restore` | Thin wrapper — all callers use `superblock_restore()` directly |
| `event_bus.c` / `.h` | `event_bus_unsubscribe` | Subscription lifecycle is process-level; never needed |
| | `event_bus_display` | Debug helper never called |
| | `event_bus_flush_to_file` | No-op stub — file logging done in subscriber |
| `logger.c` / `.h` | `log_set_file` | File logging never enabled externally |
| | `log_set_timestamp` | Timestamp always disabled in current build |

## Kept as Stub (1 function)

| File | Function | Reason |
|---|---|---|
| `metadata_manager.c` / `.h` | `metadata_upgrade` | Declares intent for future raw superblock upgrade (currently returns false) |

## Kept (Test Utilities, 2 functions)

| File | Function | Reason |
|---|---|---|
| `test_common.c` / `.h` | `test_disk_reset` | Test helper — useful for future test cases |
| | `test_check_pattern` | Test helper — useful for future test cases |

## Additional Findings (Non-Dead)

| File | Function | Issue |
|---|---|---|
| `volume_manager.c` / `.h` | `volume_gen_uuid` | Should be `static` (only called within `volume_manager.c`). Not removed — scope-only issue, no functional impact. |

## Verification

- All 15 removed functions had **zero callers** across the entire codebase.
- Test suite: **38/38 passed** after removal.
- Build: clean compile with `-Wall -O2`.
