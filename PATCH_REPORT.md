# PATCH REPORT

## Summary

Fixed 4 confirmed P0/P1 bugs from FINAL_ARCHITECTURE_AUDIT.md with surgical, minimal changes.
No API, CLI, or GUI behavior changes. All 38 regression tests pass. Full build succeeds.

---

## Fix 1 (P0) — `disk_scan_free()` heap corruption

**File:** `src/disk_scanner.c`
**Severity:** P0 (crash / heap corruption)

**Root cause:** `disk_scan_all()` allocated a contiguous `DISK_INFO*` array via `realloc`, then
set `(*out_disks)[i] = &disks[i]`. `disk_scan_free()` then called `free(disks[0])`, which freed
into the *middle* of the `realloc`'d block — not a valid heap pointer. This corrupts the heap
on the very first call.

**Fix:**
- `disk_scan_all()` now `malloc`s each `(*out_disks)[i]` individually, `memcpy`s the entry,
  then `free`s the temporary `disks` array.
- `disk_scan_free()` now iterates `for (i = 0; i < count; i++) free(disks[i])` instead of
  `free(disks[0])`.

---

## Fix 2 (P1) — Dead CLI route: `select` called `init` instead of `select`

**File:** `src/cmd_handler.c:215`
**Severity:** P1 (wrong behavior)

**Root cause:** The `"select"` command dispatched to `cmd_init()` → `raid_init_pools()`
instead of `raid_select()`. Users running `select 0 1 2` were accidentally creating pool files.

**Fix:**
- Added `static RC cmd_select(...)` wrapper calling `raid_select()`.
- Changed dispatch line to `rc = cmd_select(argc - 1, args + 1)`.

---

## Fix 3 (P1) — Stale cache flush HANDLE after volume create/mirror

**Files:** `src/raid_service.c`, `src/volume_manager.c`
**Severity:** P1 (handle leak / stale reference)

**Root cause:** `volume_create()` and `volume_mirror()` call `cleanup_volume_cache(vol)` which
closes `vol->cache.flush_thread` via `cache_destroy()`, but neither function updates
`state->cache.flush_thread` or `state->cache.cache_on`. After the call, `state` still believes
cache is enabled and holds a closed (stale) HANDLE.

**Fix:**
- In `raid_create()`: set `S()->cache.cache_on = false; S()->cache.flush_thread = NULL;` after
  `volume_create()`.
- In `raid_mirror()`: same after `volume_mirror()`.

---

## Fix 4 (P1) — Missing `gs_lock()` / `gs_unlock()` guards on global state writes

**Files:** `src/cmd_handler.c`, `src/main.c`, `src/daemon.c`
**Severity:** P1 (race condition)

**Root cause:** `g_state` CRITICAL_SECTION was initialized but only used in ~2 of 50+ mutation
sites. Most writes to `g_state` (state transitions, disk lists, volume references) were
unprotected.

**Fix:** Added `gs_lock()` / `gs_unlock()` pairs:
- `cmd_process()` in `cmd_handler.c` — wraps every CLI command dispatch.
- `cmd_interactive()` in `cmd_handler.c` — wraps final `raid_cleanup()`.
- `do_restore()` in `main.c` — wraps the full config-restore sequence (7 return paths).
- `--wizard` path in `main.c` — wraps `wizard_run(&g_state)`.
- `daemon_run()` in `daemon.c` — wraps `daemon_load_volume()` + mount.
- `daemon_main()` in `daemon.c` — wraps `daemon_load_volume()` + mount.

---

## Verification

| Check | Result |
|---|---|
| `gcc -Wall -O2` all 5 modified files | Clean (pre-existing trigraph warnings only) |
| `_build.sh` full build (all C + link) | Build OK |
| `_check.sh` (stricter subset) | All passed |
| `raidtest_tests.exe` (38 regression tests) | **38/38 passed** |

## Files Touched

| File | Lines Changed | Nature |
|---|---|---|
| `src/disk_scanner.c` | +14 / -3 | P0: ownership rewrite |
| `src/cmd_handler.c` | +6 / -2 | P1: CLI route + locks |
| `src/raid_service.c` | +4 / -0 | P1: cache handle cleanup |
| `src/main.c` | +10 / -6 | P1: g_state locks |
| `src/daemon.c` | +6 / -1 | P1: g_state locks |
