# RAIDTEST v1.0 RC2 — Known Bug List

All bugs listed as **Open** are confirmed in current code.

| ID | Severity | Status | Area | Summary |
|----|----------|--------|------|---------|
| B1 | P3 | Open | superblock.c | Superblock reader double-close on v2 read (cosmetic) |
| B2 | P2 | **Fixed** | stripe_engine.c, ram_cache.c | CreateEventW return checks added |
| B3 | — | Not a bug | stripe_engine.c | VirtualAlloc already checked — false alarm |
| B4 | P1 | **Fixed** | raid_service.c | Pool size zero rejected with `_strtoui64` end-pointer check |
| B5 | P2 | **Fixed** | all | All `atoi`/`atoll` replaced with `safe_atou32`/`safe_atou64` |
| B6 | P3 | Open | cmd_handler.c | Event log append not thread-safe |
| B7 | P1 | **Fixed (already)** | journal.c | WriteFile return + written bytes already checked at journal.c:55,86,88 |
| B8 | P2 | Open | journal.c | Journal data payload has no CRC32 |
| B9 | P3 | Open | cmd_handler.c | Expand rollback does not null stale handles |
| B10 | P1 | **Fixed (already)** | ram_cache.c | `cache_destroy` at ram_cache.c:36-39 properly waits for flush thread |
| B11 | P2 | **Fixed** | raid_service.c | Purge rejects MOUNTED state, requires explicit unmount |
| B12 | P3 | Open | superblock.c | Superblock write does not verify all disks |
| B13 | P3 | Open | cmd_handler.c | cmd_check accesses pool file without lock |
| B14 | P2 | **Fixed** | daemon.c | `pool_sizes_mb` overflow guard changed from MAX_CUSTOM_DISKS(8) to MAX_DISKS(4) |

## Priority key
- P1 = fix before next release
- P2 = fix within next sprint  
- P3 = backlog
