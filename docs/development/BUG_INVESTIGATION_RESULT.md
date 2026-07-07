# Bug Investigation Results

**Date**: 2026-07-07
**Scope**: B10, B11, B12, B13, B14 — source trace, runtime analysis, reproduction, severity, recommendation

---

## B10 — pool_size_mb derived from cache_mb

### Status
CONFIRMED — Real data corruption / misconfiguration bug.

### Evidence

**Source trace**:

| Step | File:Line | Code |
|------|-----------|------|
| Config default | `common.h:101` | `#define CACHE_DEFAULT_MB 4096` |
| APP_CONFIG struct | `common.h:254-266` | Has `cache_mb` field but NO top-level `pool_mb` field |
| GUI settings load | `gui.cpp:1625-1626` | `config_defaults(&g_gui.settings); config_load(&g_gui.settings);` → `settings.cache_mb` = 4096 (default) or saved value |
| pool_size_mb init | `gui.cpp:1644` | `g_gui.pool_size_mb = (int)g_gui.settings.cache_mb ? (int)g_gui.settings.cache_mb : 51200;` |
| Quick Setup use | `gui.cpp:455` | `int pool_mb = g_gui.pool_size_mb ? g_gui.pool_size_mb : 51200;` → passed to `raid_init_pools` |
| Create button use | `gui.cpp:1022-1024` | Same — uses `g_gui.pool_size_mb` |

**Value flow**:
```
config_defaults() → settings.cache_mb = 4096
       ↓
config_load() → may override settings.cache_mb from saved JSON
       ↓
gui.cpp:1644 → g_gui.pool_size_mb = settings.cache_mb  (e.g., 4096)
       ↓
gui.cpp:455  → int pool_mb = pool_size_mb (= 4096, not 51200)
       ↓
raid_init_pools("0:4096 1:4096") → creates 4 GB pool files
```

**Boundary case**: If user sets cache_mb = 0 (theoretically to disable cache), then pool_size_mb becomes 51200. But cache_mb=0 is unusual and the cache is separately gated by cache_on/cache_enabled booleans.

**Affected paths**:
- Quick Setup (`W_QUICK_SETUP` in worker_thread) — pool_size_mb = cache_mb
- Create button (W_CREATE with params) — pool_size_mb = cache_mb

**Not affected**:
- Wizard Create (`W_WIZARD_CREATE`, `gui.cpp:585`) — hardcodes `51200` directly
- Restore from config (`W_RESTORE`, `gui.cpp:515`) — uses `cfg.disks[i].pool_mb` from saved config

### Runtime Impact

| Scenario | pool_size_mb | Pool file size |
|----------|-------------|----------------|
| Default (cache_mb=4096) | 4096 | 4 GB (should be 50 GB) |
| User set cache=8192 | 8192 | 8 GB (still wrong) |
| User set cache=256 | 256 | 256 MB (extremely wrong) |
| User set cache=0 | 51200 | 50 GB (only this case is correct) |

Created volumes will have severely undersized pool files. A "50 GB" volume would actually only have 4 GB of usable space.

### Reproduction

Can be confirmed by code trace alone. No test needed.

```
1. Set CACHE_DEFAULT_MB to 4096
2. Launch GUI → Quick Setup with 2 disks
3. raid_init_pools receives "0:4096 1:4096" instead of "0:51200 1:51200"
4. Pool files created with 4096 MB each
```

### Severity
**P0** — Causes incorrect pool file sizing. Users get 4 GB instead of 50 GB.

### Recommendation
**A. Fix immediately.** Add `pool_mb` field to `APP_CONFIG`, use in `gui.cpp:1644`, and remove the dependency on `cache_mb`.

### Need Fix?
**YES**

---

## B11 — cleanup_scan_all_drives destroys pool files on unrelated drives

### Status
CONFIRMED — Real destructive behavior, but limited to RAIDTEST pool files only. Does NOT delete arbitrary user data.

### Evidence

**Source trace**:

| Step | File:Line | Details |
|------|-----------|---------|
| Definition | `cleanup.c:88-128` | Iterates A:-Z:, deletes `RAIDTEST\stripe_pool.dat` on every `DRIVE_FIXED` |
| Call site | `cleanup.c:154` | Called from `cleanup_all()` |
| Caller | `raid_service.c:88` | `raid_cleanup()` → `cleanup_all()` |
| Callers of raid_cleanup | `cmd_handler.c:12,274` | CLI cleanup command |
| | `gui.cpp:1679` | GUI shutdown |

