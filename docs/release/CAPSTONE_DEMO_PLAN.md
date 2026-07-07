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

## SEGMENT 3: Create RAID0 Volume (1 min)

| Aspect | Detail |
|--------|--------|
| **Action** | Check 2 disks in the "Use" column → Click **[Create]** |
| **Expected Screen** | Progress: `[Creating volume...]` → Event Log: `[INFO] Create: OK — Volume is ready for mount` → Volume Info panel populates |
| **Volume Info Shows** | State: `INITIALIZED`, RAID Level: `RAID0`, Disks: `2`, Capacity: combined GB, Mounted: `No`, Cache: `ON (1024 MB)`, UUID, Generation |
| **Source Flow** | `gui.cpp:1019-1027` → `W_CREATE` → `worker_thread:250-269` → `raid_init_pools()` (creates `stripe_pool.dat` on each disk) + `raid_create()` (writes superblock v4) |
| **Key Files** | `src/stripe_engine.c` (asymmetric LBA mapping), `src/superblock.c` (v4 format: magic 0x52444953, UUID, generation, CRC32), `src/volume_manager.c` (orchestration) |
| **Talking Point** | "Create does two things: 1) Creates pool files on each selected disk — these are the backing storage for the virtual volume. 2) Writes a superblock — our on-disk metadata format — containing UUID, generation counter, and CRC32 checksum for integrity." |

---

## SEGMENT 4: Mount & Explorer Verify (1 min)

| Aspect | Detail |
|--------|--------|
| **Action** | Click **[Mount]** → Open Windows Explorer → Navigate to `G:\` |
| **Expected Screen** | Progress: `[Mounting volume...]` → Event Log: `[OK] Mount: OK - Volume mounted at G:` → Volume Info: State → `MOUNTED`, Mounted → `Yes` (green), Uptime counter starts |
| **Explorer Shows** | `G:\` appears as a local disk in "This PC", volume label "RAIDTEST", capacity matches RAID0 combined size |
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

## SEGMENT 6: Unmount (30 sec)

| Aspect | Detail |
|--------|--------|
| **Action** | Click **[Unmount]** |
| **Expected Screen** | Progress: `[Unmounting volume...]` → Event Log: `[OK] Unmount: OK - Volume unmounted` → Volume Info: State → `UNMOUNTED`, Mounted → `No` → `G:\` disappears from Explorer |
| **Source Flow** | `gui.cpp:1040-1042` → `W_UNMOUNT` → `worker_thread:294-305` → `raid_unmount()` → cache flush → journal commit → FUSE unmount |
| **Talking Point** | "Unmount flushes all dirty cache blocks, commits the journal, then unregisters the FUSE filesystem. Pool files and metadata remain on disk." |

---

## SEGMENT 7: Restore from Saved Config (30 sec)

| Aspect | Detail |
|--------|--------|
| **Action** | Click **[Scan]** → Menu: **[Actions]** → **[Restore]** → Dialog: **[From Saved Config]** |
| **Expected Screen** | Progress: `[Restoring from config...]` → Event Log: `[OK] Restore from config: OK` → Volume Info shows same UUID and capacity as before |
| **Source Flow** | `gui.cpp:1578` → `ShowRestoreWizard()` → `W_LOAD_CONFIG` → `worker_thread:502-531` → `config_load()` → `raid_init_pools()` → `raid_create()` |
| **Talking Point** | "The volume configuration was saved to %USERPROFILE%\\.config\\RAIDTEST\\config.json earlier. Restore reads this JSON, recreates pool files, and reapplies the superblock. This proves persistence." |

---

## SEGMENT 8: CLI Alternative (30 sec)

| Aspect | Detail |
|--------|--------|
| **Action** | Switch to terminal → Run: `raidtest_winfsp.exe --cli` |
| **Expected Screen** | Banner: "RAIDTEST v1.0 RC4 - Asymmetric Stripe Engine" followed by auto-restore or quick setup |
| **Commands to Show** | Type `help` → full command list. Type `status` → live dashboard. Type `info` → volume details. Type `map` → LBA mapping table. |
| **Source Flow** | `main.c:38-43` → `cli_main()` → `cmd_process()` dispatch table → `raid_*()` |
| **Talking Point** | "All GUI functionality is available in CLI mode. 31 commands. Good for scripting and automation." |

---

## SEGMENT 9: Destroy (30 sec)

| Aspect | Detail |
|--------|--------|
| **Action** | Click **[Destroy]** → Confirmation dialog: **[Yes, Destroy]** |
| **Expected Screen** | Modal: "Are you sure you want to DESTROY the volume?" with yellow warning → Progress: `[Destroying volume...]` → Event Log: `[INFO] Destroy: OK - Volume destroyed` → Volume Info resets to "No volume - Scan + Create first" |
| **Source Flow** | `gui.cpp:1044-1046` → `ShowConfirmDestroy()` at `gui.cpp:1327-1348` → `W_DESTROY` → `worker_thread:306-317` → `raid_destroy()` → unmount + delete pool files + wipe superblock |
| **Talking Point** | "Destroy requires explicit confirmation to prevent accidental data loss. Once confirmed, it unmounts, deletes pool files, and clears metadata." |

---

## SEGMENT 10: Bonus — Developer Mode (30 sec, if time permits)

| Aspect | Detail |
|--------|--------|
| **Action** | Switch to **[Developer]** tab → CLI: `map`, `metadata C:`, `events` |
| **Expected Screen** | Performance Dashboard with live R/W throughput, IOPS, latency plots. CLI commands show raw internals. |
| **Source Flow** | `gui.cpp:988-1001` (mode switch) → `ShowPerformanceDashboard()` (`gui.cpp:860-910`) → CLI commands |
| **Talking Point** | "Developer mode exposes everything: LBA mapping table, raw superblock fields, event log. Used for debugging and verification." |

---

## DEMO TIMELINE SUMMARY

```
 0:00 ─ Launch GUI                                          (Segment 1)
 0:30 ─ Scan Disks                                          (Segment 2)
 1:00 ─ Create RAID0 Volume                                  (Segment 3)
 2:00 ─ Mount + Explorer Verify                              (Segment 4)
 3:00 ─ Create File on RAID Volume                           (Segment 5)
 3:30 ─ Unmount                                             (Segment 6)
 4:00 ─ Restore from Saved Config                            (Segment 7)
 4:30 ─ CLI Alternative                                      (Segment 8)
 5:00 ─ Destroy                                             (Segment 9)
 5:30 ─ Developer Mode (bonus)                               (Segment 10)
 6:00 ─ Q&A                                                  (remaining time)
