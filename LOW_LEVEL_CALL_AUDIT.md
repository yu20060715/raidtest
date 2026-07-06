# LOW_LEVEL_CALL_AUDIT.md — SPRINT 10C Architecture Cleanup

**Date:** 2026-07-06
**Scope:** All `.c` / `.h` files under `src/`
**Convention:** `REMAIN` = architecturally acceptable. `SHOULD GO THROUGH MANAGER` = caller should delegate through `volume_manager`, `metadata_manager`, or `device_manager`.

## Allowed files (managers + implementations)

- `metadata_manager.c/.h`
- `volume_manager.c/.h`
- `device_manager.c/.h`
- `planner_engine.c/.h`
- `pool_io.c` (implementation)
- `stripe_engine.c` (implementation)
- `mirror_engine.c` (implementation)
- `ram_cache.c` (implementation)
- `journal.c` (implementation)
- `superblock.c` (implementation)
- `storage_common.c` (implementation)
- `logger.c`, `profiler.c`
- `test_*.c` (test files — exempt)

---

## 1. stripe_engine.h calls outside allowed files

Functions: `stripe_volume_create`, `stripe_volume_expand`, `stripe_volume_destroy`, `stripe_volume_map_lba`, `stripe_volume_read`, `stripe_volume_write`, `stripe_volume_dump_mapping`, `stripe_volume_verify_io`, `stripe_volume_random_test`, `stripe_volume_normalize_ratios`

### bench_io.c

| Line | Function | Assessment |
|------|----------|------------|
| 172 | `stripe_volume_write` | SHOULD GO THROUGH MANAGER |
| 194 | `stripe_volume_read` | SHOULD GO THROUGH MANAGER |

### cleanup.c (utility — noted separately)

| Line | Function | Assessment |
|------|----------|------------|
| 31 | `stripe_volume_destroy` | SHOULD GO THROUGH MANAGER (use `volume_destroy()` or `volume_unmount()`) |

### fuse_bridge.c

| Line | Function | Assessment |
|------|----------|------------|
| 349 | `stripe_volume_read` | SHOULD GO THROUGH MANAGER |
| 421 | `stripe_volume_write` | SHOULD GO THROUGH MANAGER |

### raid_service.c

| Line | Function | Assessment |
|------|----------|------------|
| 562 | `stripe_volume_dump_mapping` | SHOULD GO THROUGH MANAGER |
| 568 | `stripe_volume_verify_io` | SHOULD GO THROUGH MANAGER |
| 581 | `stripe_volume_random_test` | SHOULD GO THROUGH MANAGER |

### Clean files

`cmd_handler.c`, `config.c`, `daemon.c`, `disk_scanner.c`, `event_bus.c`, `gui.cpp`, `main.c`, `ui_model.c`, `wizard.c` — no direct calls to stripe_engine functions.

---

## 2. mirror_engine.h calls outside allowed files

Functions: `mirror_volume_create`, `mirror_volume_read`, `mirror_volume_write`, `mirror_volume_rebuild`

**No occurrences outside allowed files.** All calls are confined to:
- `mirror_engine.c` (implementation — REMAIN)
- `stripe_engine.c` (peer engine — REMAIN, delegates RAID1 I/O)
- `volume_manager.c` (manager — REMAIN)
- `test_mirror.c`, `test_superblock.c` (tests — exempt)

---

## 3. pool_io.h calls outside allowed files

Functions: `pool_file_create`, `pool_file_open`, `pool_file_close`, `pool_file_delete`, `pool_dir_delete`

### cleanup.c (utility — noted separately)

| Line | Function | Assessment |
|------|----------|------------|
| 38 | `pool_file_close` | SHOULD GO THROUGH MANAGER (use `volume_close_pool_file()`) |
| 39 | `pool_file_delete` | SHOULD GO THROUGH MANAGER (use `volume_delete_pool_file()`) |
| 40 | `pool_dir_delete` | SHOULD GO THROUGH MANAGER (no wrapper exists yet; should be added) |

### superblock.c (allowed — implementation)

| Line | Function | Assessment |
|------|----------|------------|
| 350 | `pool_file_open` | REMAIN (superblock needs to open pool files) |
| 354 | `pool_file_close` | REMAIN (cleanup after failed open) |

### volume_manager.c (allowed — manager)

All calls REMAIN (manager legitimately orchestrates pool I/O).

### test_superblock.c (exempt)

---

## 4. ram_cache.h calls outside allowed files

Functions: `cache_init`, `cache_destroy`, `cache_write`, `cache_read`, `cache_flush_all`, `cache_flush_thread`

