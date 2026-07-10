# RELEASE_CHECKLIST — RAIDTEST v1.0 RC5

Generated 2026-07-08 after 9 B-fix + cache_init cleanup cycle. Tag: `v1.0-rc5`. Commits: `c482606`, `9e5f9ce`, `81c3550`.

---

## 1. Completed Bug Fixes (10/13)

| ID | Bug | Files Touched | Commit | Tests | Risk |
|----|-----|---------------|--------|-------|------|
| B1 | `stripe_volume_create` missing `raid_level = RAID_LEVEL_STRIPE` | `stripe_engine.c` +1 | `c482606` | 39/39 | None |
| B2 | `mirror_volume_rebuild` `FlushFileBuffers` inside loop | `mirror_engine.c` ~5 lines | `c482606` | 39/39 | Low |
| B3 | `mirror_volume_rebuild` races with concurrent I/O on `disks[]` swap | `mirror_engine.c` +17/-1, `test_mirror.c` +116 (3 new tests) | `9e5f9ce` | 42/42 | Low — healthy flag + rollback, verified by 3 new tests |
| B4 | `stripe_volume_read/write` error-path `CancelIoEx` without NULL-guard on `events[j]` | `stripe_engine.c` +4 | `c482606` | 39/39 | None |
| B5 | Cache flush thread lifecycle: `thread_stop` flag decoupled from `cache->running` | `common.h`, `ram_cache.c`, `cleanup.c` ~8 lines | `c482606` | 39/39 | None |
| B6 | `raid_config_save_locked` pool_bytes truncation (floor → round-up) | `raid_service.c` +1 | `c482606` | 39/39 | Low |
| B7 | `fuse_bridge.c` `file_table_lock_init()` no-op stub → self-initializing CS | `fuse_bridge.c` ~8 lines | `c482606` | 39/39 | Low |
| B9 | GUI checkbox `device_select` called per-toggle, replacing full selection | `gui.cpp` ~15 lines | `c482606` | 39/39 | None |
| B11 | `mirror_volume_write` write-through failure leaves stale cache data | `ram_cache.h`, `ram_cache.c`, `mirror_engine.c` ~16 lines | `c482606` | 39/39 | None |
| B14 | `cache_init` failure path: freed pointers not NULLed → `cache_destroy` double-free + uninitialized `DeleteCriticalSection` | `ram_cache.c` +2 | `81c3550` | 42/42 | High — heap corruption on cleanup after low-memory init failure |

## 2. Outstanding Issues (Not Fixed)

| ID | Priority | Bug | Complexity | Notes |
|----|----------|-----|------------|-------|
| B10 | P1 | `stripe_volume_map_lba` phase scan O(phase_count) per call | Low — cache phase index | Performance, not correctness |
| B8 | P1 | `fuse_bridge.c` `CRITICAL_SECTION` never deleted | Low — use SRWLOCK | Leaks kernel handles on rapid mount/unmount |
| B12 | P1 | `fuse_bridge.c` cache_backpressure synchronous flush in write path | Medium — async defer | Performance under high dirty ratio |
| B13 | P2 | `raid_quick` mount_letter ignored in some config ordering paths | Low | Minor UI issue |

## 3. Test Results

- **Unit tests**: 42/42 PASS (all test suites: stripe, mirror, cache, journal, superblock, normalization)
- **Stress tests**: All PASS (longrun, random_io, concurrent, metadata_corrupt, powerfail)
- **Coverage note**: No WinFsp runtime in current environment — B7, B12 changes are compile-verified only

## 4. Build Status

- **Toolchain**: MinGW GCC (fallback path), compile + link PASS
- **Warnings**: Only pre-existing warnings (ImGui -Warray-bounds, strncpy truncation, `vol->disks` array address). Zero new warnings from B-fix changes.
- **CRLF notes**: `mirror_engine.c` and `ram_cache.h` show git CRLF warnings; no functional impact.

## 5. Remaining Risks

| Risk | Severity | Mitigation |
|------|----------|------------|
| B7: FUSE CS not tested at runtime | Low | Logic is standard double-check init pattern; verified by existing codebase patterns. |
| No WinFsp runtime | Low | Mount/unmount paths cannot be integration-tested in this environment. |
| B12: Sync flush in FUSE write path | Low-Medium | Application under high write pressure may observe latency spikes. Data correctness unaffected. |
| Untracked doc files (12 .md/.json) | None | Documentation only; no impact on build or runtime. |

## 6. Release Decision

**Criteria** — all met:
- [x] Build PASS (zero new warnings)
- [x] 42/42 unit tests PASS
- [x] Stress tests PASS
- [x] All P0 bugs fixed (B3 + B14 resolved)
- [x] All modified files reviewed (11 files, +183/-24 lines across 3 commits)

**Status: RELEASED — v1.0-rc5**
