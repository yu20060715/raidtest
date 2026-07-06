# Bug Status — Sprint 9

Tracking status of all known bugs in BUG_LIST.md through Sprint 9 (Critical Bug Elimination).

---

## P1 — Critical (must fix before GA)

| Bug | Description | Status | Fix | Regression Test |
|-----|-------------|--------|-----|-----------------|
| B4 | Zero-byte Pool Crash — `pool_file_create` accepts size=0, leads to divide-by-zero in stripe engine | **FIXED** | `validate_pool_size()` added to `common.h`; fail-safe check in `pool_file_create()` | `tests/regression/test_zero_pool.c` |
| B7 | Unchecked WriteFile — partial write in `journal_data()` can cause silent corruption during replay | **FIXED** | Added `&& written == expected_size` after all `WriteFile` calls | `tests/regression/test_write_fail.c` |
| B10 | Cache Thread Race — `cache_destroy` frees buffer while flush thread may still be writing | **FIXED** | `cache_destroy()` now waits on `cache->flush_thread` before freeing | `tests/regression/test_cache_shutdown.c` |

## P2 — High Priority

| Bug | Description | Status | Notes |
|-----|-------------|--------|-------|
| B2 | `CreateEventW` return unchecked — NULL handle used in overlapped I/O | **FIXED** | All 4 call sites in `stripe_engine.c` and `ram_cache.c` now check for NULL |
| B8 | Journal CRC32 mismatch on data replay — data payload has no CRC | **OPEN** | P2 — needs data CRC in journal entries |
| B12 | cmd_expand rollback leaves stale handles — handles closed but not set to INVALID_HANDLE_VALUE | **OPEN** | P2 — low risk, fix in Sprint 10 |

## P3 — Medium Priority

| Bug | Description | Status | Notes |
|-----|-------------|--------|-------|
| B1 | Superblock reader double-close on v2 read (cosmetic) | **OPEN** | P3 — cosmetic |
| B3 | VirtualAlloc return unchecked in stripe_engine verify | **OPEN** | P3 — low memory scenario |
| B5 | cmd_expand cmd_handler.c:323-328 rollback leaks pool dir | **OPEN** | P3 — stale directory after rollback |
| B6 | stripe_expand truncation on uneven disk sizes from pool expand | **OPEN** | P3 — theoretical |
| B9 | cmd_expand rollback leaves stale paths and handles not nulled | **OPEN** | P3 — pool_file_open overwrites handle |
| B11 | Mirror rebuild on healthy disk could overwrite good data | **OPEN** | P3 — edge case |
| B13 | Pool file directory not cleaned on pool delete | **OPEN** | P3 |
| B14 | Bench file not cleaned from all drives after benchmark | **OPEN** | P3 |

## Summary

- **Total bugs:** 14
- **P1 fixed:** 3/3 (100%)
- **P2 fixed:** 1/3 (33%)
- **P3 fixed:** 0/8 (0%)
- **Overall:** 4/14 (29%)
