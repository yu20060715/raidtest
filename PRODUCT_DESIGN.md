# PRODUCT DESIGN

## 1. CURRENT FEATURE INVENTORY

### CLI Commands (31)
| Command | Backend | Description |
|---|---|---|
| `scan` | `raid_scan()` | Discover physical disks, auto-benchmark |
| `select <id...>` | `raid_select()` | Mark disks for pool creation |
| `mapdrive <id> <letter>` | `raid_mapdrive()` | Assign drive letter to a disk |
| `bench [sizeMB]` | `raid_bench()` | Benchmark selected disks |
| `init [id:mb...]` | `raid_init_pools()` | Create pool files on disks |
| `create` | `raid_create()` | Create RAID0 volume |
| `mirror` | `raid_mirror()` | Create RAID1 volume |
| `expand` | `raid_expand()` | Add disks to existing volume |
| `rebuild <idx> <id> [MB]` | `raid_rebuild()` | Replace failed mirror disk |
| `cache [sizeMB\|wt\|off]` | `raid_cache()` | RAM write-back cache control |
| `mount [letter]` | `raid_mount()` | Mount via WinFsp FUSE |
| `unmount` | `raid_unmount()` | Unmount volume |
| `load [drive]` | `raid_load()` | Restore volume from superblock |
| `purge` | `raid_purge()` | Delete all pool files + metadata |
| `destroy` | `raid_destroy()` | Unmount + delete everything |
| `test` | `raid_test()` | I/O verification stress test |
| `random <ops> [KB]` | `raid_random()` | Random I/O stress test |
| `benchfs [MB] [KB]` | `raid_benchfs()` | Filesystem-level benchmark |
| `check` | `raid_check()` | Health check + superblock audit |
| `info` | `raid_info()` | Full volume status report |
| `map` | `raid_map()` | Dump LBA → disk mapping table |
| `status` | `raid_status()` | Live dashboard (cls-based) |
| `metadata [drive]` | `raid_metadata()` | Dump raw superblock fields |
| `simulate <idx> <mode>` | `raid_simulate()` | Inject failure/healthy/disconnect |
| `planner` | `raid_planner()` | RAID capacity calculator |
| `events` | `raid_events()` | Browse event log file |
| `config-save` | `raid_config_save()` | Save disk selection to JSON |
| `config-load` | `raid_config_load()` | Reload config from JSON |
| `wizard` | `raid_wizard()` | Interactive 8-step setup guide |
| `quick` | `raid_quick()` | All-in-one setup (scan→mount) |
| `cleanup` | `raid_cleanup()` | Release all resources |
| `exit` / `quit` | loop break | Terminate CLI |

### CLI Flags (7)
| Flag | Purpose |
|---|---|
| `--version` / `-v` | Print version and exit |
| `--service` | Run as Windows Service (SCM entry) |
| `--install-service` | Register Windows Service |
| `--uninstall-service` | Unregister Windows Service |
| `--auto [letter\|cmd]` | Auto-restore from config |
| `--wizard` | Launch CLI wizard |
| `--daemon` | Run as console daemon with stdin |
| `--cleanup` | Cleanup and exit |
| `--quick` | Quick all-in-one setup |
| `--cli` | Force CLI mode (no GUI) |

### GUI Actions (10 buttons)
| Button | Worker Action | Backend |
|---|---|---|
| Scan | `W_SCAN` | `raid_scan()` |
| Create | `W_CREATE` | `raid_init_pools()` + `raid_create()` |
| Mirror | `W_MIRROR` | `raid_mirror()` |
| Mount | `W_MOUNT` | `raid_mount()` |
| Unmount | `W_UNMOUNT` | `raid_unmount()` |
| Destroy | `W_DESTROY` | `raid_destroy()` (with confirm dialog) |
| Bench | `W_BENCH` | Raw `CreateFile`/`ReadFile`/`WriteFile` |
| Export | `W_EXPORT` | Write diagnostics to temp dir |
| Refresh | `W_REFRESH` | `refresh_ui_model()` |
| (Purge) | `W_PURGE` | `raid_purge()` (hidden behind confirm) |

