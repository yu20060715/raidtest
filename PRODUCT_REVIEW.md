# PRODUCT REVIEW

**Product:** RAIDTEST v1.0 RC2 — Asymmetric Stripe RAID 0 Engine  
**Binary:** `raidtest_winfsp.exe` (2145 KB)  
**Build:** MinGW-w64 + WinFsp FUSE + Dear ImGui 1.92.8 + DirectX 11  
**Date:** 2026-07-06  
**Tester:** First-time end user (QA perspective)

---

## 1. PRODUCT COMPLETENESS SCORE

**Score: 62 / 100**

| Category | Score | Rationale |
|---|---|---|
| Core RAID 0 workflow | 85 | Fully works via CLI. GUI creates pools but uses misleading "Pool:" label. |
| Core RAID 1 (Mirror) | 60 | Works in CLI. GUI has button but no rebuild/failure UI. |
| Persistence / Restore | 50 | Two competing paths (config JSON vs superblock). Auto-restore re-creates volume rather than loading from superblock. |
| GUI Completeness | 40 | Covers ~60% of CLI commands. Missing: rebuild, expand, check, simulate, planner, cache, events, config save/load. |
| Testing | 90 | 38/38 unit tests pass. 5 integration/stress tests pass (longrun, random, concurrent, metadata corruption, power failure). |
| Documentation | 30 | Help text in garbled Chinese. No English help available. `--help` flag broken. |
| Polish/UX | 35 | Garbled console banner. Misleading GUI labels. No error recovery guidance. |

---

## 2. BEGINNER WORKFLOW SCORE

**Score: 65 / 100**

### Workflow A: Scan → Select → Pool → RAID → Mount → Bench → Unmount → Destroy

| Step | GUI | CLI | Implemented | Calls correct backend? | Working? | Notes |
|---|---|---|---|---|---|---|
| **Scan** | Yes (Scan button) | Yes (`scan`) | Yes | `raid_scan()` → `device_refresh()` → `disk_scan_all()` | OK | GUI: worker thread W_SCAN. Also benches all disks automatically. |
| **Select** | Partial (checkboxes) | Yes (`select <id> ...`) | Yes | `device_select()` / `raid_select()` | OK | GUI checkboxes bypass state machine (call `device_select` directly, not `raid_select`). |
| **Create Pool** | Partial (via Create button) | Yes (`init <id:mb>`) | Yes | `raid_init_pools()` | MISLABELED | GUI "Pool:" input label is misleading — controls pool size (not cache). Value passed as `disk_id:pool_mb`. No separate pool init step; combined with Create. |
| **Create RAID** | Yes (Create button) | Yes (`create`) | Yes | `raid_create()` → `volume_create()` | OK | GUI `W_CREATE` calls `raid_init_pools` then `raid_create`. Does NOT enable cache or mount. |
| **Mount** | Yes (Mount button) | Yes (`mount <letter>`) | Yes | `raid_mount()` → `volume_mount()` → `fuse_mount_volume()` | OK | Requires WinFsp installed. |
| **Benchmark** | Yes (Bench button) | Yes (`bench`, `benchfs`) | Yes | CLI: `raid_bench()` / `raid_benchfs()`. GUI: raw `CreateFile` I/O | PARTIAL | GUI Bench does RAW FILE I/O, not through the stripe engine. Tests the mounted drive via `CreateFile` on `G:\raidtest_bench.tmp`. CLI `bench` tests individual disks; `benchfs` tests the volume. |
| **Unmount** | Yes (Unmount button) | Yes (`unmount`) | Yes | `raid_unmount()` → `volume_unmount()` | OK | |
| **Destroy** | Yes (Destroy button + confirm dialog) | Yes (`destroy`) | Yes | `raid_destroy()` → `volume_destroy()` | OK | GUI shows confirmation dialog (modal). |

### Beginner Issues

1. **GUI "Pool:" input is mislabeled.** It says "Pool:" but the value is used as pool_size_mb (passed as `disk_id:pool_mb`). There is NO separate cache configuration in the GUI Create flow — cache is not enabled after GUI Create. The user must use CLI to enable cache.

