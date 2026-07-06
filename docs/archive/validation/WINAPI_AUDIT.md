# WinAPI Audit

Audit of all WinAPI call sites fixed during Sprint 9, documenting the bug class, affected files, and fix applied.

---

## Audit Summary

| Bug | Class | Files Fixed | Risk |
|-----|-------|-------------|------|
| B7 | `WriteFile` return unchecked | `raid_service.c`, `journal.c`, `superblock.c`, `ram_cache.c` | Partial journal writes → silent corruption |
| B10 | Thread shutdown race | `ram_cache.c` | Use-after-free on cache buffer |
| B2 | `CreateEventW` NULL unchecked | `stripe_engine.c`, `ram_cache.c` | Crash on low memory |

---

## B7 — Unchecked WriteFile (Partial Write)

### Pattern
```c
WriteFile(h, buf, len, &written, NULL);          // result ignored
BOOL ok = WriteFile(h, buf, len, &written, NULL); // written not compared to len
```

### Fix
Every `WriteFile` call must check both return value AND `written == expected_size`:
```c
BOOL ok = WriteFile(h, buf, len, &written, NULL) && written == len;
```

### Files Fixed
- **`raid_service.c:31`** — `event_log_append()`: write was completely unchecked. Now returns early on failure.
- **`journal.c:55`** — `journal_write_entry()`: added `&& written == sizeof(*je)`.
- **`journal.c:86`** — `journal_data()` entry header: added `&& written == sizeof(je)`.
- **`journal.c:88`** — `journal_data()` payload: added `&& written == length`.
- **`superblock.c:119`** — `superblock_write_to_disk()`: added `&& written == sizeof(sb)`.
- **`ram_cache.c:145`** — `GetOverlappedResult` return value was ignored; now sets `ok = false` on failure.

---

## B10 — Cache Thread Shutdown Race

### Pattern
```c
void cache_destroy(RAM_CACHE* cache) {
    cache->running = 0;
    VirtualFree(cache->buffer, 0, MEM_RELEASE);  // flush thread still running!
}
```

### Fix
`cache_destroy` now waits for the flush thread before freeing any memory. `cache_init` initializes `flush_thread = NULL` and `write_through = 0`.

```c
void cache_destroy(RAM_CACHE* cache) {
    cache->running = 0;
    if (cache->flush_thread) {
        WaitForSingleObject(cache->flush_thread, INFINITE);
        CloseHandle(cache->flush_thread);
        cache->flush_thread = NULL;
    }
    // ... free resources (safe now)
}
```

All thread creators now set `cache->flush_thread`:
- `daemon.c:93,160`
- `wizard.c:138`
- `raid_service.c:438`

Callers with redundant waits (`volume_manager.c`, `cleanup.c`, `raid_service.c`) were simplified since `cache_destroy` owns the wait.

---

## B2 — Unchecked CreateEventW

### Pattern
```c
HANDLE evt = CreateEventW(NULL, TRUE, FALSE, NULL);
// evt used without NULL check
```

### Fix
Every `CreateEventW` call now checks for NULL before using the handle:

- **`stripe_engine.c:482`** — `stripe_volume_read()` async read path
- **`stripe_engine.c:535`** — `stripe_volume_write()` write-through path
- **`stripe_engine.c:580`** — `stripe_volume_write()` async write path
- **`ram_cache.c:137`** — `cache_flush_all()` flush I/O path

On failure, previously-created handles are cleaned up and the function returns `false`.

---

## Remaining WinAPI Audit Items (Post-Sprint 9)

| File | Function | API | Status |
|------|----------|-----|--------|
| `bench_io.c:67,89` | bench helpers | `CreateEventW` | Bench code, P3 |
| `stripe_engine.c:453` | `stripe_write_raw` | `WriteFile` | Already checked (`written == length`) |
| `test_common.c:100` | test_disk_create | `WriteFile` | Test code, 0-length write (resets handle) |
| `test_superblock.c:187` | test helper | `WriteFile` | Test code, 0-length write |
