# RAIDTEST v3 — Public API Reference

## Architecture Overview

```
                 GUI (future)
                      │
                      ▼
              Raid Service (raid_service.h)
                      │
          ┌───────────┼───────────┐
          ▼           ▼           ▼
   Device Manager  Volume Mgr  Planner Engine
          │           │           │
          └───────────┼───────────┘
                      ▼
          Metadata / Journal / Cache
                      │
                      ▼
               Disk I/O Layer
                      │
                      ▼
           Windows Storage API
```

## 1. Event Bus (`event_bus.h`)

| Function | Visibility | Description |
|----------|-----------|-------------|
| `event_bus_init()` | Public | Initialize event bus (called by `raid_init()`) |
| `event_bus_subscribe(type, cb, userdata)` | Public | Subscribe to an event type |
| `event_bus_unsubscribe(type, cb)` | Public | Unsubscribe from an event type |
| `event_bus_publish(type, data)` | Internal | Publish event to all subscribers |
| `event_type_str(type)` | Public | Convert event type to string |

**Events**: `EVENT_DISK_FOUND`, `EVENT_DISK_REMOVED`, `EVENT_DISK_BENCHED`,
`EVENT_VOLUME_CREATED`, `EVENT_VOLUME_LOADED`, `EVENT_VOLUME_DESTROYED`,
`EVENT_MOUNT`, `EVENT_UNMOUNT`, `EVENT_REBUILD`, `EVENT_EXPAND`,
`EVENT_METADATA_UPDATED`, `EVENT_CACHE_CHANGED`, `EVENT_ERROR`

**Owner**: System (initialized by Raid Service)
**Dependencies**: None
**Future GUI Usage**: GUI subscribes to events for live updates

---

## 2. Device Manager (`device_manager.h`)

| Function | Visibility | Description |
|----------|-----------|-------------|
| `device_refresh()` | Internal | Scan all physical disks, publish `EVENT_DISK_FOUND` |
| `device_bench(id, size_mb)` | Internal | Benchmark single disk |
| `device_bench_all_selected(size_mb)` | Internal | Benchmark all selected disks |
| `device_get_count()` | Public | Return number of physical disks |
| `device_get(index)` | Public | Get DISK_INFO by index |
| `device_find_serial(serial)` | Public | Find disk by serial number |
| `device_find_drive(letter)` | Public | Find disk by drive letter |
| `device_get_all()` | Internal | Get raw array pointer |
| `device_select(indices, count)` | CLI | Select disks for volume membership |
| `device_is_selected(index)` | Public | Check if disk is selected |
| `device_selected_count()` | Public | Count selected disks |
| `device_map_drive(id, letter)` | CLI | Map disk to drive letter |
| `device_has_drive_letter(id)` | Public | Check if disk has letter |
| `device_capacity(id)` | Public | Get disk capacity in bytes |
| `device_speed(id)` | Public | Get write speed in MB/s |
| `device_health(id)` | Public | Check disk health status |
| `device_print_list()` | CLI | Print disk list to console |
| `device_cleanup()` | Internal | Free disk scan resources |

**Owner**: Device Manager
**Dependencies**: `disk_scanner`, `bench_io`, `pool_io`, `event_bus`
**Future GUI Usage**: `device_get_count()`, `device_get()`, `device_find_serial()`,
`device_capacity()`, `device_speed()`, `device_health()`

---

## 3. Metadata Manager (`metadata_manager.h`)

| Function | Visibility | Description |
|----------|-----------|-------------|
| `metadata_read(root, sb)` | Internal | Read raw superblock from drive |
| `metadata_write(vol)` | Internal | Write superblock from volume, publish event |
| `metadata_validate(sb)` | Internal | Validate superblock magic/version/disk_count |
| `metadata_upgrade(sb)` | Internal | Check version compatibility |
| `metadata_dump(sb, out, size)` | CLI/UI | Format superblock as human-readable string |
| `metadata_restore(sb, phys, pcount, vol, disks, dcount)` | Internal | Restore volume from superblock |
| `metadata_load_volume(root, phys, pcount, vol, disks, dcount)` | Internal | Load volume via superblock_read |

**Owner**: Metadata Manager
**Dependencies**: `superblock`, `event_bus`
**Future GUI Usage**: `metadata_dump()` for display, `metadata_validate()` for health

---

## 4. Planner Engine (`planner_engine.h`)

| Function | Visibility | Description |
|----------|-----------|-------------|
| `planner_calculate(disks, count, result)` | Public | Calculate RAID capacities from disk list |
| `planner_print(result, disks, count)` | CLI | Print planner report to console |

**Input type**: `PLANNER_DISK` — disk_index, capacity_bytes, speed_mbs, serial, selected
**Output type**: `PLANNER_RESULT` — disk_count, total_raw_mb, raid0/1/10_capacity_mb, efficiency

**Owner**: Planner Engine
**Dependencies**: None (pure calculation, no I/O)
**Future GUI Usage**: `planner_calculate()` directly, custom rendering instead of `planner_print()`

---

## 5. Volume Manager (`volume_manager.h`)

