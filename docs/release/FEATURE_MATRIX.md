# FEATURE MATRIX

**Date:** 2026-07-07  
**Validator:** Capstone Validation Engineer  

---

## Feature Classification Key

| Tier | Label | Target User | Knowledge Required |
|------|-------|-------------|-------------------|
| 1 | **Beginner** | First-time, non-technical | Zero — just click buttons |
| 2 | **Advanced** | Power user, IT admin | RAID concepts, cache strategies |
| 3 | **Developer** | Engineer, contributor | Internal state, stripe mapping, simulation |
| 4 | **Internal** | System | No direct user interaction — infrastructure |

---

## Complete Feature Matrix

| # | Feature | Tier | GUI Entry | CLI Command | Source Location | Lines | Demo Value | Status |
|---|---------|------|-----------|-------------|-----------------|-------|------------|--------|
| 1 | **Scan Physical Disks** | Beginner | [Scan] toolbar btn / Beginner [Scan Disks] | `scan` | `gui.cpp:1017,1415` / `cmd_handler.c:244` / `disk_scanner.c` | ~200 | Show disk detection via IOCTL | ✅ DONE |
| 2 | **Select Disks for RAID** | Beginner | Checkbox in disk table | `select <id...>` | `gui.cpp:1106-1117` / `cmd_handler.c:247` / `device_manager.c` | ~80 | Choose which disks to use | ✅ DONE |
| 3 | **Set Pool Size** | Beginner | Pool size spinner | `init [id:mb...]` | `gui.cpp:1021-1024` / `cmd_handler.c:248` / `pool_io.c` | ~100 | Allocate disk space for RAID | ✅ DONE |
| 4 | **Create RAID0 (Stripe)** | Beginner | [Create] toolbar btn | `create` | `gui.cpp:1019-1027` / `cmd_handler.c:249` / `stripe_engine.c` | 715 | Combine disks into one fast volume | ✅ DONE |
| 5 | **Create RAID1 (Mirror)** | Advanced | [Mirror] toolbar btn | `mirror` | `gui.cpp:1029-1031` / `cmd_handler.c:250` / `mirror_engine.c` | 180 | Create fault-tolerant mirror | ✅ DONE |
| 6 | **Mount Volume** | Beginner | [Mount] toolbar btn | `mount [letter]` | `gui.cpp:1033-1038` / `cmd_handler.c:254` / `fuse_bridge.c` | 621 | Access RAID as Windows drive | ✅ DONE |
| 7 | **Unmount Volume** | Beginner | [Unmount] toolbar btn | `unmount` | `gui.cpp:1040-1042` / `cmd_handler.c:255` / `volume_manager.c:78-91` | 13 | Safely remove drive letter | ✅ DONE |
| 8 | **Destroy Volume** | Beginner | [Destroy] → Confirm dialog | `destroy` | `gui.cpp:1044-1046,1327-1348` / `cmd_handler.c:258` / `volume_manager.c:93-110` | 17 | Delete volume + pool files | ✅ DONE |
| 9 | **Quick Setup** | Beginner | [Quick Setup] btn | `quick` | `gui.cpp:1413` (W_QUICK_SETUP) / `cmd_handler.c:273` / `raid_service.c` | ~50 | Scan+Create+Cache+Mount in one | ✅ DONE |
| 10 | **Quick Create (Wizard)** | Beginner | Welcome Wizard [Quick Setup] | `wizard` | `gui.cpp:937-979` / `cmd_handler.c:272` / `wizard.c` | ~150 | Guided 8-step setup | ✅ DONE |
| 11 | **Benchmark Volume** | Beginner | [Benchmark] btn | `benchfs [MB] [KB]` | `gui.cpp:1048-1055,1424-1429` / `cmd_handler.c:261` / `bench_io.c` | ~100 | Show R/W throughput | ✅ DONE |
| 12 | **Health Check** | Beginner | [Health Check] btn | `check` | `gui.cpp:1419` / `cmd_handler.c:262` / `raid_service.c` | ~20 | Verify disk + superblock integrity | ✅ DONE |
| 13 | **Volume Status** | Beginner | Volume Info panel (auto) | `info` / `status` | `gui.cpp:1165-1222` / `cmd_handler.c:263,265` / `raid_query.c` | ~50 | State, level, capacity, health | ✅ DONE |
| 14 | **Version / About** | Beginner | View → About | `--version` | `gui.cpp:912-935` / `main.c:71-77` | 25 | Show build info | ✅ DONE |
| 15 | **Settings** | Beginner | File → Settings | `config-save` / `config-load` | `gui.cpp:778-813,1567` / `config.c` | ~100 | Mount letter, cache size, theme | ✅ DONE |
| 16 | **Cache Configuration** | Advanced | Settings (cache size) | `cache [sizeMB\|wt\|off]` | `gui.cpp:788-792` (via Settings) / `cmd_handler.c:253` / `ram_cache.c` | 258 | Control write-back cache behavior | ✅ DONE |
| 17 | **Restore from Superblock** | Advanced | Actions → Restore → From Superblock | `load [drive]` | `gui.cpp:1449-1455` (W_LOAD_SUPERBLOCK) / `cmd_handler.c:256` / `metadata_manager.c` | ~100 | Recover volume from on-disk metadata | ✅ DONE |
| 18 | **Restore from Saved Config** | Advanced | Actions → Restore → From Saved Config | `config-load` → `load` | `gui.cpp:1457-1461` (W_LOAD_CONFIG) / `config.c` | ~100 | Recreate from JSON config | ✅ DONE |
| 19 | **Mirror Rebuild** | Advanced | Actions → Rebuild (wizard) | `rebuild <idx> <disk> [MB]` | `gui.cpp:1471-1504` / `cmd_handler.c:252` / `mirror_engine.c:126-180` | 54 | Replace failed disk + copy data | ✅ DONE |
| 20 | **Expand Volume** | Advanced | ❌ No GUI btn | `expand <id:mb...>` | `cmd_handler.c:251` / `stripe_engine.c` (expand path) | ~50 | Add disks to existing RAID0 | ✅ DONE (CLI only) |
| 21 | **Planner** | Advanced | Planner panel (auto) | `planner` | `gui.cpp:1124-1163` / `planner_engine.c` | ~80 | Preview RAID0/1/10 capacity | ✅ DONE |
| 22 | **Export Diagnostics** | Advanced | File → Export Diagnostic | N/A (GUI only) | `gui.cpp:348-437` (W_EXPORT) | 90 | Bundle metadata + event log + system info | ✅ DONE |
| 23 | **Per-Disk Benchmark** | Advanced | ❌ No GUI btn (benchmark is combined) | `bench [sizeMB]` | `cmd_handler.c:246` / `bench_io.c` | ~100 | Individual disk speed test | ✅ DONE (CLI only) |
| 24 | **Purge Metadata** | Advanced | ❌ No GUI btn (hidden) | `purge` | `cmd_handler.c:257` / `volume_manager.c` | ~20 | Delete pool files + superblock | ✅ DONE (CLI only) |
| 25 | **Manual Drive Mapping** | Advanced | ❌ No GUI btn | `mapdrive <id> <letter>` | `cmd_handler.c:245` / `device_manager.c` | ~30 | Assign drive letter before pool | ✅ DONE (CLI only) |
| 26 | **Live Status Dashboard** | Advanced | ❌ No GUI equivalent | `status` | `cmd_handler.c:265` / `raid_query.c` | ~30 | Full-screen CLI dashboard | ✅ DONE (CLI only) |
| 27 | **Metadata Dump** | Developer | ❌ No GUI panel | `metadata [drive]` | `cmd_handler.c:266` / `superblock.c` | ~200 | Raw superblock field display | ✅ DONE (CLI only) |
| 28 | **LBA Mapping Dump** | Developer | ❌ No GUI panel (described in docs) | `map` | `cmd_handler.c:264` / `stripe_engine.c` (mapping logic) | ~30 | Stripe phase table | ✅ DONE (CLI only) |
| 29 | **Simulate Disk Failure** | Developer | Developer → Simulation Controls panel | `simulate <idx> <f/h/d>` | `gui.cpp` (W_SIMULATE_FAIL/HEALTHY/DISCONNECT) / `cmd_handler.c:267` / `raid_service.c` | ~30 | Inject fault for testing | ✅ DONE (GUI + CLI) |
| 30 | **I/O Stress Test** | Developer | ❌ No GUI btn | `test` | `cmd_handler.c:259` / `stripe_engine.c` | ~50 | Engine-level write/read/verify | ✅ DONE (CLI only) |
| 31 | **Random I/O Stress** | Developer | ❌ No GUI btn | `random <ops> <maxkb>` | `cmd_handler.c:260` / `stripe_engine.c` | ~30 | Random access pattern test | ✅ DONE (CLI only) |
| 32 | **Filesystem Benchmark** | Developer | ❌ Beginner [Benchmark] (same) | `benchfs [MB] [KB]` | `gui.cpp:1048-1055` / `cmd_handler.c:261` | ~50 | FUSE-layer throughput test | ✅ DONE |
| 33 | **Event Log Browser** | Developer | Event Log panel (color-coded) | `events` | `gui.cpp:1264-1286` / `cmd_handler.c:269` | 22 | 500-line bounded, color-coded | ✅ DONE |
| 34 | **Performance Dashboard** | Developer | Developer mode Perf Dashboard | N/A (internal) | `gui.cpp:860-910` | 50 | Live R/W IOPS/latency plots | ✅ DONE |
| 35 | **Daemon Mode** | Developer | ❌ No GUI | `--daemon` | `main.c:30-32` / `daemon.c` | ~100 | Headless console operation | ✅ DONE |
| 36 | **Service Management** | Developer | ❌ No GUI | `--install/uninstall-service` | `main.c:116-124` / `daemon.c` | ~50 | Windows SCM integration | ✅ DONE |
| 37 | **Force CLI Mode** | Developer | ❌ N/A | `--cli` | `main.c:38-43` | 5 | Run without GUI | ✅ DONE |
| 38 | **Manual Cleanup** | Developer | ❌ No GUI btn | `cleanup` | `cmd_handler.c:274` / `cleanup.c` | ~50 | Release all resources | ✅ DONE |
| 39 | **Auto-Restore on Startup** | Internal | Automatic (no UI) | `(no args)` | `cmd_handler.c:214-222` (auto_restore_or_quick) | 8 | Auto-load config at startup | ✅ DONE |
| 40 | **Config JSON Persistence** | Internal | Settings → Save / Auto-save | `config-save` / `config-load` | `config.c` | ~100 | Save/load at `%USERPROFILE%\\.config\\RAIDTEST\\config.json` | ✅ DONE |
| 41 | **Superblock v4** | Internal | Background (mounted volume info) | `metadata` (dump) | `superblock.c` | ~200 | On-disk format: magic 0x52444953, UUID, gen, CRC32 | ✅ DONE |
| 42 | **Write-Back Cache Engine** | Internal | Background (Volume Info → Cache ON/OFF) | `cache` (config) | `ram_cache.c` | 258 | 64KB blocks, dirty/valid bitmaps, flush thread | ✅ DONE |
| 43 | **Write-Ahead Journal Engine** | Internal | Background (transparent) | Journal auto-managed | `journal.c` | 210 | BEGIN/DATA/COMMIT, CRC, crash replay | ✅ DONE |
| 44 | **Asymmetric Stripe Engine** | Internal | Background (Creator → RAID0) | `create` | `stripe_engine.c` | 715 | Multi-phase LBA mapping, speed-proportional ratios | ✅ DONE |
| 45 | **Mirror Engine** | Internal | Background (Creator → RAID1) | `mirror` | `mirror_engine.c` | 180 | Write-to-all, degraded read, rebuild | ✅ DONE |
| 46 | **State Machine** | Internal | Status bar / Volume Info | Status auto-tracked | `common.h:53-61` (RAID_STATE enum) / `raid_service.c` | 7 states | DISCONNECTED → DISCOVERED → INITIALIZED → MOUNTED → DEGRADED → RECOVERING → UNMOUNTED | ✅ DONE |
| 47 | **Event Bus (Pub/Sub)** | Internal | Event Log panel | `events` (browse) | `event_bus.c` | ~80 | 13 event types, subscribe/publish pattern | ✅ DONE |
| 48 | **Thread-Safe Logger** | Internal | Background | Log auto-written | `logger.c` | ~60 | File + console, CRITICAL_SECTION protected | ✅ DONE |
| 49 | **I/O Profiler** | Internal | Performance panel / Developer dashboard | Profiler auto-sampled | `profiler.c` | ~60 | R/W MB/s, IOPS, latency, queue depth | ✅ DONE |
| 50 | **CRC32 Checksum** | Internal | Background (superblock integrity) | N/A | `crc32.c` | ~40 | CRC32-Castagnoli for superblock + journal | ✅ DONE |
| 51 | **UUID v4 Generation** | Internal | Background (Volume Info → UUID) | N/A | `uuid.c` | ~40 | Unique volume identification | ✅ DONE |
| 52 | **Async OVERLAPPED I/O** | Internal | Background (transparent) | N/A | `storage_common.c` | 52 | ReadFile/WriteFile with OVERLAPPED | ✅ DONE |
| 53 | **GUI Dark Theme** | Beginner | Settings → Theme | `config-save` (persists) | `gui.cpp:652-687` | 35 | Custom 30+ color scheme | ✅ DONE |
| 54 | **GUI Light Theme** | Beginner | Settings → Theme | `config-save` (persists) | `gui.cpp:689-694` | 5 | ImGui StyleColorsLight | ✅ DONE |
| 55 | **Welcome Wizard** | Beginner | First-run popup | `(first_run)` | `gui.cpp:937-979` | 42 | Quick Setup / Explore / Dismiss | ✅ DONE |
| 56 | **Confirmation Dialogs** | Beginner | Destroy → "Are you sure?" | N/A | `gui.cpp:1327-1348` | 21 | "Yes, Destroy" / "Cancel" | ✅ DONE |
| 57 | **Toast Notifications** | Beginner | Top-center overlay | N/A | `gui.cpp:146-191` | 45 | Color-coded, 5s auto-dismiss | ✅ DONE |
| 58 | **Status Bar** | Beginner | Bottom bar | N/A | `gui.cpp:1288-1325` | 37 | Ready/busy, progress bar, ETA, version | ✅ DONE |
| 59 | **Embedded CLI Console** | Developer | ❌ NOT IMPLEMENTED | N/A | `gui.cpp` — no such widget found | 0 | Described in docs but not in source | ❌ MISSING |
| 60 | **RAID10 Planner** | Advanced | Planner panel (auto) | `planner` | `gui.cpp:1124-1163` / `planner_engine.c` | ~20 | Capacity estimation only (no RAID10 creation) | ✅ DONE |
| 61 | **Unit Tests (39)** | Internal | N/A | `raidtest_tests.exe` | `test_*.c` (6 files) | ~1000 | 39 test scenarios, all passing | ✅ DONE |
| 62 | **Concurrent Stress Test** | Internal | N/A | `test_concurrent.exe` | `tests/test_concurrent.c` | ~200 | Multi-threaded I/O stress | ✅ DONE |
| 63 | **Random I/O Stress** | Internal | N/A | `test_random_io.exe` | `tests/test_random_io.c` | ~200 | Random access pattern test | ✅ DONE |
| 64 | **Metadata Corruption** | Internal | N/A | `test_metadata_corrupt.exe` | `tests/test_metadata_corrupt.c` | ~200 | Superblock corruption → recovery | ✅ DONE |
| 65 | **Power Fail / Journal** | Internal | N/A | `test_powerfail.exe` | `stress/test_powerfail.c` | ~200 | Simulated crash → journal replay | ✅ DONE |
| 66 | **Long Run Stability** | Internal | N/A | `test_longrun.exe` | `tests/test_longrun.c` | ~200 | Extended duration stability | ✅ DONE (not executed) |

