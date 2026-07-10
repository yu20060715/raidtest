# Changelog

All notable changes to RAIDTEST are documented here.

---

## v1.0 (2026-07-10) — Final Release

### Added (GUI Audit Fixes)
- **I/O Test result popup** — `W_TEST` now displays result in modal dialog instead of silent completion
- **Random Stress result popup** — `W_RANDOM` displays progress and result in modal dialog
- **Expand dialog initialization** — `expand_sizes[0..7]` initialized to 51200 at dialog open, preventing stale-value reuse
- **Auto-restore/auto-mount on startup** — settings `auto_restore` and `auto_mount` now execute at launch
- **`benchraw` CLI command** — raw disk benchmark bypassing RAM cache

### Changed
- **Weighted Stripe normalization** — `MAX_RATIO=32` rounding replaces `RATIO_SCALE=10000`, fixing degenerate single-disk phase cycles
- **Benchmark measurement** — `bench_volume()` now flushes cache before stopping timer (measures real disk throughput, not cache speed)
- **Cache read during flush** — removed `!vol->cache_flush_in_progress` guard on read path, fixing `STATUS_IO_DEVICE_ERROR` on small file reads
- **GUI audit** — full `src/gui.cpp` audit (25 categories, 8 minor issues, 4 P2 fixes)
- **Project directory organized** — `logs/`, `temp/`, `archive/` created, root reduced from ~87 to 25 entries

### Fixed
- `KNOWN_LIMITATIONS.md`, `—help`, `--version` version strings updated to `v1.0`
- Release documents synchronized (README, DEMO, QUICK_START, CHANGELOG, PROJECT_STATUS, KNOWN_LIMITATIONS)

### QA
- Build: OK (pre-existing imgui warnings only)
- Tests: 33 passed, 10 failed (all pre-existing I/O failures requiring physical test setup)
- GUI: all P2 fixes verified
- CrystalDiskMark compatibility verified

---

## v1.0 RC4 (2026-07-06) — Release Candidate 4

### Added (8 Features)
- **Settings Window** (`ShowSettings`) — drive letter, cache size (min 256 MB), Dark/Light theme, auto restore/mount, Save/Cancel. Persisted via `config_save()` to `%APPDATA%\RAIDTEST\config.json`. (`src/gui.cpp:866-901`)
- **Toast Notifications** (`toast_push`/`render_toasts`) — 8-slot FIFO queue, 5-second duration, color-coded (OK=green, WARN=yellow, ERROR=red, INFO=blue), rendered on foreground draw list. (`src/gui.cpp:161-206`)
- **Progress System with ETA/Cancel** — worker thread reports `progress_text`, `progress_step`, `progress_frac` (0.0–1.0), `progress_start_time`, ETA calculation. Cancel via `InterlockedExchange`. (`src/gui.cpp:264-697`, `715-738`)
- **Health Dashboard (Cards)** — 4-column card layout, model/status/capacity/speed per disk, green/red tint backgrounds. Temp/SMART placeholders. (`src/gui.cpp:903-945`)
- **Performance Dashboard (Charts)** — 1-second IO_PROFILER sampling, 120-sample `PerfSample` history, `PlotLines` for R/W throughput and latency, current values display. (`src/gui.cpp:947-1001`)
- **Diagnostics Export ZIP** — exports `metadata.txt`, `event.log`, `system.txt` via PowerShell `Compress-Archive`. Result dialog with path. (`src/gui.cpp:387-476`)
- **Enhanced About Window** — version, build date/time, compiler detection (GCC/MSVC), architecture, libraries (ImGui 1.91 DX11, WinFsp 2.1, DirectX 11 / MinGW-w64), MIT license. (`src/gui.cpp:1003-1026`)
- **First-Run Welcome Wizard** — modal popup on first launch, WinFsp detection, mode explanation, Quick Setup / Explore / Don't show again buttons. (`src/gui.cpp:1028-1071`)

