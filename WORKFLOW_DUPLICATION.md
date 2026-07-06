# WORKFLOW_DUPLICATION.md — SPRINT 10C Architecture Cleanup

**Date:** 2026-07-06
**Files audited:** `raid_service.c`, `daemon.c`, `wizard.c`, `cmd_handler.c`
**Classification:** Safe | Needs Refactor | Intentional

---

## 1. SCAN: `disk_scan_all` → `volume_create_pool_file` → `volume_create` / `volume_load`

### Occurrences

| File | Line(s) | Sequence |
|------|---------|----------|
| `wizard.c` | 43, 103-108, 113 | `disk_scan_all()` → `volume_create_pool_file()` loop → `volume_create()` |
| `daemon.c` | 100, 112-116, 128 | `disk_scan_all()` → `volume_create_pool_file()` loop → `volume_create()` (legacy JSON fallback) |
| `raid_service.c` | 95-113, 271, 293-299 | `raid_scan()` (uses `device_refresh()` not `disk_scan_all()`) → `raid_init_pools()` calls `volume_create_pool_file()` → `raid_create()` calls `volume_create()` |
| `raid_service.c` | 355-379 | `raid_load()` → `volume_load()` (no scan/pool-create step) |

**Classification: Intentional** — Different scan APIs (device_refresh vs disk_scan_all); legacy vs modern path.

---

## 2. RECOVER: `journal_recover_all` → volume state restoration

### Occurrences

| File | Line | Call site |
|------|------|-----------|
| `volume_manager.c` | 69 | Inside `volume_load()` — single centralized call |

All `volume_load()` callers inherit recovery implicitly: `raid_service.c:368`, `daemon.c:68`.

**Classification: Safe** — Correctly centralized in `volume_load()`.

---

## 3. CACHE INIT: `cache_init` → create `cache_flush_thread`

### Occurrences

| File | Line(s) | Method |
|------|---------|--------|
| `volume_manager.c` | 116, 120 | **Centralized** in `volume_cache_enable()` |
| `raid_service.c` | 433, 439 | **Inline** in `raid_cache()` — duplicates `volume_cache_enable()` logic |
| `daemon.c` | 83-84, 136-137 | Uses `volume_cache_enable()` (correct) |
| `wizard.c` | 125-128 | Uses `volume_cache_enable()` (correct) |

### Detail — inline duplication at `raid_service.c:432-443`

```c
if (!cache_init(&S()->vol.volume.cache, (uint64_t)size_mb * 1024ULL * 1024ULL)) {
    LOG_ERROR("Cache init failed"); return RC_ERR_CACHE;
}
S()->vol.volume.cache_enabled = true;
S()->cache.cache_on = true;
S()->cache.cache_mb = size_mb;
S()->vol.volume.cache.flush_thread = S()->cache.flush_thread =
    (HANDLE)_beginthreadex(NULL, 0, cache_flush_thread, &S()->vol.volume, 0, NULL);
```

vs `volume_cache_enable()` at `volume_manager.c:114-122`:
```c
if (!cache_init(&vol->cache, cache_size)) return false;
vol->cache_enabled = true;
*cache_on = true;
*cache_mb = (uint32_t)(cache_size / (1024 * 1024));
vol->cache.flush_thread = *flush_thread =
    (HANDLE)_beginthreadex(NULL, 0, cache_flush_thread, vol, 0, NULL);
```

**Classification: Needs Refactor** — Structurally identical; `raid_cache()` should call `volume_cache_enable()`.

---

## 4. JOURNAL RECOVERY (separate from CACHE INIT)

`journal_recover_all()` is called in exactly **one** place: `volume_manager.c:69`.

**Classification: Safe** — Properly centralized.

---

## 5. MOUNT: `volume_mount` / `fuse_mount_volume`

### Occurrences

| File | Line | Call | Mount event? |
|------|------|------|-------------|
| `raid_service.c` | 341 | `volume_mount()` → `fuse_mount_volume()` + `EVENT_MOUNT` | Yes |
| `daemon.c` | 151 | `fuse_mount_volume()` direct | No |
| `daemon.c` | 195 | `fuse_mount_volume()` direct | No |
| `wizard.c` | 133 | `fuse_mount_volume()` direct | No |

### Differences

