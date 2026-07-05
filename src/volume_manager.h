#pragma once
#include "common.h"
#include "stripe_engine.h"

typedef enum {
    VOLUME_OP_CREATE,
    VOLUME_OP_MIRROR,
    VOLUME_OP_LOAD,
    VOLUME_OP_DESTROY,
    VOLUME_OP_EXPAND,
    VOLUME_OP_REBUILD
} VOLUME_OP;

bool volume_create(STRIPE_VOLUME* vol, DISK_INFO** disks, uint32_t disk_count);
bool volume_mirror(STRIPE_VOLUME* vol, DISK_INFO** disks, uint32_t disk_count);
bool volume_load(STRIPE_VOLUME* vol, DISK_INFO* disks_out, uint32_t* disk_count,
                 DISK_INFO** physical_disks, uint32_t physical_count,
                 const wchar_t* drive_root);
bool volume_mount(STRIPE_VOLUME* vol, char drive_letter);
bool volume_unmount(STRIPE_VOLUME* vol, bool* cache_on, HANDLE* flush_thread, bool* mounted);
bool volume_destroy(STRIPE_VOLUME* vol, DISK_INFO** disks, uint32_t disk_count,
                    bool* cache_on, HANDLE* flush_thread, bool* mounted,
                    RAID_STATE* state);

bool volume_expand(STRIPE_VOLUME* vol, DISK_INFO** physical_disks, uint32_t physical_count,
                   int argc, char* argv[]);
bool volume_rebuild(STRIPE_VOLUME* vol, DISK_INFO** physical_disks, uint32_t physical_count,
                    uint32_t replace_idx, uint32_t new_disk_id, uint64_t pool_mb);

void volume_gen_uuid(STRIPE_VOLUME* vol);