### bench_io.c

| Line | Function | Assessment |
|------|----------|------------|
| 187 | `cache_flush_all` | SHOULD GO THROUGH MANAGER |

### cleanup.c (utility — noted separately)

| Line | Function | Assessment |
|------|----------|------------|
| 13 | `cache_flush_all` | SHOULD GO THROUGH MANAGER |
| 14 | `cache_destroy` | SHOULD GO THROUGH MANAGER |

### fuse_bridge.c

| Line | Function | Assessment |
|------|----------|------------|
| 334 | `cache_read` | SHOULD GO THROUGH MANAGER |
| 380 | `cache_flush_all` | SHOULD GO THROUGH MANAGER |
| 405 | `cache_write` | SHOULD GO THROUGH MANAGER |
| 446 | `cache_flush_all` | SHOULD GO THROUGH MANAGER |
| 458 | `cache_flush_all` | SHOULD GO THROUGH MANAGER |

### raid_service.c

| Line | Function | Assessment |
|------|----------|------------|
| 433 | `cache_init` | SHOULD GO THROUGH MANAGER (use `volume_cache_enable()`) |
| 439 | `cache_flush_thread` (function pointer to `_beginthreadex`) | SHOULD GO THROUGH MANAGER |

### Peer-level calls that REMAIN

| Caller | Callee | Reason |
|--------|--------|--------|
| `mirror_engine.c` → `cache_read/write` | Cache engine | Mirror peer-calls cache for I/O |
| `stripe_engine.c` → `cache_read/write` | Cache engine | Stripe peer-calls cache for I/O |
| `volume_manager.c` → `cache_init` | Cache engine | Manager is allowed orchestrator |

---

## 5. journal.h calls outside allowed files

Functions: `journal_begin`, `journal_data`, `journal_commit`, `journal_recover_all`

**No violations in non-allowed files.** All calls are confined to:
- `ram_cache.c` (cache flush thread journals each flush — REMAIN)
- `volume_manager.c` (recovery inside `volume_load()` — REMAIN)
- `test_journal.c` (exempt)

---

## 6. superblock.h calls outside allowed files

Functions: `superblock_write`, `superblock_read`, `superblock_read_raw`, `superblock_restore`, `superblock_format_str`

### metadata_manager.c (allowed — manager)

All calls REMAIN (manager wraps superblock).

### raid_service.c

| Line | Function | Assessment |
|------|----------|------------|
| 634 | `superblock_read_raw` | SHOULD GO THROUGH MANAGER (use `metadata_read()`) |

### superblock.c (allowed — implementation)

All calls REMAIN.

---

## 7. storage_common.h calls outside allowed files

Functions: `stripe_read_raw`, `stripe_write_raw`

**No violations.** All calls are confined to:
- `mirror_engine.c` (raw disk I/O for degraded reads/rebuild — REMAIN)
- `test_mirror.c` (exempt)

---

## Summary of Violations

### Calls that SHOULD GO THROUGH MANAGER

| # | File | Line(s) | Function(s) | Recommended Manager |
|---|------|---------|-------------|---------------------|
| 1 | `bench_io.c` | 172, 194 | `stripe_volume_write`, `stripe_volume_read` | `volume_manager` I/O wrappers |
| 2 | `bench_io.c` | 187 | `cache_flush_all` | `volume_manager` cache helper |
| 3 | `fuse_bridge.c` | 334, 349, 380, 405, 421, 446, 458 | `cache_read`, `cache_write`, `cache_flush_all`, `stripe_volume_read`, `stripe_volume_write` | `volume_manager` I/O abstraction |
| 4 | `raid_service.c` | 433, 439 | `cache_init`, `cache_flush_thread` | `volume_cache_enable()` |
| 5 | `raid_service.c` | 562, 568, 581 | `stripe_volume_dump_mapping`, `stripe_volume_verify_io`, `stripe_volume_random_test` | `volume_manager` diagnostic wrappers |
| 6 | `raid_service.c` | 634 | `superblock_read_raw` | `metadata_read()` |

### cleanup.c findings (ambiguous utility)

| # | Line(s) | Function(s) | Recommended Action |
|---|---------|-------------|-------------------|
| 7 | 13-14 | `cache_flush_all`, `cache_destroy` | Use `volume_unmount()` or `volume_cache_disable()` |
| 8 | 31 | `stripe_volume_destroy` | Use `volume_unmount()` or `volume_destroy()` |
| 9 | 38-40 | `pool_file_close`, `pool_file_delete`, `pool_dir_delete` | Use `volume_close_pool_file()` / `volume_delete_pool_file()` |
