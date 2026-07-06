# STATE_ACCESS_AUDIT.md — SPRINT 10C Architecture Cleanup

**Date:** 2026-07-06
**Scope:** Every direct read/write of the global `APP_STATE g_state` (or `S()`) across `src/`.

**Legend:**
- **R** = Read
- **W** = Write (assignment or mutation)
- **ADDR** = Address taken and passed to another function
- **Encaps.** = Could plausibly be replaced by a getter/setter/function call

---

## Files accessing `g_state` directly

| File | Direct `g_state.x` | `S()->x` | `&g_state` | Total |
|------|-------------------|----------|------------|-------|
| `cmd_handler.c` | 7 | 0 | 0 | 7 |
| `main.c` | 22 | 0 | 4 | 26 |
| `cleanup.c` | 0 | 0 | 1 | 1 |
| `raid_service.c` | 0 | ~155 | 1 | ~156 |
| `ui_model.c` | 12 | 0 | 0 | 12 |
| **Total** | **41** | **~155** | **6** | **~202** |

---

## cmd_handler.c

### `cmd_mount` (line 56)

| Line | Field | R/W | Encaps. |
|------|-------|-----|---------|
| 56 | `g_state.cfg.config.mount_letter` | R | Yes |

### `auto_restore_or_quick` (lines 175-181)

| Line | Field | R/W | Encaps. |
|------|-------|-----|---------|
| 175 | `&g_state.cfg.config` (to `config_load`) | ADDR/W | Maybe |
| 176 | `g_state.cfg.config.disk_count` | R | Yes |
| 179 | `g_state.cfg.config.disk_count` | R | Yes |
| 180 | `&g_state.cfg.config` (local ptr) | ADDR/R | Maybe |
| 181 | `g_state.disk.physical_disks` | R | Yes |

---

## main.c

### `do_restore` (lines 12-40)

| Line | Field | R/W | Encaps. |
|------|-------|-----|---------|
| 13 | `g_state.cfg.config.disk_count` | R | Yes |
| 14 | `g_state.cfg.config.disk_count` | R | Yes |
| 15 | `g_state.disk.physical_disks` | R | Yes |
| 16 | `&g_state.disk.physical_disks` | ADDR/W | Maybe |
| 16 | `&g_state.disk.physical_count` | ADDR/W | Maybe |
| 22 | `g_state.cfg.config.disk_count` | R | Yes |
| 23 | `g_state.cfg.config.disks[i].disk_id` | R | Yes |
| 27 | `g_state.cfg.config.disk_count` | R | Yes |
| 28 | `g_state.cfg.config.disks[i].disk_id` | R | Yes |
| 29 | `g_state.cfg.config.disks[i].pool_mb` | R | Yes |
| 33 | `g_state.cfg.config.cache_mb` | R | Yes |

### `cli_main` (lines 42-94)

| Line | Field | R/W | Encaps. |
|------|-------|-----|---------|
| 55 | `g_state.cfg.config.mount_letter` | R | Yes |
| 57 | `g_state.rt.mounted` | R | Yes |
| 58 | `g_state.vol.volume.mount_point[0]` | R | Yes |
| 60 | `&g_state` (to `wizard_run`) | ADDR/R | Maybe |
| 62 | `&g_state` (to `daemon_start`) | ADDR/R | Maybe |
| 70 | `g_state.cfg.config.disk_count` | R | Yes |
| 71 | `g_state.cfg.config.mount_letter` | R | Yes |
| 81 | `g_state.cfg.config.disk_count` | R | Yes |
| 83 | `g_state.cfg.config.mount_letter` | R | Yes |
| 91 | `&g_state` (to `cleanup_session`) | ADDR/R | Maybe |
| 92 | `&g_state` (to `cleanup_disks`) | ADDR/R | Maybe |

---

## cleanup.c

| Line | Field | R/W | Encaps. |
|------|-------|-----|---------|
| 135 | `&g_state` (local ptr for `cleanup_all`) | ADDR/R | Maybe |

Note: `cleanup.c`'s other functions operate through a passed `APP_STATE*` parameter — they do NOT access `g_state` directly. This is good encapsulation within those functions.

---

## raid_service.c (via `S()` macro)

All ~155 accesses go through `#define S() ((APP_STATE*)&g_state)`.

### `event_log_callback` (lines 17-48)

| Line | Field | R/W | Encaps. |
|------|-------|-----|---------|
| 19 | `S()->rt.appdata_path[0]` | R | Yes |
| 21 | `S()->rt.appdata_path` | R | Yes |

### `require` (lines 50-56)

| Line | Field | R/W | Encaps. |
|------|-------|-----|---------|
| 51 | `S()->rt.state` | R | Yes |
| 52 | `S()->rt.state` | R | Yes |

