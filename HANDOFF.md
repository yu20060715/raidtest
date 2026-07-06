# RAIDTEST v1.0 — Handoff Document

## 1. Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    User Interface                            │
├──────────────────────┬──────────────────────────────────────┤
│   gui.cpp            │  cmd_handler.c                       │
│   (Dear ImGui/DX11)  │  (CLI parser + dispatch)             │
├──────────┴───────────┴──────────────────────────────────────┤
│   raid_service.h/c  — Unified API for GUI and CLI            │
├─────────────────────────────────────────────────────────────┤
│   ui_model.h/c      — Read-only state for GUI               │
├──────────┬──────────┬──────────┬───────────┬────────────────┤
│  device  │ volume   │ metadata │  planner  │  event_bus     │
│  manager │ manager  │ manager  │  engine   │  pub/sub       │
├──────────┴──────────┴──────────┴───────────┴────────────────┤
│                    Engine Layer                              │
├──────────┬──────────┬──────────┬────────────────────────────┤
│  stripe  │  mirror  │  cache   │  journal                   │
│  engine  │  engine  │  (W-B)   │  (WAL)                     │
├──────────┴──────────┴──────────┴────────────────────────────┤
│              WinFsp FUSE (mount layer)                       │
├─────────────────────────────────────────────────────────────┤
│              Win32 API + DirectX 11                          │
└─────────────────────────────────────────────────────────────┘
```

### Module Roles

| Module | Files | Role |
|--------|-------|------|
| `raid_service` | `raid_service.h/c` | Unified API — GUI + CLI call these 30+ functions |
| `device_manager` | `device_manager.h/c` | Disk enumeration, selection, health |
| `volume_manager` | `volume_manager.h/c` | Create/mirror/load/destroy/expand |
| `metadata_manager` | `metadata_manager.h/c` | Superblock read/write/restore |
| `planner_engine` | `planner_engine.h/c` | Pure-calculation capacity planner |
| `event_bus` | `event_bus.h/c` | Publish/subscribe event system |
| `ui_model` | `ui_model.h/c` | Read-only queries for GUI display |
| `stripe_engine` | `stripe_engine.h/c` | RAID0 read/write with asymmetric ratios |
| `mirror_engine` | `mirror_engine.h/c` | RAID1 read/write with degraded mode |
| `ram_cache` | `ram_cache.h/c` | Write-back cache with async flush |
| `journal` | `journal.h/c` | Write-ahead logging (prototype) |
| `cmd_handler` | `cmd_handler.h/c` | CLI parser + command dispatch |
| `main.c` | — | Entry point, CLI/GUI routing |
| `gui.cpp` | — | Dear ImGui + DirectX 11 GUI |
| `config` | `config.h/c` | JSON config save/load (`%APPDATA%\RAIDTEST\config.json`) |
| `disk_scanner` | `disk_scanner.h/c` | Physical disk enumeration via Win32 API |
| `pool_io` | `pool_io.h/c` | Pool file create/open/close/delete |
| `superblock` | `superblock.h/c` | Superblock v4 read/write/restore with CRC32 |
| `fuse_bridge` | `fuse_bridge.h/c` | WinFsp FUSE mount/unmount layer |
| `daemon` | `daemon.h/c` | Console daemon + Windows service support |
| `cleanup` | `cleanup.h/c` | Graceful resource cleanup |
| `profiler` | `profiler.h/c` | I/O latency and throughput profiler |
| `logger` | `logger.h/c` | Timestamped log with severity levels |
| `wizard` | `wizard.h/c` | Guided CLI setup wizard |

### State Machine: 7 States

```
STATE_DISCONNECTED (0) → STATE_DISCOVERED (1) → STATE_INITIALIZED (2) → STATE_MOUNTED (3)
                                                                           ↓
                                                              STATE_DEGRADED (4) → STATE_RECOVERING (5)
                                                                           ↓
                                                              STATE_UNMOUNTED (6)
```

---

## 2. Folder Structure

```
raidv3/
├── src/                 # C/C++ source files (58 files, ~8,500 lines)
│   ├── *.c              # 25 C source files
│   ├── gui.cpp          # GUI implementation (Dear ImGui + DX11)
│   ├── *.h              # 32 header files
│   └── test_*.c         # 7 test source files
├── imgui/               # Dear ImGui v1.92 (15 files)
│   ├── imgui.cpp        # Core ImGui implementation
│   ├── imgui_draw.cpp   # Drawing routines
│   ├── imgui_tables.cpp # Table widgets
│   ├── imgui_widgets.cpp# Widget implementations
│   └── backends/        # Win32 + DirectX 11 backends
├── winfsp_headers/      # WinFsp 2.1 API headers (8 files)
│   └── winfsp-2.1/inc/ # fuse/ + winfsp/ headers
├── tests/               # Regression test stubs (5 files)
├── stress/              # Power-fail stress test (1 file)
├── benchmark/           # CLI benchmark stub
├── docs/archive/        # Historical design documents (22 files)
├── build/               # Build output directory (object files)
├── build.bat            # Build script (GCC + clang-cl)
├── build_asan.bat       # Address Sanitizer build
├── build_stress.bat     # Stress test build
├── raidtest_winfsp.exe  # Main binary (~2.1 MB, static linked)
├── raidtest_tests.exe   # Test runner (~0.5 MB)
├── winfsp-x64.dll       # WinFsp runtime DLL
├── libwinfsp-x64.a      # WinFsp static import library
├── *.exe (6)            # Stress/benchmark executables
├── *.md (20+)           # Documentation
└── LICENSE              # MIT license
```

---

## 3. Build Instructions

### Requirements

| Dependency | Version | Purpose |
|-----------|---------|---------|
| MinGW-w64 (GCC) | 8+ (C11, C++17) | Primary compiler |
| MSVC (clang-cl) | Optional | Alternative compiler |
| Windows SDK | 10+ | `d3d11.lib`, `dxgi.lib`, `d3dcompiler.lib` |
| WinFsp | 2.1 | FUSE mount layer (bundled: `winfsp-x64.lib`, headers) |
| Dear ImGui | 1.92 | GUI framework (bundled in `imgui/`) |
| DirectX 11 | — | GPU rendering (system-provided) |

### Build Commands

```batch
:: Full build (main binary + tests + stress tools)
build.bat

