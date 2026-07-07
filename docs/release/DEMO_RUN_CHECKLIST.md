# DEMO DRY RUN CHECKLIST

**Project:** RAIDTEST v1.0 RC4  
**Target:** Capstone presentation dry run  
**Instructions:** Check off each item before and during the demo. Return to this checklist after each rehearsal.

---

## Pre-Demo Preparation

### Environment

- [ ] **WinFsp installed** — Verify `C:\Program Files (x86)\WinFsp\bin\winfsp-x64.dll` exists.
      If missing: download from https://github.com/billziss-gh/winfsp/releases
- [ ] **Administrator privileges** — Launch terminal or Explorer as Administrator.
      `raidtest_winfsp.exe` will fail to scan physical disks or mount without elevation.
- [ ] **Test disks ready** — Minimum 2 physical disks (or pool files on existing NTFS drive).
      Physical disks must not be system drives. USB flash drives work for demo.
- [ ] **RAID1 configuration confirmed** — Demo uses Mirror mode for fault-tolerance demonstration.
      Verify rebuild target disk is accessible.
- [ ] **No conflicting drive letter** — Ensure `G:\` is not in use. Run `mountvol G: /L` to check.
      If occupied, change mount letter in Settings (GUI) or use `--cli mount H`.
- [ ] **Pool file fallback prepared** — If no physical disks available, CLI command:
      `init 0:1024 1:1024` creates two 1 GB file-backed disks on C:.
- [ ] **CLI fallback confirmed** — `raidtest_winfsp.exe --cli` launches. Type `help` to verify.
- [ ] **Display resolution** — Minimum 1280×800 for GUI. Maximize window on smaller screens.

### Build

- [ ] **Latest build** — Run `build.bat` from project root. Wait for completion (~30 sec).
      Output: `raidtest_winfsp.exe` + `raidtest_tests.exe` + stress test binaries.
- [ ] **Build passes** — Verify exit code 0. Check for any NEW warnings (2 pre-existing OK).
- [ ] **39/39 tests pass** — Run `raidtest_tests.exe` as Administrator.
- [ ] **Stress tests pass** (optional but recommended) — Run at minimum:
      - `test_concurrent.exe`
      - `test_metadata_corrupt.exe`
      - `test_powerfail.exe`
- [ ] **Benchmark script works** — GUI [Benchmark] button or CLI `bench 256` runs without error.

### Data Cleanup

- [ ] **Previous volume destroyed** — If a volume from an earlier run exists:
      GUI: [Destroy] → [Yes, Destroy] OR CLI: `destroy`
- [ ] **Pool files removed** — Check C:\RAIDTEST\ for stale pool files. Delete if present.
- [ ] **Config reset** (optional) — Delete `%USERPROFILE%\\.config\\RAIDTEST\\config.json`
      to ensure clean first-run wizard appears.

### Presenter Prep

- [ ] **Script printed or on second monitor** — `docs/release/PRESENTATION_SCRIPT.md`
- [ ] **Familiarity with fallback paths** — Review CAPSTONE_DEMO_PLAN.md "Fallback Plan" table
- [ ] **Known bugs memorized** — Review Q&A section on limitations (Q8, Q12, Q15)
- [ ] **Timer ready** — Target 5–6 min demo + 4 min Q&A = 10 min total

---

## Demo Flow Checklist

### Step 1: Startup

- [ ] Double-click `raidtest_winfsp.exe` (or run from Admin terminal)
- [ ] Dark-themed GUI window appears, maximized
- [ ] Three mode tabs visible: Beginner / Advanced / Developer
- [ ] Status bar: "Ready — 0 disk(s) detected"
- [ ] Volume Info panel: "No volume — Scan + Create first"
- [ ] ❌ If GUI fails → `--cli` mode or `--cli quick`

### Step 2: Scan

- [ ] Click [Scan] (toolbar or Actions menu)
- [ ] Progress bar: yellow `[Scanning disks...]`
- [ ] Event Log: `[INFO] Scan: OK (X disk(s) found)`
- [ ] Physical Disks table populates with Model, Serial, Type, Bus, Size, Speed
- [ ] Checkboxes appear in "Use" column
- [ ] ❌ If no disks found → Run as Admin, or use pool file fallback

### Step 3: Create RAID1 Mirror

- [ ] Confirm volume mode selected is **RAID1** (Mirror)
- [ ] Check 2+ disks in "Use" column
- [ ] Click [Mirror] (not [Create])
- [ ] Progress bar: `[Creating volume...]`
- [ ] Event Log: `[INFO] Mirror: OK — Volume is ready for mount`
- [ ] Volume Info populated:
      - State: `INITIALIZED`
      - RAID Level: `RAID1`
      - Capacity: smallest disk GB (mirror)
      - UUID: 36-hex-char string
- [ ] ❌ If Mirror fails → Verify disk selection, try [Rescan], check pool paths

### Step 4: Mount

- [ ] Click [Mount]
- [ ] Progress bar: `[Mounting volume...]`
- [ ] Event Log: `[OK] Mount: OK — Volume mounted at G:`
- [ ] Volume Info: State → `MOUNTED` (green), Mounted → `Yes`
- [ ] Uptime counter starts ticking
- [ ] [Mount] button disabled, [Unmount] enabled
- [ ] Open Windows Explorer → `G:\` appears as local disk
- [ ] ❌ If Mount fails → Check WinFsp installation, try different drive letter

### Step 5: Write File

- [ ] In Explorer on G:\: Right-click → New → Text Document
- [ ] Name: `capstone.txt`
- [ ] Open → Type `"RAIDTEST capstone demo successful!"`
- [ ] Save → Close
- [ ] Verify: reopen file → content preserved
- [ ] Verify: Volume Info Written counter increments
- [ ] ❌ If Explorer fails → Use CLI: `--cli mount G` then `echo test > G:\test.txt`

### Step 6: Simulate Failure

- [ ] Switch to **Developer** tab (top tabs)
- [ ] Find **Simulation Controls** panel
- [ ] Enter disk index `0` in the input field
- [ ] Click [Simulate Fail]
- [ ] Event Log: fault injection message
- [ ] Switch back to **Advanced** tab
- [ ] Volume Info: State → `DEGRADED` (yellow)
- [ ] ❌ If Simulate not visible → Use CLI: `simulate 0 fail`

### Step 7: Show Degraded

- [ ] Narrate: "The system is now in degraded mode. RAID1 still serves reads/writes
      from the remaining healthy disk(s)."
- [ ] (Optional) Open file on G:\ to show it's still accessible
- [ ] (Optional) Create a new file to show write still works

### Step 8: Rebuild

- [ ] Click [Actions] → [Rebuild]
- [ ] Rebuild wizard dialog appears
- [ ] Select replacement disk (can be same physical disk with new pool file)
- [ ] Click [Start Rebuild]
- [ ] Progress: indeterminate `[Rebuilding...]`
- [ ] Event Log: `[OK] Rebuild: OK — Rebuild complete`
- [ ] Volume Info: State → `MOUNTED` (green)
- [ ] ❌ If Rebuild fails → Check replacement disk readiness, verify pool size
- [ ] ❌ If no replacement disk available → Demonstrate CLI Rebuild with new pool file:
      `init 0:1024` → `rebuild 0 0`

### Step 9: Verify Data

- [ ] Open `G:\capstone.txt` in Explorer
- [ ] Confirm content: `"RAIDTEST capstone demo successful!"`
- [ ] Verify: Event Log shows no errors
- [ ] Narrate: "Data survived simulated failure and rebuild intact."

### Step 10: Cleanup

- [ ] (Optional) Click [Unmount] → G:\ disappears
- [ ] Click [Destroy] → Confirmation dialog appears
- [ ] Click [Yes, Destroy]
- [ ] Progress: `[Destroying volume...]`
- [ ] Event Log: `[INFO] Destroy: OK — Volume destroyed`
- [ ] Volume Info resets to "No volume — Scan + Create first"
- [ ] ❌ If Destroy fails → Close all Explorer windows on G:\, retry

---

## Post-Demo Checklist

- [ ] Demo completed within time target (5–6 min)
- [ ] All 9 steps executed successfully
- [ ] Fallback plan was NOT needed (or was executed cleanly)
- [ ] Q&A segment: reviewed PROFESSOR_QA.md sections
- [ ] Notes for improvement written down

---

## Quick Reference: CLI Fallback Commands

| Step | GUI Path | CLI Fallback |
|------|---------|-------------|
| Scan | [Scan] | `scan` |
| Create (Mirror) | [Mirror] | `init 0:1024 1:1024` → `mirror` |
| Mount | [Mount] | `mount G` |
| Write file | Explorer | `echo test > G:\t.txt` |
| Simulate fail | Developer → Simulation | `simulate 0 fail` |
| Rebuild | Actions → Rebuild | `rebuild 0 0` |
| Unmount | [Unmount] | `unmount` |
| Destroy | [Destroy] | `destroy` |