### `raid_init` (lines 59-83)

| Line | Field | R/W | Encaps. |
|------|-------|-----|---------|
| 62 | `&S()->cfg.config` (to `config_load`) | ADDR/W | Maybe |
| 63 | `S()->cache.cache_mb = S()->cfg.config.cache_mb` | W + R | Yes |
| 64 | `S()->rt.state = STATE_DISCONNECTED` | W | Yes |
| 66 | `S()->rt.appdata_path` (to `GetEnvironmentVariableW`) | ADDR/W | Maybe |

### `raid_cleanup` (lines 85-92)

| Line | Field | R/W | Encaps. |
|------|-------|-----|---------|
| 88 | `S()->rt.state = STATE_DISCONNECTED` | W | Yes |
| 89 | `S()->vol.volume_valid = false` | W | Yes |
| 90 | `S()->cache.cache_on = false` | W | Yes |
| 91 | `S()->rt.mounted = false` | W | Yes |

### `raid_scan` (lines 95-114)

| Line | Field | R/W | Encaps. |
|------|-------|-----|---------|
| 96 | `S()->rt.state = STATE_DISCONNECTED` | W | Yes |
| 102 | `S()->rt.state = STATE_DISCOVERED` | W | Yes |

### `raid_init_pools` (lines 159-290) — 20+ accesses

| Line | Field | R/W | Encaps. |
|------|-------|-----|---------|
| 160 | `S()->rt.state` | R | Yes |
| 161, 165 | `S()->rt.state` | R | Yes |
| 166 | `S()` (to `cleanup_pool_session`) | ADDR/R | Maybe |
| 236 | `S()->disk.disk_count`, `S()->disk.pool_sizes_mb[i]` | R | Yes |
| 259 | `S()->disk.disk_count`, `S()->disk.pool_sizes_mb[i]` | R | Yes |
| 265 | `S()->disk.disk_count = 0` | W | Yes |
| 272, 273 | `S()->disk.disk_count` | R | Yes |
| 274, 275 | `S()->disk.disks[j]` | R | Yes |
| 277 | `S()->disk.disk_count = 0` | W | Yes |
| 280 | `S()->disk.pool_sizes_mb[S()->disk.disk_count] = pair_sizes[i]` | W + R | Yes |
| 281 | `S()->disk.disks[S()->disk.disk_count++] = disk` | W + R | Maybe |
| 283 | `S()->disk.disk_count` | R | Yes |
| 284 | `S()->rt.state = STATE_INITIALIZED` | W | Yes |

### `raid_create` (lines 293-300)

| Line | Field | R/W | Encaps. |
|------|-------|-----|---------|
| 295 | `S()->disk.disk_count` | R | Yes |
| 296 | `&S()->vol.volume`, `S()->disk.disks`, `S()->disk.disk_count` | ADDR/W + R + R | Maybe |
| 297 | `S()->vol.volume_valid = true` | W | Yes |
| 298 | `S()->rt.state = STATE_MOUNTED` | W | Yes |

### `raid_mirror` (lines 302-309)

| Line | Field | R/W | Encaps. |
|------|-------|-----|---------|
| 304 | `S()->disk.disk_count` | R | Yes |
| 305 | `&S()->vol.volume`, `S()->disk.disks`, `S()->disk.disk_count` | ADDR/W + R + R | Maybe |
| 306 | `S()->vol.volume_valid = true` | W | Yes |
| 307 | `S()->rt.state = STATE_MOUNTED` | W | Yes |

### `raid_expand` (lines 311-316)

| Line | Field | R/W | Encaps. |
|------|-------|-----|---------|
| 313 | `&S()->vol.volume`, `S()->disk.physical_disks`, `S()->disk.physical_count` | ADDR/W + R + R | Maybe |

### `raid_rebuild` (lines 318-334)

| Line | Field | R/W | Encaps. |
|------|-------|-----|---------|
| 319-320 | `S()->rt.state` | R | Yes |
| 330-331 | `&S()->vol.volume`, `S()->disk.physical_disks`, `S()->disk.physical_count` | ADDR/W + R + R | Maybe |

### `raid_mount` (lines 337-344)

| Line | Field | R/W | Encaps. |
|------|-------|-----|---------|
| 339 | `S()->vol.volume_valid` | R | Yes |
| 340 | `S()->rt.mounted` | R | Yes |
| 341 | `&S()->vol.volume` | ADDR/W | Maybe |
| 342 | `S()->rt.mounted = true` | W | Yes |

### `raid_unmount` (lines 346-353)

| Line | Field | R/W | Encaps. |
|------|-------|-----|---------|
| 348 | `&S()->vol.volume`, `&S()->cache.cache_on`, `&S()->cache.flush_thread`, `&S()->rt.mounted` | ADDR/W | Maybe |
| 349 | `S()->rt.state = STATE_UNMOUNTED` | W | Yes |
| 350 | `S()->vol.volume_valid = false` | W | Yes |

