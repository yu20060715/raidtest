# Bug Report

> **Status: All 4 bugs have been fixed. See per-bug "Fix Applied" notes below.**

## Critical

### BUG-1: Data Corruption — dirty bits cleared before journal DATA write in `cache_flush_all`

**Location:** `src/ram_cache.c:128-134`

**Root Cause:**
In `cache_flush_all()`, for each batch of dirty blocks the sequence is:
1. `memcpy` to flush buffer (line 128)
2. Clear dirty-map bits (lines 129-130)  
3. Release `cache->lock` (line 131)
4. `journal_data()` writes DATA entry to journal (line 134)

If the process crashes between step 2-3 and step 4, dirty bits are cleared (in-memory state says "flushed") but the journal has no DATA entry. On restart, journal replay finds no DATA for these blocks → data never reaches the volume.

The write-ahead log contract requires the journal DATA entry to be persisted BEFORE the dirty bits are cleared.

**Severity:** Critical — permanent data loss.

**Reproduce:**
1. Enable cache, write data
2. Trigger cache flush
3. Power-cycle or kill process in the window between dirty-bit clear and `journal_data()`

**Impact:** Acknowledged writes silently lost. Volume metadata generation is advanced but data is stale/zero.

**Suggested Fix:**
Call `journal_data()` before clearing dirty bits:

```c
/* Journal first (write-ahead) */
journal_data(vol, block_offset, total_len, cache->flush_buffer);

/* Then clear dirty bits under lock */
EnterCriticalSection(&cache->lock);
memcpy(cache->flush_buffer, cache->buffer + block_offset, total_len);
for (uint32_t i = b; i < b + run; i++)
    cache->dirty_map[i / 8] &= ~(1 << (i % 8));
LeaveCriticalSection(&cache->lock);
```

**Fix Applied:** `src/ram_cache.c:124-134` — `journal_data()` now called after memcpy to flush_buffer but before clearing dirty bits. Lock is released between memcpy and journal_data, then re-acquired to clear dirty bits, ensuring write-ahead ordering.

---

### BUG-2: Data Corruption — TOCTOU race between `has_work` check and `journal_begin`

**Location:** `src/ram_cache.c:92-134`

**Root Cause:**
The `has_work` check (lines 93-96) and `journal_begin()` call (line 97) are done WITHOUT holding `cache->lock`. A concurrent `cache_write()` can dirty blocks between the check and the call:

1. Flush thread: `has_work` check → no dirty blocks → `has_work = false`
2. Write thread: `cache_write()` dirties block X
3. Flush thread: `journal_begin()` is SKIPPED because `has_work == false`
4. Flush thread: loop finds block X dirty, clears dirty bits, calls `journal_data()`
5. Physical `WriteFile`

Result: Journal has DATA entries without a preceding BEGIN. Recovery code at `journal.c:133` ignores DATA when `has_begin == false`:

```c
} else if (je.entry_type == JT_DATA && has_begin && !has_commit) {
```

On crash after step 4, DATA is in the journal but skipped during recovery → permanent data loss.

**Severity:** Critical — permanent data loss.

**Reproduce:**
High write load with concurrent cache flushing. System crashes during the race window.

**Impact:** Acknowledged writes silently lost.

**Suggested Fix:**
Always call `journal_begin()` at the start of `cache_flush_all` unconditionally. An extra BEGIN without matching DATA/COMMIT is harmless — recovery sees `has_begin` set and finds zero DATA entries, so it does nothing:

```c
/* Always begin a journal cycle */
journal_begin(vol);
```

**Fix Applied:** `src/ram_cache.c:93` — `journal_begin()` now called unconditionally at the start of `cache_flush_all()`. The `has_work` check and its downstream usage in the commit logic were removed. Commit is now called only when `flushed > 0`.

---

## High

### BUG-3: Data Corruption — journal truncated after failed recovery write

**Location:** `src/journal.c:160-176`

**Root Cause:**
During journal replay, if `stripe_volume_write()` fails for any replayed range, the journal is still truncated:

```c
for (uint32_t i = 0; i < range_count; i++) {
    bool ok = stripe_volume_write(vol, raw + ranges[i].data_off,
                                   ranges[i].off, ranges[i].len);
    if (ok) replayed++; else failed++;
}
/* Always truncated regardless of failure */
h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
```

If a transient IO error (bad sector being remapped, disk busy, etc.) causes `stripe_volume_write()` to fail, the corresponding data range is permanently lost. The journal is gone, so no retry is possible on next restart.

**Severity:** High — data loss on failed recovery write.

**Reproduce:**
1. Cache flush writes DATA entries to journal
2. System crashes before COMMIT written
3. On restart, recovery reads journal
4. One `stripe_volume_write()` returns failure (e.g., sector error)
5. Journal truncated despite partial failure

**Impact:** Partial data loss that is unrecoverable on subsequent restarts.

**Suggested Fix:**
Preserve journal on failure for retry:

```c
if (failed > 0) {
    LOG_ERROR("Journal replay: %u OK, %u FAILED — journal preserved for retry",
              replayed, failed);
    free(raw);
    continue;
}
/* Only truncate if all replays succeeded */
```

**Fix Applied:** `src/journal.c:160-166` — On replay failure, the journal file is now preserved (not truncated) by `continue`-ing to the next disk. Truncation only happens after all ranges replay successfully.

---

## Medium

### BUG-4: Race Condition — `is_dir_by_path()` reads `g_open_files` without lock

**Location:** `src/fuse_bridge.c:90-93`

**Root Cause:**
`is_dir_by_path()` calls `find_open_file_locked()` without holding `g_file_table_lock`. This is accessed from multiple FUSE operations (`raid_open`, `raid_create`, `raid_mkdir`, `raid_rename`) which run concurrently because `fuse_loop_mt` is used (line 513).

```c
static bool is_dir_by_path(const char* name) {
    int idx = find_open_file_locked(name);   // No lock held!
    return (idx >= 0 && g_open_files[idx].is_dir);
}
```

Concurrent `raid_create`/`raid_unlink`/`raid_rename` from other threads can modify `g_open_files[]` and `g_open_count` while `is_dir_by_path` reads them, causing:
- Wrong directory-existence result leading to `-ENOENT` on valid paths
- Out-of-bounds read on `g_open_files[]` if `g_open_count` is concurrently decremented

**Severity:** Medium — race causing incorrect filesystem behavior, potential crash.

**Reproduce:**
Multiple concurrent file/directory create/delete operations via FUSE while multi-threaded.

**Impact:** Random `ENOENT` on create operations, potential `g_open_files[]` OOB read.

**Suggested Fix:**
Acquire the lock inside `is_dir_by_path()`:

```c
static bool is_dir_by_path(const char* name) {
    file_table_lock_init();
    EnterCriticalSection(&g_file_table_lock);
    int idx = find_open_file_locked(name);
    bool result = (idx >= 0 && g_open_files[idx].is_dir);
    LeaveCriticalSection(&g_file_table_lock);
    return result;
}
```

**Fix Applied:** `src/fuse_bridge.c:90-95` — `is_dir_by_path()` now acquires `g_file_table_lock` before calling `find_open_file_locked()`.

---

## Summary

| ID   | Severity | Category          | Module         | Fix Complexity |
|------|----------|-------------------|----------------|----------------|
| BUG-1| Critical | Data corruption   | ram_cache.c    | 3 lines        |
| BUG-2| Critical | Data corruption   | ram_cache.c    | 1 line         |
| BUG-3| High     | Data corruption   | journal.c      | 5 lines        |
| BUG-4| Medium   | Race condition    | fuse_bridge.c  | 4 lines        |
