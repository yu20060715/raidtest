# RAIDTEST v3 — Test Plan

## Scope

End-to-end integration tests covering the full lifecycle: scan → init → create → mount → write → unmount → load → verify. Plus edge cases for serial matching, metadata corruption, and disk failure.

---

## TC1: Normal Lifecycle (Happy Path)

**Objective:** Verify a first-time user can set up, use, and reload a volume.

| Step | Action | Expected |
|------|--------|----------|
| 1 | `scan` | Physical disks listed with model, serial, size |
| 2 | `init 0:51200 1:51200` | Pool files created at 50 GB each |
| 3 | `create` | UUID generated, v4 superblock written to all disks, state MOUNTED |
| 4 | `mount Z` | Volume mounted at Z:\ via WinFsp |
| 5 | Copy a 1 GB file to Z:\ | File written without error |
| 6 | Read file from Z:\ back | Content matches original |
| 7 | `unmount` | Volume unmounted, state UNMOUNTED, pool files preserved |
| 8 | `load` | Superblock scanned, serial matching restores disks, state MOUNTED |
| 9 | Read file from Z:\ | File still accessible (must `mount` first) |

**Verification:** `info` shows correct UUID, generation, and disk count after load.
**Automated test coverage:** `test_sb_stripe_write_read`, `test_sb_v4_uuid`

---

## TC2: Drive Letter Change

**Objective:** Verify serial-based restore survives drive letter reassignment.

| Step | Action | Expected |
|------|--------|----------|
| 1 | Create volume on disks C:\, D:\ | Superblock written |
| 2 | Swap drive letters so C:→E:, D:→F: | — |
| 3 | `load` | Serial matching finds disks by serial, opens pools at new letters E:\, F:\ |
| 4 | `check` | Both disks healthy, superblock present on both |

**Automated test coverage:** `test_sb_serial_restore` (mocks different drive letters with same serial)

---

## TC3: SATA Port Change

**Objective:** Verify restore works when disks are moved to different SATA ports (same Windows discovery order may change).

| Step | Action | Expected |
|------|--------|----------|
| 1 | Create volume with disks ID 0, 1 | — |
| 2 | Reboot, disks enumerate in different order | Disk 0 serial appears as physical disk 3, etc. |
| 3 | `scan` + `load` | Serial match maps each SB disk entry to correct physical disk regardless of order |

**Automated test coverage:** `test_sb_serial_restore` (physical array order is independent of SB disk order)

---

## TC4: Reboot Persistence

**Objective:** Verify volume survives system restart.

| Step | Action | Expected |
|------|--------|----------|
| 1 | Create volume, write test file | — |
| 2 | Reboot system | — |
| 3 | `scan` | Disks discovered (letters may differ) |
| 4 | `load` | Superblock found (generation matches pre-reboot) |
| 5 | `check` | All disks healthy, data intact |

**Limitation:** No auto-load on boot. User must manually `scan` + `load`.
**Automated test coverage:** Not possible in unit test framework (requires real reboot).

---

## TC5: Metadata Corruption

**Objective:** Verify corrupted superblock is rejected and data from intact copy is used.

| Step | Action | Expected |
|------|--------|----------|
| 1 | Create volume with 2 disks | Both carry superblock copies |
| 2 | Corrupt `C:\RAIDTEST\superblock.dat` magic/checksum | — |
| 3 | `load` | `try_read_superblock_from_drive(C:\)` fails (CRC mismatch). `superblock_read` scans D:\, finds intact copy. Load succeeds. |
| 4 | `check` | Superblock only on D:\, C:\ copy missing — warning shown |

**Automated test coverage:** `test_sb_checksum_corruption` (corrupts magic, verifies read fails)
**Note:** Current test only verifies read failure. Cross-disk fallback is not tested automatically.

---

## TC6: Serial Mismatch

**Objective:** Verify a disk whose serial does not match the SB entry falls back gracefully.