:: ASan build (for debugging)
build_asan.bat

:: Stress tests only
build_stress.bat
```

The build script (`build.bat`) will:
1. Detect `clang-cl` first (MSVC toolchain) — if found, compile in one pass
2. Fall back to MinGW GCC:
   - Compile all C sources with `gcc -Wall -O2`
   - Compile C++ (gui.cpp + 6 ImGui files) with `g++ -Wall -O2`
   - Link with `g++ -static-libgcc -static-libstdc++ -static`
   - Build `raidtest_winfsp.exe` (~2.1 MB, fully static)
3. Build test executable `raidtest_tests.exe`
4. Build 6 stress/validation executables

### Manual Compilation (GCC)

```bash
# Compile C sources
gcc -Wall -O2 -DUSE_WINFSP -Iwinfsp_headers/winfsp-2.1/inc \
    -Iwinfsp_headers/winfsp-2.1/inc/fuse -Isrc -c src/main.c -o build/main.o

# Compile C++ sources  
g++ -Wall -O2 -DUSE_WINFSP -Iwinfsp_headers/winfsp-2.1/inc \
    -Iwinfsp_headers/winfsp-2.1/inc/fuse -Isrc -Iimgui -c src/gui.cpp -o build/gui.o

# Link
g++ -Wall -O2 build/*.o -o raidtest_winfsp.exe \
    -L. -lwinfsp-x64 -lole32 -lshell32 -lshlwapi -lws2_32 \
    -ladvapi32 -lntdll -lcomctl32 -lgdi32 -ldwmapi \
    -ld3d11 -ldxgi -ld3dcompiler \
    -static-libgcc -static-libstdc++ -static
```

---

## 4. Dependencies

### Runtime Dependencies
| Dependency | Required? | Notes |
|-----------|-----------|-------|
| Windows 7+ | Yes | Win32 API throughout |
| WinFsp runtime | For mount | `winfsp-x64.dll` bundled |
| DirectX 11 GPU | For GUI | Falls back to WARP software renderer |
| PowerShell 5+ | For export | `Compress-Archive` used by diagnostics export |

### Build Dependencies
| Dependency | Source |
|-----------|--------|
| MinGW-w64 (MSYS2) | `pacman -S mingw-w64-x86_64-gcc` |
| WinFsp SDK | Bundled in repo (`winfsp_headers/`, `libwinfsp-x64.a`) |
| Windows SDK | Part of Visual Studio Build Tools |
| Dear ImGui | Bundled in repo (`imgui/`) |

---

## 5. Known Limitations

See [KNOWN_LIMITATIONS.md](KNOWN_LIMITATIONS.md) for the complete list (45+ items).

### Critical
- **Windows only** — Win32 API, no POSIX support
- **No RAID5/6** — only RAID0, RAID1, RAID10 planner
- **Max 4 disks** per volume (`MAX_DISKS = 4`)
- **No encryption** — data on disk is plaintext
- **No end-to-end checksum** — silent corruption not detected
- **Journal is prototype** — no circular buffering, no data CRC

### GUI-Specific
- Temperature/SMART data: unavailable (placeholder only)
- Cache hit %: unavailable (placeholder only)
- Diagnostics export requires PowerShell

### Tests
- Superblock tests require `C:\RAIDTEST\` (real filesystem, no mocking)
- Tests are order-dependent and not isolated

---

## 6. Future Roadmap

| Sprint | Focus | Status |
|--------|-------|--------|
| 1–3 | Core engine, CLI, superblock, tests | ✅ Complete |
| 4 | Validation, stabilization, docs | ✅ Complete |
| 5 | Architecture refactoring (7 new modules) | ✅ Complete |
| 6 | GUI MVP (Dear ImGui + DX11) | ✅ Complete |
| 7 | RC1 — Polish, docs, release | ✅ Complete |
| 8 | Demo prep, thesis, performance validation | ✅ Complete |
| RC2–RC4 | Incremental improvements (8 features) | ✅ Complete |
| **v1.0** | **Final release** | **← CURRENT** |

### Beyond v1.0
- RAID5/6 implementation
- Cross-platform (Linux via FUSE)
- S.M.A.R.T. monitoring integration
- Circular-buffered journal with data CRC
- LRU cache eviction
- End-to-end data checksumming
- Boot-time auto-load service
- Encryption support
- GUI modularization (separate panel files)
- I/O scheduler with NCQ passthrough
- Test isolation and mocking infrastructure
