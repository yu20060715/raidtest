#include "volume_manager.h"
#include "metadata_manager.h"
#include "pool_io.h"
#include "fuse_bridge.h"
#include "journal.h"
#include "mirror_engine.h"
#include "ram_cache.h"
#include "cleanup.h"
#include "event_bus.h"

void volume_gen_uuid(STRIPE_VOLUME* vol) {
    uuid_generate(&vol->volume_uuid);
    vol->created_time = 0;
}

bool volume_create(STRIPE_VOLUME* vol, DISK_INFO** disks, uint32_t disk_count) {
    if (vol->cache_enabled) {
        vol->cache.running = 0;
        cache_flush_all(&vol->cache, vol);
        cache_destroy(&vol->cache);
    }
    stripe_volume_destroy(vol);
    vol->cache_enabled = false;

    uint32_t opened = 0;
    for (uint32_t i = 0; i < disk_count; i++) {
        if (!pool_file_open(disks[i])) {
            for (uint32_t j = 0; j < opened; j++) pool_file_close(disks[j]);
            return false;
        }
        opened++;
    }
    if (!stripe_volume_create(vol, disks, disk_count, DEFAULT_STRIPE_UNIT)) {
        for (uint32_t j = 0; j < opened; j++) pool_file_close(disks[j]);
        return false;
    }
    volume_gen_uuid(vol);
    if (!metadata_write(vol))
        LOG_WARN("Superblock write failed — volume is volatile");
    event_bus_publish(EVENT_VOLUME_CREATED, "RAID0");
    return true;
}

bool volume_mirror(STRIPE_VOLUME* vol, DISK_INFO** disks, uint32_t disk_count) {
    if (vol->cache_enabled) {
        vol->cache.running = 0;
        cache_flush_all(&vol->cache, vol);
        cache_destroy(&vol->cache);
    }
    stripe_volume_destroy(vol);
    vol->cache_enabled = false;

    uint32_t opened = 0;
    for (uint32_t i = 0; i < disk_count; i++) {
        if (!pool_file_open(disks[i])) {
            for (uint32_t j = 0; j < opened; j++) pool_file_close(disks[j]);
            return false;
        }
        opened++;
    }
    if (!mirror_volume_create(vol, disks, disk_count)) {
        for (uint32_t j = 0; j < opened; j++) pool_file_close(disks[j]);
        return false;
    }
    volume_gen_uuid(vol);
    if (!metadata_write(vol))
        LOG_WARN("Superblock write failed — volume is volatile");
    event_bus_publish(EVENT_VOLUME_CREATED, "RAID1");
    return true;
}

bool volume_load(STRIPE_VOLUME* vol, DISK_INFO* disks_out, uint32_t* disk_count,
                 DISK_INFO** physical_disks, uint32_t physical_count,
                 const wchar_t* drive_root) {
    uint32_t loaded = 0;
    if (!metadata_load_volume(drive_root, physical_disks, physical_count, vol, disks_out, &loaded))
        return false;
    *disk_count = loaded;
    journal_recover_all(vol);
    event_bus_publish(EVENT_VOLUME_LOADED, "");
    return true;
}

bool volume_mount(STRIPE_VOLUME* vol, char drive_letter) {
    if (!fuse_mount_volume(vol, drive_letter)) return false;
    event_bus_publish(EVENT_MOUNT, "");
    return true;
}

bool volume_unmount(STRIPE_VOLUME* vol, bool* cache_on, HANDLE* flush_thread, bool* mounted) {
    if (flush_thread && *flush_thread) {
        vol->cache.running = 0;
        cache_flush_all(&vol->cache, vol);
        cache_destroy(&vol->cache);
        *flush_thread = NULL;
        vol->cache_enabled = false;
    }
    if (*mounted) {
        fuse_unmount_volume(vol->mount_point[0]);
        *mounted = false;
    }
    stripe_volume_destroy(vol);
    *cache_on = false;
    event_bus_publish(EVENT_UNMOUNT, "");
    return true;
}