2. **GUI Create does not mount.** After clicking Create, the volume is created (state = MOUNTED) but NOT mounted to a drive letter. User must separately click Mount. This is inconsistent with CLI `quick` which does both.

3. **No "Quick Setup" button in GUI.** The CLI `quick` command is a one-shot scan+select+init+create+cache+mount. The GUI has no equivalent — each step is manual.

4. **GUI "Pool:" defaults to 1024.** This means each disk gets only 1 GB pool by default. A beginner clicking "Create" with the default 1024 would get a tiny volume and not understand why.

5. **`--help` flag broken.** Running `raidtest_winfsp.exe --help` gives "Unknown command: --help (try 'help')". User must run the binary and wait for the GUI, or use CLI mode via `--cli help`.

6. **Garbled banner.** Console output shows garbage characters from Chinese text:
   ```
   Non-撖寧妍敹急瘛瑟 RAID 0
   ```

7. **No wizard in GUI.** The CLI `wizard` command is a guided 8-step setup. The GUI has no equivalent — just individual buttons.

---

## 3. ADVANCED WORKFLOW SCORE

**Score: 45 / 100**

### Workflow B: Create → Exit → Restart → Restore → Mount

| Step | GUI | CLI | Working? | Notes |
|---|---|---|---|---|
| **Create** | Yes | Yes | OK | |
| **Exit** | Yes (Alt+F4 / File→Exit) | Yes (`exit`) | OK | |
| **Restart** | Run `raidtest_winfsp.exe` with no args | Same | OK | Starts GUI by default. |
| **Restore** | NO | Yes (`--auto` / `load`) | PARTIAL | **Two competing restore paths:** (1) `do_restore()` in main.c re-creates volume from saved JSON config (disk IDs, pool sizes). (2) `raid_load()` reads superblock from disk. The GUI has NO restore mechanism — it starts fresh every time. |
| **Mount** | Yes | Yes | OK | After restore. |

### Advanced Issues

1. **Dual restore paths are inconsistent.** `do_restore()` re-creates pools and volume from JSON config. `raid_load()` reads the on-disk superblock. They produce different results if the JSON is out of sync with the superblock.

2. **No restore in GUI.** After restarting the GUI, there's no "Load" or "Restore" button. The user must know to use CLI commands.

3. **JSON config is only saved by `raid_config_save()` / `config-save` command.** The auto-restore in main.c reads the config, but the config is only auto-saved by `wizard_run()` and `raid_quick()`. If the user created manually via CLI, the config might not be saved.

### Workflow C: Mirror → Disk Failure → Recovery → Rebuild

| Step | GUI | CLI | Working? | Notes |
|---|---|---|---|---|
| **Create Mirror** | Yes (Mirror button) | Yes (`mirror`) | OK | |
| **Simulate Failure** | NO | Yes (`simulate <idx> f`) | OK | CLI only. Three modes: fail, healthy, disconnect. |
| **Check Health** | NO | Yes (`check`) | OK | CLI only. Checks disk health + superblock. |
| **Rebuild** | NO | Yes (`rebuild <idx> <disk_id> <pool_mb>`) | OK | CLI only. Manual rebuild onto new disk. |
| **Automatic Recovery** | NO | NO | MISSING | No automatic failure detection. No hot spare concept. |

### Mirror/Rebuild Issues

1. **No failure detection in GUI.** The health status in the Volume Info panel shows healthy/degraded counts, but there's no way to trigger simulate/rebuild from GUI.

2. **Rebuild requires manual pool creation first.** The user must `init` the replacement disk before `rebuild`. This is not mentioned in help.

3. **No automatic monitoring.** No periodic health check, no SMART monitoring, no email/notification on failure.

---

## 4. DEVELOPER WORKFLOW SCORE

**Score: 70 / 100**

### Workflow D: GUI → Buttons → Worker Thread → Backend → Result