- `volume_mount()` (volume_manager.c:74-78) calls `fuse_mount_volume()` AND publishes `EVENT_MOUNT`.
- Three direct callers skip `EVENT_MOUNT` entirely.
- All three direct callers manually set `state->rt.mounted = true` — `volume_mount()` does NOT set this flag.

**Classification: Needs Refactor** — 3 callers bypass `volume_mount()`, losing event-bus notifications. Manual `mounted` flag logic duplicated.

---

## 6. DESTROY / CLEANUP: unmount → close → delete pool files → delete superblock

### Occurrences

| File | Line(s) | Sequence |
|------|---------|----------|
| `volume_manager.c` | 95-112 (`volume_destroy()`) | `volume_unmount()` → `pool_file_close()` × N → `pool_file_delete()` × N → `DeleteFileW(sb)` × N → `RemoveDirectoryW(dir)` × N |
| `cleanup.c` | 60-72 (`cleanup_pool_session()`) | `cleanup_pool_files()` (close + delete × N) → `DeleteFileW(sb)` × N → `RemoveDirectoryW(dir)` × N |
| `cleanup.c` | 53-58 (`cleanup_session()`) | `cleanup_cache()` → `cleanup_volume()` — no pool-file/superblock deletion |
| `raid_service.c` | 346-353 (`raid_unmount()`) | `volume_unmount()` — no pool-file deletion |
| `raid_service.c` | 381-392 (`raid_destroy()`) | Calls `volume_destroy()` (full sequence) |
| `raid_service.c` | 394-405 (`raid_purge()`) | Calls `cleanup_pool_session(S())` — pool files + superblock, no unmount |

### Duplicated deletion logic

The `pool_file_close() + pool_file_delete() + DeleteFileW(superblock) + RemoveDirectoryW(dir)` pattern appears in:

1. `volume_destroy()` (volume_manager.c:99-108)
2. `cleanup_pool_session()` (cleanup.c:38-41 via `cleanup_pool_files()` + lines 63-71)

These are nearly identical file-deletion loops.

Additionally, the `cleanup_session() + cleanup_disks()` shutdown sequence is duplicated between:
- `daemon.c:164-165` (inside `daemon_run()`)
- `daemon.c:212-213` (inside `daemon_main()`)
- `main.c:91-92` (inside `cli_main()`)

**Classification: Needs Refactor** — Pool-file + superblock cleanup should be a single shared function.

---

## 7. CONFIG LOAD: `config_load()` → parse → apply

### Occurrences

| File | Line | Context |
|------|------|---------|
| `raid_service.c` | 62 | `raid_init()` — loads config at startup |
| `raid_service.c` | 770 | `raid_config_load()` — command handler |
| `cmd_handler.c` | 175 | `auto_restore_or_quick()` — auto-restore on empty command |
| `daemon.c` | 80, 94, 190 | Various config reads |

### Config-based restore duplication

The pattern `config_load()` → scan → create/restore volumes appears in three places with different approaches:

| File | Lines | Behavior |
|------|-------|----------|
| `cmd_handler.c` | 174-190 | `config_load()` → `raid_scan()` → tells user "Use 'load' to restore" |
| `daemon.c` | 92-141 | `config_load()` → `disk_scan_all()` → `volume_create()` — fully restores from JSON (legacy) |
| `main.c` | 12-40 (`do_restore()`) | Config-driven CLI replay → `cmd_cache()` → `cmd_mount()` |

**Classification: Needs Refactor** — Three different implementations for the same business logic (config-based volume restoration).

---

## 8. POOL FILE CREATE: `pool_file_create` → `metadata_write`

Not a tight sequence — pool files are created in one step, metadata is written later inside `volume_create()` / `volume_mirror()` / `volume_expand()` / `volume_rebuild()`.

**Classification: Safe** — Properly mediated by `volume_create_pool_file()` wrapper.

---

## 9. VOLUME CREATE: create → cache init → mount

The full "set up volume, enable cache, mount filesystem" workflow appears in **four** distinct implementations.

### Appearance A: wizard.c (lines 113, 125-128, 133)
```c
volume_create(&state->vol.volume, state->disk.disks, state->disk.disk_count);
volume_cache_enable(&state->vol.volume, ...);
fuse_mount_volume(&state->vol.volume, mount_letter);
state->rt.mounted = true;
```

