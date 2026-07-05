#pragma once
#include "common.h"

bool mirror_volume_create(STRIPE_VOLUME* vol, DISK_INFO** disks, uint32_t disk_count);
bool mirror_volume_read(STRIPE_VOLUME* vol, void* buffer, uint64_t virtual_offset, uint32_t length);
bool mirror_volume_write(STRIPE_VOLUME* vol, const void* buffer, uint64_t virtual_offset, uint32_t length);
bool mirror_volume_rebuild(STRIPE_VOLUME* vol, DISK_INFO* replacement, uint32_t replace_idx);
