# RAIDTEST v1.0 RC4 — Demo Walkthrough

> **Target time:** 8–10 minutes
> **Prerequisites:** Windows 10/11, WinFsp installed, Administrator privileges

---

## Step 1: Launch

**Action:** Double-click `raidtest.exe` (or run from an Admin terminal)

**Expected screen:**
- Dark-themed GUI window, maximized
- Mode tabs: Beginner | **Advanced** | Developer (Advanced selected)
- Top toolbar: Scan, Create, Mirror, Mount, Unmount, Destroy, Bench buttons
- Left panel: Physical Disks table (empty: "0 disk(s)")
- Right panel: Volume Info (shows "No volume — Scan + Create first")
- Bottom: Event Log (empty), Status Bar ("Ready — 0 disk(s) detected")

**If failure:**
- "Failed to create DirectX 11 device": GPU/driver too old (try updating GPU drivers)
- "Failed to create window": another instance may be running

---

## Step 2: Scan Disks

**Action:** Click **Scan** button (top toolbar)

**Expected screen:**
- Status bar shows yellow `[Scanning disks...]` with progress bar
- Event Log: `[INFO] Scan: OK (2 disk(s) found)`
- Physical Disks table populates with detected drives:
  - Model, ID, Serial, Type, Bus, Size, Speed, Status
- Planner panel (below disk table) activates
- Status changes to "Ready — 2 disk(s) detected"

**If failure:**
- "Scan: FAILED": run as Administrator (physical disk access denied)

---

## Step 3: Create RAID0 Volume

**Action:**
- Ensure at least 2 disks are checked in the **Use** column (far right)
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

**If failure:**
- "Create: FAILED": disk selection problem (try Rescan)

---

## Step 4: Mount Volume

**Action:** Click **Mount**

**Expected screen:**
- Status bar: yellow `[Mounting volume...]`
- Event Log: `[OK] Mount: OK — Volume mounted at G:`
- Volume Info updates:
  - State: `MOUNTED`
  - Mounted: `Yes` (green text)
  - Uptime: `00:00:0X` (live counter)
- **Mount** button becomes disabled; **Unmount** button enabled

**If failure:**
- "Mount: FAILED": WinFsp not installed or another process holds G:

---

## Step 5: Explorer Verification

**Action:** Open Windows File Explorer → navigate to `G:\`

**Expected screen:**
- `G:\` appears as a local disk in "This PC"
- Volume label: `RAIDTEST`
- Capacity shows the RAID0 combined size
- You can create folders, copy files, edit documents

---

## Step 6: Create Test File

**Action:** In Explorer: right-click `G:\` → New → Text Document → name it `test.txt`
→ Open it, type "RAIDTEST demo!", save, close

**Expected screen:**
- File appears in `G:\` with content preserved
- Volume Info panel: Written counter increments

---

## Step 7: Unmount

**Action:** Click **Unmount**

**Expected screen:**
- Status bar: yellow `[Unmounting...]`
- Event Log: `[OK] Unmount: OK — Volume unmounted`
- Volume Info: State → `UNMOUNTED`, Mounted → `No`
- `G:\` disappears from Explorer
- **Mount** button becomes enabled; **Unmount** becomes disabled

**If failure:**
- "Unmount: FAILED": file handles open on G:\ (close all Explorer windows)

---

## Step 8: Restore (Load from Saved Config)

**Action:**
- Click **Scan** (to re-detect disks)
- From the **Actions** menu (top-left), select **Restore**
- In the dialog, choose **From Saved Config**

**Expected screen:**
- Volume Info shows the same UUID and capacity as before
- State: `UNMOUNTED` (ready to mount again)

**What it demonstrates:** The volume configuration was saved to disk.
Even after unmount, the UUID and disk mapping are preserved and restored.

---

## Step 9: Re-mount & Verify

**Action:** Click **Mount**

**Expected screen:**
- Volume mounts at `G:` again
- Open `G:\test.txt` in Explorer — the file content is preserved from Step 6

---

## Step 10: Destroy (Cleanup)

**Action:** Click **Destroy**

**Expected screen:**
- **Confirmation Dialog** appears (centered modal):
  - "Are you sure you want to DESTROY the volume?"
  - [Yes, Destroy] [Cancel]

**Action:** Click **Yes, Destroy**

**Expected screen:**
- Status bar: yellow `[Destroying volume...]`
- Event Log: `[INFO] Destroy: OK — Volume destroyed`
- Volume Info resets: "No volume — Scan + Create first"
- Pool files deleted from disk

---

## Quick Reference

```
 Step         | Button / Action        | Key Indicator
--------------+------------------------+-----------------------------
 1. Launch    | Double-click exe       | Dark GUI, empty tables
 2. Scan      | [Scan]                 | Disks appear in list
 3. Create    | [Create]               | Volume Info populated
 4. Mount     | [Mount]                | G:\ appears in Explorer
 5. Explorer  | Navigate to G:\        | Files work normally
 6. File      | Create test.txt        | Write counter increases
 7. Unmount   | [Unmount]              | G:\ disappears
 8. Restore   | Actions → Restore      | Same UUID restored
 9. Re-mount  | [Mount]                | File content preserved
 10. Destroy  | [Destroy] → Confirm    | Volume Info clears
```

## Beginner Mode Alternative

Switch to **Beginner** mode (top tabs) for simplified one-click operations:
1. **Quick Setup** — scan + create + cache + mount all-in-one
2. **Restore Volume** — restore from saved config
3. **Unmount / Benchmark** — available when volume is mounted

## CLI Alternative

All steps work via CLI:
```
raidtest.exe --cli
scan
init 0:1024 1:1024
create
mount G
unmount
destroy
```