### Engine Components (internal)
| Component | File | Role |
|---|---|---|
| Stripe Engine | `stripe_engine.c/h` | Asymmetric RAID0 LBA mapping + I/O |
| Mirror Engine | `mirror_engine.c/h` | RAID1 mirror write/read/rebuild |
| RAM Cache | `ram_cache.c/h` | Write-back cache with flush thread |
| Journal | `journal.c/h` | Write-ahead journal for crash recovery |
| Superblock | `superblock.c/h` | On-disk v4 metadata format |
| Pool I/O | `pool_io.c/h` | File-based pool create/open/close/delete |
| Disk Scanner | `disk_scanner.c/h` | Physical disk enumeration via IOCTL |
| Bench I/O | `bench_io.c/h` | Sequential read/write benchmark |
| Event Bus | `event_bus.c/h` | Publish/subscribe event system |
| Config | `config.c/h` | JSON persistence for settings |
| Logger | `logger.c/h` | Thread-safe logging |
| Profiler | `profiler.c/h` | I/O throughput/latency tracking |
| Planner Engine | `planner_engine.c/h` | RAID capacity/speed calculator |
| Volume Manager | `volume_manager.c/h` | Volume lifecycle orchestrator |
| Cleanup | `cleanup.c/h` | Graceful resource release |
| Device Manager | `device_manager.c/h` | Disk list abstraction |
| Metadata Manager | `metadata_manager.c/h` | Superblock read/write helpers |
| FUSE Bridge | `fuse_bridge.c/h` | WinFsp FUSE filesystem callbacks |
| CMD Handler | `cmd_handler.c/h` | CLI dispatch + global state |
| RAID Service | `raid_service.c/h` | Unified backend API |
| UI Model | `ui_model.c/h` | GUI data model |
| Daemon | `daemon.c/h` | Console daemon + Windows service |
| Wizard | `wizard.c/h` | Interactive setup flow |
| GUI | `gui.cpp/h` | Dear ImGui + DirectX 11 frontend |

---

## 2. FEATURE CLASSIFICATION

### BEGINNER features (core workflow, zero config knowledge required)

These features form the 8-step flow from QUICK_START.md. A first-time user should be able to complete the entire workflow using ONLY Beginner mode. Every Beginner feature has an obvious, single purpose.

| Feature | Why Beginner |
|---|---|
| **Scan** | First step — discover what disks are available. Single button, immediate result. |
| **Select disks** | Checkbox list. Users pick which disks to use. No parameters needed beyond a tap. |
| **Set pool size** | Slider or spinner per disk showing available vs allocated. The only numeric input beginners need. |
| **Quick Create** | One button: init pools + create RAID0 + enable cache + mount. Hides all intermediate states. |
| **Mount** | Assign drive letter (default G:). One click. |
| **Unmount** | One click. |
| **Destroy** | One click + "Are you sure?" confirmation. |
| **Benchmark** | Run a simple speed test on the mounted volume. Show MB/s read/write. |
| **Quick Setup** | Scan → auto-select first 2+ disks → prompt pool size → create → cache → mount. The "I just want RAID" button. |
| **Wizard** | Guided 8-step walkthrough for users who want to understand each step. |
| **Version info** | About dialog. |

### ADVANCED features (power user, configuration choices)

These features require understanding of RAID concepts, cache strategies, or recovery procedures. They offer meaningful configuration choices but don't require code-level knowledge.

| Feature | Why Advanced |
|---|---|
| **Mirror (RAID1)** | Requires understanding of mirroring vs striping. User must choose between RAID0 and RAID1. |
| **Cache configuration** | Size, write-through vs write-back, disable. Requires understanding of cache tradeoffs. |
| **Restore from superblock** | User must understand that metadata is on-disk and can be reloaded. |
| **Expand volume** | Add disks to an existing RAID0 stripe. Requires understanding of stripe expansion. |
| **Mirror rebuild** | Replace a failed disk in a mirror set. User must identify failed disk and provide replacement. |
| **Health check** | Manual check of all disks, pool files, and superblock consistency. |
| **Planner** | Preview RAID capacity/efficiency before committing. Useful for capacity planning. |
| **Export diagnostics** | Bundle metadata + event log + system info for support. |
| **Per-disk benchmark** | Individual disk speed test (not volume test). Helps identify slow disks before creating array. |
| **Save/Load config** | Persist disk selection to JSON for later restore. |
| **Purge metadata** | Advanced cleanup — delete all pool files and superblocks without destroying volume first. |
| **Manual drive mapping** | Assign specific drive letters to disks before pool creation. |
| **Volume info** | Detailed status: RAID level, stripe phases, cache hit rate, dirty ratio, per-disk health. |
| **Live status** | Full-screen live dashboard with throughput, IOPS, per-disk stats. |