### Appearance B: raid_service.c (lines 794, 802, 804) — raid_quick()
```c
raid_create();          // -> volume_create() + metadata_write()
raid_cache(1, argv);    // -> inline cache_init + flush_thread
raid_mount(mount_letter);  // -> volume_mount()
```

### Appearance C: daemon.c (lines 128, 83-84, 151) — daemon_load_volume() + daemon_run()
```c
volume_create(&state->vol.volume, state->disk.disks, state->disk.disk_count);
volume_cache_enable(&state->vol.volume, ...);
fuse_mount_volume(&state->vol.volume, state->vol.volume.mount_point[0]);
state->rt.mounted = true;
```

### Appearance D: main.c (lines 26-39) — do_restore()
```c
cmd_process("create");    // -> raid_create()
cmd_cache(1, cache_av);   // -> raid_cache() -> inline cache_init
cmd_mount(1, mount_av);   // -> raid_mount() -> volume_mount()
```

### Key differences

| Aspect | wizard.c | raid_quick | daemon.c | do_restore |
|--------|----------|------------|----------|------------|
| Cache init | `volume_cache_enable()` | Inline in `raid_cache()` | `volume_cache_enable()` | Inline via `raid_cache()` |
| Mount path | `fuse_mount_volume()` direct | `volume_mount()` wrapper | `fuse_mount_volume()` direct | `volume_mount()` wrapper |
| Event-bus publish | No mount event | Mount event | No mount event | Mount event |
| `mounted` flag | Set manually | Set in `raid_mount()` | Set manually | Set in `raid_mount()` |
| Error handling | `if`-guarded | Checks RC | Partial | None |

**Classification: Needs Refactor** — Same business workflow implemented 4 different ways with behavioral inconsistencies.

---

## F1. Shutdown sequence duplication (`cleanup_session` + `cleanup_disks`)

| File | Lines | Code |
|------|-------|------|
| `daemon.c` | 164-165 | `cleanup_session(state); cleanup_disks(state);` |
| `daemon.c` | 212-213 | `cleanup_session(state); cleanup_disks(state);` |
| `main.c` | 91-92 | `cleanup_session(&g_state); cleanup_disks(&g_state);` |

**Classification: Needs Refactor** — Duplicated within `daemon.c` itself.

---

## F2. Drive-letter mapping duplication

| File | Line | Call |
|------|------|------|
| `wizard.c` | 75 | `disk_map_drive((char[]){dl, 0}, ...)` |
| `daemon.c` | 110 | `disk_map_drive((char[]){dl, 0}, ...)` |

**Classification: Safe** — Thin call to the same utility function.

---

## F3. Pool file rollback on error

| File | Lines | Context |
|------|-------|---------|
| `raid_service.c` | 273-276 | In `raid_init_pools()` |
| `volume_manager.c` | 187-189 | In `volume_expand()` |
| `volume_manager.c` | 212-214 | In `volume_rebuild()` |

**Classification: Safe** — Localized error paths; minor opportunity for helper function.

---

## Summary Table

| # | Workflow Pattern | Classification | Primary Issue |
|---|-----------------|----------------|---------------|
| 1 | SCAN | **Intentional** | Different scan APIs; legacy vs modern |
| 2 | RECOVER | **Safe** | Single call in `volume_load()` |
| 3 | CACHE INIT | **Needs Refactor** | `raid_cache()` inlines `volume_cache_enable()` |
| 4 | JOURNAL RECOVERY | **Safe** | Single call site |
| 5 | MOUNT | **Needs Refactor** | 3 callers bypass `volume_mount()`, lose events |
| 6 | DESTROY / CLEANUP | **Needs Refactor** | Duplicated file-deletion loops |
| 7 | CONFIG LOAD | **Needs Refactor** | 3 different config-restore implementations |
| 8 | POOL FILE CREATE | **Safe** | Mediated by `volume_create_pool_file()` |
| 9 | VOLUME CREATE | **Needs Refactor** | 4 implementations with behavioral inconsistencies |
| F1 | SHUTDOWN | **Needs Refactor** | `cleanup_session() + cleanup_disks()` duplicated |
| F2 | DRIVE-LETTER MAP | **Safe** | Same utility function |
| F3 | ROLLBACK | **Safe** | Local error paths |
