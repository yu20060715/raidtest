# RAIDTEST v3 — Feature Validation Matrix

## 1. scan

| Item | Status |
|------|--------|
| **Success** | Enumerates all fixed drives, detects physical disks, reads model/serial/size |
| **Failure** | No admin rights → CreateFile on physical drive fails → empty list |
| **Limits** | Only fixed NTFS drives; no network/USB/optical; max 256 disks |
| **Test** | `cmd_scan()` in `cmd_handler.c:46`; tested manually only (no auto test) |

## 2. init (select / mapdrive)

| Item | Status |
|------|--------|
| **Success** | Creates pool file of specified size on disk, opens for I/O |
| **Failure** | Disk not found, out of space, path error → `RC_ERR_INVALID_DISK`/`RC_ERR_IO` |
| **Limits** | Pool size clamped to min 1024 MB; no sparse file support |
| **Test** | `cmd_init()` in `cmd_handler.c:63`; tested indirectly via `sb_disk_create()` in test_superblock |

## 3. create (RAID0)

| Item | Status |
|------|--------|
| **Success** | Builds stripe mapping, generates UUID, writes v4 superblock |
| **Failure** | < 2 disks, pool file open fails → `RC_ERR_INVALID_STATE`/`RC_ERR_IO` |
| **Limits** | RAID0 only (no parity); max 4 disks; stripe unit fixed at 1 MB |
| **Test** | `test_stripe_create_2disks`, `test_stripe_create_3disks_asymmetric` (7 stripe tests) |

## 4. mirror (RAID1)

| Item | Status |
|------|--------|
| **Success** | Creates 2+ disk mirror, writes to all, reads from healthiest |
| **Failure** | < 2 disks, pool file open fails |
| **Limits** | No RAID10; rebuild requires manual index; no automatic re-sync |
| **Test** | `test_mirror_create_2disks`, `test_mirror_write_to_all`, `test_mirror_degraded_read` (6 mirror tests) |

## 5. load

| Item | Status |
|------|--------|
| **Success** | Scans all fixed drives, picks highest generation superblock, restores volume with serial matching |
| **Failure** | No superblock found → `RC_ERR_METADATA`; pool file missing → `RC_ERR_IO` |
| **Limits** | Requires specific drive root or scans C:-Z:; no user-specified search path |
| **Test** | `test_sb_stripe_write_read`, `test_sb_generation`, `test_sb_serial_restore` (8 superblock tests) |

## 6. restore (superblock_restore)

| Item | Status |
|------|--------|
| **Success** | Serial match finds correct physical disk → opens pool at matched drive letter |
| **Failure** | Serial mismatch falls back to SB drive_letter; all pool opens fail → returns false |
| **Limits** | Serial match only works if `disk_scan_all` was called first to populate physical_disks |
| **Test** | `test_sb_serial_restore`, `test_sb_serial_fallback`, `test_sb_blank_serial`, `test_sb_restore_no_phys` |

## 7. destroy

| Item | Status |
|------|--------|
| **Success** | Unmounts, deletes all pool files, superblocks, journals → state DISCOVERED |
| **Failure** | State not MOUNTED/UNMOUNTED → `RC_ERR_INVALID_STATE` |
| **Limits** | No rollback if delete fails partway through (partial cleanup possible) |
| **Test** | Manual only (no auto test — would destroy real pool files) |

## 8. planner

| Item | Status |
|------|--------|
| **Success** | Displays disk list, total capacity, RAID0/RAID1/RAID10 estimates |
| **Failure** | No disks scanned → shows "scan first" message |
| **Limits** | Estimates are rough (uses min disk for RAID1, sorted pairs for RAID10); no GUI |
| **Test** | Manual only; output correctness depends on disk state |

## 9. metadata

| Item | Status |
|------|--------|
| **Success** | Reads superblock from specified drive, dumps formatted report (UUID, generation, disks, timestamps) |
| **Failure** | No superblock on drive → `RC_ERR_NOT_FOUND` |
| **Limits** | Reports one drive at a time; no cross-drive consistency check |
| **Test** | `test_sb_metadata_format`, `test_sb_format_v3_compat` |

## 10. event log

| Item | Status |
|------|--------|
| **Success** | Appends timestamped event to `events.log` in appdata; `events` command displays it |
| **Failure** | No appdata path → silently returns; file too large → trims last 256 KB |
| **Limits** | Simple text file; no structured querying; no log rotation |
| **Test** | Manual only |

## 11. check

| Item | Status |
|------|--------|
| **Success** | Checks each disk state, pool file accessibility, superblock presence on all members |
| **Failure** | Any disk unhealthy, pool missing, or superblock missing → `RC_ERR_IO` |
| **Limits** | Does not scrub data; does not verify checksums of data blocks; only metadata-level check |
| **Test** | Manual only |

## 12. simulate

| Item | Status |
|------|--------|
| **Success** | Fails/restores/disconnects a disk; transitions state to DEGRADED or back to MOUNTED |
| **Failure** | State not MOUNTED, invalid index, invalid mode → error return |
| **Limits** | Does not simulate real hardware failure; only toggles in-memory flags |
| **Test** | Manual only |

## 13. mount / unmount

| Item | Status |
|------|--------|
| **Success** | Mounts volume as WinFsp drive letter; unmount releases |
| **Failure** | Mount requires WinFsp installed; state check enforced |
| **Limits** | Requires WinFsp runtime; not a true block device; CrystalDiskMark may not detect |
| **Test** | Manual only (requires WinFsp) |

## 14. cache

| Item | Status |
|------|--------|
| **Success** | Enables write-back cache with dirty block tracking; flush thread writes back asynchronously |
| **Failure** | Zero-size cache rejected |
| **Limits** | No write-back throttle; no LRU eviction; flush thread can fall behind under heavy write load |
| **Test** | `test_cache_write_read`, `test_cache_multi_write_flush`, `test_cache_flush_integration` (8 cache tests) |

## 15. journal

| Item | Status |
|------|--------|
| **Success** | begin → data → commit cycle; recovery replays incomplete journals |
| **Failure** | Journal file open/close failures logged; recovery skips on error |
| **Limits** | No journal trimming; no circular buffer; one journal per disk; no checksum on journal data blocks |
| **Test** | `test_journal_roundtrip`, `test_journal_recover_clean`, `test_journal_recover_replay` (4 journal tests) |

## 16. expand

| Item | Status |
|------|--------|
| **Success** | Adds new disks to existing RAID0 volume; rewrites superblock |
| **Failure** | Pool file create/open fails → rollback (delete created pools) |
| **Limits** | Only RAID0; no online expand (volume must not be in use); up to 4 disks total |
| **Test** | `test_stripe_expand` |

## 17. rebuild (mirror)

| Item | Status |
|------|--------|
| **Success** | Creates pool on new disk, copies data from healthy mirror member |
| **Failure** | Source disk unhealthy, pool create fails → error |
| **Limits** | Only RAID1; requires manual source/replacement index; no automatic hot spare |
| **Test** | `test_mirror_rebuild` |

## 18. info / status

| Item | Status |
|------|--------|
| **Success** | Shows physical disks, volume layout, cache hit rate, runtime, throughput |
| **Failure** | No volume → shows "No virtual volume" |
| **Limits** | CLI only; no real-time graphs; status uses `system("cls")` which is platform-dependent |
| **Test** | Manual only |
