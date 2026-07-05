# Sprint 5 — Architecture Refactoring Report

## Architecture Before (Sprint 4)

```
                          CLI (cmd_handler.c)
                                │
           ┌────────────────────┼────────────────────┐
           ▼                    ▼                    ▼
    disk_scanner.c        superblock.c          stripe_engine.c
    pool_io.c             (metadata +            mirror_engine.c
    bench_io.c             version +             ram_cache.c
    fuse_bridge.c          checksum)             journal.c
    config.c                                      pool_io.c
```

**Problems**:
- All business logic in `cmd_handler.c` (~1148 lines)
- No separation between CLI parsing and domain logic
- Event log was inline in cmd_handler with `event_log_append()`/`event_log_display()`
- Planner was a CLI-only function — no reuse possible
- `disk_scan_all()` called directly from multiple locations
- GUI (future) would have to call `cmd_create()`, `cmd_scan()` — coupling to CLI
- No event system — modules couldn't observe state changes

## Architecture After (Sprint 5)

```
                         GUI (future — Sprint 6)
                                │
                                ▼
                        Raid Service (raid_service.h/c)
                                │
           ┌────────────────────┼────────────────────┐
           ▼                    ▼                    ▼
    Device Manager        Volume Manager        Planner Engine
    (device_manager)      (volume_manager)      (planner_engine)
           │                    │                    │
           └────────────────────┼────────────────────┘
                                ▼
                      Metadata Manager (metadata_manager)
                                │
           ┌────────────────────┼────────────────────┐
           ▼                    ▼                    ▼
    disk_scanner.c        stripe_engine.c        superblock.c
    pool_io.c             mirror_engine.c
    bench_io.c            ram_cache.c
    fuse_bridge.c         journal.c
                          pool_io.c

                     Event Bus (event_bus.h/c)
                           │
              ┌────────────┼────────────┐
              ▼            ▼            ▼
           Logger        CLI      GUI (future)

                     UI Model (ui_model.h/c)
                    (read-only queries)
```

## New Modules Created (7)

| Module | Files | Lines | Purpose |
|--------|-------|-------|---------|
| Event Bus | `event_bus.h/c` | ~120 | Publish/subscribe for system events |
| Device Manager | `device_manager.h/c` | ~160 | Centralized disk operations |
| Metadata Manager | `metadata_manager.h/c` | ~100 | Centralized metadata operations |
| Planner Engine | `planner_engine.h/c` | ~120 | Pure calculation, no CLI dependency |
| Volume Manager | `volume_manager.h/c` | ~250 | Volume lifecycle operations |
| Raid Service | `raid_service.h/c` | ~750 | Single API for GUI/CLI |
| UI Model | `ui_model.h/c` | ~100 | Read-only state queries |

**Total new code**: ~1600 lines (mostly moved from cmd_handler.c)

## Dependencies Removed

| Before | After |
|--------|-------|
| `disk_scan_all()` called from cmd_handler, device_manager | Only `device_manager` calls `disk_scan_all()` |
| `superblock_read/write` called from cmd_handler, volume_manager | Only `metadata_manager` calls superblock directly |
| `cmd_*` functions contained all business logic | `cmd_*` are 1-3 line wrappers calling `raid_*` |
| Event log inline in cmd_handler | Event bus with subscribers; file logger in raid_service |

## File Size Reduction

| File | Before (Sprint 4) | After (Sprint 5) | Delta |
|------|-------------------|-------------------|-------|
| `cmd_handler.c` | ~1148 lines | ~190 lines | **-958 lines** |
| `raid_service.c` | — | ~750 lines | +750 lines (moved from cmd_handler) |

## New Call Flow

```
User input → cmd_process() → cmd_xxx() → raid_xxx() → managers → engines
                                                    ↓
                                             event_bus_publish()
                                                    ↓
                                             Logger subscriber writes file
                                                    
GUI → raid_xxx() → managers → engines
   → ui_get_*() for display data
```

## What cmd_handler.c Now Contains

1. Command parsing (`strtok`, dispatch table)
2. Help text (Chinese — not changed)
3. One-liner wrappers to `raid_*` functions
4. Interactive loop (`cmd_interactive`)
5. Legacy `auto_restore_or_quick` logic

## What Still Needs Refactoring (Future)

| Issue | Location | Plan |
|-------|----------|------|
| `cleanup.h` depends on `cmd_handler.h` | `cleanup.h: #include "cmd_handler.h"` | Move `APP_STATE` to `common.h` or dedicated `app_state.h` |
| `wizard.h` depends on `cmd_handler.h` | `wizard.h: #include "cmd_handler.h"` | Same fix |
| `daemon.h` depends on `cmd_handler.h` | `daemon.h: #include "cmd_handler.h"` | Same fix |
| `cmd_handler.h` still declares `g_state` | Global state — eventual DI | Requires larger refactor |
| CRC32 function duplicated | `superblock.c` + `journal.c` | Move to shared `crc32.h` |