bool volume_destroy(STRIPE_VOLUME* vol, DISK_INFO** disks, uint32_t disk_count,
                    bool* cache_on, HANDLE* flush_thread, bool* mounted,
                    RAID_STATE* state) {
    volume_unmount(vol, cache_on, flush_thread, mounted);
    for (uint32_t i = 0; i < disk_count; i++) {
        pool_file_close(disks[i]);
        pool_file_delete(disks[i]);
        wchar_t sb_path[MAX_DRIVE_PATH], dir_path[MAX_DRIVE_PATH];
        wchar_t root[4] = { disks[i]->drive_letter[0], L':', L'\\', 0 };
        StringCchPrintfW(sb_path, MAX_DRIVE_PATH, L"%ls%s\\%ls", root, CONFIG_DIR, SUPERBLOCK_FILENAME);
        StringCchPrintfW(dir_path, MAX_DRIVE_PATH, L"%ls%s", root, CONFIG_DIR);
        DeleteFileW(sb_path);
        RemoveDirectoryW(dir_path);
    }
    *state = STATE_DISCOVERED;
    event_bus_publish(EVENT_VOLUME_DESTROYED, "");
    return true;
}

bool volume_expand(STRIPE_VOLUME* vol, DISK_INFO** physical_disks, uint32_t physical_count,
                   int argc, char* argv[]) {
    (void)physical_count;
    if (vol->raid_level != RAID_LEVEL_STRIPE) { LOG_ERROR("Expand only for RAID0"); return false; }
    uint32_t sel_indices[16];
    uint32_t sel_sizes[16];
    uint32_t sel_count = 0;

    for (int i = 0; i < argc && sel_count < 16; i++) {
        uint32_t id; uint32_t mb;
        if (sscanf(argv[i], "%u:%u", &id, &mb) == 2) {
            if (id < physical_count && physical_disks[id]->selected) {
                sel_indices[sel_count] = id;
                sel_sizes[sel_count] = mb;
                sel_count++;
            } else { LOG_WARN("Disk %u not available", id); }
        }
    }
    if (sel_count == 0) { LOG_ERROR("No valid disks specified"); return false; }

    DISK_INFO* new_disks[16];
    uint32_t created = 0;
    for (uint32_t i = 0; i < sel_count; i++) {
        uint32_t di = sel_indices[i];
        DISK_INFO* disk = physical_disks[di];
        if (!disk->drive_letter[0]) { LOG_ERROR("Disk %u has no drive letter", di); goto rollback; }
        uint64_t size_bytes = (uint64_t)sel_sizes[i] * 1024ULL * 1024ULL;
        if (validate_pool_size(size_bytes) != RC_OK) {
            LOG_ERROR("Invalid pool size %llu MB for expand disk %u",
                      (unsigned long long)sel_sizes[i], di);
            goto rollback;
        }
        if (!pool_file_create(disk, size_bytes)) goto rollback;
        if (!pool_file_open(disk)) { pool_file_delete(disk); goto rollback; }
        new_disks[created++] = disk;
    }

    if (!stripe_volume_expand(vol, new_disks, created)) {
        LOG_ERROR("Expand failed");
        goto rollback;
    }

    if (!metadata_write(vol))
        LOG_WARN("Superblock write failed");
    LOG_OK("Volume expanded by %u disk(s)", created);
    event_bus_publish(EVENT_EXPAND, "");
    return true;

rollback:
    for (uint32_t i = 0; i < created; i++) {
        pool_file_close(new_disks[i]);
        pool_file_delete(new_disks[i]);
    }
    return false;
}

bool volume_rebuild(STRIPE_VOLUME* vol, DISK_INFO** physical_disks, uint32_t physical_count,
                    uint32_t replace_idx, uint32_t new_disk_id, uint64_t pool_mb) {
    (void)physical_count;
    if (vol->raid_level != RAID_LEVEL_MIRROR) { LOG_ERROR("Mirror volume required"); return false; }
    if (replace_idx >= vol->disk_count) { LOG_ERROR("Invalid disk index %u", replace_idx); return false; }
    if (new_disk_id >= physical_count) { LOG_ERROR("Invalid disk id %u", new_disk_id); return false; }

    DISK_INFO* new_disk = physical_disks[new_disk_id];
    if (!new_disk->drive_letter[0]) { LOG_ERROR("Disk %u has no drive letter", new_disk_id); return false; }

    uint64_t size_bytes = pool_mb * 1024ULL * 1024ULL;
    if (validate_pool_size(size_bytes) != RC_OK) {
        LOG_ERROR("Invalid pool size %llu MB for rebuild", (unsigned long long)pool_mb);
        return false;
    }
    if (!pool_file_create(new_disk, size_bytes)) return false;
    if (!pool_file_open(new_disk)) { pool_file_delete(new_disk); return false; }

    if (!mirror_volume_rebuild(vol, new_disk, replace_idx)) {
        pool_file_close(new_disk);
        pool_file_delete(new_disk);
        return false;
    }

    if (!metadata_write(vol))
        LOG_WARN("Superblock write failed");
    event_bus_publish(EVENT_REBUILD, "");
    return true;
}
