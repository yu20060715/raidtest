# RAIDTEST v1.0 RC4 — Master Engineering Backlog (codename: RAIDV3)

This is the single source of truth for engineering work.
All archived reports are superseded.

---

## Confirmed Bugs

| ID | Priority | Area | Description | Source |
|----|----------|------|-------------|--------|
| B1 | P0 | `storage_common.c` | OVERLAPPED allocated on stack; kernel writes to freed stack memory on async completion | FUTURE_ROADMAP.md S5 |
| B2 | P0 | `fuse_bridge.c` | `parent_dir_exists()` copies unchecked path length into fixed `char[256]` buffer | FUTURE_ROADMAP.md S3 |
| B3 | P0 | `fuse_bridge.c` | `raid_rename()` concatenates path+suffix without bounds check into fixed buffer | FUTURE_ROADMAP.md S3 |
| B4 | P1 | `fuse_bridge.c` | `file_table_lock_init()` has no synchronization; double-checked locking race | FUTURE_ROADMAP.md S4 |
| B5 | P1 | `fuse_bridge.c` | `fuse_unmount()` calls `DeleteCriticalSection` while FUSE callbacks may still execute | FUTURE_ROADMAP.md S4 |
| B6 | P1 | `journal.c` | Journal file writes (`SetFilePointer`+`WriteFile`) have no lock; concurrent flush+FUSE path corrupts entries | FUTURE_ROADMAP.md S6 |
| B7 | P1 | `event_bus.c` | `InitializeCriticalSection` has no matching `DeleteCriticalSection`; resource leak | FUTURE_ROADMAP.md S11 |
| B8 | P1 | `raid_service.c` | 8 `device_get()` call sites dereference without NULL check | FUTURE_ROADMAP.md S2 |
| B9 | P1 | `stripe_engine.c`, `mirror_engine.c`, `journal.c`, `superblock.c`, `cleanup.c` | `vol->disks[i]` accessed ~25+ times without NULL checks on individual disk pointers | FUTURE_ROADMAP.md S2 |
| B14 | P2 | `gui.cpp` | Export diagnostic uses deprecated `GetVersion()`; returns wrong OS build on Windows 10/11 without manifest; cosmetic only | BUG_VERIFICATION.md |

## Resolved Bugs

| ID | Priority | Area | Description | Fix |
|----|----------|------|-------------|-----|
| B10 | P0 | `gui.cpp`, `config.c/h`, `common.h` | `pool_size_mb` derived from `cache_mb` instead of own config field; pool files created with 4096 MB instead of 51200 MB | Added `pool_mb` field to `APP_CONFIG`, default 51200, save/load from JSON, use in `gui.cpp:1644` |
| B11 | P0 | `cleanup.c` | `cleanup_scan_all_drives()` destroys pool files on ALL fixed drives (not just current volume); mount-point guard was dead code | Removed `cleanup_scan_all_drives()`; `cleanup_all()` now calls `cleanup_pool_session()` which operates only on current volume disks |
| B12 | P1 | `mirror_engine.c` | `bytes_read += length` at line 65 is non-atomic; loses updates under concurrent read load | Changed to `InterlockedExchangeAdd64((volatile LONG64*)&vol->bytes_read, length)` |

## Dispositioned

| ID | Priority | Area | Description | Resolution |
|----|----------|------|-------------|------------|
| B13 | P3 | `gui.cpp` | `strtok()` used in worker thread instead of `strtok_s`; INVESTIGATED: all 9 call sites are inside single `worker_thread()`; no other thread calls `strtok`. Latent only — not exploitable in current code. Document in KNOWN_LIMITATIONS.md. | FALSE POSITIVE |

## Confirmed Technical Debt

| ID | Priority | Area | Description | Source |
|----|----------|------|-------------|--------|
| T1 | P0 | `raid_service.c`, `fuse_bridge.c`, `gui.cpp` | 24 raid_service functions and all 14 FUSE callbacks access `g_state` without acquiring `gs_lock()`/`gs_unlock()` | FUTURE_ROADMAP.md S1 |
| T2 | P1 | `tests/` | All superblock tests use hardcoded `C:\RAIDTEST\` path; fail on systems without write access to C: | OPENCODE_TODO.md |
| T3 | P2 | `storage_common.c/h`, engines | I/O functions return `bool` instead of RC error codes; errors silently swallowed | FUTURE_ROADMAP.md S9 |
| T4 | P2 | `cmd_handler.h` | `APP_STATE` defined in CLI header; `cleanup.h`, `wizard.h`, `ui_model.h`, `gui.cpp` include it only for type | FUTURE_ROADMAP.md S10 |
| T5 | P2 | Project | No documented lock ordering between `g_state_cs`, `cache->lock`, `g_eb_cs`; deadlock risk on future changes | FUTURE_ROADMAP.md S12 |
| T6 | P2 | `journal.c` | Journal grows unbounded; no circular buffering or entry trimming | FUTURE_ROADMAP.md S7 |
| T7 | P2 | `fuse_bridge.c` | File table is flat 64-entry array with O(n) lookup; silent overwrite when full | FUTURE_ROADMAP.md S8 |

## Resolved Architecture Work

| ID | Priority | Area | Description | Resolution |
|----|----------|------|-------------|------------|
| A2 | P2 | `ram_cache.c` | Cache flush race: re-dirty blocks missed when FUSE write races with stale flush_buffer | Fixed: re-dirty check after I/O rewinds loop to re-process |
| A3 | P3 | `raid_service.c` | `auto_restore_or_quick()` restore path should handle all disks | Verified: `do_restore()` iterates all disks (no `break`/`disks[0]` bug) |
| A4 | P3 | `src/gui.cpp` | Dead UI panels: residual unreachable surfaces | Removed: `bench_elapsed`, `PerfSample.iops_read/write/cache_hit_pct`; added `rebuild_failed_idx` input |

## Remaining Architecture Work

| ID | Priority | Area | Description | Source |
|----|----------|------|-------------|--------|
| A1 | P2 | `fuse_bridge.c` | Add reference counting to file table; quiesce before `DeleteCriticalSection` | FUTURE_ROADMAP.md S4 |
| A5 | P3 | Project | Version string consistency: banner/GUI/title bar should all match (RC2->RC4 addressed; verify no drift) | OPENCODE_PROGRESS Task 5 |

## Remaining Documentation Work

| ID | Priority | Description |
|----|----------|-------------|
| D1 | P2 | Create `LOCK_ORDER.md` documenting acquisition order: `g_state_cs` -> `cache->lock` -> `g_eb_cs` |
| D2 | P3 | Update `README.md` demo instructions to match current GUI layout and RC4 feature set |
| D3 | P3 | Align `PRODUCT_DESIGN.md` feature claims with actual implemented behavior |

---

## References

- `KNOWN_LIMITATIONS.md` — comprehensive list of design constraints (platform, RAID levels, cache, journal, etc.)
- `FUTURE_ROADMAP.md` — detailed proposals for S1-S12 with cost/risk estimates
- `API.md` — public API surface
- `TEST_PLAN.md` — integration test coverage
