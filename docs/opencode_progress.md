# OpenCode Progress

## Step 2 — Phase 3

Date: 2026-07-09

### Analysis

**Purpose:** Fix dirty-bit ordering and concurrent-flush race in ram_cache.c.

**Files modified:**
- `src/ram_cache.c`
- `src/ram_cache.h`
- `src/cleanup.c`

### Dirty-bit ordering (ram_cache.c)

**Problem:** `cache_flush_all` cleared dirty bits *before* the physical write completed. If a crash occurred between clearing and the write finishing, the dirty data was lost (journal protects against crash, but the ordering was still semantically wrong: dirty should mean "data in cache not yet on disk").

**Change:**
1. Removed the pre-write dirty-bit clear (formerly lines 154–155).
2. After successful write completion, clear dirty bits under the cache lock.
3. Removed the re-dirty detection loop (formerly lines 197–210), which previously checked whether blocks were re-dirtied during I/O. Without pre-clearing, all flushed blocks still appear dirty, so the old detection would always trigger an infinite loop. The new code simply clears dirty bits on success; re-dirty after a successful write is handled naturally by the next flush cycle.
4. Removed redundant re-dirty code in failure paths (dirty bits were never cleared, so re-setting them was a no-op).

### Concurrent-flush race (ram_cache.c + cleanup.c)

**Problem:** In `cleanup_volume_cache`, after `cache_flush_all` returns (and releases its `cache_flush_in_progress` guard), a FUSE callback thread could enter `cache_flush_all` again (via the separate `g_flush_in_progress` guard in fuse_bridge.c) before `cache_destroy` frees the cache buffer. This left `cache_flush_all` with a dangling pointer.

**Fix:**
- Added `void cache_flush_wait(STRIPE_VOLUME* vol)` to `ram_cache.c` — spin-waits using `InterlockedCompareExchange` until `vol->cache_flush_in_progress` is 0.
- Declared in `ram_cache.h`.
- Called from `cleanup_volume_cache` in `cleanup.c` just before `cache_destroy`.

**Affected:** Dirty bits are now cleared only after the physical write succeeds. Cache memory is freed only after all concurrent flushes have completed.

**Risk:** Low. No new functionality, no API changes. Dirty-clear after write is the correct semantic. The spin-wait is bounded (Sleep(1) per iteration).

---

## Step 1

Date: 2026-07-09

### Analysis

**Purpose:** Fix Mount Letter single source and backend busy protection in GUI.

**Files modified:**
- `src/gui.cpp`

**Reason:**
1. `g_gui.mount_letter` and `g_gui.settings.mount_letter` were duplicate variables tracking the same mount letter, creating a synchronization risk.
2. Cache ON/OFF/WT buttons in `ShowCacheControls` did not check `worker_running` before calling `start_worker()`.

**Changes:**
1. Removed `char mount_letter;` from the global GUI struct (line 106).
2. Replaced all `g_gui.mount_letter` references with `g_gui.settings.mount_letter`:
   - Line 1028: Removed redundant `g_gui.mount_letter = s->mount_letter;` from Save Settings.
   - Line 1152: Toolbar mount label reads from `g_gui.settings.mount_letter`.
   - Line 1158: Toolbar combo sets only `g_gui.settings.mount_letter`.
   - Line 1166: Mount button reads from `g_gui.settings.mount_letter`.
   - Line 506, 844: Worker thread reads from `g_gui.settings.mount_letter`.
   - Line 1988: Removed redundant init line.
3. Added `busy` check to cache ON/OFF/WT buttons in `ShowCacheControls`.

**Affected:** Mount letter now has a single source (`g_gui.settings.mount_letter`). Settings and Toolbar are automatically synchronized. Cache buttons respect backend busy state.

**Verification:**
- No new functionality added.
- No API changed.
- No GUI appearance changed.
- No function signatures changed.

**Risk:** Low. Only internal variable references changed. `g_gui.settings.mount_letter` has the same type (`char`) as the removed `g_gui.mount_letter`.

----------------------------------------