```

**Total: 5–6 minutes core demo + 4 minutes Q&A buffer = 10 minutes.**

---

## FALLBACK PLAN

| Failure Scenario | Workaround |
|-----------------|------------|
| No physical disks | Use pool files on existing NTFS drive. `init 0:1024 1:1024` creates file-backed pools. |
| WinFsp not installed | Show welcome dialog detection. All CLI workflows work without mount. Demonstrate CLI + test commands. |
| GUI fails to launch (no D3D11) | Use `--cli` mode for full demo. Run `quick` command. |
| Only 1 disk available | Show planner panel. Explain RAID0 requires 2+. Demonstrate with simulated pool files. |
| Mount fails (letter conflict) | Change mount letter in Settings or use different letter. |

---

## KEY SOURCE FILES REFERENCED

| File | Role in Demo |
|------|-------------|
| `src/main.c` | Entry point, mode selection |
| `src/gui.cpp` | Full GUI (1695 lines, Dear ImGui + DX11) |
| `src/cmd_handler.c` | CLI dispatch (31 commands) |
| `src/raid_service.c/.h` | Unified backend API (30 functions) |
| `src/stripe_engine.c/.h` | Asymmetric RAID0 LBA mapping |
| `src/mirror_engine.c/.h` | RAID1 mirror write/read/rebuild |
| `src/ram_cache.c/.h` | Write-back cache (64KB blocks, async flush) |
| `src/journal.c/.h` | Write-ahead log (crash recovery) |
| `src/fuse_bridge.c/.h` | WinFsp FUSE filesystem callbacks |
| `src/superblock.c/.h` | On-disk v4 metadata format |
| `src/volume_manager.c/.h` | Volume lifecycle orchestrator |
| `src/device_manager.c/.h` | Disk list abstraction |
| `src/config.c/.h` | JSON persistence |
| `src/planner_engine.c/.h` | Capacity calculator |
