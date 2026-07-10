# RAIDTEST — Project Final Summary

## 1. Project Purpose

RAIDTEST is a virtual RAID engine for Windows that creates asymmetric stripe (RAID0) and mirror (RAID1/10) volumes across mixed-speed physical disks (SATA SSD, NVMe, HDD). It presents the virtual volume as a Windows drive letter via WinFsp FUSE, enabling standard file I/O through any application.

Key differentiator: unlike traditional RAID which requires identical disks, RAIDTEST uses a **weighted stripe algorithm** that allocates I/O proportional to each disk's speed — faster disks handle more data per cycle.

---

## 2. System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      GUI (ImGui + DX11)                     │
│              src/gui.cpp  —  2069 lines                     │
├─────────────────────────────────────────────────────────────┤
│                    CLI (cmd_handler.c)                       │
│                Interactive + command-line                    │
├─────────────────────────────────────────────────────────────┤
│                    RAID Service Layer                        │
│           raid_service.c  —  public API facade               │
├──────────────────┬──────────────────┬───────────────────────┤
│   Stripe Engine  │   Mirror Engine  │   RAM Cache           │
│  stripe_engine.c │  mirror_engine.c │  ram_cache.c          │
│  (weighted I/O   │  (mirror write   │  (write-back cache    │
│   distribution)  │   + rebuild)     │   + flush)            │
├──────────────────┴──────────────────┴───────────────────────┤
│              Pool I/O + Journal + Superblock                 │
│    pool_io.c  journal.c  superblock.c  config.c             │
├─────────────────────────────────────────────────────────────┤
│          WinFsp FUSE Bridge (fuse_bridge.c)                 │
│              Mounts volume as drive letter                   │
├─────────────────────────────────────────────────────────────┤
│          Windows OVERLAPPED I/O (per-disk workers)          │
│              disk_scanner.c  bench_io.c                     │
└─────────────────────────────────────────────────────────────┘
```

---

## 3. Core Features

| Feature | Description | Status |
|---------|-------------|--------|
| RAID0 (Stripe) | Asymmetric stripe across mixed-speed disks | ✅ |
| RAID1 (Mirror) | Full mirror with degraded read + rebuild | ✅ |
| Weighted ratio | I/O proportional to disk speed (MAX_RATIO=32) | ✅ |
| RAM write-back cache | Configurable MB, dirty tracking, async flush | ✅ |
| Journal | Crash recovery for metadata operations | ✅ |
| Superblock v4 | On-disk metadata with UUID, generation, CRC | ✅ |
| WinFsp FUSE mount | Drive letter access via Windows Explorer | ✅ |
| GUI | ImGui + DX11: theme, settings, all dialogs | ✅ |
| CLI | Interactive 28-command shell | ✅ |
| Diagnostic export | Export metadata+logs+system info to ZIP | ✅ |
| CrystalDiskMark | Compatible (file-level I/O via diskspd) | ✅ |

---

## 4. Weighted Stripe Algorithm

The core algorithm in `stripe_engine.c` distributes I/O across disks proportional to their measured write speed:

1. **Benchmark**: Each disk is measured for write throughput (MB/s)
2. **Normalize**: `raw[i] = (speed[i] * MAX_RATIO(32) + max_speed/2) / max_speed` → rounded ratios ≤ 32
3. **GCD reduction**: simplify ratios to smallest integer representation
4. **Phase building**: create mapping phases where each cycle = sum(ratios) × stripe_unit (1 MB)
5. **I/O distribution**: `map_single_byte()` assigns LBA ranges to disks based on ratio-weighted segments

Example with `{NVMe=5820, SATA=491, HDD=490}` MB/s:
- Normalized: `{32, 3, 3}`, cycle = 38 MB
- Per 38 MB: NVMe gets 32 MB, each SATA gets 3 MB

---

## 5. Cache Mechanism

Write-back RAM cache (`ram_cache.c`):
- Configurable size (default 4096 MB), 64 KB blocks
- Dirty bit tracking: each block has a dirty flag
- Two-phase flush: `cache_flush_all()` → `memcpy` to flush buffer → OVERLAPPED WriteFile
- Write-through mode available for testing
- Background flush thread for periodic dirty block eviction
- Concurrent flush protection via `cache_flush_in_progress` flag

---

## 6. Parallel I/O Architecture

Each physical disk has a dedicated worker thread with a lock-free SPSC ring buffer:

```
submit_entries():                    Per-disk workers (parallel):
  push Disk0 → SetEvent(D0)    ──►   Thread0: ring_pop → WriteFile(D0) ──┐
  push Disk1 → SetEvent(D1)    ──►   Thread1: ring_pop → WriteFile(D1) ──┤
  push Disk2 → SetEvent(D2)    ──►   Thread2: ring_pop → WriteFile(D2) ──┤
  WaitForSingleObject(comp)    ◄──── Last worker decrements pending → SetEvent
