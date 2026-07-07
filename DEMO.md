# RAIDTEST v1.0 RC4 — Quick Demo Walkthrough

> **Target time:** 5–6 minutes
> **Prerequisites:** Windows 10/11, WinFsp installed, Administrator privileges
> **See also:** `docs/release/FINAL_DEMO_OPERATOR_GUIDE.md` (full operator manual)

---

## Step 1: Launch

**Action:** Double-click `raidtest_winfsp.exe` (or run from an Admin terminal)

**Expected screen:**
- Dark-themed GUI window, maximized
- Mode tabs: Beginner | **Advanced** | Developer (Advanced selected)
- Top toolbar: Scan, Create, Mirror, Mount, Unmount, Destroy, Bench buttons
- Left panel: Physical Disks table (empty)
- Right panel: Volume Info ("No volume — Scan + Create first")

**If failure:**
- "Failed to create DirectX 11 device": update GPU drivers or use `--cli` mode
- "Failed to create window": another instance may be running

---

## Step 2: Scan Disks

**Action:** Click **Scan**

**Expected screen:**
- Status bar: yellow `[Scanning disks...]`
- Event Log: `[INFO] Scan: OK (2 disk(s) found)`
- Physical Disks table populates with Model, Serial, Type, Bus, Size, Speed
- Checkboxes appear in "Use" column

**If failure:**
- "Scan: FAILED": run as Administrator

---

## Step 3: Create RAID1 Mirror

**Action:**
- Check 2 disks in the **Use** column
- Click **Mirror**

**Expected screen:**
- Event Log: `[INFO] Mirror: OK — Volume is ready for mount`
- Volume Info: State `INITIALIZED`, RAID Level `RAID1`, capacity = smallest disk

**If failure:**
- Mirror button disabled — select at least 2 disks

---

## Step 4: Mount

**Action:** Click **Mount**

**Expected screen:**
- Event Log: `[OK] Mount: OK — Volume mounted at G:`
- Volume Info: State `MOUNTED` (green), Uptime counter starts
- `G:\` appears in Windows Explorer

**If failure:**
- WinFsp not installed, or drive letter G: in use

---

## Step 5: Create Demo File

**Action:** In Explorer on `G:\`: New → Text Document → `RAIDV3_DEMO.txt`
→ Type "RAIDV3 capstone demo successful!" → Save

**Expected screen:**
- File appears on `G:\` with content preserved
- Volume Info: Written counter increments

---

## Step 6: Simulate Disk Failure

**Action:** Switch to **Developer** tab → **Simulation Controls** → Enter disk `0` → **Simulate Fail**

**Expected screen:**
- Event Log: fault injection message
- Volume Info: State changes to `DEGRADED` (yellow)

---

## Step 7: Show DEGRADED State

**Action:** Switch to **Advanced** tab → Volume Info shows `DEGRADED`

**Narrate:** "System is in degraded mode. RAID1 still serves reads/writes from remaining healthy disks."

---

## Step 8: Rebuild

**Action:** Click **Actions** → **Rebuild** → Enter failed disk index → **Start Rebuild**

**Expected screen:**
- Progress: `[Rebuilding...]`
- Event Log: `[OK] Rebuild: OK — Rebuild complete`
- Volume Info: State returns to `MOUNTED` (green)

---

## Step 9: Verify Recovered Data

**Action:** Open `G:\RAIDV3_DEMO.txt`

**Expected screen:**
- File content: "RAIDV3 capstone demo successful!"
- **Data survived failure + rebuild intact.**

---

## Quick Reference

```
 Step         | Button / Action              | Key Indicator
--------------+------------------------------+-----------------------------
 1. Launch    | Double-click exe             | Dark GUI, empty tables
 2. Scan      | [Scan]                       | Disks appear in list
 3. Create    | [Mirror]                     | Volume Info: RAID1
 4. Mount     | [Mount]                      | G:\ appears in Explorer
 5. File      | Create RAIDV3_DEMO.txt       | Write counter increases
 6. Failure   | Developer → Simulate Fail    | State → DEGRADED
 7. Degraded  | Advanced tab                | Yellow DEGRADED badge
 8. Rebuild   | Actions → Rebuild            | State → MOUNTED
 9. Verify    | Open file on G:\             | Content preserved
```

## CLI Alternative

All steps work via CLI:
```
raidtest_winfsp.exe --cli
scan
init 0:1024 1:1024
mirror
mount G
simulate 0 fail
rebuild 0 0
type G:\RAIDV3_DEMO.txt
```

## Beginner Mode Alternative

Switch to **Beginner** tab for simplified one-click operations:
1. **Quick Setup** — scan + create + cache + mount all-in-one
2. **Unmount / Benchmark** — when volume is mounted
