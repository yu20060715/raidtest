# USER FLOWS

---

## FLOW 1: FIRST-TIME USER

**Persona:** Alice — has two NVMe SSDs in her Windows PC. Wants a single fast virtual drive. Has never used RAID software before. Has no idea what "stripe unit" or "asymmetric" mean.

**Goal:** Combine two SSDs into one fast drive with minimal friction.

**Starting state:** Application freshly downloaded, WinFsp installed. No config file exists.

```
BEGINNER MODE
══════════════

┌─────────────────────────────────────────────────────────────────┐
│  "I just want a fast drive"                                    │
│                                                                 │
│  1. Launch → GUI opens → Mode defaults to BEGINNER             │
│     → "No disks found. Click Scan to discover drives."          │
│                                                                 │
│  2. [Click Scan]                                               │
│     → Disk list populates:                                     │
│       ☐ [0] NVMe SSD 512 GB  (3000 MB/s)                       │
│       ☐ [1] SATA SSD  256 GB  (550 MB/s)                       │
│     → Both disks are healthy, benches auto-run.                │
│     → Status: "2 disks found. Select disks and click Create."   │
│                                                                 │
│  3. [Check both disks]                                         │
│     → Selected count: 2                                        │
│     → Pool size spinner appears per disk:                      │
│       Disk 0: [ 256000 ] MB  ← of 512 GB                       │
│       Disk 1: [ 256000 ] MB  ← of 256 GB (auto-capped)         │
│     → Total virtual capacity: 512 GB (RAID0)                   │
│     → Mount: [ G: ▼ ]                                          │
│                                                                 │
│  4. [Click Quick Create]                                       │
│     → Progress: "Creating pool files..." 30%                   │
│     → Progress: "Creating stripe volume..." 60%                │
│     → Progress: "Enabling cache..." 80%                        │
│     → Progress: "Mounting at G:..." 100%                       │
│     → ✅ "Volume mounted at G:. Total capacity: 512 GB"        │
│                                                                 │
│  5. Open Windows Explorer → G: drive appears.                  │
│     Drag files to G: — they write at ~800 MB/s.                │
│                                                                 │
│  6. [📊 Benchmark Mounted Drive]                               │
│     → Progress bar runs (256 MB test).                         │
│     → Results: R: 850 MB/s  W: 420 MB/s  Lat: 0.8 ms          │
│     → "Benchmark complete."                                    │
│                                                                 │
│  7. Done with work → [🔓 Unmount]                              │
│     → "Volume unmounted. Pool files preserved."                │
│                                                                 │
│  8. Done forever → [🗑️ Destroy]                               │
│     → Modal: "Destroy ALL data on this volume?"                │
│     → [Yes, Destroy] → "Volume destroyed."                     │
│     → Disk list still shows physical disks. Ready for reuse.   │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

**Beginner mode hides:**
- Stripe phase internals
- Cache configuration (uses sensible default: 1024 MB write-back)
- RAID level choice (always RAID0)
- Journal/superblock details
- Per-disk benchmark
- Planner
- Config save/load

**What if something goes wrong?**

| Problem | Beginner Response |
|---|---|
| "No disks found" after Scan | "No physical disks detected. Check connections or run as Administrator." |
| Pool creation fails | Not enough disk space. Reduce pool size. |
| Mount fails | WinFsp not installed? Show download link. |
| Only 1 disk found | "RAID requires 2+ disks. Detected 1." |
| Destroy clicked by accident | Confirmation dialog with yellow warning. |

---

## FLOW 2: RETURNING USER

**Persona:** Bob — used Beginner mode last week to create a 2-disk RAID0 at G:. He shut down his PC. Today he wants to access the volume again.

**Goal:** Restore the existing volume and continue using it.

**Starting state:** Pool files, superblock, and config JSON exist from previous session.

```
BEGINNER MODE → ADVANCED MODE
══════════════════════════════

