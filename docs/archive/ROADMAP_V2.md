# RAIDTEST v3 — Sprint 5+ Roadmap

## Current Phase: Validation (Sprint 4)

Complete the validation sprint, fix P1 bugs, then begin MVP GUI.

---

## Sprint 5: GUI MVP (Minimum Viable Product)

**Goal:** A usable GUI that covers the main workflow. Not beautiful, but functional.

### Scope
- [ ] Main window: disk list, volume info, action log
- [ ] Buttons: scan, select, create, mount, unmount, destroy
- [ ] Status bar: state, healthy count, throughput
- [ ] Real-time log window (pipe stdout/stderr from CLI engine)
- [ ] Drive letter selector (combo box)
- [ ] Pool size input (numeric up-down)

### Out of scope
- No fancy graphs
- No dark mode
- No system tray
- No settings dialog

### Rationale
The user's advice is correct — a working GUI elevates the project from "CLI prototype" to "demonstrable system." The engine is ready; wrapping it in a basic Win32 dialog is 3–5 days of work.

---

## Sprint 6: S.M.A.R.T. & Health Monitoring

- [ ] Read S.M.A.R.T. attributes via `IOCTL_ATA_PASS_THROUGH` / `IOCTL_STORAGE_QUERY_PROPERTY`
- [ ] Display: temperature, power-on hours, reallocated sectors, wear level
- [ ] Health score per disk (derived from S.M.A.R.T.)
- [ ] Alert threshold configuration
- [ ] `smart` CLI command + GUI health panel
- [ ] Background polling thread (every 60 s)

---

## Sprint 7: Benchmark Integration

- [ ] In-application benchmark (no external tools needed)
- [ ] Sequential read/write throughput
- [ ] Random 4 KB IOPS
- [ ] Latency histogram (min/avg/max/p99)
- [ ] Cache hit-rate impact analysis
- [ ] Benchmark result export (CSV)

### Why not CrystalDiskMark compatibility?
CDM expects a physical drive or a partition. WinFsp mount points are not block devices. Instead, build a purpose-built benchmark that exercises the engine directly (`stripe_volume_read/write`) bypassing WinFsp.

---

## Sprint 8: Storage Planner GUI

- [ ] Graphical disk list with capacity bar
- [ ] Drag-to-select disks for volume
- [ ] Live capacity estimate (RAID0 / RAID1 / RAID10)
- [ ] Visual stripe phase map
- [ ] "What-if" scenario (add/remove disks, see capacity change)

---

## Sprint 9: Advanced RAID Levels

- [ ] **RAID5** (single parity, rotating stripe)
  - XOR parity computation
  - Read-modify-write for small writes
  - Rebuild from parity
- [ ] **RAID6** (dual parity, Reed-Solomon or Cauchy)
  - P+Q parity
  - Survive 2 disk failures
- [ ] **Hot spare** (automatic rebuild when a disk is detected as failed)
  - Spare pool
  - Spare activation policy

---

## Sprint 10: Kernel Driver (Optional / Stretch)

- [ ] Windows KMDF driver for true block device
- [ ] Direct I/O without FUSE overhead
- [ ] CrystalDiskMark compatible
- [ ] BitLocker compatible
- [ ] Boot volume support

### Requirements
- Windows Driver Kit (WDK)
- Code signature (EV certificate)
- Significant development effort (3–6 months)

---

## Cross-Platform (Future)

- [ ] Linux via FUSE (libfuse3)
- [ ] macOS via NFS or FUSE for macOS
- [ ] Abstract platform layer (current: Win32 API → POSIX shim)
- [ ] Build system: CMake

### Estimated effort
- Linux port: ~2 months (rewrite disk_scanner, pool_io, fuse_bridge)
- macOS port: ~3 months (similar to Linux + different FUSE API)
- Cross-platform build: ~2 weeks (CMake)

---

## Summary Timeline

| Sprint | Focus | Duration | Deliverable |
|--------|-------|----------|-------------|
| 4 | Validation & bug fixes | 1 week | This document set + P1 fixes |
| 5 | GUI MVP | 1–2 weeks | Win32 dialog with main workflow |
| 6 | S.M.A.R.T. | 1 week | Health monitoring |
| 7 | Benchmark | 1 week | In-app performance tool |
| 8 | Storage Planner GUI | 1 week | Visual capacity planning |
| 9 | RAID5/6 + Hot Spare | 2–3 weeks | Advanced RAID levels |
| 10 | Kernel Driver (opt) | 3–6 months | True block device |

**Total to MVP GUI:** ~2–3 weeks from today.
**Total to feature-complete CLI:** Already achieved (Sprint 3).
