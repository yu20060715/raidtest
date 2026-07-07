# RAIDTEST v1.0 RC4 — Architecture Presentation

## 1. Architecture Diagram

```
┌──────────────────────────────────────────────────────────────────────────┐
│  L7: FRONTEND                                                            │
│  ┌──────────────┐  ┌───────────────────────┐  ┌──────────────────────┐  │
│  │  main.c       │  │  gui.cpp / gui.h      │  │  cmd_handler.c/h    │  │
│  │  (entry)      │  │  (ImGui + D3D11)      │  │  (CLI dispatch)     │  │
│  └──────┬───────┘  └──────────┬────────────┘  └──────────┬───────────┘  │
│         │                     │                           │               │
│         └─────────────────────┼───────────────────────────┘               │
│                               ▼                                           │
│  L6: SERVICE LAYER                                                        │
│  ┌───────────────────────────────────────────┐                           │
│  │  raid_service.c/h  (30 public API funcs)  │                           │
│  │  wizard.c/h  (interactive setup)          │                           │
│  │  daemon.c/h  (service + background)       │                           │
│  └──────────────────────┬────────────────────┘                           │
│                          │                                                │
│                          ▼                                                │
│  L5: MANAGER LAYER                                                        │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐                   │
│  │ volume_mgr    │  │ device_mgr   │  │ metadata_mgr │                   │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘                   │
│         │                 │                  │                            │
│         ▼                 ▼                  ▼                            │
│  L4: ENGINE LAYER                                                         │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐                   │
│  │ stripe_engine │  │mirror_engine │  │planner_engine│                   │
│  │ (RAID0 LBA)   │  │ (RAID1)      │  │ (calculator) │                   │
│  └──────┬───────┘  └──────┬───────┘  └──────────────┘                   │
│         │                 │                                              │
│         ▼                 ▼                                              │
│  L3: STORAGE LAYER                                                        │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐                   │
│  │storage_common │  │  ram_cache   │  │   journal    │                   │
│  │(async I/O)    │  │ (write-back) │  │ (write-ahead)│                   │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘                   │
│         │                 │                  │                            │
│         ▼                 ▼                  ▼                            │
│  L2: DISK I/O LAYER                                                       │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐                   │
│  │   pool_io    │  │disk_scanner  │  │  bench_io    │                   │
│  │(pool files)   │  │(IOCTL scan)  │  │(benchmark)   │                   │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘                   │
│         │                 │                  │                            │
│         ▼                 ▼                  ▼                            │
│  L1: OS / HARDWARE                                                        │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐                   │
│  │ WinFsp FUSE  │  │ Windows API  │  │ DirectX 11   │                   │
│  │ (fuse_bridge)│  │ (CreateFile, │  │ (gui.cpp)    │                   │
│  │              │  │  IOCTL, etc) │  │              │                   │
│  └──────────────┘  └──────────────┘  └──────────────┘                   │
└──────────────────────────────────────────────────────────────────────────┘
  L0: CROSS-CUTTING (event_bus, config, logger, profiler, cleanup, crc32,
                      uuid, superblock, ui_model, raid_query)
```

**Seven layers, 30+ modules.** Each layer has a single responsibility. Data flows
down for I/O and up for results.

---

## 2. Innovation: Asymmetric Stripe Engine

RAID0 traditionally requires identical disks. Asymmetric striping removes this
constraint by computing per-disk I/O ratios based on measured write speed.

### Algorithm

1. **Benchmark**: Each disk is benchmarked (sequential write MB/s).
2. **Ratio computation**: Speeds are normalized to a GCD-based ratio.
   Example: NVMe (2800 MB/s) : SATA SSD (500 MB/s) = 28:5.
3. **Phased mapping**: The virtual LBA space is divided into cycles.
   Each cycle is split into phases proportional to the disk ratios.
   A faster disk services more phases per cycle.
4. **I/O dispatch**: Each virtual address maps to exactly one
   (disk, offset) pair via the phase table.

```
Example: Disk 0 (500 MB/s), Disk 1 (1000 MB/s)
         Ratio: 1:2
         Cycle: 3 phases (Phase 0: Disk 0, Phase 1-2: Disk 1)

  Virtual LBA: |  0-1  |  2-3  |  4-5  |  6-7  |  8-9  | ...
               | Ph0   | Ph1   | Ph2   | Ph0   | Ph1   | Ph2   |
  Disk 0:      | 0-1   |       |       | 2-3   |       |       |
  Disk 1:      |       | 0-1   | 2-3   |       | 4-5   | 6-7   |
```

**Result**: The faster disk handles proportionally more I/O, utilizing
full bandwidth of mixed-speed arrays.

### Why Not Standard RAID?

| Feature | Standard RAID0 | Asymmetric Stripe |
|---------|---------------|-------------------|
| Disk requirement | Identical size/speed | Any size, any speed |
| Capacity | N x smallest | Sum of all pool sizes |
| Speed utilization | Limited by slowest | Proportional to each |
| Mixed NVMe+SSD+HDD | Not practical | Fully supported |

