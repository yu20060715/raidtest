## Goal
Complete Sprint 5 — Architecture Refactoring. Convert the monolithic cmd_handler.c (~1148 lines) into 7 clean manager modules with a service layer, event bus, and UI model — without changing CLI behavior or breaking tests.

## Constraints
- NO new RAID features, NO new RAID levels, NO driver work, NO GUI
- All 36 tests must still pass
- CLI behavior must be identical
- Move existing code, don't rewrite algorithms

## Progress
### Done — Sprint 5 Complete
- **Event Bus**: `event_bus.h/c` — publish/subscribe with 13 event types, thread-safe
- **Device Manager**: `device_manager.h/c` — wraps disk_scanner; no direct `disk_scan_all()` calls anywhere else
- **Metadata Manager**: `metadata_manager.h/c` — wraps superblock read/write/validate/dump
- **Planner Engine**: `planner_engine.h/c` — pure calculation, zero CLI dependency; GUI-safe
- **Volume Manager**: `volume_manager.h/c` — wraps create/mirror/load/mount/unmount/destroy/expand/rebuild
- **Raid Service**: `raid_service.h/c` — single API for CLI and GUI; all state checking here
- **UI Model**: `ui_model.h/c` — read-only queries for GUI (disk summary, volume info, health)
- **cmd_handler.c slimmed** from ~1148 to ~190 lines — each cmd_xxx is now a 1-3 line wrapper calling raid_xxx
- **build.bat** updated with all 7 new .c files
- **36/36 tests pass**, build OK on both clang-cl and GCC
- **API.md** created with full public API reference
- **Architecture Report** (`ARCHITECTURE_REPORT_SPRINT5.md`) with before/after diagrams
- **ROADMAP.md** updated to reflect Sprint 5 as current
- **ARCHITECTURE.md** updated with manager-level component map

### Architecture Changes Summary
| Before (Sprint 4) | After (Sprint 5) |
|---|---|
| cmd_handler.c: ~1148 lines | cmd_handler.c: ~190 lines (thin CLI wrappers) |
| All business logic in cmd_xxx() | Logic in volume_manager, metadata_manager, etc. |
| Planner was CLI-locked | planner_engine.c is pure calculation |
| Event log inline in cmd_handler | Event bus with subscriber pattern |
| disk_scan_all() called everywhere | Only device_manager calls it |
| GUI would need to call cmd_create() | GUI calls raid_create() — clean API |
| No event system | 13 event types, subscribers for logger+CLI+GUI |

### Key Metrics
- **7 new modules** created in `src/`: event_bus, device_manager, metadata_manager, planner_engine, volume_manager, raid_service, ui_model
- **~1600 new lines** (mostly moved from cmd_handler.c)
- **cmd_handler.c reduced** by ~960 lines (84% thinner)
- **0 test changes** — all 36 pass identically
- **0 behavior changes** — all commands work same as before

## Remaining Cleanup (Future)
- `cleanup.h` depends on `cmd_handler.h` (needs APP_STATE in its own header)
- `wizard.h` and `daemon.h` same issue
- CRC32 duplicated in superblock.c + journal.c

## Next Sprint
**Sprint 6: GUI MVP + Bug Fixes** — build Win32 dialog GUI using Raid Service API
