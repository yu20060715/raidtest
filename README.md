# RAIDTEST — Software RAID Prototype for Windows

[![Platform](https://img.shields.io/badge/Platform-Windows%2010%2F11-blue)]()
[![GUI](https://img.shields.io/badge/GUI-Dear%20ImGui%20%2B%20DirectX%2011-green)]()
[![API](https://img.shields.io/badge/Mount-WinFsp%20FUSE-orange)]()

**RAIDTEST** is a Windows-native software RAID prototype that creates, mounts, and manages virtual RAID volumes from physical disks or pool files. Includes a full graphical interface (Dear ImGui + DirectX 11) and CLI.

> ⚠️ **PROTOTYPE — NOT PRODUCTION READY**  
> Data loss possible. Test with non-critical data only.  
> Requires WinFsp for mount functionality. Windows only.

---

## Features

### RAID Levels
| Level | Status | Description |
|-------|--------|-------------|
| RAID0 | ✅ Complete | Weighted striping — proportional I/O by disk speed |
| RAID1 | ✅ Complete | Mirroring — identical copies, degraded read, rebuild |
| RAID10| ✅ Planner | Nested stripe-of-mirrors (capacity estimation only) |

### Core Engine
- **Weighted Stripe** — `MAX_RATIO=32` ratio normalization distributes I/O proportional to disk write speed
- **Superblock v4** — UUID, generation counter, CRC32 checksum, serial-based disk matching
- **Write-back Cache** — configurable (256 MB–4 GB), 64 KB blocks, async flush thread
- **Journal (WAL)** — crash-recovery via write-ahead log per disk
- **Rollback** — atomic multi-step operations with cleanup on failure
- **State Machine** — 7-state model (DISCONNECTED→DISCOVERED→INITIALIZED→MOUNTED→DEGRADED→RECOVERING→UNMOUNTED)
- **Health Check** — disk accessibility, superblock consistency validation
- **CrystalDiskMark Compatible** — file-level I/O via `diskspd.exe` works correctly on FUSE mounts

### Graphical Interface (GUI)
- **Dear ImGui v1.91** + DirectX 11 + Win32 backend
- **Dark/Light Theme** — toggle in Settings
- **Mode Tabs** — Beginner / Advanced / Developer
- **Toolbar** — Scan / Create / Mirror / Mount / Unmount / Destroy / Quick / Check / Bench
- **Disk List** — 10-column table with per-disk pool allocation
- **Volume Info** — state, RAID level, capacity, UUID, cache, R/W bytes, health
- **Event Log** — 500-line bounded, color-coded (ERROR/WARN/OK/INFO)
- **Status Bar** — progress bar, ETA, cancel button
- **Dialogs** — Settings, About, Welcome, Destroy/Purge Confirm, Expand, LBA Map, Metadata, Benchmark, Raw Bench, Export, Restore, Rebuild, I/O Test, Random Stress
- **Developer Mode** — Simulation controls (Fail/Healthy/Disconnect), Cache controls
- **Background Workers** — all operations run on a separate thread with progress/cancel

### Command-Line Interface (CLI)
- Full feature parity with GUI — 30+ commands
- `scan`, `select`, `init`, `create`, `mirror`, `mount`, `unmount`, `load`, `destroy`, `purge`
- `check`, `metadata`, `info`, `status`, `test`, `bench`, `benchfs`, `benchraw`, `planner`, `events`
- `simulate`, `expand`, `mapdrive`, `random`, `wizard`, `quick`, `cache`, `cleanup`, `help`
- Auto-mode: restore from saved config on empty input

---

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      User Interface                          │
├──────────────────────┬──────────────────────────────────────┤
│   gui.cpp            │  cmd_handler.c                       │
│   (Dear ImGui/DX11)  │  (CLI parser + dispatch)             │
├──────────┴───────────┴──────────────────────────────────────┤
│              raid_service.h/c  —  Unified API                │
├─────────────────────────────────────────────────────────────┤
│  device_manager  │ volume_manager │ metadata_manager        │
│  planner_engine  │ event_bus     │ ui_model                 │
├──────────────────┴──────────────────────────────────────────┤
│  stripe_engine   │ mirror_engine │ ram_cache  │ journal     │
│  (weighted I/O)  │ (mirror+rebld)│ (W-B cache)│ (WAL)       │
├─────────────────────────────────────────────────────────────┤
│              WinFsp FUSE Bridge (fuse_bridge.c)              │
├─────────────────────────────────────────────────────────────┤
│              Win32 API + DirectX 11 + OVERLAPPED I/O        │
└─────────────────────────────────────────────────────────────┘
```

### Module Map
| Module | Role |
|--------|------|
| `raid_service` | Single API — 30+ functions callable from GUI + CLI |
| `stripe_engine` | RAID0 weighted stripe: map, read, write, expand |
| `mirror_engine` | RAID1 mirror: write-to-all, degraded read, rebuild |
| `ram_cache` | Write-back cache: 64 KB blocks, async flush, write-through mode |
| `journal` | Write-ahead log per pool file (prototype) |
| `device_manager` | Disk enumeration, selection, health tracking |
| `volume_manager` | Create/mirror/load/destroy/expand lifecycle |
| `metadata_manager` | Superblock v4 read/write/restore |
| `fuse_bridge` | WinFsp FUSE callbacks: getattr, read, write, ioctl |
| `pool_io` | OVERLAPPED file I/O with `FILE_FLAG_NO_BUFFERING` |

---

## Build

### Requirements
- **MinGW-w64** (GCC 8+ with C11 and C++17 support)
- **Windows SDK** (for `d3d11.lib`, `dxgi.lib`, `d3dcompiler.lib`)
- **WinFsp SDK** (for FUSE bridge — `winfsp-x64.lib` included in repo)

### Build Command
```
build.bat
```

Output: `raidtest_winfsp.exe` and `raidtest_tests.exe`

---

## Usage

### GUI Mode
```
raidtest_winfsp.exe
```

### CLI Mode
```
raidtest_winfsp.exe --cli
```

### CLI Reference
| Command | Description |
|---------|-------------|
| `scan` | Detect physical disks + auto-benchmark |
| `select <id> ...` | Select disks by ID |
| `init <id:mb> ...` | Create pool files |
| `create` | Create RAID0 stripe volume |
| `mirror` | Create RAID1 mirror volume |
| `mount <letter>` | Mount volume via WinFsp |
| `unmount` | Unmount volume |
| `load` | Restore from superblock |
| `destroy` | Delete volume + pool files |
| `purge` | Remove all metadata from disks |
| `test` | I/O verification (write + read + verify) |
| `benchfs` | Filesystem-level benchmark |
| `benchraw` | Raw disk benchmark (bypasses cache) |
| `check` | Health check |
| `info` | Volume information |
| `status` | Live status dashboard |
| `map` | LBA-to-disk mapping |
| `metadata` | Superblock contents |
| `planner` | Capacity planner |
| `expand <id:mb>...` | Add disks to stripe |
| `rebuild <idx> <disk> [MB]` | Replace failed disk |
| `simulate <idx> <f\|h\|d>` | Simulate failure/healthy/disconnect |
| `random <ops> [maxKB]` | Random I/O stress test |
| `cache <size>\|off\|wt` | Configure cache |
| `config-save` / `config-load` | Save/load configuration |
| `quick` | All-in-one setup |
| `wizard` | Guided setup (8 steps) |
| `cleanup` | Release all resources |
| `help` | Show command list |

---

## Demo Flow

```
scan → select → init → mount → Notepad → CrystalDiskMark → GUI → unmount
```

1. **Launch** — `raidtest_winfsp.exe` opens GUI
2. **Scan** — detects physical disks
3. **Select** — choose disks for RAID (Beginner mode auto-selects all healthy)
4. **Init** — create pool files with per-disk size
5. **Create** — build RAID0 (stripe) or RAID1 (mirror) volume
6. **Mount** — assign drive letter (e.g., `R:`) via WinFsp
7. **Notepad** — create file on mounted volume; verify read/write
8. **CrystalDiskMark** — run benchmark on the mounted volume
9. **GUI** — explore volume info, benchmark, event log, dialogs
10. **Unmount** — safely detach volume

---

## Tests

### 43 Test Scenarios (33 pass, 10 pre-existing I/O failures)

| Suite | Tests | Area |
|-------|-------|------|
| Superblock | 11 | Read/write/restore/backward compat/corruption |
| Cache | 8 | Write-back, flush, dirty block, cross-block |
| Journal | 5 | Write-ahead log, replay, recovery |
| Mirror Engine | 9 | RAID1 create/degraded/rebuild/concurrent write |
| Stripe Engine | 10 | RAID0 create/normalize/expand + normalization |

```
raidtest_tests.exe
```

10 pre-existing failures require specific physical test setup (temp directory pool files).

---

## Performance

| Configuration | Write | Read | Ratio |
|--------------|-------|------|-------|
| 2× NVMe SSD | 4413 MB/s | 3419 MB/s | 32:25 |
| NVMe + 2× SATA SSD | 2011 MB/s | 1933 MB/s | 32:3:3 |

Full report: `docs/performance_validation.md`

---

## Known Limitations

Full list: [KNOWN_LIMITATIONS.md](KNOWN_LIMITATIONS.md)

**Key items:**
- Windows only, WinFsp required
- Max 4 disks per volume
- No RAID5/6 — RAID0, RAID1, RAID10 planner only
- Journal is prototype (no circular buffering, unbounded growth)
- No encryption, no S.M.A.R.T., no TRIM passthrough
- Pool file I/O serialized at hardware level on same physical drive
- Per-disk workers process one I/O at a time (no pipelining)

---

## Project Structure

```
raidv3/
├── src/                    # Source files (30+ modules)
│   ├── main.c             # Entry point
│   ├── gui.cpp            # Dear ImGui GUI (~2075 lines)
│   ├── raid_service.*     # Unified API layer
│   ├── stripe_engine.*    # RAID0 weighted stripe
│   ├── mirror_engine.*    # RAID1 mirror + rebuild
│   ├── ram_cache.*        # Write-back cache
│   ├── journal.*          # Write-ahead log
│   ├── fuse_bridge.c      # WinFsp FUSE callbacks
│   ├── cmd_handler.*      # CLI dispatch
│   ├── pool_io.c          # OVERLAPPED file I/O
│   ├── superblock.c       # On-disk metadata v4
│   ├── config.c           # JSON config save/load
│   ├── disk_scanner.c     # Physical disk enumeration
│   ├── bench_io.c         # Benchmark engine
│   ├── device_manager.*   # Disk lifecycle
│   ├── volume_manager.*   # Volume lifecycle
│   ├── metadata_manager.* # Superblock operations
│   ├── planner_engine.*   # Capacity calculator
│   ├── event_bus.*        # Publish/subscribe
│   ├── ui_model.*         # Read-only GUI state
│   └── ... (common.h, logger, crc32, uuid, profiler, cleanup, etc.)
├── imgui/                  # Dear ImGui v1.91 + backends
├── winfsp_headers/        # WinFsp API headers
├── docs/                   # Documentation
│   ├── PROJECT_FINAL_SUMMARY.md
│   ├── CrystalDiskMark_Compatibility.md
│   ├── performance_validation.md
│   ├── architecture/
│   ├── development/
│   ├── release/
│   └── learning/
├── logs/                   # Runtime logs (mount, scan, daemon)
├── temp/                   # Pool data files (Craidtest_*.dat)
├── archive/                # Test sources, old executables, historical reports
├── build/                  # Object files
├── raidtest_winfsp.exe     # Main executable
├── raidtest_tests.exe      # Test suite executable
├── build.bat               # Build script
└── winfsp-x64.dll          # WinFsp runtime DLL
```

---

## License

MIT License — see [LICENSE](LICENSE).

---

## Acknowledgments

- **Dear ImGui** (ocornut) — GUI framework
- **WinFsp** (billziss-gh) — FUSE for Windows
- **MinGW-w64** — GCC toolchain for Windows