| GUI Button | Worker Action | Backend Call | Working? | Notes |
|---|---|---|---|---|
| Scan | W_SCAN | `raid_scan()` | OK | |
| Create | W_CREATE | `raid_init_pools()` + `raid_create()` | OK | Pool:ID format used. Cache NOT enabled. |
| Mirror | W_MIRROR | `raid_mirror()` | OK | |
| Mount | W_MOUNT | `raid_mount(char)` | OK | |
| Unmount | W_UNMOUNT | `raid_unmount()` | OK | |
| Destroy | W_DESTROY | `raid_destroy()` | OK | Modal confirmation dialog. |
| Bench | W_BENCH | Raw `CreateFile`/`WriteFile`/`ReadFile` | PARTIAL | NOT through `raid_benchfs()`. Raw Windows file I/O on the mounted drive. |
| Export | W_EXPORT | Diagnostics dump (metadata, event log, system info) | OK | |
| Refresh | W_REFRESH | `refresh_ui_model()` | OK | |
| Purge | NO (hidden behind confirm) | W_PURGE → `raid_purge()` | OK | Only accessible if state is INITIALIZED or UNMOUNTED. |

### Developer Issues

1. **GUI Bench bypasses stripe engine.** Unlike CLI `benchfs` which calls `bench_volume()` → `stripe_volume_write/read`, the GUI Bench does `CreateFile` on `G:\raidtest_bench.tmp` with `FILE_FLAG_NO_BUFFERING`. This tests the Windows FUSE mount point, not the stripe engine directly. Results may differ from CLI benchfs.

2. **State machine bypass in GUI.** GUI checkboxes call `device_select()` directly instead of `raid_select()`. This means the state machine (`require(STATE_DISCOVERED)`) is bypassed. You can select disks before scanning.

3. **No pool size feedback in GUI.** The GUI shows "Total: X GB" but doesn't let the user configure pool sizes per disk. All disks get the same pool size from the "Pool:" field.

4. **GUI does not expose developer commands.** The following CLI commands have NO GUI equivalent:
   - `load` / `--auto` — restore from superblock
   - `expand` — expand volume
   - `rebuild` — rebuild mirror
   - `simulate` — simulate disk failure
   - `test` — test I/O
   - `random` — random I/O test
   - `check` — health check
   - `map` — dump LBA mapping
   - `status` — live status
   - `events` — event log
   - `metadata` — dump superblock
   - `planner` — RAID planner
   - `config-save` / `config-load`
   - `purge`
   - `wizard`

---

## 5. MISSING FEATURES

| Feature | Impact | Notes |
|---|---|---|
| **Auto-restore in GUI** | High | After restart, GUI starts fresh. Must use CLI to restore. |
| **Cache setup in GUI Create flow** | Medium | GUI Create does not enable cache. Must use CLI `cache` or cache is off. |
| **GUI Quick Setup** | Medium | No one-click beginner flow in GUI. |
| **Automatic mirror failure detection** | Medium | Simulate is CLI-only. No SMART or periodic health check. |
| **Hot spare / automatic rebuild** | Medium | Rebuild is manual CLI only. |
| **Per-disk pool size in GUI** | Low | All disks get same pool size from "Pool:" field. |
| **English help/documentation** | Low | Help text is garbled Chinese. |
| **Proper `--help` flag** | Low | Broken; returns "Unknown command" error. |
| **CLI/GUI unified state** | Medium | GUI bypasses state machine for disk selection. |

---

## 6. UNREACHABLE FEATURES

| Feature | Where | Why unreachable |
|---|---|---|
| `cli_bench.exe` | Separate executable | Referenced in build.bat but `benchmark/` directory doesn't exist. Never built. |
| `expand` | CLI only, no GUI | Requires STATE_MOUNTED. Valid command but no GUI path. |
| `rebuild` | CLI only, no GUI | Requires STATE_MOUNTED or STATE_DEGRADED. No GUI button. |
| `simulate` | CLI only, no GUI | Requires STATE_MOUNTED. No GUI button. |
| `purge` | CLI only, no GUI | Requires INITIALIZED or UNMOUNTED. Not in GUI (even though W_PURGE exists in worker enum). |
| `load` | CLI only, no GUI | Requires UNMOUNTED or DISCOVERED. No GUI button. |
| `planner` / `events` / `metadata` / `map` / `status` / `check` / `test` / `random` | CLI only, no GUI | All require specific states. No GUI buttons. |