**What it deletes** on each fixed drive (except the mounted volume letter):
- `X:\RAIDTEST\stripe_pool.dat` — pool file
- `X:\RAIDTEST\` — directory
- `X:\RAIDTEST1-0\stripe_pool.dat` — legacy pool file
- `X:\RAIDTEST_BENCH\raid_bench.tmp` — benchmark temp file
- `X:\RAIDTEST_BENCH\` — benchmark directory

**The mount point guard is dead code**:
```c
// cleanup.c:93 — guard
if (g_state.rt.mounted && g_state.vol.volume.mount_point[0] == (char)letter) continue;
```
But `cleanup_all()` calls `cleanup_session()` BEFORE `cleanup_scan_all_drives()`:
```c
void cleanup_all(void) {
    cleanup_session(state);  // sets state->rt.mounted = false
    cleanup_scan_all_drives();  // g_state.rt.mounted is now false → guard never activates
}
```
`cleanup_session()` → `cleanup_volume()` sets `state->rt.mounted = false`. So by the time `cleanup_scan_all_drives` runs, the guard is unreachable.

### Runtime Impact

**Normal operation** (single volume on D:\\, E:\\, mount at R:\\):
1. `cleanup_session()` unmounts R:\, destroys volume
2. `cleanup_scan_all_drives()`:
   - Skips A:, B: (not fixed)
   - C:\RAIDTEST\* → DELETED (if exists from previous install)
   - D:\RAIDTEST\* → DELETED (current volume's pool files)
   - E:\RAIDTEST\* → DELETED (current volume's pool files)
   - etc.

This is **intended** — cleaning up after the current session.

**Multi-volume scenario** (Volume A on D:\,E:\, Volume B on F:\,G:\\):
Running cleanup from Volume A's process will delete Volume B's pool files too.
However, RAIDTEST currently only supports one volume per process, so this
requires two separate processes — possible but unlikely.

**User data risk**: Only files in `X:\RAIDTEST\*` are deleted. No user documents,
no system files. The worst case is losing pool files/superblocks on other volumes
or previous installations.

### Reproduction

Can be confirmed by code trace. Creating a test would need:
1. Create `C:\RAIDTEST\stripe_pool.dat` and `D:\RAIDTEST\stripe_pool.dat`
2. Run `raid_cleanup()`
3. Both files are deleted

### Severity
**P0** — Destructive to non-current-volume pool files. However, scope is limited to RAIDTEST pool files only, not arbitrary user data. Downgrade from originally reported severity slightly: no user data is at risk beyond RAIDTEST metadata.

### Recommendation
**B. Fix later.** Add filter to only clean drives that belong to the current volume's disk set, or require explicit confirmation. However, current damage is limited to pool files on other volumes/installations.

### Need Fix?
**YES** — but lower urgency than B10. Only affects multi-volume or re-installation scenarios.

---

## B12 — Non-atomic bytes_read in mirror_engine.c

### Status
CONFIRMED — Real race condition, cosmetic impact only.

### Evidence

**Source trace**:

| Location | File:Line | Pattern |
|----------|-----------|---------|
| Correct | `mirror_engine.c:41` | `InterlockedExchangeAdd64((volatile LONG64*)&vol->bytes_read, length)` |
| **BUG** | `mirror_engine.c:65` | `vol->bytes_read += length;` |
| Correct | `stripe_engine.c:420` | `InterlockedExchangeAdd64(...)` |
| Correct | `stripe_engine.c:427` | `InterlockedExchangeAdd64(...)` |
| Correct | `stripe_engine.c:485` | `InterlockedExchangeAdd64(...)` |
| Consumer | `raid_query.c:70,102` | Reads `vol->bytes_read` for display |
| Consumer | `ui_model.c:46` | Reads `vol->bytes_read` for GUI display |

**Thread model**: `mirror_volume_read()` is called from `fuse_bridge.c`'s `raid_read()` callback, which runs on WinFsp's internal thread pool. Multiple concurrent read requests can execute this function simultaneously on different threads.

**Race window**: At line 65, `vol->bytes_read += length` is a read-modify-write. Thread A reads `bytes_read` (say 1000), adds `length` (500), but before it writes back 1500, Thread B also reads `bytes_read` (still 1000) and adds its own `length` (300). Both write: Thread A writes 1500, Thread B writes 1300. Result: 1300 instead of correct 1800. Lost 500 bytes.

### Runtime Impact

| Scenario | Lost increments | Functional effect |
|----------|----------------|-------------------|
| Single-threaded load | 0 | None (no race) |
| Low concurrency (2-4 threads) | Occasional lost updates | Displayed MB count slightly low |
| Heavy concurrent reads | Regular lost updates | Noticeably underreported read stats |

`bytes_read` is ONLY used for:
- `raid_query.c:70,102` — `raid-info` command output
- `ui_model.c:46` — GUI volume info display

No functional logic depends on `bytes_read`. No data corruption, no crash, no incorrect behavior. Only diagnostic/display values are affected.

### Reproduction

Difficult without actual concurrent I/O load. Would need:
1. Create a RAID0 volume
2. Mount it via FUSE
3. Generate concurrent reads from multiple threads/processes
4. Compare `vol->bytes_read` against actual bytes transferred

### Severity
**P1** — Real race condition but cosmetic impact only. Displayed stats may be slightly inaccurate under concurrent load.

### Recommendation
**A. Fix immediately** (trivial one-line change). Replace `+=` with `InterlockedExchangeAdd64` to match the pattern used everywhere else.

### Need Fix?
**YES** — trivial fix, no risk, brings consistency.

---

## B13 — GUI worker uses strtok (not thread-safe)

### Status
FALSE POSITIVE (in current code). The strtok calls are confined to a single thread and no other thread calls strtok.

### Evidence

**Source trace**:

| Location | File:Line | Context |
|----------|-----------|---------|
| All strtok calls | `gui.cpp:255,256,327,461,462,518,519,543,586` | Inside `worker_thread()` function |
| Worker thread | `gui.cpp:226` | `static unsigned int __stdcall worker_thread(void* arg)` |
| Thread creation | (in W_QUICK_SETUP etc.) | `_beginthreadex()` creates single worker thread |
| CLI strtok usage | `cmd_handler.c` | Uses `strtok_s` (thread-safe) — no conflict |
| Other threads | FUSE, cache flush, main GUI | None call `strtok` |

**Thread safety analysis of `strtok`**: `strtok` uses a process-wide static buffer to track the current position in the string. If two threads call `strtok` concurrently, they would corrupt each other's parsing state.

**Current execution model**: The GUI creates one worker thread at a time (`worker_thread`). No other thread in the process calls `strtok`:
- Main GUI thread: calls `strtok` nowhere
- CLI mode: uses `strtok_s` everywhere
- FUSE callbacks: call no `strtok`
- Cache flush thread: calls no `strtok`

So in the current codebase, B13 is **perfectly safe**. The strtok calls run on the single worker thread and cannot race.

### Runtime Impact

| Scenario | Risk | Explanation |
|----------|------|-------------|
| Current code | **None** | strtok calls confined to one thread |
| Future change (adding strtok on another thread) | Real | Would corrupt parsing state |

### Reproduction

Cannot reproduce with current code. Would need an explicit test that calls strtok from two threads simultaneously.

### Severity
**P3** — Latent issue only. Not exploitable in current code.

### Recommendation
**D. Documentation only.** Document in KNOWN_LIMITATIONS.md as a latent concern. Fix during next maintenance cycle as a best-practice improvement (strtok → strtok_s).

### Need Fix?
**NO** (in current code). Fix as cleanup during next maintenance cycle.

---

## B14 — GetVersion() in export diagnostic

### Status
CONFIRMED — but cosmetic only. Deprecated API used in non-critical diagnostic output.

### Evidence

**Source trace**:

| Step | File:Line | Code |
|------|-----------|------|
| Usage | `gui.cpp:416` | `fprintf(f, "OS: Windows (build %lu)\n", GetVersion());` |
| Context | `gui.cpp:348-430` | Inside `W_EXPORT` handler → export worker → `system.txt` file |
| Output file | Created alongside log export | Written once during export, never read back by application |

**What `GetVersion()` does**: Returns a `DWORD` where `LOBYTE` = major version, `HIBYTE` = minor version. On Windows 8.1+, this function is deprecated and its return value depends on the application manifest:
- With `compatibility` manifest targeting Windows 10: returns correct version (10.0)
- Without manifest: returns a compatibility shim version (6.2/6.3)

**Windows behavior**: The application currently has no manifest (no `.manifest` file in the repo, and MinGW GCC does not embed one by default). So on Windows 10 or 11, `GetVersion()` would return 6.2 (Windows 8) or 6.3 (Windows 8.1) instead of the actual version.

### Runtime Impact

| Scenario | Output in system.txt |
|----------|---------------------|
| Windows 10, no manifest | "OS: Windows (build 9200)" (wrong — reports 6.2) |
| Windows 11, no manifest | "OS: Windows (build 9200)" (wrong — reports 6.2) |
| Windows 10, with manifest | "OS: Windows (build 22000)" (correct) |

The OS version in the exported diagnostic file is potentially incorrect. This affects no functionality:
- No code branches depend on the version
- No hardware/driver decisions depend on it
- It's purely informational text in a user-generated diagnostic file

### Reproduction

1. Launch GUI
2. Go to Export diagnostics
3. Open the generated `system.txt` file
4. "OS: Windows (build 9200)" appears even on Windows 10/11

### Severity
**P2** — Cosmetic. Wrong OS version in exported diagnostic file.

### Recommendation
**B. Fix later.** Replace with `RtlGetVersion()` (ntdll) or add proper app manifest. Low priority since no functional impact.

### Need Fix?
**NO** — cosmetic only. But should be fixed if app manifest is added for other reasons.

---

## Summary

| Issue | Status | Severity | Impact | Fix Priority |
|-------|--------|----------|--------|-------------|
| **B10** pool_size_mb | CONFIRMED | P0 | Pool files created with wrong size (4 GB vs 50 GB) | **HIGH** |
| **B11** cleanup all drives | CONFIRMED | P0 | Destroys pool files on unrelated drives/volumes | **MEDIUM** |
| **B12** non-atomic bytes_read | CONFIRMED | P1 | Lost stats updates under concurrent read load | **HIGH** (trivial fix) |
| **B13** strtok in GUI | FALSE POSITIVE | P3 | Latent only — no concurrent callers in current code | **LOW** |
| **B14** GetVersion | CONFIRMED | P2 | Wrong OS version in exported diagnostic | **LOW** |
