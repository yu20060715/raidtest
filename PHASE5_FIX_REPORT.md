# Phase 5 — Architecture/Integration Fix Report

## Files Changed
- `src/main.c` — Rewrote `do_restore()` (P0)
- `src/common.h` — Added `volatile` to `bytes_written`, `bytes_read` (P1)

---

## P0-1: `do_restore()` ignores state machine — restore completely broken

**Severity:** P0  
**File:** `src/main.c:10-38`  
**Root cause:** `cli_main()` → `cmd_init_all()` → `raid_init()` sets `STATE_DISCONNECTED`. Then `do_restore()` called `cmd_process("select ...")` which dispatched to `cmd_init()` → `raid_init_pools()`, which requires `STATE_DISCOVERED` or `STATE_UNMOUNTED`. Since state was `DISCONNECTED`, every command silently failed with `RC_ERR_INVALID_STATE`. Additionally, `device_manager(g_disks)` was never populated, so disk ID validation in `raid_init_pools()` also failed (`device_get_count() == 0` → all IDs rejected).

The `do_restore()` function also had a secondary problem: the "select" command mapped to `raid_init_pools()` (non-colon, default-size path), which created pool files at `POOL_SIZE_DEFAULT_MB` (50GB), then the subsequent "init" command (colon format, config-specified sizes) would fail because state was already `STATE_INITIALIZED`.

**Affected paths:**
- `--auto <drive_letter>` e.g. `raidtest --auto G`
- `--cli` with existing config
- `raidtest.exe` with no args + existing config
- Legacy JSON-based restore fallback path

**Verification:**
1. State is `STATE_DISCONNECTED` after `cmd_init_all()` → `raid_init()` (`raid_service.c:62`)
2. `do_restore()` calls `cmd_process("select 0 1 ...")` → `raid_init_pools()` checks `S()->rt.state != STATE_DISCOVERED` → fails (`raid_service.c:159`)
3. Error logged but `do_restore()` ignores return values (`main.c:22-38` old)
4. All subsequent init/create/cache/mount commands also fail

**Fix:** Replaced entire `do_restore()` body to call `raid_*` functions directly:
1. `raid_scan()` — advances state to `STATE_DISCOVERED`, populates `device_manager`
2. Build `id:size` args from config, call `raid_mapdrive()` for each disk
3. `raid_init_pools()` — creates pool files at config-specified sizes, sets `STATE_INITIALIZED`
4. `raid_create()` — creates volume, sets `STATE_MOUNTED`
5. `raid_cache()` — enables cache if `cfg.cache_mb > 0`
6. `raid_mount()` — mounts via FUSE

Added `#include "raid_service.h"` to `main.c` for direct `raid_*` access.

---

## P1-1: Non-atomic `bytes_written` / `bytes_read` from FUSE thread

**Severity:** P1  
**File:** `src/common.h:267-268`  
**Root cause:** `STRIPE_VOLUME.bytes_written` and `bytes_read` are `uint64_t` fields updated from both the FUSE thread (`fuse_bridge.c:408,418`) and the main/CLI thread (`stripe_engine.c:483,613`). No `volatile` qualifier and no atomic guards. While aligned 64-bit reads/writes are usually atomic on x64, the lack of `volatile` allows the compiler to cache values or reorder accesses, potentially producing stale statistics or, in theory, torn reads if the compiler emits a multi-instruction load/store.

**Verification:**
- `fuse_bridge.c:408`: `vol->bytes_written += cache_len;`
- `fuse_bridge.c:418`: `vol->bytes_written` updated inside `stripe_volume_write`
- `stripe_engine.c:483,613`: `vol->bytes_read += ...` / `vol->bytes_written += ...`
- `raid_service.c:500-502`: Read by `raid_info()` for display

**Fix:** Added `volatile` qualifier to both fields to prevent compiler optimizations that could cache or reorder the multi-threaded accesses.