### `raid_load` (lines 355-379)

| Line | Field | R/W | Encaps. |
|------|-------|-----|---------|
| 356-357 | `S()->rt.state` | R | Yes |
| 368 | `&S()->vol.volume`, `S()->disk.loaded_disks`, `&loaded` | ADDR/W | Maybe |
| 369 | `S()->disk.physical_disks`, `S()->disk.physical_count` | R | Yes |
| 373 | `S()->disk.disk_count = loaded` | W | Yes |
| 375 | `S()->disk.disks[i] = &S()->disk.loaded_disks[i]` | W + R | Maybe |
| 376 | `S()->vol.volume_valid = true` | W | Yes |
| 377 | `S()->rt.state = STATE_MOUNTED` | W | Yes |

### `raid_destroy` (lines 381-392)

| Line | Field | R/W | Encaps. |
|------|-------|-----|---------|
| 382-383 | `S()->rt.state` | R | Yes |
| 387 | `&S()->vol.volume`, `S()->disk.disks`, `S()->disk.disk_count` | ADDR/W + R + R | Maybe |
| 388 | `&S()->cache.cache_on`, `&S()->cache.flush_thread`, `&S()->rt.mounted`, `&S()->rt.state` | ADDR/W | Maybe |
| 389 | `S()->vol.volume_valid = false` | W | Yes |

### `raid_purge` (lines 394-406)

| Line | Field | R/W | Encaps. |
|------|-------|-----|---------|
| 395-396 | `S()->rt.state` | R | Yes |
| 399 | `S()` (to `cleanup_pool_session`) | ADDR/R | Maybe |
| 400 | `S()->rt.state = STATE_DISCOVERED` | W | Yes |
| 401 | `S()->vol.volume_valid = false` | W | Yes |
| 402 | `S()->cache.cache_on = false` | W | Yes |
| 403 | `S()->rt.mounted = false` | W | Yes |

### `raid_cache` (lines 409-444)

| Line | Field | R/W | Encaps. |
|------|-------|-----|---------|
| 412 | `S()->cache.cache_on` | R | Yes |
| 413 | `S()->vol.volume.cache.write_through = !S()->vol.volume.cache.write_through` | R/W | Yes |
| 414 | `S()->vol.volume.cache.write_through` | R | Yes |
| 418 | `S()->cache.cache_on` | R | Yes |
| 419 | `S()->cache.flush_thread = NULL` | W | Yes |
| 420 | `&S()->vol.volume` (to `cleanup_volume_cache`) | ADDR/R | Maybe |
| 421 | `S()->cache.cache_on = false` | W | Yes |
| 426 | `S()->cache.cache_on` | R | Yes |
| 433 | `&S()->vol.volume.cache` (to `cache_init`) | ADDR/W | Maybe |
| 436 | `S()->vol.volume.cache_enabled = true` | W | Yes |
| 437 | `S()->cache.cache_on = true` | W | Yes |
| 438 | `S()->cache.cache_mb = size_mb` | W | Yes |
| 439 | `S()->vol.volume.cache.flush_thread = S()->cache.flush_thread = ...` | W | Maybe |
| 440 | `S()->cache.flush_thread` | R | Yes |

### `raid_info` (lines 447-514)

| Line | Field | R/W | Encaps. |
|------|-------|-----|---------|
| 448 | `S()->rt.state` | R | Yes |
| 465 | `S()->vol.volume_valid` | R | Yes |
| 466 | `&S()->vol.volume` | ADDR/R | Maybe |
| 493 | `S()->cache.cache_mb` | R | Yes |

### `raid_status` (lines 516-556)

| Line | Field | R/W | Encaps. |
|------|-------|-----|---------|
| 517-518 | `S()->rt.state` | R | Yes |
| 521 | `&S()->vol.volume` | ADDR/R | Maybe |
| 528 | `S()->rt.state` | R | Yes |
| 534 | `S()->cache.cache_mb` | R | Yes |

### `raid_map` (lines 558-564)

| Line | Field | R/W | Encaps. |
|------|-------|-----|---------|
| 561 | `S()->vol.volume.virtual_total_bytes` | R | Yes |
| 562 | `&S()->vol.volume` | ADDR/R | Maybe |

### `raid_test` (lines 566-569)

| Line | Field | R/W | Encaps. |
|------|-------|-----|---------|
| 568 | `&S()->vol.volume` | ADDR/R | Maybe |

### `raid_random` (lines 572-583)

| Line | Field | R/W | Encaps. |
|------|-------|-----|---------|
| 581 | `&S()->vol.volume` | ADDR/R | Maybe |

