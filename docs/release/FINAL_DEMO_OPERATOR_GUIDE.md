# RAIDV3 — Final Demo Operator Guide

**Purpose:** Complete operator manual for running the RAIDTEST (RAIDV3) capstone demonstration.
**Audience:** Student presenter with no source code knowledge.
**Last updated:** 2026-07-08

---

## Section 1 — Demo Day Preparation

### Hardware Checklist

| Item | Required | Notes |
|------|----------|-------|
| Windows PC | Yes | Windows 10 or 11, 64-bit |
| Administrator account | Yes | Required for disk scan (IOCTL) and mount (WinFsp FUSE) |
| Two physical disks | Recommended | USB flash drives work. Not system drives. |
| Pool file fallback | Alternative | If no disks: `init 0:1024 1:1024` creates file-backed pools on `C:\` |
| WinFsp installed | Yes | Download from https://github.com/billziss-gh/winfsp/releases |
| Display resolution | 1280x800 minimum | Maximize GUI window for best view |

### Software Verification

Run these commands **as Administrator** in the project root directory:

```powershell
# 1. Build verification
.\build.bat
# Expected: Compiles without errors (2 pre-existing warnings OK)

# 2. Test verification
.\raidtest_tests.exe
# Expected: 39/39 PASS
```

### File Checklist

Verify these files exist in the project root:

| File | Purpose |
|------|---------|
| `raidtest_winfsp.exe` | Main GUI/CLI executable |
| `winfsp-x64.dll` | WinFsp runtime DLL (in root) |
| `build.bat` | Build script |
| `raidtest_tests.exe` | Unit test runner |

### Pre-Demo Cleanup

```powershell
# If a volume from a previous run exists, destroy it:
# GUI: Click [Destroy] -> [Yes, Destroy]
# CLI: raidtest_winfsp.exe --cli -> destroy

