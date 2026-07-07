# CAPSTONE DEMO PLAN

**Target Duration:** 5–10 minutes  
**Mode:** GUI (Advanced mode, with one CLI segment)  
**Audience:** Professor / Review Committee  
**Prerequisites:** Windows 10/11, WinFsp installed, Administrator, 2+ physical disks or pool file capable drives

---

## SEGMENT 1: Launch & First Impressions (30 sec)

| Aspect | Detail |
|--------|--------|
| **Action** | Double-click `raidtest_winfsp.exe` |
| **Expected Screen** | Dark-themed GUI window, maximized to 1280×800 |
| **Key Indicators** | Mode tabs (Beginner / Advanced / Developer), title bar "RAIDTEST v1.0 RC4 - GUI Edition", status bar "Ready" |
| **Source Flow** | `main.c:131` → `gui_run()` → `gui.cpp:1607-1648` (SetupWindow + D3D11 + ImGui init) |
| **Talking Point** | "This is the main GUI. Three mode tabs for different user levels. Dark theme, Dear ImGui + DirectX 11 rendering." |

---

## SEGMENT 2: Scan Disks (30 sec)

| Aspect | Detail |
|--------|--------|
| **Action** | Click **[Scan]** in the toolbar |
| **Expected Screen** | Yellow progress bar `[Scanning disks...]` → Event Log: `[INFO] Scan: OK (X disk(s) found)` → Physical Disks table populates with Model, Serial, Type, Bus, Size, Speed columns |
| **Expected Result** | Disk list shows detected drives with checkboxes in the "Use" column |
| **Source Flow** | `gui.cpp:1017` → `W_SCAN` → `worker_thread:239-249` → `raid_scan()` → `disk_scanner.c` (IOCTL_STORAGE_QUERY_PROPERTY) |
| **Talking Point** | "We scan physical disks using Windows IOCTLs. Each disk shows its model, serial number, type, bus, size, and benchmarked speed. Checkboxes let us select which disks to use." |

---

## SEGMENT 3: Create RAID1/Mirror Volume (1 min)

| Aspect | Detail |
|--------|--------|
| **Action** | Check 2 disks in the "Use" column → Click **[Mirror]** |
| **Expected Screen** | Progress: `[Creating volume...]` → Event Log: `[INFO] Mirror: OK — Volume is ready for mount` → Volume Info panel populates |
| **Volume Info Shows** | State: `INITIALIZED`, RAID Level: `RAID1`, Disks: `2`, Capacity: smallest disk GB, Mounted: `No`, Cache: `ON (1024 MB)`, UUID, Generation |
| **Source Flow** | `gui.cpp:1029-1031` → `W_MIRROR` → `worker_thread:270-280` → `raid_init_pools()` (creates `stripe_pool.dat` on each disk) + `raid_mirror()` (writes superblock v4 with RAID1 type) |
| **Key Files** | `src/mirror_engine.c` (write-to-all, degraded read, rebuild), `src/superblock.c` (v4 format: magic 0x52444953, UUID, generation, CRC32), `src/volume_manager.c` (orchestration) |
| **Talking Point** | "Mirror creates a RAID1 volume. Data written to one disk is duplicated to all other disks in the array. This provides fault tolerance — if any disk fails, the system continues operating from the remaining mirror members." |

---

## SEGMENT 4: Mount & Explorer Verify (1 min)

