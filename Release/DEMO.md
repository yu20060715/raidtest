# RAIDTEST v1.0 RC1 — Demo Walkthrough

> **Target time:** 8–10 minutes  
> **Prerequisites:** Windows 10/11, WinFsp installed, Administrator privileges  

---

## Step 1: Launch

**Action:** Double-click `raidtest.exe` (or run `raidtest.exe --gui` from an Admin terminal)

**Expected screen:**  
- Dark-themed GUI window, maximized (~1280×800)
- Title bar: `RAIDTEST v1.0 RC1 — GUI Edition`
- Left panel: Physical Disks table (empty: "0 disk(s)")
- Right panel: Volume Info (shows "No volume — Scan + Create first")
- Bottom: Event Log (empty), Status Bar ("Ready — 0 disk(s) detected")

**If failure:**  
- "Failed to create DirectX 11 device": GPU/driver too old (try updating GPU drivers)
- "Failed to create window": another instance may be running
- Blank window / immediate exit: check Windows Event Viewer

---

## Step 2: Scan Disks

**Action:** Click **Scan** button (top toolbar)

**Expected screen:**  
- Status bar shows yellow `[Scanning disks...]` with progress bar
- Event Log: `[INFO] Scan: OK (2 disk(s) found)`
- Physical Disks table populates with detected drives:
  - **Model**, **ID**, **Serial**, **Type**, **Bus**, **Size**, **Speed**, **Status**
- Disk names appear (e.g., "NVMe SSD 1TB", "SATA SSD 512GB")
- Status changes to "Ready — 2 disk(s) detected"
- State indicator: `SCAN_DONE`

