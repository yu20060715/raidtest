# Bug Verification Report — Phase 2

**Date**: 2026-07-07
**Scope**: Independent source scan for P0/P1 issues NOT in MASTER_BACKLOG.md
**Method**: Manual code review of all 32 source files

---

## Summary

| Severity | Previously Known | Newly Found |
|----------|-----------------|-------------|
| P0       | 3 (B1,B2,B3)    | 2 (B10,B11) |
| P1       | 6 (B4-B9)       | 3 (B12,B13,B14) |

---

## 1. Confirmation of Known Bugs

### B1 — P0: OVERLAPPED on stack in storage_common.c

**Status**: CONFIRMED (partially mitigated)

`stripe_read_raw` (line 27) and `stripe_write_raw` (line 43) allocate
`OVERLAPPED ov = {0}` on the stack, then pass it to `ReadFile`/`WriteFile`.
If the I/O completes asynchronously (`ERROR_IO_PENDING`), the kernel writes
to the OVERLAPPED after the function returns.

**Mitigation**: Both functions call `async_io_wait()` → `GetOverlappedResult(..., TRUE)`
which blocks until completion. So the stack OVERLAPPED is safe in the current
code because the stack frame is still alive during the wait. This is a **latent**
P0 — any future change that makes these truly async (e.g., fire-and-forget)
will immediately cause use-after-free.

### B2 — P0: parent_dir_exists() buffer in fuse_bridge.c

**Status**: CONFIRMED (partially mitigated)

Line 106: `char parent[256]; strncpy(parent, path, plen);` — plen is checked
at line 105 (`plen >= 256` → false), so plen max is 255. With `strncpy`
copying at most `plen` bytes (≤255) into a 256-byte buffer, the null
terminator `parent[plen] = 0` at line 108 is safe. The bounds check was **added
after the original report** — the original code lacked the `>= 256` guard.

### B3 — P0: raid_rename() concat in fuse_bridge.c

**Status**: CONFIRMED (partially mitigated)

Line 265 checks `strlen(src) > 254` with a 256-buffer (`wsrc/wdst`).
Line 282-283 re-uses wsrc for `wcscat(wsrc, L"/")` after checking
`slen >= 255`. Safe in current code.

### B4 — P1: file_table_lock_init() race in fuse_bridge.c

**Status**: CONFIRMED (function is now a no-op)

`file_table_lock_init()` at line 40-44 is an empty stub. The comment says
it should be called from single-threaded context. The actual init happens
in `fuse_mount_volume()` at line 541. The stub exists for backward compat
but no longer provides any protection against the double-checked locking
pattern at all the call sites (lines 79, 91, 137, 163, 186, etc.).

### B5 — P1: DeleteCriticalSection race in fuse_bridge.c

**Status**: CONFIRMED — `fuse_unmount_volume()` line 609-614 explicitly
skips DeleteCriticalSection because WinFsp worker threads may outlive
the unmount call. The CS is leaked; OS reclaims on process exit.

### B6 — P1: Journal write lock in journal.c

**Status**: PARTIALLY RESOLVED

The journal functions (`journal_begin`, `journal_data`, `journal_commit`)
all acquire `g_journal_cs`. However, `journal_write_entry()` (called from
begin/commit) re-opens and closes the journal file each time — no file
handle is held open across calls. The race described (concurrent flush+FUSE
path corrupting entries) appears to be mitigated by per-function locking.

### B7 — P1: Missing DeleteCriticalSection in event_bus.c

**Status**: CONFIRMED — `g_eb_cs` initialized at line 29, never deleted.
No cleanup function exists for the event bus. `event_bus_init` is idempotent
(called once from `raid_init`). The CS is leaked; OS reclaims on exit.

### B8 — P1: device_get() NULL dereferences in raid_service.c

**Status**: MOSTLY RESOLVED

All 8 device_get() call sites I checked now have NULL checks:
- `raid_service.c:109` — `if (!d || ...)` 
- `raid_service.c:147` — `if (!d)`
- `raid_service.c:182-183` — `if (!d)`
- `raid_service.c:238-239` — `if (!d)`
- `raid_service.c:274-275` — `if (!disk)`
- `raid_service.c:626` — `if (d && ...)`
- `gui.cpp:385-386` — `if (!d) continue;`
- `gui.cpp:1082-1083` — `if (!d) continue;`

### B9 — P1: vol->disks[i] NULL checks missing

**Status**: CONFIRMED (partially mitigated by CHECK_DISKS macro)

CHECK_DISKS macro (common.h:131-136) validates the volume and all disk
pointers at the start of public engine functions. However, internal
paths like `cache_flush_all()` (ram_cache.c:152), `mirror_volume_read()`
internals, and `stripe_volume_map_lba()` → `map_single_byte()` access
`vol->disks[entries[e].disk_index]` at line 152 without re-validating
between LBA mapping and disk I/O.

---

## 2. Newly Found Bugs

### B10 — P0: pool_size_mb derived from cache_mb in GUI