---

## Feature Count Summary

| Tier | Features | GUI Available | CLI Available | Status |
|------|----------|--------------|---------------|--------|
| **Beginner** | 17 | 17 (100%) | 16 (94%) | ✅ All functional |
| **Advanced** | 12 | 7 (58%) | 12 (100%) | ⚠️ 5 CLI-only features |
| **Developer** | 12 | 4 (33%) | 12 (100%) | ⚠️ 1 missing (embedded CLI console) |
| **Internal** | 25 | 0 (infrastructure) | N/A | ✅ All functional |
| **Total** | **66** | **28** | **40** | **✅ 65/66 implemented** |

---

## Features with Documentation-Only Status

| # | Feature | Documented In | Reality |
|---|---------|--------------|---------|
| 59 | Embedded CLI Console (Developer) | `PRODUCT_DESIGN.md:273`, `USER_FLOW.md:329-331` | ❌ NOT FOUND in `gui.cpp` source — feature not implemented |
| D2 | Destroy button in Beginner | `USER_FLOW.md:56-59` | ❌ Not in Beginner mode — design decision per PRODUCT_DESIGN.md |

---

## Cross-Reference: Documentation vs Source

Every feature listed above was cross-referenced against:

- `gui.cpp` (1695 lines) — all GUI panels, dialogs, worker actions
- `cmd_handler.c` (282 lines) — full CLI dispatch table
- `raid_service.h` (56 lines) — all 30 public API functions
- `main.c` (134 lines) — all CLI flags, entry point logic
- `PRODUCT_DESIGN.md` (360 lines) — feature classification
- `USER_FLOW.md` (382 lines) — workflow descriptions
- `SYSTEM_MAP.md` (501 lines) — architecture map, module reference
- `RELEASE_VERIFICATION.md` (199 lines) — RC4 release check

No feature claimed in documentation was found to be non-existent, with the documented exception of the embedded CLI console.
