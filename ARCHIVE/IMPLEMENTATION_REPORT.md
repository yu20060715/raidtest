# Implementation Report — Security & Correctness Hardening

All fixes applied; 38/38 tests pass.

---

## Fixes Applied

### P0 — Data Loss / Security (5 fixed, 2 already present)

| ID | Description | File | Fix |
|----|-------------|------|-----|
| P0-04 | `newpath[256]` overflow in `raid_rename` — unbounded `wcscat` chains up to 511 chars into 256-byte buffer | `fuse_bridge.c:284-289` | Added length check (`wdlen + 1 + suflen >= 256`) and replaced `wcscpy/wcscat` with bounded `wcscpy_s/wcscat_s` |
| P0-05 | 32 `raid_*` functions in `raid_service.c` + `raid_query.c` never held `g_state_cs` — all state access was a data race | `raid_service.c`, `raid_query.c` | Made every public `raid_*` function self-locking (gs_lock/gs_unlock). Created `_locked` variants for 6 functions called by `raid_quick` to avoid re-entrant deadlock. Removed redundant gs_lock from `cmd_handler.c`, `daemon.c`, `main.c` callers |
| P0-06 | `swscanf` with unbounded `%[^\"]` overflows `lang[8]` from crafted `config.json` | `config.c:86` | Added width specifier `%7[^\"]` |
| P0-07 | `malloc((size_t)file_size.QuadPart)` from untrusted journal file — no size limit | `journal.c:100` | Added `if (file_size.QuadPart > 64 MB) continue;` before allocation |
| P0-02 | `parent_dir_exists` overflow — guard already present at line 106 (`plen >= 256`) | (already fixed) | — |
| P0-03 | `wsrc` overflow in `raid_rename` — guard already present at line 279 (`slen >= 255`) | (already fixed) | — |

### P1 — Significant Correctness Risk (5 fixed, 2 already present)

| ID | Description | File | Fix |
|----|-------------|------|-----|
| P1-01 | `cache_flush_all` not re-entrant — concurrent calls clobber `flush_buffer` and interleave journal entries | `ram_cache.c:86` | Added `InterlockedExchange` re-entrancy guard at function entry using `vol->cache_flush_in_progress` |
| P1-02 | `journal_data` return value discarded — failure causes dirty-bit clear + data loss | `ram_cache.c:127` | Check `journal_ok`; skip dirty-bit clear and physical I/O on journal write failure |
| P1-04 | `cache_flush_in_progress` set after dirty-bit clear — TOCTOU window | `ram_cache.c:135-190` | Moved flag to function-level (set at entry, cleared at exit, removed per-batch toggle) |
| P1-07 | `raid_open` lacks `strlen(p) > 255` guard unlike `raid_create`/`raid_mkdir` | `fuse_bridge.c:182` | Added `if (strlen(p) > 255) return -ENAMETOOLONG` |
| P2-03 | `DWORD dummy` passed for overlapped `WriteFile` — should be `NULL` per MSDN | `ram_cache.c:163` | Replaced `&dummy` with `NULL` |
| P2-04 | `cache->running = 0` not atomic — inconsistent with rest of codebase | `ram_cache.c:38` | Changed to `InterlockedExchange(&cache->running, 0)` |
| P2-06 | `InterlockedIncrement` inside `EnterCriticalSection` — unnecessary atomic | `fuse_bridge.c:68` | Changed to plain `g_open_count++` |
| P1-06 | Missing `strlen(src)` check in `raid_rename` — guard already present at line 264 | (already fixed) | — |

### P0 Architecture (from ARCHITECTURE_REFACTOR_PLAN.md, partially addressed)

| ID | Description | Status |
|----|-------------|--------|
| 0.3 | Cache/journal data-consistency race | Addressed via re-entrancy guard and journal-return-value check |

---

## Files Modified

| File | Changes |
|------|---------|
| `src/fuse_bridge.c` | P0-04 (newpath bounds), P1-07 (raid_open length check), P2-06 (remove InterlockedIncrement) |
| `src/ram_cache.c` | P1-01 (re-entrancy guard), P1-02 (journal return check), P1-04 (flag ordering), P2-03 (NULL overlapped), P2-04 (atomic running) |
| `src/config.c` | P0-06 (swscanf width limit) |
| `src/journal.c` | P0-07 (journal size cap) |
| `src/raid_service.c` | P0-05 (self-locking all 25 public functions + 6 `_locked` variants for raid_quick call chain) |
| `src/raid_query.c` | P0-05 (self-locking all 7 public functions) |
| `src/cmd_handler.c` | Removed redundant gs_lock/gs_unlock from `cmd_process` and `cmd_interactive` |
| `src/daemon.c` | Removed redundant gs_lock/gs_unlock from `daemon_process_stdin` |
| `src/main.c` | Removed redundant gs_lock/gs_unlock from `do_restore` |

---

## Bugs Confirmed NOT Present (from BUG_AUDIT.md)

- **P0-01**: Stale snapshot in `cache_flush_all` — lock IS held across memcpy → journal_data → dirty-bit-clear (lines 97-132)
- **P0-02**: `parent_dir_exists` overflow — guard `if (plen >= 256) return false` already at line 106
- **P0-03**: `wsrc` overflow in `raid_rename` — guard `if (strlen(src) > 254) return -ENAMETOOLONG` at line 264 + `if (slen >= 255)` at line 279
- **P1-03**: Dirty-bit ownership tracking — lock held during entire critical section, concurrent `cache_write` cannot interleave
- **P1-06**: Missing `strlen(src)` check — already present at line 264 alongside the `dst` check

---

## Test Results

**38/38 passed, 0 failed**

- Superblock: 11 tests
- Journal: 5 tests
- Cache: 8 tests
- Mirror: 6 tests
- Stripe: 5 tests
- Normalize: 3 tests