┌─────────────────────────────────────────────────────────────────┐
│  "I already have a RAID set up"                                 │
│                                                                 │
│  1. Launch → GUI opens → BEGINNER MODE                         │
│     → Background check: config JSON exists!                    │
│     → Banner: "📂 Existing configuration found (3 disks).     │
│        Switch to Advanced for restore options."                 │
│     → [Beginner] or [Advanced]                                 │
│                                                                 │
│  ── If Bob stays in Beginner ──                                │
│  2a. Sees empty disk list. Clicking Scan shows disks.           │
│  3a. "Quick Create" would ERASE old volume. (Warning shown.)    │
│  4a. ⚠️ No "Restore" button in Beginner.                        │
│                                                                 │
│  ── If Bob switches to Advanced (recommended) ──               │
│                                                                 │
│  2. Switch to ADVANCED mode.                                   │
│     → "Existing config detected. Choose recovery method:"       │
│                                                                 │
│     Option A: [🔄 Restore from Superblock] (recommended)       │
│     → Reads on-disk superblock from all disks.                 │
│     → Reconstructs volume with original stripe phases.         │
│     → "Volume restored from superblock. Generation: 42"        │
│                                                                 │
│     Option B: [💾 Load Saved Config]                           │
│     → Reads JSON config.                                       │
│     → Re-creates pools and volume from scratch.                │
│     → "Volume recreated from config."                          │
│                                                                 │
│  3. [Mount G:] — Volume appears at G: with all previous data.  │
│                                                                 │
│  4. In HEALTH panel:                                            │
│     Disk 0: ✅ Healthy  │  Superblock: ✅ Consistent           │
│     Disk 1: ✅ Healthy  │                                     │
│                                                                 │
│  5. [💾 Save Config] — updates JSON with current state.        │
│                                                                 │
│  6. Work as usual. Unmount when done.                          │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

**Key design decision:** Beginner mode does NOT offer restore. Restore requires understanding the difference between superblock and JSON config. This prevents beginners from accidentally picking the wrong method and losing data.

---

## FLOW 3: MIRROR RECOVERY

**Persona:** Carol — created a 2-disk RAID1 (mirror) in Advanced mode for data safety. One disk started clicking.

**Goal:** Identify the failed disk, replace it, and rebuild the mirror.

**Starting state:** RAID1 volume mounted. One disk has hardware failure.

```
ADVANCED MODE → DEVELOPER MODE
═══════════════════════════════

┌─────────────────────────────────────────────────────────────────┐
│  "My disk failed. I need to recover."                           │
│                                                                 │
│  ⚠️ Event bus fires EVENT_ERROR → GUI shows toast:             │
│     "🔴 I/O error on Disk 1. Volume may be degraded."          │
│                                                                 │
│  1. Switch to ADVANCED mode → HEALTH panel.                    │
│     → State: DEGRADED                                          │
│     → Disk 1 shows: 🔴 FAILED  (writes failed, I/O errors)     │
│     → Superblock: ✅ Consistent (on Disk 0 only)               │
│     → "1 of 2 disks healthy. Volume is DEGRADED."             │
│                                                                 │
│  2. [🩺 Run Full Check]                                        │
│     → Checks pool file accessibility.                          │
│     → Reports: Disk 1 pool file not accessible.                │
│     → "Disk 1 is physically failed. Replace disk to rebuild."  │
│                                                                 │
│  3. Physically replace the failed disk with a new one.          │
│     → [Scan] → Disk 1 now shows new healthy disk (unallocated).│
│                                                                 │
│  4. [🔄 Rebuild Mirror]                                        │
│     → Dialog: "Select replacement disk for Disk 1 position:"   │
│     → Dropdown: [New NVMe SSD 1TB]                             │
│     → Pool size: [51200] MB  (same as original)                │
│     → [Start Rebuild]                                          │
│     → Progress: "Rebuilding Disk 1 from Disk 0..."             │
│     → Progress bar fills as data copies.                       │
│     → ✅ "Mirror rebuild complete. Volume is HEALTHY."         │
│                                                                 │
│  5. HEALTH panel updates:                                       │
│     Disk 0: ✅ Healthy  │  Disk 1: ✅ Healthy (Rebuilt)        │
│     State: MOUNTED                                              │
│                                                                 │
│  ⚙️ Developer mode alternative:                                  │
│     Use [Simulate Disk Failure] to test recovery without        │
│     physically unplugging a drive.                              │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

**Advanced mode exposes:**
- Mirror RAID level choice
- Health panel with per-disk status
- Rebuild workflow
- Full health check

---

## FLOW 4: BENCHMARK WORKFLOW

**Persona:** Dave — wants to understand his storage performance. He has mixed-speed disks and wants to know the real-world throughput of his RAID array.

**Goal:** Benchmark at disk level, volume level, and filesystem level to identify bottlenecks.

**Starting state:** Four SATA SSDs (500 MB/s each) in a RAID0 array.

```
ALL THREE MODES
═══════════════