---

## 3. Demo Explanation

### Segment 1: Launch (30s)
GUI starts in Beginner mode. Welcome wizard offers Quick Setup or Explore.

### Segment 2: Scan Disks (30s)
Click [Scan]. IOCTL-based disk enumeration detects physical drives,
runs sequential benchmark (read/write MB/s), populates disk table.

### Segment 3: Create RAID1 Mirror (1 min)
Check 2+ disks, click [Mirror]. Pool files (`stripe_pool.dat`) are
created on each disk. Mirror engine configures write-to-all mode.
Superblock v4 metadata is written (UUID, generation, CRC32, RAID level=1).
Volume object constructed in memory.

### Segment 4: Mount (1 min)
Click [Mount]. WinFsp FUSE filesystem registered as `G:\`. Write-back
cache initialized (64 KB blocks, async flush thread). Journal replays
any uncommitted WAL entries from previous crashes.

### Segment 5: Create Demo File (30s)
Create `RAIDV3_DEMO.txt` on `G:\`. Data path: FUSE → mirror engine
(write-to-all) → cache (dirty/valid bitmaps) → journal (WAL entry) →
async OVERLAPPED I/O.

### Segment 6: Simulate Disk Failure (30s)
Switch to Developer tab → Simulation Controls → Inject fault on disk 0.
Error counter triggers `InterlockedExchange(&disk->faulty, 1)`.
Volume enters DEGRADED mode. Data still accessible from healthy mirror member.

### Segment 7: Show DEGRADED + Rebuild (1 min)
Volume Info shows yellow DEGRADED badge. System continues serving reads/writes
from remaining healthy disk. Run Rebuild wizard → 64MB chunks copied from
healthy disk to replacement → `FlushFileBuffers()` after each chunk.
State returns to MOUNTED with full redundancy restored.

### Segment 8: Verify Recovered Data (30s)
Open the demo file on `G:\`. Content preserved through failure and rebuild.
Confirms RAID1 fault tolerance: disk failure → degraded mode → rebuild → zero data loss.

### Fallback: CLI Mode
If GUI fails (no D3D11), run `raidtest_winfsp.exe --cli`. All 31 commands
available. Type `help` for command list.

---

## 4. Known Limitations

### Platform
- **Windows-only**: WinFsp and Windows API dependencies. No Linux/macOS.
- **Administrator required**: Disk IOCTLs and WinFsp mount need elevation.
- **Max 4 disks**: Hard-coded `MAX_DISKS` and `MAX_IO_ENTRIES` limits.
- **Max 8 custom disks**: `MAX_CUSTOM_DISKS` limits file-backed pool count.

### RAID Levels
- **RAID0 only (stripe)**: No RAID5/6 parity. No RAID10 creation (planner
  estimates capacity only).
- **RAID1 only (mirror)**: No erasure coding, no distributed parity.
- **No online expansion for RAID1**: Expand only works on RAID0.
- **No hot spare**: Replacement disk must be specified manually in rebuild.

### Cache
- **Journal grows unbounded**: No circular buffering. WAL file grows
  until process restart.
- **Write-back cache volatile**: Cached writes not committed to disk until
  flush thread runs (1s interval). Power loss may lose up to 1s of data.
- **Cache size limited**: Default 4096 MB max, bound by available RAM.
- **No TRIM/discard**: Cache does not pass through TRIM commands to disks.

### FUSE / Filesystem
- **Flat file table**: 64-entry array, O(n) lookup, silent overwrite when full.
- **No symlinks, ACLs, or extended attributes**: Basic FAT-like semantics.
- **Single mount point**: One volume at a time.
- **No concurrent FUSE access**: All callbacks share a single critical section.

### Testing & Stability
- **Test pool files path**: Superblock tests hardcode `C:\RAIDTEST\` path.
- **No long-run validation**: `test_longrun.exe` exists but not verified.
- **Limited concurrent stress**: 4 threads, 250 ops per thread.
- **No fuzz testing**: Input validation is minimal (basic bounds checks).

### Security
- **No encryption**: Data stored in plaintext on pool files.
- **No authentication**: Any process with access to the mount point
  can read/write.
- **No secure erase**: Pool files are deleted with `DeleteFileW`, not
  overwritten.

### Code Quality
- **Global state**: 24 `raid_service` functions and 14 FUSE callbacks
  access `g_state` without acquiring the state lock (T1 in backlog).
- **OVERLAPPED on stack**: `stripe_read_raw`/`stripe_write_raw` allocate
  OVERLAPPED on stack — safe only because `async_io_wait()` blocks
  before return (B1 in backlog).
- **FUSE buffer overflows**: `parent_dir_exists()` and `raid_rename()`
  have bounds checks but no dynamic sizing (B2, B3 in backlog).
- **Warnings**: 2 pre-existing compiler warnings (always-true comparison,
  strncpy truncation).
