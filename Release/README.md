# RAIDTEST — Software RAID Prototype for Windows

[![Platform](https://img.shields.io/badge/Platform-Windows%2010%2F11-blue)]()
[![GUI](https://img.shields.io/badge/GUI-Dear%20ImGui%20%2B%20DirectX%2011-green)]()
[![API](https://img.shields.io/badge/Mount-WinFsp%20FUSE-orange)]()
[![Status](https://img.shields.io/badge/Status-RC1-yellow)]()

**RAIDTEST** is a Windows-native software RAID prototype that creates, mounts, and manages virtual RAID volumes from physical disks or pool files. Originally a CLI tool, it now includes a full graphical interface built with Dear ImGui and DirectX 11.

> ⚠️ **PROTOTYPE — NOT PRODUCTION READY**  
> Data loss possible. Test with non-critical data only.  
> Requires WinFsp for mount functionality. Windows only.

---

## Features

### RAID Levels
| Level | Status | Description |
|-------|--------|-------------|
| RAID0 | ✅ Complete | Striping — combines disks for capacity + speed |
| RAID1 | ✅ Complete | Mirroring — identical copies for redundancy |
| RAID10| ✅ Planner | Nested stripe-of-mirrors (capacity estimation) |

### Core Engine
- **Superblock v4** — UUID, generation, serial-based disk matching
- **Write-back Cache** — configurable (256 MB–4 GB), async flush thread
- **Journal (WAL)** — crash-recovery via write-ahead log (prototype)
- **Rollback** — atomic multi-step operations with cleanup on failure
- **State Machine** — 7-state model with per-command guards
- **Serial-based Restore** — `load` uses physical disk serial numbers (not drive letters)
- **Health Check** — `check` command verifies disk accessibility

### Graphical Interface (GUI)
- **Dear ImGui v1.92.8** + DirectX 11 + Win32 backend
- **Dark Theme** — custom color scheme, zero-rounding flat design
- **Toolbar** — Scan / Create / Mount / Unmount / Destroy / Bench / Export / Refresh
- **Physical Disk List** — 10-column table (Model, Serial, Type, Bus, Size, Speed, Status, RAID, Use)
- **Storage Planner** — capacity estimation for RAID0/1/10 from selected disks
- **Volume Info** — state, level, capacity, mount, cache, UUID, generation, uptime, health
- **Benchmark** — read/write throughput + latency measurement (256 MB test, 1 MB blocks)
- **Export Diagnostic** — dumps metadata, event log, system info to a timestamped folder
- **Event Log** — 500-line bounded, color-coded (ERROR/FAILED red, WARN yellow, OK green, INFO blue)
- **Confirmation Dialogs** — Destroy requires explicit "Yes" confirmation
- **State-based Buttons** — Mount/Unmount/Destroy auto-disable based on volume state
- **Status Bar** — current state, busy indicator, progress bar, version string
- **Menu Bar** — File (Refresh/Export/Exit), Actions (Scan/Create/Mirror/Mount/Unmount/Destroy/Benchmark), Help (About)

### Command-Line Interface (CLI)
- Full feature parity with GUI
- `scan`, `init`, `create`, `mirror`, `mount`, `unmount`, `load`, `destroy`, `purge`
- `check`, `metadata`, `info`, `bench`, `planner`, `events`, `simulate`, `help`
- `--gui` flag launches the graphical interface
- Auto-mode: 18 CLI commands with 7-state validation

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    User Interface                        │
├──────────────────────┬──────────────────────────────────┤
│   gui.cpp            │  cmd_handler.c                   │
│   (Dear ImGui/DX11)  │  (CLI parser + dispatch)         │
├──────────┴───────────┴──────────────────────────────────┤
│   raid_service.h/c  — Unified API for GUI and CLI       │
├─────────────────────────────────────────────────────────┤
│   ui_model.h/c      — Read-only state for GUI           │
├──────────┬──────────┬──────────┬───────────┬────────────┤
│  device  │ volume   │ metadata │  planner  │  event_bus │
│  manager │ manager  │ manager  │  engine   │  pub/sub   │
├──────────┴──────────┴──────────┴───────────┴────────────┤
│                    Engine Layer                          │
├──────────┬──────────┬──────────┬────────────────────────┤
│  stripe  │  mirror  │  cache   │  journal               │
│  engine  │  engine  │  (W-B)   │  (WAL)                 │
├──────────┴──────────┴──────────┴────────────────────────┤
│              WinFsp FUSE (mount layer)                  │
├─────────────────────────────────────────────────────────┤
│              Win32 API + DirectX 11                     │
└─────────────────────────────────────────────────────────┘
```

### Module Map
| Module | Files | Role |
|--------|-------|------|
| `raid_service` | `raid_service.h/c` | Single API — GUI + CLI call these 30+ functions |
| `device_manager` | `device_manager.h/c` | Disk enumeration, selection, health |
| `volume_manager` | `volume_manager.h/c` | Create/mirror/load/destroy/expand |
| `metadata_manager` | `metadata_manager.h/c` | Superblock read/write/restore |
| `planner_engine` | `planner_engine.h/c` | Pure-calculation capacity planner |
| `event_bus` | `event_bus.h/c` | Publish/subscribe event system |
| `ui_model` | `ui_model.h/c` | Read-only queries for GUI display |
| `stripe_engine` | `stripe_engine.h/c` | RAID0 read/write |
| `mirror_engine` | `mirror_engine.h/c` | RAID1 read/write |
| `ram_cache` | `ram_cache.h/c` | Write-back cache with async flush |
| `journal` | `journal.h/c` | Write-ahead logging (prototype) |

---

## Build

### Requirements
- **MinGW-w64** (GCC 8+ with C11 and C++17 support)
- **Windows SDK** (for `d3d11.lib`, `dxgi.lib`, `d3dcompiler.lib`)
- **WinFsp SDK** (for FUSE bridge — `winfsp-x64.lib` included in repo)

### Build Commands

```batch
build.bat
```

The script:
1. Compiles C sources with `gcc` (C11, no extensions)
2. Compiles C++ sources (gui.cpp + 13 Dear ImGui files) with `g++`
3. Links with DirectX 11, WinFsp, GDI32, DWMAPI
4. Builds test executable (all objects + test runner)

Output: `raidtest.exe` and `raidtest_tests.exe`

### clang-cl Alternative
`build.bat` includes a commented `:clang_cl` path using MSVC toolchain.

---

## Usage

### GUI Mode (Recommended for Demo)

```
raidtest.exe
```
or
```
raidtest.exe --gui
```

### CLI Mode (Developer)

```
raidtest.exe scan
raidtest.exe init 0:1024 1:1024
raidtest.exe create
raidtest.exe mount G
raidtest.exe unmount
raidtest.exe destroy
```

### Full CLI Reference

| Command | Description |
|---------|-------------|
| `scan` | Detect physical disks |
| `init <id:mb> ...` | Create pool files (e.g., `init 0:1024 1:1024`) |
| `create` | Create RAID volume from selected disks |
| `mirror` | Create RAID1 from selected disks |
| `mount <letter>` | Mount volume to a drive letter |
| `unmount` | Unmount volume |
| `load` | Restore volume from metadata |
| `destroy` | Delete volume and pool files |
| `purge` | Remove all metadata from disks |
| `check` | Health check |
| `info` | Volume information |
| `metadata <disk>` | Show superblock details |
| `bench <letter>` | Run throughput benchmark |
| `planner` | Show capacity planner |
| `events` | Show event log |
| `help` | Command list |

---

## Demo (8-Minute Walkthrough)

See [DEMO.md](DEMO.md) for a complete step-by-step demo:

1. Launch → 2. Scan → 3. Select Disks → 4. Create RAID0 → 5. Mount → 6. Explorer Verify → 7. Create File → 8. Unmount → 9. Load (Restore) → 10. Destroy

---

## Tests

### 49 Test Scenarios (All Passing)

| Suite | Tests | Area |
|-------|-------|------|
| Superblock | 18 | Read/write/restore/backward compat/corruption |
| Stripe Engine | 12 | RAID0 I/O patterns |
| Mirror Engine | 9 | RAID1 I/O patterns |
| Service | 10 | raid_service API integration |

```batch
raidtest_tests.exe
```

Tests use real files on `C:\RAIDTEST\` (no mocking layer). Run as Administrator.

---

## Known Limitations

- **Windows only** — no Linux/macOS support
- **WinFsp dependency** — mount is FUSE-based, not a block device
- **Max 4 disks** per volume (`MAX_DISKS = 4`)
- **No RAID5/6** — only RAID0, RAID1, RAID10 planner
- **Journal is prototype** — no circular buffering, no data CRC
- **No S.M.A.R.T.** — disk health attributes not read
- **No encryption** — data on disk is plaintext
- **No boot-time auto-load** — manual scan + create each session

Full list: [KNOWN_LIMITATIONS.md](KNOWN_LIMITATIONS.md)

---

## Project Structure

```
raidv3/
├── src/               # C/C++ source files (20+ modules)
│   ├── gui.cpp        # Dear ImGui GUI (~770 lines)
│   ├── raid_service.* # Unified API layer
│   ├── device_manager.*
│   ├── volume_manager.*
│   ├── metadata_manager.*
│   ├── planner_engine.*
│   ├── event_bus.*
│   ├── ui_model.*
│   ├── stripe_engine.*
│   ├── mirror_engine.*
│   ├── ram_cache.*
│   ├── journal.*
│   ├── cmd_handler.*  # CLI dispatch
│   ├── main.c         # Entry point
│   └── ...
├── imgui/             # Dear ImGui v1.92.8 + backends (13 files)
├── winfsp_headers/    # WinFsp API headers
├── build.bat          # Build script
├── DEMO.md            # Demo walkthrough
├── README.md          # This file
├── CHANGELOG.md       # Version history
├── RC1_REPORT.md      # RC1 assessment
└── docs/              # Additional documentation
```

---

## Roadmap

| Sprint | Focus | Status |
|--------|-------|--------|
| 1–3 | Core engine, CLI, superblock, tests | ✅ Complete |
| 4 | Validation, stabilization, docs | ✅ Complete |
| 5 | Architecture refactoring (7 new modules) | ✅ Complete |
| 6 | GUI MVP (Dear ImGui + DX11) | ✅ Complete |
| **7** | **RC1 — Polish, docs, release** | **In Progress** |
| 8 | Demo prep, thesis, performance validation | Upcoming |

Full roadmap: [ROADMAP.md](ROADMAP.md)

---

## License

MIT License — see [LICENSE](LICENSE).

---

## Acknowledgments

- **Dear ImGui** (ocornut) — GUI framework
- **WinFsp** (billziss-gh) — FUSE for Windows
- **MinGW-w64** — GCC toolchain for Windows