### Changed
- `src/gui.cpp` — reconstructed all panel functions (Toolbar, DiskList, Planner, VolumeInfo, PerformancePanel, EventLog, StatusBar, ConfirmDestroy, ConfirmPurge, Benchmark, ExportDialog, BeginnerPanel, RestoreWizard, RebuildWizard, RenderMainUI, gui_run)
- `src/common.h` — `APP_CONFIG` extended with `theme`, `language[8]`, `auto_restore`, `auto_mount`, `first_run`. Added `APP_THEME` enum.
- `src/config.c` — `config_defaults()` updated to version 2 with new field defaults. `config_save()` and `config_load()` persist all new fields.
- `src/main.c` — version string updated to `v1.0 RC4`. `--help` output expanded.

### Fixed
- `strncpy` argument order bugs (6 occurrences)
- Undeclared `pos` variable
- Unused `ctx`/`card_bg` variables

### QA
- 38 unit tests: 38 passed, 0 failed
- Stress tests: all pass (longrun, random_io, concurrent, metadata_corrupt, powerfail, cli_bench)
- Build: C objects OK, C++ objects OK, link OK (static, WinFsp + D3D11)

---

## v1.0 RC3 (2026-07-05) — Release Candidate 3

### Added
- Volume rebuild wizard
- Config-based volume restore (`W_LOAD_CONFIG`)
- Quick Setup workflow (auto scan + create + mount)
- Cache enable/disable/flush worker actions
- Health check worker action
- Beginner Mode with Quick Actions panel
- Restore wizard (superblock vs config)
- Developer mode with Performance Dashboard
- Light theme support with `ApplyTheme()`
- Console panel in Developer mode

### Changed
- `gui.cpp` expanded from ~770 to ~1400 lines
- Worker thread extended to support 18 distinct action types
- Event bus subscriptions expanded (8 event types)

### Fixed
- GUI state machine sync (state_value tracking)
- Progress bar rendering in status bar
- Drive letter parsing in Settings

---

## v1.0 RC2 (2026-07-04) — Release Candidate 2

### Added
- Full GUI implementation (Dear ImGui + DirectX 11)
- Dark theme with custom color scheme
- Toolbar with RAID operations (Scan, Create, Mount, etc.)
- Physical Disk List (10-column table)
- Storage Planner panel (RAID0/1/10 capacity)
- Volume Info panel (state, level, UUID, generation, uptime)
- Event Log (500-line bounded, color-coded)
- Status Bar with state indicator
- Benchmark dialog (read/write MB/s, latency)
- Export Diagnostic dialog
- Confirmation dialogs for Destroy/Purge
- Menubar (File, Actions, View, Help)
- State-based button enable/disable

### Changed
- `gui.c` rewritten as `gui.cpp` (C++ for ImGui)
- `build.bat` updated for multi-language compilation (gcc + g++)
- Version header changed from `v3.0` to `v1.0 RC2`

### Fixed
- All P3 warnings (format specifiers, strncpy truncation)
- GUI auto-refresh timer (1-second polling of ui_model)

---

## v1.0 RC1 (2026-07-03) — Release Candidate 1

### Added
- Graphical User Interface — Dear ImGui + DirectX 11 + Win32
- Dark theme with custom color scheme
- Toolbar with all RAID operations
- Physical Disk List (10-column table)
- Storage Planner panel (RAID0/1/10 capacity estimation)
- Volume Info panel (state, level, capacity, mount, cache, UUID, generation, uptime, health)
- Event Log (500-line bounded, color-coded by severity)
- Status Bar with progress indicator and version
- Menubar (File/Actions/Help)
- About window with version, build date, architecture
- Confirmation dialogs for Destroy
- State-based button enable/disable
- Benchmark panel (read/write MB/s, latency)
- Export Diagnostic (metadata + event log + system info)
- GUI build pipeline (gcc + g++ + DirectX 11)
- 36 automated tests (cache, journal, mirror, stripe, superblock)

### Changed
- `gui.c` rewritten as `gui.cpp` (~770 lines of Dear ImGui code)
- `build.bat` updated for multi-language compilation
- Version header changed from `v3.0` to `v1.0 RC1`