┌─────────────────────────────────────────────────────────────────┐
│  "How fast is my RAID?"                                          │
│                                                                 │
│  ── BEGINNER MODE ──                                           │
│                                                                 │
│  [📊 Benchmark Mounted Drive]                                  │
│  → Quick 256 MB raw file I/O test on G:\                       │
│  → Results: R: 950 MB/s  W: 480 MB/s  Lat: 1.2 ms             │
│  → This is a simple "does it work?" check.                     │
│  → Uses raw Windows file API (CreateFile/ReadFile/WriteFile).  │
│                                                                 │
│  ── ADVANCED MODE ──                                           │
│                                                                 │
│  [📊 Per-Disk Benchmark]                                       │
│  → Tests each physical disk individually before pool creation. │
│  → Disk 0: 512 MB/s W  │ Disk 1: 495 MB/s W                   │
│  → Disk 2: 510 MB/s W  │ Disk 3: 505 MB/s W                   │
│  → Average: 505 MB/s — Stripe should yield ~2 GB/s (4×500).   │
│                                                                 │
│  [📊 Run Filesystem Benchmark]  (CLI: `benchfs 1024 1024`)    │
│  → 1024 MB test with 1024 KB blocks.                           │
│  → Tests through FUSE layer + stripe engine.                   │
│  → Results: R: 1850 MB/s  W: 920 MB/s                         │
│  → Reveals FUSE overhead (~10% vs raw).                        │
│                                                                 │
│  ── DEVELOPER MODE ──                                          │
│                                                                 │
│  [🧪 I/O Stress Test]  (CLI: `test`)                          │
│  → Engine-level write/read/verify.                             │
│  → Tests stripe engine directly, bypassing FUSE.               │
│  → "I/O verification: 100% passed (1024 blocks)"               │
│                                                                 │
│  [🎲 Random I/O]  (CLI: `random 500 64`)                      │
│  → 500 random operations, up to 64 KB each.                    │
│  → Tests mixed read/write at random offsets.                   │
│  → "Random I/O: 500/500 passed."                               │
│                                                                 │
│  Profiler panel in DEVELOPER mode shows live rates:            │
│  Read: 420 MB/s @ 12K IOPS  │  Write: 210 MB/s @ 6K IOPS      │
│  Queue depth: 4.2  │  Avg latency: 0.8 ms                     │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

**Benchmark comparison table:**

| Feature | Beginner | Advanced | Developer |
|---|---|---|---|
| Method | Raw file I/O on mount point | `bench_volume()` via FUSE | `stripe_volume_write/read` direct |
| What it tests | Windows I/O stack + FUSE | Stripe engine + FUSE | Stripe engine only |
| Block size | 1 MB fixed | Configurable (4 KB – 8 MB) | Random sizes |
| Duration | 256 MB total | 64 MB – 4 GB | Configurable ops count |
| Output | R/W MB/s + latency | R/W MB/s + block-level | Pass/fail + full timing |

---

## FLOW 5: DEVELOPER DEBUGGING

**Persona:** Eve — software developer contributing to RAIDTEST. She needs to verify engine correctness, inspect internal state, and test edge cases.

**Goal:** Debug a suspected stripe phase mapping issue.

**Starting state:** Source code open in editor. Binary freshly built.

