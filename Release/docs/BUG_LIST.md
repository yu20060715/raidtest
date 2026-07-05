# RAIDTEST v3 — Known Bug List

All bugs are **confirmed in current code** (not theoretical).

---

## B1: superblock reader double-close on v2 read

**File:** `superblock.c:167-168`
**Code:**
```c
if (!ReadFile(h, raw, sizeof(raw), &read, NULL)) { CloseHandle(h); return false; }
CloseHandle(h);
```
**Problem:** If `ReadFile` fails, `CloseHandle(h)` is called, then `return false`. But on success (line 168), the handle is closed again. Not a double-close on the same path, but the error path closes `h` and the caller has no way to know.

**Severity:** Low — the `return false` prevents use-after-close. Handle is not reused.
**Workaround:** None needed; cosmetic.
**Priority:** P3
**Data loss risk:** None

---

## B2: CreateEventW return values unchecked

**Files:** `stripe_engine.c:482,535,580`, `ram_cache.c:130`, `bench_io.c:67,89`
**Code pattern:**
```c
HANDLE evt = CreateEventW(NULL, TRUE, FALSE, NULL);
// evt is used without checking for NULL
```
**Problem:** `CreateEventW` can return NULL on low-memory conditions. The code uses the handle directly in `OVERLAPPED.hEvent` and `WaitForMultipleObjects` without checking.

**Severity:** Medium — crash on NULL dereference in low-memory scenario.
**Workaround:** Ensure system has sufficient memory.
**Priority:** P2
**Data loss risk:** Low — crash before write occurs.

---

## B3: VirtualAlloc return unchecked in stripe_engine verify

**File:** `stripe_engine.c:675-677`
**Code:**
```c
uint8_t* write_buf = (uint8_t*)VirtualAlloc(NULL, test_size, MEM_COMMIT, PAGE_READWRITE);
uint8_t* read_buf = (uint8_t*)VirtualAlloc(NULL, test_size, MEM_COMMIT, PAGE_READWRITE);
if (!write_buf || !read_buf) { LOG_ERROR(...); return false; }
```
**Note:** Actually checked — lines 675-676 assign, line 677 checks. **Not a bug.** False alarm from static analysis.

---

## B4: Pool file `size_mb` zero acceptance

**File:** `cmd_handler.c:138`
**Code:**
```c
uint64_t mb = (uint64_t)atoll(colon + 1);
```
**Problem:** If user types `init 0:` (no number after colon), `atoll("")` returns 0. Pool file of 0 bytes is created, which later causes divide-by-zero in phase computation.

**Severity:** High — pool file of size 0 causes undefined behavior in stripe engine.
**Workaround:** Always specify a size (e.g., `init 0:10240`).
**Priority:** P1
**Data loss risk:** Medium — can lead to crash during I/O.

---

## B5: atoi returns 0 for non-numeric input

**Files:** All `atoi`/`atoll` call sites (cmd_handler.c:84,98,137,156,179,374,375,428,666,939,940; wizard.c:12,58)
**Code:**
```c
uint32_t id = (uint32_t)atoi(argv[0]);
```
**Problem:** `atoi("abc")` returns 0 without error. Invalid input is silently accepted. For disk ID, ID 0 might be valid, making detection impossible.

**Severity:** Medium — user gets unexpected behavior without feedback.
**Workaround:** Verify arguments manually before using.
**Priority:** P2
**Data loss risk:** None — just incorrect operation.

---

## B6: event_log_append uses non-thread-safe file append

**File:** `cmd_handler.c:522-546`
**Code:**
```c
HANDLE h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, ...);
SetFilePointer(h, 0, NULL, FILE_END);
WriteFile(h, buf, (DWORD)len, &written, NULL);
CloseHandle(h);
```
**Problem:** No lock around create/write/close sequence. If two threads call `event_log_append` simultaneously, writes can interleave or the second thread's `SetFilePointer` can overwrite the first thread's data.

**Severity:** Low — event log is not critical data. Interleaving causes garbled but recoverable log lines.
**Workaround:** Single-threaded usage only.
**Priority:** P3
**Data loss risk:** Low — at worst a few log lines are lost.

---

## B7: journal_data `WriteFile` result not always checked

**File:** `journal.c:88`
**Code:**
```c
WriteFile(h, data, length, &written, NULL);
```
**Problem:** Return value stored in `ok` but `ok` is only checked for `FlushFileBuffers` afterward. Partial write to journal is undetected.

**Severity:** Medium — journal may contain incomplete data entries, causing silent data corruption during replay.
**Workaround:** None.
**Priority:** P1
**Data loss risk:** High — incomplete journal entry can corrupt recovered data.

---

## B8: journal `CRC32` mismatch on data replay