| Step | Action | Expected |
|------|--------|----------|
| 1 | Create volume, note serials | — |
| 2 | Replace one disk with a different one (different serial, same drive letter) | — |
| 3 | `load` | Serial match fails → uses SB `drive_letter` → opens pool (if pool file exists at that letter) |
| 4 | `check` | Disk pool accessible but serial mismatch logged |

**Automated test coverage:** `test_sb_serial_fallback`, `test_sb_blank_serial`

---

## TC7: Missing Disk (Degraded Mode)

**Objective:** Verify mirror volume continues to operate with one disk missing.

| Step | Action | Expected |
|------|--------|----------|
| 1 | Create mirror volume with 2 disks | — |
| 2 | Remove one disk (pull cable, or `simulate 0 f`) | State transitions to DEGRADED |
| 3 | Read file from volume | Mirror reads from healthy disk |
| 4 | Write file to volume | Mirror writes to healthy disk only (second write fails silently) |
| 5 | `check` | Shows 1/2 healthy, pool file missing on failed disk |

**Automated test coverage:** `test_mirror_degraded_read`, `test_mirror_all_dead_read_fails`

---

## TC8: Destroy Cleans Everything

**Objective:** Verify `destroy` removes all artifacts.

| Step | Action | Expected |
|------|--------|----------|
| 1 | Create volume, mount, write some files | — |
| 2 | `destroy` | All pool files deleted, all superblocks deleted, all journals deleted. State DISCOVERED. |
| 3 | Check `C:\RAIDTEST\`, `D:\RAIDTEST\` | Directories empty or deleted |
| 4 | `load` | Fails — no superblock found |

**Automated test coverage:** None (would destroy test infrastructure files).

---

## TC9: Planner Capacity

**Objective:** Verify planner estimates are reasonable.

| Step | Action | Expected |
|------|--------|----------|
| 1 | `scan` to populate physical disks | — |
| 2 | Select 2 disks, 50 GB each | — |
| 3 | `planner` | RAID0 = 100 GB, RAID1 = 50 GB |
| 4 | Select 4 disks (100, 50, 80, 30 GB) | — |
| 4 | `planner` | RAID0 = 260 GB, RAID10 = 30+80 = 110 GB (pairs: 50+30, 100+80 → min each) |

**Automated test coverage:** Manual only. Planner logic is in `cmd_handler.c:670-800` and uses `g_state.physical_disks`.

---

## TC10: Metadata Report

**Objective:** Verify `metadata` dumps correct information.

| Step | Action | Expected |
|------|--------|----------|
| 1 | Create volume | — |
| 2 | `metadata C` | Shows v4, UUID, disk count, generation, stripe unit |
| 3 | Verify UUID matches `info` output | Both show same UUID |

**Automated test coverage:** `test_sb_metadata_format`, `test_sb_format_v3_compat`

---

## TC11: Journal Recovery

**Objective:** Verify crash recovery via journal replay.

| Step | Action | Expected |
|------|--------|----------|
| 1 | Create volume, enable cache, mount | — |
| 2 | Write data, immediately kill power (simulate: delete `superblock.dat` and leave journal incomplete) | — |
| 3 | Re-create pool files, `load` | Journal replay fires, transparently recovers last write |
| 4 | Read written data | Data matches pre-crash content |

**Automated test coverage:** `test_journal_recover_replay`

---

## Summary

| TC | Scenario | Automated | Priority |
|----|----------|-----------|----------|
| 1 | Normal lifecycle | Partial | P0 |
| 2 | Drive letter change | Yes | P0 |
| 3 | SATA port change | Yes | P1 |
| 4 | Reboot | No | P0 |
| 5 | Metadata corruption | Partial | P0 |
| 6 | Serial mismatch | Yes | P0 |
| 7 | Missing disk (degraded) | Yes | P1 |
| 8 | Destroy | No | P1 |
| 9 | Planner capacity | No | P2 |
| 10 | Metadata report | Yes | P2 |
| 11 | Journal recovery | Yes | P1 |