---

## 7. AI HALLUCINATION FEATURES

| Feature | Evidence | Status |
|---|---|---|
| `benchmark/cli_bench.c` | Referenced in build.bat line 75, but directory `benchmark/` does not exist | **HALLUCINATION** — build.bat references a file that doesn't exist. |
| `raid_expand()` | Function exists in `raid_service.c:314`. Calls `volume_expand()` which exists in `volume_manager.c` but testing reveals issues | Exists but **untested** — no test covers expand. Likely broken or incomplete. |
| `event_bus_init()` | Called in `raid_init()`. Events are published at various points but subscribers only exist in `gui.cpp` and `raid_service.c` (log callback). | **Partially real** — event_bus works but events have no visible effect unless GUI or event log is active. |

---

## 8. SUGGESTED BEGINNER MODE

| Feature | Action |
|---|---|
| **One-click "Quick Setup"** | Add GUI button that does scan → auto-select all healthy disks → prompt for pool size → init → create → enable cache → mount. |
| **Fix "Pool:" label → "Pool Size (MB)"** | Clearly label the input field and show available disk space. |
| **Wizard in GUI** | Add a step-by-step wizard panel (like CLI `wizard`) guiding through disk selection → pool size → cache → mount. |
| **Fix garbled text** | Remove Chinese banner text or add proper Unicode support. |
| **`--help` should work** | Add `--help` handling to print usage. |
| **English help** | Translate help text to English. |

---

## 9. SUGGESTED ADVANCED MODE

| Feature | Action |
|---|---|
| **Load/Restore button in GUI** | After startup, check for existing config or superblock and offer to restore. |
| **Cache configuration in GUI** | Add cache size input to Create flow. Show cache status (on/off, hit rate, dirty ratio). |
| **Health monitoring panel** | Show live disk health, IOPS, throughput. |
| **Simulate failure button** | GUI dropdown to mark a disk as failed for testing. |
| **Rebuild button** | GUI-guided rebuild process. |
| **Expand button** | GUI to expand volume with new disks. |

---

## 10. SUGGESTED DEVELOPER MODE

| Feature | Action |
|---|---|
| **CLI panel in GUI** | Embedded terminal window for CLI commands. |
| **All CLI commands in menu** | Add menu items for: `load`, `expand`, `rebuild`, `simulate`, `check`, `map`, `planner`, `events`, `metadata`, `purge`, `config-save`, `config-load`. |
| **State machine viewer** | Show current state (DISCOVERED, INITIALIZED, MOUNTED, UNMOUNTED, DEGRADED) and valid transitions. |
| **LBA mapping viewer** | Visual display of stripe phases and disk mappings. |
| **Event log browser** | Browse/filter event history from within GUI. |
| **Benchmark comparison** | Compare `raid_benchfs()` results with raw file I/O to detect FUSE overhead. |
| **Volume integrity test** | Button to run `stripe_volume_verify_io` and display results. |

---

## SUMMARY OF CRITICAL ISSUES

1. **MISLABELED GUI INPUT:** "Pool:" field actually controls both pool size (via Create) and cache size (via separate cache variable). This is confusing and could produce unexpected pool sizes.

2. **GUI Create does not enable cache.** After clicking Create, volume works without cache acceleration.

3. **No GUI restore path.** After restarting the application, the GUI starts fresh without offering to load previous config or superblock.

4. **Dual restore paths (config JSON vs superblock).** `do_restore()` re-creates from JSON; `load` reads from superblock. They may disagree.

5. **`--help` flag broken.** User cannot discover CLI commands from command line.

6. **Garbled Chinese banner.** Console output is unreadable.

7. **`benchmark/cli_bench.c` does not exist.** Build.bat references a non-existent file (build fails if it reaches that step).

8. **No mirror rebuild in GUI.** Mirror is unusable for serious use without GUI rebuild support.

9. **GUI Bench does not test the stripe engine.** It tests raw Windows file I/O on the mounted drive. Results from CLI `benchfs` and GUI Bench will differ.

10. **38/38 unit tests pass. 5/5 integration tests pass.** The ENGINE is solid. The UI/UX layer is where the problems are.