### DEVELOPER features (debugging, diagnostics, simulation)

These features expose internal state, inject faults, or stress-test the engine. They require understanding of the stripe mapping, journal, superblock format, or event system.

| Feature | Why Developer |
|---|---|
| **Metadata dump** | Raw superblock field-by-field display. Requires understanding of the on-disk format. |
| **LBA mapping dump** | Stripe phase table showing how virtual LBAs map to physical disks. Core engine detail. |
| **Simulate disk failure** | Inject healthy/fail/disconnect on a specific disk to test mirror recovery. |
| **I/O stress test** | `test` + `random` commands for engine-level write/read/verify. |
| **Filesystem benchmark** | `benchfs` — benchmark through FUSE layer vs raw engine. |
| **Event log browser** | Raw event bus history. Useful for debugging state transitions. |
| **Daemon mode** | `--daemon` — headless operation with stdin control. For integration with other tools. |
| **Service management** | `--install-service` / `--uninstall-service` / `--service`. For production deployment. |
| **Force CLI mode** | `--cli` — run without GUI. For scripting and automation. |
| **Cleanup** | `cleanup` — manual resource release. For debugging resource leaks. |
| **Check** | Superblock consistency audit across all disks. For debugging split-brain scenarios. |

### INTERNAL features (no user-facing UI needed)

These are infrastructure components. They execute automatically or are called by other features. No direct user control is needed — their configuration surfaces through Beginner/Advanced features.

| Component | Why Internal |
|---|---|
| **State machine** | Transitions happen automatically behind every command. |
| **Config JSON auto-load** | Happens at startup. User only needs config-save/config-load. |
| **Logger** | Always active. Log level can be tuned but not exposed in UI. |
| **Event bus** | All components use it internally. GUI subscribes for live updates. No direct user interaction. |
| **Profiler** | Behind Volume Info / Performance panels. Data is consumed by UI, not configured directly. |
| **FUSE bridge** | Mount/unmount call it. No direct manipulation needed. |
| **Disk scanner IOCTL** | Called by scan. User sees disk list, not raw IOCTL. |
| **Journal engine** | Write/commit/recover happens transparently behind stripe writes. |
| **Superblock read/write** | Called by create/load/mount/destroy. User sees "volume created" not raw bytes. |
| **Pool file management** | Create/open/close/delete are called by init/purge/destroy. |
| **Stripe engine internals** | Phase calculation, LBA mapping, async I/O. Only the mapping dump is developer-facing. |
| **Mirror engine internals** | Write-to-all, degraded read, rebuild logic. Only simulate/rebuild commands are developer-facing. |
| **Cache engine internals** | Block allocation, dirty tracking, flush thread. Cache size/wt/off is advanced, internals are hidden. |
| **Cleanup** | Called automatically on exit. Manual `cleanup` command is developer-facing. |
| **Test infrastructure** | 38 unit tests + 5 integration tests. Not shipped in release binary. |

---

## 3. GUI NAVIGATION TREE