**File:** `journal.c:144` (recovery replay)
**Code:** When replaying journal entries, the CRC32 of the entry header is checked but the **data payload has no CRC**. If the journal file was partially corrupted between `JE_DATA` write and `JE_COMMIT` write, the replay applies corrupted data.

**Severity:** Medium — corrupted journal data is recovered as if it were valid.
**Workaround:** None.
**Priority:** P2
**Data loss risk:** Medium — corrupted journal data silently accepted.

---

## B9: cmd_expand rollback closes but does not null handles

**File:** `cmd_handler.c:323-328`
**Code:**
```c
rollback_expand:
    for (uint32_t i = 0; i < created; i++) {
        pool_file_close(new_disks[i]);
        pool_file_delete(new_disks[i]);
    }
```
**Problem:** After rollback, the `DISK_INFO` entries still have stale file paths and non-null `handle` fields. The `handle` was closed but not set to `INVALID_HANDLE_VALUE`. A subsequent `pool_file_open` may succeed on stale handle value.

**Severity:** Low — `pool_file_open` creates a new `CreateFileW` call and overwrites the handle.
**Priority:** P3
**Data loss risk:** None.

---

## B10: Cache flush thread termination race

**File:** `ram_cache.c:185-191`
**Code:**
```c
cache->running = 0;  // volatile LONG
SetEvent(cache->flush_event); // (if exists)
// Thread sees running=0, exits
```
**Problem:** No `WaitForSingleObject` at the caller in all paths. The `cleanup_cache` in `cleanup.c:12-14` does wait, but `cache_destroy` in `ram_cache.c:32-37` does not. If `cache_destroy` is called directly (not through cleanup_cache), the flush thread may still be writing when the cache buffer is freed.

**Severity:** High — use-after-free on cache buffer.
**Workaround:** Always call `cleanup_cache` before `cache_destroy`.
**Priority:** P1
**Data loss risk:** High — crash or silent data corruption.

---

## B11: cmd_purge accessible in STATE_MOUNTED without unmount

**File:** `cmd_handler.c:468-481`
**Code:**
```c
static RC cmd_purge(void) {
    if (g_state.state != STATE_INITIALIZED && g_state.state != STATE_MOUNTED && g_state.state != STATE_UNMOUNTED) {
```
**Problem:** `purge` from `MOUNTED` state will delete pool files while the WinFsp mount is still active. Subsequent reads from the mount point will target deleted files.

**Severity:** Medium — crash/hang on the mount thread.
**Workaround:** Always `unmount` before `purge`.
**Priority:** P2
**Data loss risk:** Medium — data loss if writes are in-flight.

---

## B12: superblock_write does not verify all disks succeed

**File:** `superblock.c:103-136`
**Code:** Loop writes to each disk. `any_success` is true if at least one write succeeded. If one disk is failing, the superblock is written to a subset.

**Severity:** Low — subsequent load picks the best generation, so at least one valid copy exists.
**Priority:** P3
**Data loss risk:** None as long as at least one disk is healthy.

---

## B13: cmd_check accesses pool_file without lock

**File:** `cmd_handler.c:622-630`
**Code:**
```c
HANDLE h = CreateFileW(d->file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, ...);
```
**Problem:** `cmd_check` can run concurrently with I/O. `CreateFile` with `OPEN_EXISTING` on an active pool file may fail if another thread holds exclusive access.

**Severity:** Low — false positive "pool file not accessible" in check output.
**Priority:** P3
**Data loss risk:** None.

---

## B14: No bounds check on physical disk count

**File:** `cmd_handler.c` `cmd_scan`/`init`
`g_state.physical_count` is set from `disk_scan_all`. If there are more than `MAX_CUSTOM_DISKS` (8) disks, the array `g_state.disks[MAX_CUSTOM_DISKS]` may overflow.

**Severity:** Medium — buffer overflow on `g_state.disks` assignment.
**Workaround:** Keep under 8 physical disks.
**Priority:** P2
**Data loss risk:** Low — overflow writes to adjacent global memory.

---

## Bug Severity Summary

| ID | Severity | Data Risk | Area |
|----|----------|-----------|------|
| B1 | P3 | None | superblock.c |
| B2 | P2 | Low | stripe_engine.c, ram_cache.c |
| B4 | P1 | Medium | cmd_handler.c |
| B5 | P2 | None | cmd_handler.c |
| B6 | P3 | Low | cmd_handler.c |
| B7 | P1 | High | journal.c |
| B8 | P2 | Medium | journal.c |
| B9 | P3 | None | cmd_handler.c |
| B10 | P1 | High | ram_cache.c |
| B11 | P2 | Medium | cmd_handler.c |
| B12 | P3 | None | superblock.c |
| B13 | P3 | None | cmd_handler.c |
| B14 | P2 | Low | cmd_handler.c |

**Priority key:** P1 = fix before next release, P2 = fix within next sprint, P3 = backlog.
