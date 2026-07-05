# Changelog

All notable changes to RAIDTEST are documented here.

---

## v1.0 RC1 (In Progress) — Release Candidate

### Added
- **Graphical User Interface** — Dear ImGui v1.92.8 + DirectX 11 + Win32
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
- **GUI build** — C source compiled with `gcc`, C++/ImGui compiled with `g++`, linked with DirectX 11
- **49 automated tests** (up from 36) — added service-layer integration tests

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
- **36 test scenarios** covering all core paths
- **Code review** — 0 memory leaks, 0 deadlocks, 0 handle leaks
- **Static analysis** — ASan/UBSan: 0 errors; cppcheck: 0 critical findings
- **10 validation documents** (BUG_LIST.md, KNOWN_LIMITATIONS.md, VALIDATION.md, etc.)
- **VM demo infrastructure** (raidtest-vm GitHub repository)
- **Demo video** — full lifecycle demonstration

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
- **State Machine** — 7 states (DISCONNECTED, SCAN_DONE, INITIALIZED, MOUNTED, UNMOUNTED, DEGRADED, FAILED) with per-command guards
- **CLI commands** — `scan`, `init`, `create`, `mirror`, `mount`, `unmount`, `load`, `destroy`, `purge`, `check`, `metadata`, `info`, `bench`, `planner`, `events`, `simulate`, `help`
- **UUID generation** on create/mirror (format: `XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX`)
- **Health check** — `check` verifies disk accessibility before I/O
- **Rollback** — atomic multi-step operations with cleanup on failure
- **Event log** — timestamped, auto-trim, append-only
- **Error code system** — `RC` enum with 20+ return codes
- **Bug tracking** — BUG_LIST.md with 14 confirmed bugs
- **Known limitations** — KNOWN_LIMITATIONS.md (45 documented items)

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

---

## Legend

| Symbol | Meaning |
|--------|---------|
| ✅ | Complete |
| 🚧 | In progress |
| ⏳ | Planned |