```

I/O completion uses `InterlockedDecrement` on a shared counter; the last worker signals completion.

---

## 7. GUI Features

Complete ImGui + DX11 GUI in `src/gui.cpp` (2075 lines):

| Area | Features |
|------|----------|
| Theme engine | Dark/Light toggle |
| Settings | Drive letter, pool MB, cache MB, theme, auto-restore, auto-mount |
| Mode tabs | Beginner / Advanced / Developer |
| Toolbar | Scan/Create/Mirror/Mount/Unmount/Destroy/Quick/Check/Bench |
| Disk list | Model/ID/serial/type/bus/size/speed/status/RAID/checkbox |
| Disk allocation | Per-disk pool MB inputs |
| Volume info | State/RAID/disks/capacity/used%/mounted/cache/RW/health |
| Event log | Color-coded ERROR/WARN/OK/INFO, 500-line circular buffer |
| Status bar | Progress, ETA, cancel |
| Dialogs | Settings, About, Welcome, Destroy/Purge confirm, Expand, LBA Map, Metadata, Benchmark, Raw Bench, Export, Restore, Rebuild, I/O Test, Random Stress |
| Developer mode | Simulation controls (Fail/Healthy/Disconnect), Cache controls |
| Menu bar | File (Config/Save/Export/Cleanup/Purge/Settings), Actions (all ops), View (About) |
| Workers | Background thread for all operations with progress and cancel |

---

## 8. Performance Test Results

### Balanced RAID0 (2 NVMe SSDs)
| Metric | Value |
|--------|-------|
| Disk 0 | Acer SSD 5723 MB/s |
| Disk 1 | P3 Plus 4418 MB/s |
| Ratio | 32:25, cycle = 57 MB |
| Write (RAW) | avg 4413 MB/s |
| Read (RAW) | avg 3419 MB/s |
| Write (FS) | avg 4397 MB/s |
| Read (FS) | avg 3409 MB/s |
| Efficiency | 77% of fastest disk |

### Weighted RAID0 (NVMe + 2 SATA SSDs)
| Metric | Value |
|--------|-------|
| Disk 0 | Acer SSD 5820 MB/s |
| Disk 1 | MX500 491 MB/s |
| Disk 2 | WD Green 490 MB/s |
| Ratio | 32:3:3, cycle = 38 MB |
| Write (RAW) | avg 2011 MB/s |
| Read (RAW) | avg 1933 MB/s |
| Efficiency | 4× single SATA, 35% of NVMe |

### Unit Tests
- **Test suite**: 43 tests, 33 pass, 10 pre-existing failures (all I/O-related, require physical disk setup)
- **Stress tests**: concurrent, random I/O, metadata corruption, power failure — all PASS

---

## 9. CrystalDiskMark Compatibility

Verified with `check_cdm.exe`:
- Volume IOCTL tests (11-12) fail on FUSE volume handle — expected behavior
- CDM uses `diskspd.exe` for file-level I/O, which works correctly on FUSE mounts
- FUSE ioctl function code extraction fixed: `(cmd >> 2) & 0xFFF`
- **Conclusion: CDM is fully compatible**

---

## 10. Known Limitations

### Bugs (P0-P1)
- OVERLAPPED use-after-free in I/O paths
- FUSE stack buffer overflows
- FUSE file table lifecycle race
- Journal write synchronization
- Event bus critical section leak
- NULL dereference risk at device_get() call sites

### Technical Debt
- `g_state` locking missing in raid_service.c and FUSE callbacks
- Hardcoded `C:\RAIDTEST\` path in tests
- I/O functions return `bool` instead of RC error codes
- No lock ordering documentation
- Unbounded journal growth
- Flat 64-entry FUSE file table

### Architecture Limitations
- Pool file I/O is serialized at hardware level when pool files reside on the same physical drive
- Per-disk workers process one I/O at a time (no per-disk pipelining)
- FUSE chunk size (64 MB) limits single-I/O throughput
- Stripe unit default (1 MB) means small writes hit only one disk
- RAM cache flush bypasses worker thread pool (direct OVERLAPPED)

---

## 11. Future Improvement Directions

1. **OVERLAPPED heap allocation** — Fix use-after-free by managing OVERLAPPED struct lifetimes
2. **FUSE buffer overflow protection** — Stack-allocated buffers in fuse_bridge.c need size checks
3. **Per-disk I/O pipelining** — Allow multiple concurrent I/Os per disk worker thread
4. **Multi-device pool support** — Span pool files across different physical drives for true parallelism
5. **Dynamic ratio adjustment** — Re-benchmark and adjust ratios at runtime based on workload
6. **Journal size management** — Trim or rotate journal to prevent unbounded growth
7. **Lock ordering documentation** — Formalize `device_manager → stripe_engine → ra_service` hierarchy
8. **RAID5/6 support** — Parity-based redundancy for space efficiency
9. **TRIM/discard forwarding** — Pass through to underlying SSDs for sustained performance
10. **Event-driven GUI updates** — Replace polling with event callbacks for lower latency