| Aspect | Detail |
|--------|--------|
| **Action** | Click **[Mount]** → Open Windows Explorer → Navigate to `G:\` |
| **Expected Screen** | Progress: `[Mounting volume...]` → Event Log: `[OK] Mount: OK - Volume mounted at G:` → Volume Info: State → `MOUNTED`, Mounted → `Yes` (green), Uptime counter starts |
| **Explorer Shows** | `G:\` appears as a local disk in "This PC", volume label "RAIDTEST", capacity matches smallest disk size (mirror) |
| **Source Flow** | `gui.cpp:1033-1038` → `W_MOUNT` → `worker_thread:282-292` → `raid_mount('G')` → `volume_mount()` → `fuse_mount()` (WinFsp FUSE_FSCTL) → `ram_cache_init()` (starts flush thread) → `journal_recover()` (replays WAL) |
| **Key Files** | `src/fuse_bridge.c` (13 FUSE callbacks: getattr, readdir, read, write, create, rename, mkdir, rmdir, unlink, statfs, open, flush, release), `src/ram_cache.c` (write-back cache, 64KB blocks, dirty/valid bitmaps, async flush thread), `src/journal.c` (WAL) |
| **Talking Point** | "Mounting registers a WinFsp FUSE filesystem. Behind the scenes: cache is initialized with a background flush thread, and the journal replays any uncommitted writes from a previous crash. The drive appears as a normal Windows drive letter." |

---

## SEGMENT 5: Create File on RAID Volume (30 sec)

| Aspect | Detail |
|--------|--------|
| **Action** | In Explorer on `G:\`: Right-click → New → Text Document → name `capstone.txt` → Open → Type "RAIDTEST demo successful!" → Save → Close |
| **Expected Result** | File appears in `G:\` with content preserved. Volume Info panel shows Written counter increment |
| **Data Path** | Explorer → `CreateFile()` → FUSE callback (`fuse_bridge.c`) → `stripe_engine_write()` (LBA mapping → per-disk I/O split) → `ram_cache_write()` (buffer, mark dirty) → `journal_write()` (WAL entry) → `storage_common_write()` (async OVERLAPPED WriteFile) |
| **Talking Point** | "Data flows through four layers: FUSE → stripe engine (LBA mapping) → write-back cache (64KB blocks) → journal (WAL for crash recovery) → physical disk via async OVERLAPPED I/O." |

---

## SEGMENT 6: Simulate Disk Failure (30 sec)

| Aspect | Detail |
|--------|--------|
| **Action** | Switch to **[Developer]** tab → **Simulation Controls** panel → Enter disk index `0` → Click **[Simulate Fail]** |
| **Expected Screen** | Event Log: fault injection message. Volume Info: State → `DEGRADED` (yellow) |
| **Source Flow** | `gui.cpp` (W_SIMULATE_FAIL) → `worker_thread` → `raid_service.simulate_fail()` → `storage_common.c:3-13` → `InterlockedExchange(&disk->faulty, 1)` |
| **Talking Point** | "We inject a fault on disk 0. The system detects I/O errors, increments the error counter, and marks the disk as faulty. RAID1 enters degraded mode but continues serving data from the remaining healthy mirror member." |

---

## SEGMENT 7: Show DEGRADED State (30 sec)

| Aspect | Detail |
|--------|--------|
| **Action** | Switch to **[Advanced]** tab → Observe Volume Info panel shows `DEGRADED` (yellow badge) |
| **Expected Screen** | State: `DEGRADED` (yellow). System still at `G:\` — files are readable and writable. |
| **Source Flow** | `gui.cpp:1165-1222` (Volume Info render) → reads `vol->state` → displays `RAID_STATE_DEGRADED` → `mirror_engine.c:33-76` (degraded read skips faulty disk) |
| **Talking Point** | "DEGRADED means one mirror member has failed, but the volume remains fully accessible. Every read is served from the healthy disk, every write is written to all remaining healthy disks. No data is lost." |
| **(Optional)** | Open `G:\RAIDV3_DEMO.txt` to verify read still works. Create a new file to verify write still works. |

---

## SEGMENT 8: Rebuild (1 min)

| Aspect | Detail |
|--------|--------|
| **Action** | Click **[Actions]** → **[Rebuild]** → Rebuild wizard appears → Select replacement disk → Click **[Start Rebuild]** |
| **Expected Screen** | Progress: `[Rebuilding...]` indeterminate bar → Event Log: `[OK] Rebuild: OK — Rebuild complete` → Volume Info: State → `MOUNTED` (green) |
| **Source Flow** | `gui.cpp:1471-1504` (ShowRebuildWizard) → `W_REBUILD` → `worker_thread` → `raid_rebuild()` → `mirror_engine.c:126-180` (64MB chunk copy + FlushFileBuffers) |
| **Talking Point** | "Rebuild copies data from the healthy disk to the replacement disk in 64 MB chunks. After each chunk, FlushFileBuffers ensures data is persisted. Once complete, the volume returns to MOUNTED state with full redundancy restored." |

---

## SEGMENT 9: Verify Recovered Data (30 sec)

| Aspect | Detail |
|--------|--------|
| **Action** | Open Windows Explorer → `G:\RAIDV3_DEMO.txt` |
| **Expected Screen** | File content: `"RAIDV3 capstone demo successful!"` — identical to the content written before the failure |
| **Talking Point** | "The file we created before the failure is intact. Data survived the simulated disk failure and the rebuild process. This is the core value of RAID1: fault tolerance with zero data loss." |

---

## SEGMENT 10: Destroy (Cleanup, 30 sec)

| Aspect | Detail |
|--------|--------|
| **Action** | Click **[Destroy]** → Confirmation dialog: **[Yes, Destroy]** |
| **Expected Screen** | Modal: "Are you sure you want to DESTROY the volume?" → Progress: `[Destroying volume...]` → Event Log: `[INFO] Destroy: OK - Volume destroyed` → Volume Info resets to "No volume - Scan + Create first" |
| **Source Flow** | `gui.cpp:1044-1046` → `ShowConfirmDestroy()` at `gui.cpp:1327-1348` → `W_DESTROY` → `worker_thread:306-317` → `raid_destroy()` → unmount + delete pool files + wipe superblock |
| **Talking Point** | "Destroy requires explicit confirmation to prevent accidental data loss. Once confirmed, it unmounts the volume, deletes pool files, and clears all metadata from disk." |

---

## DEMO TIMELINE SUMMARY

```
 0:00 ─ Launch GUI                                          (Segment 1)
 0:30 ─ Scan Disks                                          (Segment 2)
 1:00 ─ Create RAID1/Mirror Volume                           (Segment 3)
 2:00 ─ Mount + Explorer Verify                              (Segment 4)
 3:00 ─ Create File on RAID Volume                           (Segment 5)
 3:30 ─ Simulate Disk Failure                                (Segment 6)
 4:00 ─ Show DEGRADED State                                  (Segment 7)
 4:30 ─ Rebuild                                              (Segment 8)
 5:30 ─ Verify Recovered Data                                (Segment 9)
 6:00 ─ Destroy (Cleanup)                                    (Segment 10)
 6:30 ─ Q&A                                                  (remaining time)