```
DEVELOPER MODE
══════════════

┌─────────────────────────────────────────────────────────────────┐
│  "I need to debug the stripe mapping."                         │
│                                                                 │
│  1. Launch with `--cli` flag or switch to DEVELOPER mode.      │
│                                                                 │
│  2. Set up test volume:                                         │
│     raidtest> scan                                              │
│     raidtest> select 0 2                                        │
│     raidtest> init 0:102400 2:102400                            │
│     raidtest> create                                            │
│     raidtest> mount G                                           │
│     → Volume mounted at G:                                     │
│     → State: MOUNTED                                           │
│                                                                 │
│  3. [🗺️ LBA Mapping Dump]  (CLI: `map`)                        │
│     → Stripe mapping table displayed:                          │
│       Phase 0: 0 MB - 102400 MB                                │
│         Disk 0: 0-51200 MB  (50%)  speed=3000 MB/s             │
│         Disk 2: 0-51200 MB  (50%)  speed=550 MB/s              │
│     → Eve notices Disk 0 (NVMe) has same allocation as Disk 2  │
│       (SATA). She suspects the asymmetry ratio is wrong.       │
│                                                                 │
│  4. [🧪 I/O Stress]  (CLI: `test`)                             │
│     → Verifies every byte written matches what's read back.    │
│     → Passes — the engine is correct, but performance is       │
│       suboptimal for the mixed-speed configuration.            │
│                                                                 │
│  5. [📋 Metadata Dump]  (CLI: `metadata C`)                   │
│     → Raw superblock dump:                                     │
│       Magic:    0x52444953  (valid)                            │
│       Version:  4                                              │
│       Generation: 1                                            │
│       UUID:     a1b2c3d4-e5f6-7890-abcd-ef1234567890          │
│       Feature flags: 0x01 (CACHE)                              │
│       Checksum: 0xAABBCCDD  (valid)                            │
│       Disk 0: C:\ | pool=102400 MB | SN=1234                  │
│       Disk 2: C:\ | pool=102400 MB | SN=5678                  │
│     → Useful for debugging save/load issues.                   │
│                                                                 │
│  6. [💥 Simulate Failure]  (CLI: `simulate 0 f`)              │
│     → "Simulated: disk 0 -> FAILED"                            │
│     → State changes: MOUNTED → DEGRADED                       │
│     → Now test degraded read:                                  │
│     → `test` — reads from Disk 2 only (mirror works).         │
│                                                                 │
│  7. [🖥️ CLI Console] — full command line in a GUI panel       │
│     → Embedded terminal within Developer tab.                  │
│     → Run any CLI command and see output in-panel.             │
│     → No need to restart in --cli mode.                        │
│                                                                 │
│  8. [🔍 Event Log]  (CLI: `events`)                            │
│     → Displays all events from current session:                │
│       [2026-07-06 14:30:01] DISK_FOUND: Disk 0, Disk 2         │
│       [2026-07-06 14:30:05] VOLUME_CREATED: RAID0, 2 disks     │
│       [2026-07-06 14:30:10] MOUNT: G:                         │
│       [2026-07-06 14:31:00] ERROR: simulate: disk failed       │
│     → Helps trace state transitions.                           │
│                                                                 │
│  9. [📦 Full Export] — bundles everything for bug report:      │
│     → metadata.txt                                             │
│     → event.log                                                │
│     → system.txt (OS, CPU, RAM)                                │
│     → Saved to C:\Users\Eve\AppData\Local\Temp\raidtest_export │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

**Developer mode exposes ALL internal state:**
- Superblock byte-level dump
- Stripe phase mapping
- Event bus history
- Fault injection controls
- Engine-level stress testing (bypasses FUSE)
- Disk simulation (fail/healthy/disconnect)
- Performance profiler with IOPS, latency, queue depth

---

## MODE COMPARISON SUMMARY

| Aspect | Beginner | Advanced | Developer |
|---|---|---|---|
| **Target user** | First-time, non-technical | Power user, IT admin | Engineer, contributor |
| **RAID levels** | RAID0 only | RAID0 + RAID1 | RAID0 + RAID1 |
| **Cache** | Auto (1024 MB write-back) | Full control (size/wt/off) | Full control |
| **Pool size** | Single spinner | Per-disk spinner + `init` | CLI `init id:mb` |
| **Mount** | One click | One click | CLI `mount` |
| **Unmount/Destroy** | One click + confirm | One click + confirm | CLI commands |
| **Restore** | ❌ Not available | ✅ Superblock + Config | ✅ Full CLI |
| **Mirror rebuild** | ❌ Not available | ✅ Guided rebuild | ✅ CLI `rebuild` |
| **Health check** | ✅ Basic (healthy/degraded) | ✅ Full check + superblock | ✅ + simulate |
| **Planner** | ❌ Not available | ✅ Capacity/RAID level | ✅ CLI `planner` |
| **Benchmark** | ✅ Raw file I/O | ✅ Filesystem bench | ✅ Engine stress test |
| **Diagnostics** | ❌ Not available | ✅ Export wizard | ✅ Full export + event log |
| **Mapping** | ❌ Not available | ❌ Not available | ✅ Stripe phase dump |
| **Metadata** | ❌ Not available | ❌ Not available | ✅ Superblock dump |
| **Simulation** | ❌ Not available | ❌ Not available | ✅ Fail/healthy/disconnect |
| **Service mgmt** | ❌ Not available | ❌ Not available | ✅ Install/uninstall |
| **CLI console** | ❌ Not available | ❌ Not available | ✅ Embedded terminal |
| **Fault tolerance** | None | Config save/load | Full simulation + recovery |