```
┌─────────────────────────────────────────────────────────────────┐
│  RAIDTEST v1.0                             ─  □  ×             │
│  File  Mode  Help                                               │
├─────────────────────────────────────────────────────────────────┤
│  [Beginner | Advanced | Developer]  ◄── MODE SELECTOR (tabs)   │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│ ┌─ BEGINNER ──────────────────────────────────────────────────┐ │
│ │                                                             │ │
│ │  ┌──────────────────────────────────┐ ┌──────────────────┐  │ │
│ │  │  ⚡ QUICK ACTIONS                │ │  DISK LIST       │  │ │
│ │  │                                   │ │                  │  │ │
│ │  │  [🔄 Scan]  [⚡ Quick Setup]      │ │  ☐ [0] NVMe SSD │  │ │
│ │  │  [📀 Create] [🔗 Mount]          │ │      512 GB     │  │ │
│ │  │  [🔓 Unmount] [🗑️ Destroy]       │ │  ☐ [1] SATA SSD │  │ │
│ │  │                                   │ │      256 GB     │  │ │
│ │  │  ─────────────────────────────    │ │  ☐ [2] SATA SSD │  │ │
│ │  │  Mount: [G▼]  Pool: [51200] MB   │ │      256 GB     │  │ │
│ │  │  ─────────────────────────────    │ │                  │  │ │
│ │  │                                   │ │  📊 Total: 3     │  │ │
│ │  │  [📊 Benchmark Mounted Drive]     │ │  Selected: 2     │  │ │
│ │  │                                   │ │  Capacity: 100GB │  │ │
│ │  └──────────────────────────────────┘ └──────────────────┘  │ │
│ │                                                             │ │
│ │  ┌─ VOLUME STATUS ────────────────────────────────────────┐ │ │
│ │  │  State: ✅ MOUNTED at G:   │   Size: 97.6 GB          │ │ │
│ │  │  RAID0 · 2 disks · Cache ON (1024 MB)                  │ │ │
│ │  │  Written: 1.2 GB │ Read: 0.8 GB │ Health: ✅ Healthy   │ │ │
│ │  └────────────────────────────────────────────────────────┘ │ │
│ │                                                             │ │
│ └─────────────────────────────────────────────────────────────┘ │
│                                                                 │
│ ┌─ ADVANCED ──────────────────────────────────────────────────┐ │
│ │                                                             │ │
│ │  ┌──────────────────────┐ ┌──────────────────────────────┐  │ │
│ │  │  RAID CONFIG         │ │  CACHE & PERFORMANCE        │  │ │
│ │  │                      │ │                              │  │ │
│ │  │  ○ RAID0 (Stripe)    │ │  Mode: [Write-Back ▼]       │  │ │
│ │  │  ○ RAID1 (Mirror)    │ │  Size: [1024] MB            │  │ │
│ │  │                      │ │  📈 Hit Rate: 87.3%         │  │ │
│ │  │  [🔄 Rebuild Mirror] │ │  Dirty: 2.1%                │  │ │
│ │  │  [📈 Expand Volume]  │ │                              │  │ │
│ │  │                      │ │  ──────────────────────────  │  │ │
│ │  └──────────────────────┘ │  📊 VOLUME BENCHMARK         │  │ │
│ │                           │  [▶ Run Filesystem Bench]    │  │ │
│ │  ┌──────────────────────┐ │  Read: 850 MB/s  Write: 420  │  │ │
│ │  │  PERSISTENCE         │ │                              │  │ │
│ │  │                      │ └──────────────────────────────┘  │ │
│ │  │  [💾 Save Config]    │                                   │ │
│ │  │  [📂 Load Config]    │  ┌─ HEALTH ────────────────────┐  │ │
│ │  │  [🔄 Restore Volume] │  │  Disk 0: ✅ Healthy        │  │ │
│ │  │                      │  │  Disk 1: ✅ Healthy        │  │ │
│ │  └──────────────────────┘  │  Superblock: ✅ Consistent  │  │ │
│ │                            │  [🩺 Run Full Check]        │  │ │
│ │  ┌──────────────────────┐  └────────────────────────────┘  │ │
│ │  │  PLANNER             │                                   │ │
│ │  │                      │  ┌─ EXPORT ────────────────────┐  │ │
│ │  │  Selected: 2 disks   │  │  [📦 Export Diagnostics]    │  │ │
│ │  │  RAID0: 100 GB (95%) │  │  Last export: C:\Temp\...   │  │ │
│ │  │  RAID1: 50 GB  (48%) │  └────────────────────────────┘  │ │
│ │  │  RAID10: 100 GB(95%) │                                   │ │
│ │  └──────────────────────┘                                   │ │
│ │                                                             │ │
│ └─────────────────────────────────────────────────────────────┘ │
│                                                                 │
│ ┌─ DEVELOPER ──────────────────────────────────────────────────┐ │
│ │                                                             │ │
│ │  ┌──────────────────────┐ ┌──────────────────────────────┐  │ │
│ │  │  DEBUG / SIMULATE    │ │  RAW DATA                    │  │ │
│ │  │                      │ │                              │  │ │
│ │  │  [💥 Fail Disk 0]    │ │  ┌─ Superblock Dump ─────┐  │  │ │
│ │  │  [💥 Fail Disk 1]    │ │  │ Magic:  0x52444953    │  │  │ │
│ │  │  [💥 Disconnect]     │ │  │ Version: 4            │  │  │ │
│ │  │  [✅ Restore Disk]   │ │  │ Gen:    42            │  │  │ │
│ │  │                      │ │  │ UUID:   a1b2...       │  │  │ │
│ │  │  ──────────────────  │ │  │ Checksum: OK          │  │  │ │
│ │  │  [🧪 I/O Stress]     │ │  └───────────────────────┘  │  │ │
│ │  │  [🎲 Random I/O]     │ │                              │  │ │
│ │  │                      │ │  ┌─ LBA Mapping ──────────┐  │  │ │
│ │  └──────────────────────┘ │  │ Phase 0: 0-50GB        │  │  │ │
│ │                           │  │  Disk 0: 0-25GB (50%)  │  │  │ │
│ │  ┌──────────────────────┐ │  │  Disk 1: 0-25GB (50%)  │  │  │ │
│ │  │  CONSOLE / SERVICE   │ │  │ Phase 1: 50-100GB       │  │  │ │
│ │  │                      │ │  │  Disk 0: 25-75GB(100%) │  │  │ │
│ │  │  [🖥️ Open CLI Console]│ │  └───────────────────────┘  │  │ │
│ │  │  [⚙️ Install Service] │ │                              │  │ │
│ │  │  [⚙️ Uninstall Svc]   │ │  ┌─ Event Log ───────────┐  │  │ │
│ │  │                      │ │  │ [2026-07-06] DISK_FOUND│  │  │ │
│ │  └──────────────────────┘ │  │ [2026-07-06] VOL_CREATE│  │  │ │
│ │                           │  │ [2026-07-06] MOUNT     │  │  │ │
│ │  ┌──────────────────────┐ │  └───────────────────────┘  │  │ │
│ │  │  DIAGNOSTICS         │ │                              │  │ │
│ │  │                      │ │  ┌─ Profiler ─────────────┐  │  │ │
│ │  │  [📦 Export (Full)]  │ │  │ Read: 420 MB/s 12K IOPS│  │  │ │
│ │  │  [🧹 Cleanup]        │ │  │ Write: 210 MB/s 6K IOPS│  │  │ │
│ │  │                      │ │  │ Queue: 4.2  Lat: 0.8ms │  │  │ │
│ │  └──────────────────────┘ │  └───────────────────────┘  │  │ │
│ │                                                         │  │ │
│ └─────────────────────────────────────────────────────────────┘ │
│                                                                 │
├─────────────────────────────────────────────────────────────────┤
│  Status: Ready  │  Mode: Beginner  │  RAIDTEST v1.0 RC2        │
└─────────────────────────────────────────────────────────────────┘
```