| Function | Visibility | Description |
|----------|-----------|-------------|
| `volume_create(vol, disks, count)` | Internal | Create RAID0 volume, write superblock |
| `volume_mirror(vol, disks, count)` | Internal | Create RAID1 volume, write superblock |
| `volume_load(vol, disks_out, count, phys, pcount, root)` | Internal | Load volume from superblock |
| `volume_mount(vol, letter)` | Internal | Mount volume via WinFsp |
| `volume_unmount(vol, cache_on, flush, mounted)` | Internal | Unmount, flush cache, destroy volume |
| `volume_destroy(vol, disks, count, cache_on, flush, mounted, state)` | Internal | Destroy — unmount + delete pool files |
| `volume_expand(vol, phys, pcount, argc, argv)` | Internal | Add disks to RAID0 |
| `volume_rebuild(vol, phys, pcount, idx, disk_id, mb)` | Internal | Replace mirror member |
| `volume_gen_uuid(vol)` | Internal | Generate UUID for volume |

**Owner**: Volume Manager
**Dependencies**: `metadata_manager`, `pool_io`, `fuse_bridge`, `journal`,
`mirror_engine`, `ram_cache`, `cleanup`, `event_bus`
**Future GUI Usage**: Called through Raid Service only

---

## 6. Raid Service (`raid_service.h`)

| Function | Visibility | Description |
|----------|-----------|-------------|
| `raid_init()` | System | Initialize all subsystems, event bus, config |
| `raid_cleanup()` | System | Clean shutdown of all subsystems |
| `raid_scan()` | CLI/GUI | Scan disks, benchmark, enter DISCOVERED |
| `raid_select(argc, argv)` | CLI | Select disks |
| `raid_mapdrive(id, letter)` | CLI | Map disk to drive letter |
| `raid_bench(argc, argv)` | CLI | Benchmark selected disks |
| `raid_init_pools(argc, argv)` | CLI/GUI | Create pool files |
| `raid_create()` | CLI/GUI | Create RAID0 volume |
| `raid_mirror()` | CLI/GUI | Create RAID1 volume |
| `raid_expand(argc, argv)` | CLI | Expand RAID0 |
| `raid_rebuild(argc, argv)` | CLI | Rebuild mirror |
| `raid_mount(letter)` | CLI/GUI | Mount via WinFsp |
| `raid_unmount()` | CLI/GUI | Unmount |
| `raid_load(root)` | CLI/GUI | Load from superblock |
| `raid_destroy()` | CLI/GUI | Destroy volume |
| `raid_purge()` | CLI | Force cleanup |
| `raid_cache(argc, argv)` | CLI/GUI | Cache control |
| `raid_info()` | CLI | Volume info |
| `raid_status()` | CLI/GUI | Live status |
| `raid_map()` | CLI | Show LBA mapping |
| `raid_test()` | CLI | I/O verify |
| `raid_random(argc, argv)` | CLI | Stress test |
| `raid_benchfs(argc, argv)` | CLI | Benchmark |
| `raid_check()` | CLI/GUI | Health check |
| `raid_simulate(argc, argv)` | CLI | Fault injection |
| `raid_metadata(argc, argv)` | CLI | Dump metadata |
| `raid_planner()` | CLI/GUI | Capacity planner |
| `raid_events()` | CLI | Show event log |
| `raid_config_save()` | CLI/GUI | Save JSON config |
| `raid_config_load()` | CLI | Load JSON config |
| `raid_wizard()` | CLI | Interactive wizard |
| `raid_quick()` | CLI | All-in-one setup |

**Owner**: Raid Service
**Dependencies**: All managers, `disk_scanner`, `bench_io`, `config`, `wizard`,
`journal`, `cleanup`, `ram_cache`, `event_bus`, `pool_io`, `stripe_engine`
**Future GUI Usage**: **Primary API**. GUI calls `raid_scan()`, `raid_create()`,
`raid_mount()`, `raid_status()`, etc. directly. GUI must NOT call `cmd_*` functions.

---

## 7. UI Model (`ui_model.h`)

| Function | Visibility | Description |
|----------|-----------|-------------|
| `ui_get_disk_summary(out)` | GUI | Get disk count, selected, capacity, speed |
| `ui_get_volume_info(out)` | GUI | Get volume UUID, cache, I/O stats |
| `ui_get_health_summary(out)` | GUI | Get healthy/total disk count |
| `ui_get_state()` | GUI | Get current RAID_STATE enum |
| `ui_get_state_str()` | GUI | Get state as string |

**Owner**: UI Model
**Dependencies**: `cmd_handler` (for `g_state`)
**Future GUI Usage**: Read-only state queries for GUI rendering

---

## Dependency Rules

1. **GUI → Raid Service only**. GUI never calls `cmd_*`, `device_*` directly.
2. **CLI → Raid Service → Managers**. CLI never calls managers directly.
3. **Managers → lower modules**. Never the reverse.
4. **No circular includes**. Verified.

## Remaining Cleanup Items

- `cleanup.h` → should depend on a standalone `APP_STATE` header, not `cmd_handler.h`
- `wizard.h` → same issue (needs APP_STATE from dedicated header)
- `daemon.h` → same issue