# Clear stale pool files:
Remove-Item -LiteralPath "C:\RAIDTEST\stripe_pool.dat" -ErrorAction SilentlyContinue
```

---

## Section 2 — Startup Procedure

### Step 1: Open Administrator PowerShell

1. Press **Windows Key + X**
2. Select **Windows PowerShell (Admin)** or **Terminal (Admin)**
3. Click **Yes** on the UAC prompt

### Step 2: Navigate to Repository

```powershell
cd C:\Users\Yu\Desktop\raidv3
```

### Step 3: Verify Executable Files

```powershell
Test-Path -LiteralPath "raidtest_winfsp.exe"
Test-Path -LiteralPath "winfsp-x64.dll"
# Both should return True
```

### Step 4: Launch the Application

```powershell
.\raidtest_winfsp.exe
```

**Expected result:** Dark-themed GUI window appears, maximized, with:

- **Title bar:** "RAIDTEST v1.0 RC4 - GUI Edition"
- **Mode tabs:** Beginner | **Advanced** | Developer
- **Toolbar:** Scan, Create, Mirror, Mount, Unmount, Destroy, Bench buttons
- **Left panel:** Physical Disks table (empty: "0 disk(s)")
- **Right panel:** Volume Info ("No volume — Scan + Create first")
- **Bottom:** Event Log (empty), Status Bar ("Ready — 0 disk(s) detected")

### GUI Mode Explanation

| Mode | When to Use | Description |
|------|-------------|-------------|
| **Beginner** | Quick one-click setup | Simplified interface: Quick Setup, Restore, Unmount, Benchmark |
| **Advanced** | **Main demo mode** | Full control: scan, select, create/mirror, mount, unmount, destroy |
| **Developer** | Failure simulation, rebuild | Adds Simulation Controls panel, Performance Dashboard, CLI console |

---

## Section 3 — RAID1 Demonstration Flow

### Step 1: Scan Disks

| Aspect | Detail |
|--------|--------|
| **GUI action** | Click **[Scan]** in the toolbar |
| **CLI fallback** | `.\raidtest_winfsp.exe --cli` then type `scan` |
| **Expected result** | Status bar: yellow `[Scanning disks...]` → Event Log: `[INFO] Scan: OK (2 disk(s) found)` → Physical Disks table populates with Model, Serial, Type, Bus, Size, Speed |
| **Possible failure** | "Scan: FAILED" — not running as Administrator |
| **Recovery** | Close and re-launch as Administrator. If still no disks, use pool file fallback: CLI mode → `init 0:1024 1:1024` |

### Step 2: Create RAID1 Mirror

| Aspect | Detail |
|--------|--------|
| **GUI action** | Check 2 disks in the **Use** column → Click **[Mirror]** |
| **CLI fallback** | `init 0:1024 1:1024` → `mirror` |
| **Expected result** | Status bar: yellow `[Creating volume...]` → Event Log: `[INFO] Mirror: OK — Volume is ready for mount` → Volume Info: State `INITIALIZED`, RAID Level `RAID1`, Disks `2`, Capacity shows smallest disk, UUID shown |
| **Possible failure** | Mirror button disabled — fewer than 2 disks selected. Click [Rescan] or check Use column checkboxes |
| **Recovery** | In CLI: verify disks exist with `status`, then retry `init 0:1024 1:1024` → `mirror` |

### Step 3: Mount Filesystem

| Aspect | Detail |
|--------|--------|
| **GUI action** | Click **[Mount]** |
| **CLI fallback** | `mount G` |
| **Expected result** | Status bar: yellow `[Mounting volume...]` → Event Log: `[OK] Mount: OK — Volume mounted at G:` → Volume Info: State `MOUNTED` (green), Mounted `Yes`, Uptime counter starts → `G:\` appears in Windows Explorer |
| **Possible failure** | "Mount: FAILED" — WinFsp not installed, or drive letter G: in use |
| **Recovery** | Verify WinFsp installed at `C:\Program Files (x86)\WinFsp\bin\winfsp-x64.dll`. Change mount letter via Settings (GUI) or CLI `mount H` |

### Step 4: Write Demo File

| Aspect | Detail |
|--------|--------|
| **GUI action** | Open Windows Explorer → `G:\` → Right-click → New → Text Document → name `RAIDV3_DEMO.txt` → Open → Type `"RAIDV3 capstone demo successful!"` → Save → Close |
| **CLI fallback** | `echo RAIDV3 capstone demo successful! > G:\RAIDV3_DEMO.txt` |
| **Expected result** | File appears in `G:\` with content preserved. Volume Info Written counter increments |
| **Possible failure** | Cannot write to G:\ — volume may have been unmounted |
| **Recovery** | Check Volume Info state. If UNMOUNTED, click [Mount] again. If DEGRADED, continue — degraded mode still accepts writes |

### Step 5: Show Metadata

| Aspect | Detail |
|--------|--------|
| **GUI action** | Switch to **Developer** tab → Find **Metadata** panel or use CLI |
| **CLI fallback** | `metadata 0` (shows superblock for disk 0) |
| **Expected result** | Displays superblock v4 fields: Magic `0x52444953`, UUID, Generation, RAID level, CRC32 checksum, disk serial |
| **Possible failure** | Metadata command fails — volume not created or disk index wrong |
| **Recovery** | Use `status` to verify volume exists. Use `info` for volume-level details instead |

### Step 6: Simulate Disk Failure

| Aspect | Detail |
|--------|--------|
| **GUI action** | While in **Developer** tab → **Simulation Controls** panel → Enter disk index `0` → Click **[Simulate Fail]** |
| **CLI fallback** | `simulate 0 fail` |
| **Expected result** | Event Log: fault injection message. Volume Info: State changes to `DEGRADED` (yellow) |
| **Possible failure** | Simulation Controls not visible — must be in Developer mode with volume mounted |
| **Recovery** | Use CLI `simulate 0 fail` from any mode. Verify with `status` command |

### Step 7: Show DEGRADED State

| Aspect | Detail |
|--------|--------|
| **GUI action** | Switch to **Advanced** tab → Volume Info shows `DEGRADED` (yellow) |
| **CLI fallback** | `status` — shows live state dashboard |
| **Expected result** | DEGRADED badge in yellow. System still reads/writes from remaining healthy disk(s) |
| **Possible failure** | State shows MOUNTED instead of DEGRADED — simulation may not have triggered |
| **Recovery** | Click [Scan] or [Refresh] to force UI update, or verify with CLI `status` |

### Step 8: Rebuild

| Aspect | Detail |
|--------|--------|
| **GUI action** | Click **[Actions]** → **[Rebuild]** → Rebuild wizard appears → Enter failed disk index (0), replacement disk ID, pool size → Click **[Start Rebuild]** |
| **CLI fallback** | `rebuild 0 <replacement_id> [pool_mb]` |
| **Expected result** | Indeterminate progress `[Rebuilding...]` → Event Log: `[OK] Rebuild: OK — Rebuild complete` → State returns to `MOUNTED` (green) |
| **Possible failure** | No replacement disk configured |
| **Recovery** | If no physical replacement, create a pool file first in CLI: `init 0:1024` → then `rebuild 0 0`. Or prepare replacement before demo |

### Step 9: Verify Recovered Data

| Aspect | Detail |
|--------|--------|
| **GUI action** | Open Windows Explorer → `G:\RAIDV3_DEMO.txt` → Content should read `"RAIDV3 capstone demo successful!"` |
| **CLI fallback** | `type G:\RAIDV3_DEMO.txt` or `more G:\RAIDV3_DEMO.txt` |
| **Expected result** | File content preserved exactly as written in Step 4. Data survived failure + rebuild |
| **Possible failure** | File missing or corrupted — rebuild may have failed silently |
| **Recovery** | If file missing, check Event Log for rebuild errors. Re-run rebuild with correct parameters |

---

## Section 4 — Emergency Troubleshooting

| Problem | Cause | Solution |
|---------|-------|----------|
| **GUI does not start** | DirectX 11 not available (old GPU, remote desktop) | Use CLI: `.\raidtest_winfsp.exe --cli` |
| **"Failed to create DirectX 11 device"** | GPU/driver too old | Update GPU drivers, or use `--cli` mode |
| **Disk scan fails / 0 disks** | Not running as Administrator | Close and re-launch PowerShell as Administrator |
| **Scan: FAILED** | IOCTL access denied | Must run as Administrator for physical disk access |
| **WinFsp not installed** | Mount cannot register FUSE filesystem | Install WinFsp from https://github.com/billziss-gh/winfsp/releases, or skip mount and demonstrate CLI-only path |
| **Mount: FAILED** | Drive letter G: in use | Change mount letter in GUI Settings or CLI: `mount H` |
| **RAID creation fails** | Disk selection problem, or pool file path error | Use pool file fallback: CLI `init 0:1024 1:1024` → `mirror` |
| **Rebuild fails** | No replacement disk | Create pool file first: `init 0:1024` → `rebuild 0 0` |
| **GUI freezes or crashes** | Rare — likely D3D11 device lost | Restart application. If persistent, use `--cli` mode |
| **"Failed to create window"** | Another instance running | Close all `raidtest_winfsp.exe` processes from Task Manager |

---

## Section 5 — Professor Talking Points

Prepare short (30-60 second) answers:

| Question | Key Points |
|----------|------------|
| **"What problem does RAIDV3 solve?"** | Data growth → need to combine multiple disks for capacity + speed (RAID0) or redundancy (RAID1). Windows lacks a lightweight, open-source software RAID solution. |
| **"Why RAID1 for the demo?"** | RAID1 demonstrates fault tolerance: disk failure → degraded mode → rebuild → data survives. RAID0 has no fault tolerance — failure = total data loss. |
| **"Why not Windows Storage Spaces?"** | Storage Spaces is closed-source, complex (3-layer abstraction), and cannot be modified. RAIDV3 is ~6000 lines of open C/C++, every line reviewable. Educational value. |
| **"What is innovative?"** | **Asymmetric stripe algorithm** — allows mixing different-speed disks (NVMe + SATA SSD + HDD) by allocating I/O proportionally to measured speed. Traditional RAID0 requires identical disks. |
| **"How does asymmetric stripe work?"** | 1) Benchmark each disk's write speed. 2) Calculate GCD-based ratio (e.g., 2800:500 → 28:5). 3) Virtual LBA space divided into cycles, each cycle has phases — faster disks serve more phases. |
| **"How does crash recovery work?"** | Write-ahead journal (WAL): every write goes through BEGIN → DATA → COMMIT. CRC32 on every entry. On startup, `journal_recover_all()` scans for incomplete transactions and replays DATA entries. |
| **"Why WinFsp?"** | WinFsp provides a kernel-level FUSE driver without writing a Windows kernel driver (which requires WDK, digital signing, and risks BSOD). Userspace implementation with 13 FUSE callbacks. |
| **"Why C/C++?"** | C for zero-cost abstraction, direct Windows API access, no GC pauses. C++ only for GUI (Dear ImGui). Core engine is pure C — maximum portability and control over memory. |

---

## Section 6 — Things NOT To Do During Demo

| ❌ Do Not | Why |
|-----------|-----|
| Open source code files | Professor may ask about architecture, not implementation details. Opening `.c` files wastes time. |
| Modify config randomly | Changing `config.json` can break the demo flow. Leave defaults. |
| Run cleanup commands | `cleanup` releases resources and may destroy volume state mid-demo. |
| Test unknown disks | Plugging in random USB drives may cause unexpected behavior. Only use pre-tested disks. |
| Rebuild without failure state | Rebuilding a healthy volume demonstrates nothing. Must show DEGRADED → rebuild flow. |
| Open `ARCHIVE/` folder | Contains 34 old reports that will confuse the presentation narrative. |
| Click Destroy without confirmation dialog | Always narrate "Destroy requires explicit confirmation" and show the modal. |
| Skip steps to save time | Each step builds the narrative. Skipping breaks the logical flow. Use the 5-min condensed version instead. |
| Run stress tests during demo | Stress tests take minutes and produce no visual output. Run them before demo day. |
> Use `build.bat --help` instead of guessing flags | The build script has no --help flag. Just run `build.bat` without arguments. |

---

## Quick Reference Card

```
┌──────────────────────────────────────────────────────────────┐
│                 RAID1 DEMO — QUICK REFERENCE                  │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  1. SCAN      [Scan] or `scan`                              │
│  2. CREATE    [Mirror] or `init 0:1024 1:1024` → `mirror`   │
│  3. MOUNT     [Mount] or `mount G`                          │
│  4. WRITE     Explorer → create RAIDV3_DEMO.txt             │
│  5. METADATA  Developer tab or `metadata 0`                 │
│  6. FAILURE   Developer → Simulate or `simulate 0 fail`     │
│  7. DEGRADED  Auto-display or `status`                      │
│  8. REBUILD   Actions → Rebuild or `rebuild 0 0`            │
│  9. VERIFY    Open file in Explorer or `type G:\file.txt`   │
│                                                              │
│  CLI FALLBACK: .\raidtest_winfsp.exe --cli                   │
│  POOL FALLBACK: init 0:1024 1:1024                           │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```