### Mode Navigation Rules

| Transition | Rule |
|---|---|
| Beginner → Advanced | Allowed anytime. Beginner volume state carries forward. |
| Beginner → Developer | Allowed anytime. |
| Advanced → Beginner | Allowed. Advanced features may leave state that Beginner doesn't show (e.g., cache on/off still works). |
| Developer → Advanced | Allowed. Simulation state (failed disks) visible in Advanced Health panel. |
| Mode switch | No data loss. All panels in non-active mode are hidden, not destroyed. |

---

## 4. COMPLETE CLASSIFICATION TABLE

| Feature | Source | Mode | Rationale |
|---|---|---|---|
| Scan disks | CLI `scan`, GUI Scan btn | **Beginner** | First step for every user |
| Select disks | CLI `select`, GUI checkboxes | **Beginner** | Core workflow step |
| Set pool size | CLI `init`, GUI Pool field | **Beginner** | Single numeric parameter |
| Quick Create | CLI `quick`, GUI Create btn | **Beginner** | One-click RAID0 |
| Mount | CLI `mount`, GUI Mount btn | **Beginner** | Core workflow step |
| Unmount | CLI `unmount`, GUI Unmount btn | **Beginner** | Core workflow step |
| Destroy | CLI `destroy`, GUI Destroy btn | **Beginner** | Core workflow step (with confirmation) |
| Quick Setup | CLI `quick`, no GUI btn | **Beginner** | All-in-one beginner flow |
| Wizard | CLI `wizard`, no GUI btn | **Beginner** | Guided setup for learning |
| Volume benchmark | GUI Bench btn (raw I/O) | **Beginner** | Simple speed check |
| Per-disk benchmark | CLI `bench` | **Advanced** | Diagnostic before pool creation |
| Mirror (RAID1) | CLI `mirror`, GUI Mirror btn | **Advanced** | Requires RAID level choice |
| Cache config | CLI `cache`, no GUI panel | **Advanced** | Requires understanding write-back vs write-through |
| Restore from superblock | CLI `load`, no GUI btn | **Advanced** | Requires understanding persistence |
| Expand volume | CLI `expand`, no GUI btn | **Advanced** | Requires understanding stripe expansion |
| Mirror rebuild | CLI `rebuild`, no GUI btn | **Advanced** | Requires identifying failed disk |
| Health check | CLI `check` | **Advanced** | Manual consistency verification |
| Planner | CLI `planner`, GUI Planner panel | **Advanced** | Capacity planning tool |
| Export diagnostics | GUI Export btn | **Advanced** | Support/debugging aid |
| Save/Load config | CLI `config-save/load` | **Advanced** | Power user persistence |
| Purge metadata | CLI `purge` | **Advanced** | Destructive cleanup |
| Manual drive mapping | CLI `mapdrive` | **Advanced** | Manual drive letter assignment |
| Volume info | CLI `info` | **Advanced** | Detailed status report |
| Live status | CLI `status` | **Advanced** | Real-time dashboard |
| Metadata dump | CLI `metadata` | **Developer** | Raw superblock inspection |
| LBA mapping dump | CLI `map` | **Developer** | Stripe phase internals |
| Simulate disk failure | CLI `simulate` | **Developer** | Fault injection for testing |
| I/O stress test | CLI `test`, `random` | **Developer** | Engine-level stress testing |
| Filesystem benchmark | CLI `benchfs` | **Developer** | FUSE layer throughput test |
| Event log browser | CLI `events` | **Developer** | Raw event history debugging |
| CLI Console | `--cli` flag | **Developer** | Scripting/automation |
| Daemon mode | `--daemon` | **Developer** | Headless operation |
| Service management | `--install/uninstall-service` | **Developer** | Production deployment |
| Manual cleanup | CLI `cleanup` | **Developer** | Resource leak debugging |
| Check command | CLI `check` (full mode) | **Developer** | Superblock consistency audit |
| Profiler data | GUI Performance panel | **Internal** | Consumed by Advanced Volume Info |
| Event bus | Internal | **Internal** | Infrastructure — no direct UI |
| State machine | Internal | **Internal** | Automatic transitions |
| Logger | Internal | **Internal** | Always active background |
| Journal engine | Internal | **Internal** | Transparent crash protection |
| Superblock format | Internal | **Internal** | Called by create/load/destroy |
| Pool file management | Internal | **Internal** | Called by init/purge/destroy |
| Stripe engine core | Internal | **Internal** | Engine internals |
| Mirror engine core | Internal | **Internal** | Engine internals |
| Cache engine core | Internal | **Internal** | Engine internals |
| FUSE bridge | Internal | **Internal** | Called by mount/unmount |
| Disk scanner IOCTL | Internal | **Internal** | Called by scan |
| UI model | Internal | **Internal** | Data plumbing for GUI |
| Test infrastructure | `tests/`, `stress/` | **Internal** | Not shipped in binary |
| Version info | `--version`, About dialog | **Beginner** | First thing users check |
| `--help` flag | CLI flag | **Beginner** | First resort for new users |