### `raid_check` (lines 599-650)

| Line | Field | R/W | Encaps. |
|------|-------|-----|---------|
| 600-601 | `S()->rt.state` | R | Yes |
| 604 | `&S()->vol.volume` | ADDR/R | Maybe |

### `raid_simulate` (lines 652-690)

| Line | Field | R/W | Encaps. |
|------|-------|-----|---------|
| 653-654 | `S()->rt.state` | R | Yes |
| 660 | `S()->vol.volume.disk_count` | R | Yes |
| 661 | `S()->vol.volume.disks[idx]` | R | Yes |
| 666 | `&S()->vol.volume.healthy_count` (InterlockedDecrement) | ADDR/W | Maybe |
| 667 | `S()->rt.state = STATE_DEGRADED` | W | Yes |
| 673 | `&S()->vol.volume.healthy_count` (InterlockedIncrement) | ADDR/W | Maybe |
| 674 | `S()->vol.volume.healthy_count`, `S()->vol.volume.disk_count` | R | Yes |
| 674 | `S()->rt.state = STATE_MOUNTED` | W | Yes |
| 680 | `&S()->vol.volume.healthy_count` | ADDR/W | Maybe |
| 681 | `S()->rt.state = STATE_DEGRADED` | W | Yes |

### `raid_events` (lines 728-747)

| Line | Field | R/W | Encaps. |
|------|-------|-----|---------|
| 729 | `S()->rt.appdata_path[0]` | R | Yes |
| 731 | `S()->rt.appdata_path` | R | Yes |

### `raid_config_save` (lines 750-767)

| Line | Field | R/W | Encaps. |
|------|-------|-----|---------|
| 751 | `&S()->cfg.config` | ADDR/R | Maybe |
| 762 | `S()->cache.cache_mb` | R | Yes |
| 763 | `S()->vol.volume.mount_point[0]` | R | Yes |

### `raid_config_load` (lines 769-773)

| Line | Field | R/W | Encaps. |
|------|-------|-----|---------|
| 770 | `&S()->cfg.config` (to `config_load`) | ADDR/W | Maybe |

### `raid_wizard` (lines 776-779)

| Line | Field | R/W | Encaps. |
|------|-------|-----|---------|
| 777 | `S()` (to `wizard_run`) | ADDR/R | Maybe |

### `raid_quick` (lines 781-808)

| Line | Field | R/W | Encaps. |
|------|-------|-----|---------|
| 796 | `S()->disk.disk_count`, `S()->disk.pool_sizes_mb[i]` | R | Yes |
| 803 | `S()->cfg.config.mount_letter` | R | Yes |

---

## ui_model.c

All reads, no writes.

### `ui_get_disk_summary` (lines 4-21)

| Line | Field | R/W | Encaps. |
|------|-------|-----|---------|
| 6 | `g_state.disk.physical_disks` | R | Yes |
| 7 | `g_state.disk.physical_count` | R | Yes |
| 10-11 | `g_state.disk.physical_disks[i]` | R | Yes |

### `ui_get_volume_info` (lines 23-49)

| Line | Field | R/W | Encaps. |
|------|-------|-----|---------|
| 25 | `g_state.vol.volume_valid` | R | Yes |
| 26 | `&g_state.vol.volume` | ADDR/R | Maybe |
| 33 | `g_state.rt.mounted` | R | Yes |
| 35 | `g_state.cache.cache_mb` | R | Yes |

### `ui_get_health_summary` (lines 51-58)

| Line | Field | R/W | Encaps. |
|------|-------|-----|---------|
| 53 | `g_state.vol.volume_valid` | R | Yes |
| 54 | `&g_state.vol.volume` | ADDR/R | Maybe |

### `ui_get_state` (lines 60-62)

| Line | Field | R/W | Encaps. |
|------|-------|-----|---------|
| 61 | `g_state.rt.state` | R | Yes |

### `ui_get_state_str` (lines 64-66)

| Line | Field | R/W | Encaps. |
|------|-------|-----|---------|
| 65 | `g_state.rt.state` | R | Yes |

---

## Encapsulation Feasibility Summary

| Access type | Count | Encapsulation |
|-------------|-------|---------------|
| Granular field reads (~140) | ~70% | Yes — trivially replaceable with getter functions |
| Direct field writes (~45) | ~22% | Yes — replaceable with setter functions |
| Address-of-write (ADDR/W) (~15) | ~7% | Maybe — needs wrapper function or API change |
| `&g_state` passed to functions (~6) | ~3% | Maybe — callers could be simplified |

**Key note:** The `S()` macro already provides a single point of indirection. If a future refactoring replaces `S()` with a parameterized accessor, only the macro definition + `&g_state` callers would need to change. However, every `S()->field = value` would still need to become a setter call.
