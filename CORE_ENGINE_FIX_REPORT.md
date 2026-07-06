# Core Engine Fix Report

## Summary

- **Issues examined**: 3 P0, 4 P1 (from CORE_ENGINE_REPORT.md)
- **Fixed**: 3 P0 + 2 P1 = **5 fixes**
- **Rejected**: 2 (P0-3 mirror partial write = correct RAID1 degraded behavior; P1-2 cache_flush_in_progress TOCTOU = no data corruption)
- **Partially confirmed, no fix**: 1 (P1-4 journal replay through cache = fragile but correct today)
- **Tests**: 38/38 passed, all stress builds OK

---

## Fix Details

### P0-1: cache_read returns zeros for unwritten blocks
**Files**: `src/common.h`, `src/ram_cache.c`

**Root cause**: `RAM_CACHE` had no `valid_map` bitmap. `cache_read` memcpy'd from zero-initialized VirtualAlloc buffer without tracking which blocks had been written.

**Fix**:
- Added `uint8_t* valid_map` to `RAM_CACHE` struct in `common.h`
- Allocated + zeroed `valid_map` in `cache_init`, freed in `cache_destroy`
- `cache_write` sets `valid_map` bits (alongside `dirty_map`)
- `cache_read` checks `valid_map`; returns `false` (miss) if any block in range is invalid

### P0-2: Write-through marks dirty cache before physical I/O completes
**File**: `src/stripe_engine.c` (stripe_volume_write)

**Root cause**: `cache_write` was called first, then physical I/O. If phy I/O failed, the cache was already dirty with data that was never persisted.

**Fix**: Reordered write-through path:
1. Physical I/O completes first (concurrent overlapped writes)
2. `cache_write` is called only after physical I/O succeeds
3. If phy I/O fails, cache is never touched

### P0-4: Journal recovery uses fixed 64KB buffer
**File**: `src/journal.c`

**Root cause**: `uint8_t raw[65536]` on stack cannot hold flush batches up to 16MB (256 entries × 64KB).

**Fix**: Use `GetFileSizeEx` to determine file size, then `malloc` a matching buffer.

### P0-5: Rebuild does not flush after writing to replacement disk
**File**: `src/mirror_engine.c` (mirror_volume_rebuild)

**Root cause**: `stripe_write_raw` without `FlushFileBuffers` leaves data volatile in OS cache.

**Fix**: Added `FlushFileBuffers(replacement->handle)` after each successful write chunk.

### P1-1: Profiler slot claim has TOCTOU race
**File**: `src/profiler.c`, `src/profiler.h`

**Root cause**: `find_free_slot` checked `!active` then set `active = true` non-atomically. Two threads could claim the same slot.

**Fix**: Changed `IO_SAMPLE.active` from `bool` to `volatile LONG` and used `InterlockedCompareExchange(&active, 1, 0)` to atomically claim a free slot.

### P1-3: Orphan .tmp blindly replaces valid .dat
**File**: `src/superblock.c` (try_recover_orphan_tmp)

**Root cause**: Any `.tmp` file was blindly renamed to `.dat`, potentially replacing a good superblock with a stale or corrupt one.

**Fix**:
- Read and validate `.tmp`: check magic, CRC32 checksum
- Read existing `.dat` (if present) and extract its generation
- Only replace if `.tmp` generation > `.dat` generation (newer = better)
- Otherwise delete the stale `.tmp`

---

## Verification

- `raidtest_tests.exe`: **38/38 passed**
- Stress tests: all build successfully
- Compiler warnings: 0 new (only pre-existing `-Wincompatible-pointer-types` in non-modified code)