### Fixed
- All P3 warnings eliminated (format specifiers, strncpy truncation)
- GUI auto-refresh timer (1-second polling of ui_model)

---

## v3.0 (Sprint 5) — Architecture Refactoring

### Added
- **Event Bus** (`event_bus.h/c`) — publish/subscribe system with 13 event types
- **Device Manager** (`device_manager.h/c`) — centralized disk operations
- **Metadata Manager** (`metadata_manager.h/c`) — centralized superblock operations
- **Planner Engine** (`planner_engine.h/c`) — pure-calculation capacity planner
- **Volume Manager** (`volume_manager.h/c`) — create/mirror/load/destroy/expand/rebuild wrappers
- **Raid Service** (`raid_service.h/c`) — single API layer for GUI and CLI (30+ functions)
- **UI Model** (`ui_model.h/c`) — read-only state queries for GUI display
- **ARCHITECTURE_REPORT.md** — formal architecture documentation
- **API.md** — full public API reference

### Changed
- `cmd_handler.c` reduced ~1148 → ~190 lines (all business logic moved to managers)
- CLI now only parses input and calls `raid_*` functions
- All modules expose clean `.h` interfaces with no CLI or GUI coupling

### Fixed
- Module separation: no circular dependencies, no global state leakage

---

## v2.0 (Sprint 4) — Validation & Stabilization

### Added
- 36 test scenarios covering all core paths
- Code review — 0 memory leaks, 0 deadlocks, 0 handle leaks
- Static analysis — ASan/UBSan: 0 errors; cppcheck: 0 critical findings
- 10 validation documents (BUG_LIST.md, KNOWN_LIMITATIONS.md, VALIDATION.md, etc.)
- VM demo infrastructure (raidtest-vm GitHub repository)
- Demo video — full lifecycle demonstration

### Changed
- All documentation rewritten for consistency
- BUG_LIST.md established with 14 confirmed bugs (P1/P2/P3)

### Fixed
- All P1 bugs fixed (B4: zero pool size, B7: journal write check, B10: cache thread race)
- Cppcheck warnings: 0 remaining

---

## v1.0 (Sprint 3) — Core Engine + CLI

### Added
- **RAID0** (stripe engine) — strip read/write, full I/O pipeline
- **RAID1** (mirror engine) — mirror read/write with disk selection
- **Superblock v4** — UUID, generation, disk count, per-disk serial, created/mount timestamps
- **Backward compatibility** — readers for superblock v1/v2/v3
- **Serial-based volume restore** — `load` matches physical disk serials (not drive letters)
- **Write-back Cache** — configurable size, async flush thread, LRU candidate design
- **Journal (WAL)** — write-ahead log with CRC32 entry headers, replay on load
- **State Machine** — 7 states (DISCONNECTED → MOUNTED) with per-command guards
- **CLI commands** — scan, init, create, mirror, mount, unmount, load, destroy, purge, check, metadata, info, bench, planner, events, simulate, help (18 commands)
- **UUID generation** on create/mirror
- **Health check** — `check` verifies disk accessibility before I/O
- **Rollback** — atomic multi-step operations with cleanup on failure
- **Event log** — timestamped, auto-trim, append-only
- **Error code system** — `RC` enum with 13 return codes

### Changed
- Complete rewrite from v0.1 prototype
- Pool file format updated (superblock v4)

---

## v0.1 (Sprint 1–2) — Initial Prototype

### Added
- Basic disk scanning via `IOCTL_STORAGE_QUERY`
- Pool file creation with fixed size
- Simple stripe read/write (no metadata, no journal)
- WinFsp FUSE bridge (mount/unmount)
- Superblock v1 (drive-letter-based)
- CLI skeleton with hard-coded paths
- Initial test infrastructure

### Known Issues (v0.1)
- Drive-letter-based restore (breaks when letters change)
- No journal — crash = data loss
- No cache — every read goes to disk
- No state machine — any command at any time
- Single-threaded I/O
- No UUID — volumes identified by order only
- No rollback — partial failure leaves inconsistent state
