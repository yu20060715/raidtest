# Validation Matrix

| Test | Scope | Status | Pass/Fail |
|------|-------|--------|-----------|
| **RAID0 Create** | stripe_volume_create with 2+ disks | automated | ✓ Pass |
| **RAID0 Destroy** | stripe_volume_destroy, resource cleanup | automated | ✓ Pass |
| **RAID0 Write** | stripe_volume_write data integrity | automated | ✓ Pass |
| **RAID0 Read** | stripe_volume_read data integrity | automated | ✓ Pass |
| **RAID0 Write+Read Verify** | write pattern, read back, compare | automated | ✓ Pass |
| **RAID1 Create** | mirror_volume_create with 2 disks | automated | ✓ Pass |
| **RAID1 Mirror Write** | mirror_volume_write data integrity | automated | ✓ Pass |
| **RAID1 Mirror Read** | mirror_volume_read data integrity | automated | ✓ Pass |
| **RAID1 Rebuild** | mirror_volume_rebuild after disk replacement | automated | ✓ Pass |
| **Mount** | raid_mount (FUSE-based) | manual + test | ✓ Pass |
| **Unmount** | raid_unmount | manual + test | ✓ Pass |
| **Destroy** | raid_destroy volume deletion | manual + test | ✓ Pass |
| **Long Run** | 10,000 write/read/verify cycles | automated | (Task 1) |
| **Random IO** | Random write/read/seek/overwrite | automated | (Task 2) |
| **Concurrent Access** | Multi-thread read/write | automated | (Task 7) |
| **Power Failure Recovery** | Abort + journal recovery | automated | (Task 3) |
| **Metadata Corruption** | Corrupted magic/version/uuid/crc | automated | (Task 4) |
| **Journal Begin/Commit** | journal_begin, journal_data, journal_commit | automated | ✓ Pass |
| **Journal Recovery** | journal_recover_all after crash | automated | ✓ Pass |
| **Cache Write** | ram_cache write-through | automated | ✓ Pass |
| **Cache Read** | ram_cache read hit/miss | automated | ✓ Pass |
| **Cache Flush** | cache_flush_all dirty block flush | automated | ✓ Pass |
| **Superblock Write** | superblock_write to disk | automated | ✓ Pass |
| **Superblock Read** | superblock_read volume reconstruction | automated | ✓ Pass |
| **Superblock CRC** | CRC32 validation on load | automated | ✓ Pass |
| **Disk Scanner** | disk_scan_all device enumeration | manual | ✓ Pass |
| **Planner** | raid_planner capacity calculation | automated | ✓ Pass |
| **GUI Create** | GUI state-based create button | manual | ✓ Pass |
| **GUI Mount** | GUI mount/unmount workflow | manual | ✓ Pass |
| **GUI Destroy** | GUI destroy with confirmation dialog | manual | ✓ Pass |
| **GUI Error Dialog** | MessageBox on worker failure | manual | ✓ Pass |
| **GUI Benchmark** | GUI benchmark button + display | manual | ✓ Pass |
| **CLI Auto** | --auto restore from config | manual | ✓ Pass |
| **CLI Quick** | --quick auto setup | manual | ✓ Pass |
| **CLI Bench** | --bench CSV output | automated | (Task 5) |
| **CLI Version** | --version / -v | manual | ✓ Pass |
| **Config Save/Load** | raid_config_save / raid_config_load | automated | ✓ Pass |
| **Event Bus** | event_bus_publish / subscribe | automated | ✓ Pass |
| **Resource Leak** | 48 resources audited, 0 leaks | audit | ✓ Pass |
| **Thread Safety** | No deadlock in concurrent operations | automated | (Task 7) |
| **Memory Safety** | ASan/UBSan clean build | automated | (Task 6) |
| **B4: Zero-byte Pool** | pool_file_create rejects 0 and <512 | automated | ✓ Pass |
| **B7: Partial Write** | journal_data checks written==length | automated | ✓ Pass |
| **B10: Thread Race** | cache_destroy waits on flush thread | automated | ✓ Pass |
| **B2: Event Handle** | CreateEventW NULL check | automated | ✓ Pass |
| **_WIN32_WINNT** | No redefinition warning (0 own-code) | compile | ✓ Pass |