**If failure:**  
- "Scan: FAILED": run as Administrator (physical disk access denied)
- "0 disk(s) found": no physical disks detected (check disk connections)
- WinFsp error: WinFsp not installed (install from https://winfsp.dev/rel/)

---

## Step 3: Select Disks

**Action:** Check the **Use** checkbox for two disks (far-right column)

**Expected screen:**  
- Selected count updates: `"2 disk(s) | 2 selected | Total: X.X GB"`
- Planner panel (below disk table) activates:
  - Shows selected disk count
  - **RAID0 capacity**, **RAID1 capacity**, **RAID10 capacity**
  - Efficiency percentages for each level
- Checked disks show "Yes" in the **RAID** column

**If failure:**  
- Planner shows "Select 2+ disks": need to select at least 2 disks
- Planner shows wrong capacity: disks may have bad sectors (check Status column)
- Cannot check checkbox: run as Administrator

---

## Step 4: Create RAID0

**Action:**  
- Ensure Cache input shows `1024` (1 GB write-back cache)
- Click **Create**

**Expected screen:**  
- Status bar: yellow `[Creating volume...]` with progress bar
- Event Log: `[INFO] Create: OK — Volume is ready for mount`
- Right panel (Volume Info) populates:
  - State: `INITIALIZED`
  - RAID Level: `RAID0`
  - Disks: `2`
  - Capacity: shows combined capacity
  - Mounted: `No`
  - Cache: `ON (1024 MB)`
  - UUID: 36-character hex string
  - Generation: `1`

**If failure:**  
- "Create: FAILED": disk selection problem (try Rescan)
- Progress bar stuck: check Event Log for specific error
- App crash: check disk health (Status column should show "Online")

---

## Step 5: Mount Volume

**Action:**  
- Drv input shows `G` (default mount letter)
- Click **Mount**

**Expected screen:**  
- Status bar: yellow `[Mounting volume...]`
- Event Log: `[OK] Mount: OK — Volume mounted at G:`
- Volume Info updates:
  - State: `MOUNTED`
  - Mounted: `Yes` (green text)
  - Uptime: `00:00:0X` (live counter)
- Health: `X/X healthy` (green)
- **Mount** button becomes disabled; **Unmount** button becomes enabled

**If failure:**  
- "Mount: FAILED": WinFsp not installed or another process holds G:
- "Mount: FAILED": check if G: already in use (change mount letter)
- WinFsp permission error: run as Administrator

---

## Step 6: Explorer Verification

**Action:** Open Windows File Explorer → navigate to `G:\`

**Expected screen:**  
- `G:\` appears as a local disk in "This PC"
- Volume label: `RAIDTEST`
- Capacity shows the RAID0 combined size
- You can create folders, copy files, edit documents

**If failure:**  
- `G:\` not visible: refresh Explorer (F5) or wait 5 seconds
- "Access denied": volume may be mounted but not ready (check Event Log)
- Wrong capacity: check Volume Info panel vs Explorer properties

---

## Step 7: Create Test File

**Action:** In Explorer: right-click `G:\` → New → Text Document → name it `test.txt`
→ Open it, type "RAIDTEST demo!", save, close

**Expected screen:**  
- File appears in `G:\` with content preserved
- Volume Info panel updates:
  - Written: `~0 MB` (bytes written counter increments)
  - Read: `0 MB` (no read from our action)
- Can also try: copy a small photo/video to `G:\`, play it directly from the mounted volume

**If failure:**  
- File disappears / can't save: volume may be read-only (check Volume Info)
- Very slow write: cache flushing (normal for first write)
- Error "The file is too large": FAT32 limitation (use NTFS — WinFsp default)

---

## Step 8: Unmount

**Action:** Click **Unmount**

**Expected screen:**  
- Status bar: yellow `[Unmounting...]`
- Event Log: `[OK] Unmount: OK — Volume unmounted`
- Volume Info updates:
  - State: `UNMOUNTED`
  - Mounted: `No`
  - Uptime: shows last uptime before unmount
- `G:\` disappears from Explorer (may take 2-3 seconds)
- **Mount** button becomes enabled; **Unmount** becomes disabled

**If failure:**  
- "Unmount: FAILED": file handles open on G:\ (close all Explorer windows to G:\)
- "Unmount: FAILED — Volume in use": another process has open files
- Can't unmount: try closing all programs accessing G:\ first

---

## Step 9: Load (Restore from Metadata)

**Action:**  
- Click **Scan** (to re-detect disks)
- Click **Create** again (the `load` happens automatically on create when metadata exists)

> **What it demonstrates:** The volume is restored using serial-number matching.
> Even if the disks were disconnected and reconnected, the UUID and serial
> mapping in the superblock ensures the correct disks are assembled.

**Expected screen:**  
- Volume Info shows the same UUID as before
- State: `UNMOUNTED` (ready to mount again)
- Generation may increment (shows it was reloaded)

**If failure:**  
- Volume UUID differs: wrong disks selected (check selected disks match original)
- "Load: FAILED — Disk mismatch": serial numbers don't match (superblock on disk corrupted)
- No volume after load: metadata may have been destroyed (re-create)

---

## Step 10: Destroy (Cleanup)

**Action:** Click **Destroy**

**Expected screen:**  
- **Confirmation Dialog** appears (centered modal):
  - "Are you sure you want to DESTROY the volume?"
  - "This will delete ALL data on the volume."
  - "This action CANNOT be undone."
  - [Yes, Destroy] [Cancel]

**Action:** Click **Yes, Destroy**

**Expected screen:**  
- Status bar: yellow `[Destroying volume...]`
- Event Log: `[INFO] Destroy: OK — Volume destroyed`
- Volume Info resets: "No volume — Scan + Create first"
- Pool files deleted from disk
- State: `SCAN_DONE` (disks still detected, but no volume)

**Alternative:** Click **Cancel** → dialog closes, nothing happens (safety confirmed)

**If failure:**  
- "Destroy: FAILED": pool file locked by another process
- Pool files remain on disk: check `C:\RAIDTEST\` for orphaned files
- Destroy succeeded but data still accessible: WinFsp cache delay (run Scan)

---

## Quick Reference

```
┌─────────────┬──────────────────────┬──────────────────────────┐
│ Step        │ Button / Action      │ Key Indicator            │
├─────────────┼──────────────────────┼──────────────────────────┤
│ 1. Launch   │ Double-click exe     │ Dark GUI, empty tables   │
│ 2. Scan     │ [Scan]               │ Disks appear in list     │
│ 3. Select   │ Checkboxes (Use)     │ Planner shows capacities │
│ 4. Create   │ [Create]             │ Volume Info populated    │
│ 5. Mount    │ [Mount]              │ G:\ appears in Explorer  │
│ 6. Explorer │ Navigate to G:\      │ Files work normally      │
│ 7. File     │ Create test.txt      │ Write counter increases  │
│ 8. Unmount  │ [Unmount]            │ G:\ disappears           │
│ 9. Load     │ [Scan] + [Create]    │ Same UUID restored       │
│ 10. Destroy │ [Destroy] → Confirm  │ Volume Info clears       │
└─────────────┴──────────────────────┴──────────────────────────┘
```

## CLI Alternative

If GUI is unavailable, all steps work via CLI:

```
raidtest.exe scan
raidtest.exe init 0:1024 1:1024
raidtest.exe create
raidtest.exe mount G
rem ... use G:\ in Explorer ...
raidtest.exe unmount
raidtest.exe load
raidtest.exe destroy
```
