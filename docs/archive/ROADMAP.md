# RAIDTEST v3 — Roadmap

## Current Status: Sprint 5 — Architecture Refactoring (COMPLETE)

### Completed (Sprint 1-3)

- [x] State machine with 7 states and per-command guards
- [x] Error code system (`RC` enum)
- [x] Serial number capture via `STORAGE_DEVICE_DESCRIPTOR`
- [x] Superblock v4 with UUID + created/mount timestamps
- [x] Backward compat readers for v1/v2/v3
- [x] Serial-based volume restore (`superblock_restore`)
- [x] `load` rewritten to use serial matching
- [x] Event log with timestamp, auto-trim
- [x] New commands: `destroy`, `metadata`, `check`, `simulate`, `planner`, `events`
- [x] UUID generation on `create`/`mirror`
- [x] 36 test scenarios — all passing
- [x] VM infrastructure (github.com/yugood/raidtest-vm), demo video

### Sprint 4: Validation & Stabilisation (COMPLETE)

- [x] Code review — 0 memory leaks, 0 deadlocks, 0 handle leaks
- [x] Static analysis — ASan/UBSan 0 errors; cppcheck 0 critical
- [x] 10 validation documents created
- [x] Docs consistency check — all 4 main docs fixed
- [x] RC report

### Sprint 5: Core Architecture Refactoring (CURRENT — COMPLETE)

- [x] **Event Bus** (`event_bus.h/c`) — publish/subscribe system, 13 event types
- [x] **Device Manager** (`device_manager.h/c`) — centralizes all disk operations
- [x] **Metadata Manager** (`metadata_manager.h/c`) — centralizes superblock ops
- [x] **Planner Engine** (`planner_engine.h/c`) — pure calculation, no CLI dep
- [x] **Volume Manager** (`volume_manager.h/c`) — wraps create/mirror/load/destroy/expand/rebuild
- [x] **Raid Service** (`raid_service.h/c`) — single API layer for GUI/CLI
- [x] **UI Model** (`ui_model.h/c`) — read-only state queries for GUI
- [x] **cmd_handler.c slimmed** — from ~1148 to ~190 lines (thin CLI wrappers)
- [x] **build.bat** — includes all 7 new modules
- [x] **All 36 tests pass** — no behavior changes
- [x] **API.md** — full public API reference
- [x] **Architecture Report** (`ARCHITECTURE_REPORT_SPRINT5.md`)

**Impact**: `cmd_handler.c` reduced by ~960 lines. Business logic moved to
managers. CLI now only parses input and calls `raid_*` functions. GUI (Sprint 6)
can call `raid_*` directly — no `cmd_*` coupling.

## Sprint 6: GUI MVP + Bug Fixes

- [ ] Win32 dialog-based GUI (status, disk map, event log)
- [ ] P3 bug fixes and code quality improvements
- [ ] English help text (`--help` and `help` command)
- [ ] Admin / WinFsp detection at startup
- [ ] Pre-built binary release workflow
- [ ] `--quickstart` and `--selftest` flags

## Sprint 6: Advanced Features

- [ ] RAID10 (stripe of mirrors)
- [ ] Full journal commit on `destroy`, journal replay progress, trim
- [ ] Hot spare detection and auto-rebuild
- [ ] Volume encryption (XTS-AES)
- [ ] Key management (`key load`/`key save`)

## Sprint 7: Remote & Monitoring

- [ ] Remote replication (TCP-based)
- [ ] Split-brain detection
- [ ] System tray icon, notification on disk failure
- [ ] Real-time performance graph
- [ ] `monitor` CLI command

## Sprint 8: Production Hardening

- [ ] Windows service mode
- [ ] Auto-load on boot
- [ ] Multi-volume support
- [ ] Localization (zh-TW, ja-JP)
- [ ] Performance optimization pass
- [ ] Benchmark compatibility mode

## Longer Term

- [ ] Dedup / compression
- [ ] SSD TRIM pass-through
- [ ] S.M.A.R.T. monitoring
- [ ] ZFS-style snapshot
- [ ] NVMe-oF target
- [ ] Kernel driver (stretch goal)