| Field | Value |
|-------|-------|
| File | `gui.cpp:1644` |
| Code | `g_gui.pool_size_mb = (int)g_gui.settings.cache_mb ? (int)g_gui.settings.cache_mb : 51200;` |
| Impact | **Pool files created with wrong size** |

The GUI initializes `pool_size_mb` from the `cache_mb` config field instead
of from a separate pool size setting. Since `cfg.cache_mb` defaults to 4096,
all pool creation through the GUI will allocate **4096 MB (4 GB) per disk**
instead of the intended 51200 MB (50 GB). This affects:
- Quick Setup (`W_QUICK_SETUP`, line 455-459)
- Create button (`W_CREATE`, line 1022-1024)
- Wizard Create (`W_WIZARD_CREATE`, line 585)

**Root cause**: The `APP_CONFIG` struct has no `pool_mb` field; the GUI
reuses `cache_mb` as a proxy for pool size.

**Reproduction**: Launch GUI → Quick Setup → pools created with 4 GB
instead of 50 GB.

### B11 — P0: cleanup_scan_all_drives destroys ALL pool files

| Field | Value |
|-------|-------|
| File | `cleanup.c:88-128` |
| Code | Iterates `A:` to `Z:`, deletes `stripe_pool.dat` on every fixed drive |
| Called from | `cleanup_all()` → `raid_cleanup()` → atexit |
| Impact | **Data loss on all RAIDTEST volumes** |

`cleanup_scan_all_drives()` scans all 26 drive letters, checks for
`DRIVE_FIXED`, and deletes `stripe_pool.dat` + `RAIDTEST\` directory
on EVERY drive it finds, with no filtering by the current volume's
disk set.

Call chain:
1. `raid_cleanup()` (line 87) called atexit
2. → `cleanup_all()` (line 150)
3. → `cleanup_session()` (line 152) — properly cleans current volume
4. → `cleanup_scan_all_drives()` (line 154) — **deletes ALL pool files**

If two RAIDTEST volumes exist on different disk sets, cleaning up one
will destroy the other's pool files and superblocks.

### B12 — P1: Non-atomic bytes_read update in mirror_engine.c

| Field | Value |
|-------|-------|
| File | `mirror_engine.c:65` |
| Code | `vol->bytes_read += length;` |
| Pattern elsewhere | `InterlockedExchangeAdd64((volatile LONG64*)&vol->bytes_written, length)` |

`bytes_read` is `volatile uint64_t` but uses plain `+=` instead of
`InterlockedExchangeAdd64`. The `volatile` qualifier prevents compiler
reordering but does NOT make read-modify-write atomic on x64.
From the FUSE callback thread pool, concurrent reads can race to update
this counter, causing lost increments.

Four other sites use the atomic pattern correctly:
- `fuse_bridge.c:428` — bytes_read (correct)
- `fuse_bridge.c:429` — bytes_written (correct)
- `stripe_engine.c:420` — bytes_read (correct)
- `stripe_engine.c:485` — bytes_read (correct)

### B13 — P1: GUI worker uses strtok (not thread-safe)

| Field | Value |
|-------|-------|
| File | `gui.cpp` — multiple lines |
| Code | `strtok(buf, " ")` at lines 255, 327, 461-462, 518-519, 543, 586 |
| Fixed version | `strtok_s` (used correctly in C files like cmd_handler.c) |

The GUI worker thread uses the non-thread-safe `strtok()` instead of
`strtok_s()`. While the GUI worker is the only thread likely calling
strtok simultaneously, this is still a latent concurrency bug if any
future code calls strtok from another thread (e.g., an event callback).

### B14 — P1: Export diagnostic uses deprecated GetVersion()

| Field | Value |
|-------|-------|
| File | `gui.cpp:416` |
| Code | `fprintf(f, "OS: Windows (build %lu)\n", GetVersion());` |
| Issue | `GetVersion()` is deprecated since Windows 8.1; results depend on app manifest |

The exported system information may show incorrect OS version if the
application manifest doesn't specify Windows 10 compatibility.

---

## 3. Previously Reported Issues Found Safe

| Bug | Claim | Verdict |
|-----|-------|---------|
| B4 double-checked lock | Race in file_table_lock_init | Function is no-op; CS init happens in single-threaded fuse_mount_volume |
| B6 journal no lock | Concurrent flush corrupts entries | g_journal_cs acquired in all journal write paths |
| B8 8 device_get sites | NULL dereference | All call sites now have NULL checks |

## 4. Recommendations

1. **B10 (P0)**: Add a `pool_mb` field to `APP_CONFIG` and use it in
   `gui.cpp:1644` instead of reusing `cache_mb`.
2. **B11 (P0)**: Either remove `cleanup_scan_all_drives()` or add a
   guard that only deletes pool files for disks that belong to the
   currently active volume.
3. **B12 (P1)**: Replace `+=` with `InterlockedExchangeAdd64` on
   `vol->bytes_read` in `mirror_engine.c:65`.
4. **B13 (P1)**: Replace all `strtok` calls in `gui.cpp` with `strtok_s`.
5. **B14 (P1)**: Replace `GetVersion()` with `RtlGetVersion()` or
   `VerifyVersionInfo()`.