```

**Total: 6–7 minutes core demo + 3–4 minutes Q&A buffer = 10 minutes.**

---

## FALLBACK PLAN

| Failure Scenario | Workaround |
|-----------------|------------|
| No physical disks | Use pool files on existing NTFS drive. CLI: `init 0:1024 1:1024` → `mirror` |
| WinFsp not installed | Show welcome dialog detection. All CLI workflows work without mount. Demonstrate CLI + test commands. |
| GUI fails to launch (no D3D11) | Use `--cli` mode. Full flow: `scan` → `init 0:1024 1:1024` → `mirror` → `mount G` → `simulate 0 fail` → `rebuild 0 0` |
| Only 1 disk available | Show planner panel. Explain RAID1 requires 2+. Demonstrate with simulated pool files. |
| Mount fails (letter conflict) | Change mount letter in Settings or use `mount H` in CLI. |
| Simulate Fail not visible | Use CLI: `simulate 0 fail` from any mode. Verify with `status`. |
| Rebuild has no replacement disk | Create pool file first: CLI `init 0:1024` → then `rebuild 0 0` |

---

## KEY SOURCE FILES REFERENCED

| File | Role in Demo |
|------|-------------|
| `src/main.c` | Entry point, mode selection |
| `src/gui.cpp` | Full GUI (1695 lines, Dear ImGui + DX11) |
| `src/cmd_handler.c` | CLI dispatch (31 commands) |
| `src/raid_service.c/.h` | Unified backend API (30 functions) |
| `src/stripe_engine.c/.h` | Asymmetric RAID0 LBA mapping |
| `src/mirror_engine.c/.h` | RAID1 mirror write/read/rebuild (fault tolerance core) |
| `src/storage_common.c/.h` | Fault detection, error counting, OVERLAPPED I/O |
| `src/ram_cache.c/.h` | Write-back cache (64KB blocks, async flush) |
| `src/journal.c/.h` | Write-ahead log (crash recovery) |
| `src/fuse_bridge.c/.h` | WinFsp FUSE filesystem callbacks |
| `src/superblock.c/.h` | On-disk v4 metadata format |
| `src/volume_manager.c/.h` | Volume lifecycle orchestrator |
| `src/device_manager.c/.h` | Disk list abstraction |
| `src/config.c/.h` | JSON persistence |
| `src/planner_engine.c/.h` | Capacity calculator |
