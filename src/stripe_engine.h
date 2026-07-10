#pragma once
#include "common.h"

bool stripe_volume_create(STRIPE_VOLUME* vol, DISK_INFO** disks, uint32_t disk_count, uint32_t stripe_unit);
bool stripe_volume_expand(STRIPE_VOLUME* vol, DISK_INFO** new_disks, uint32_t new_count);
void stripe_volume_destroy(STRIPE_VOLUME* vol);
bool stripe_volume_map_lba(const STRIPE_VOLUME* vol, uint64_t virtual_offset,
                           IO_MAPPING_ENTRY* entries, uint32_t* out_count, uint32_t request_length);
bool stripe_volume_read(STRIPE_VOLUME* vol, void* buffer, uint64_t virtual_offset, uint32_t length);
bool stripe_volume_write(STRIPE_VOLUME* vol, const void* buffer, uint64_t virtual_offset, uint32_t length);
bool stripe_volume_dump_mapping(const STRIPE_VOLUME* vol, uint64_t start, uint64_t end);
bool stripe_volume_verify_io(STRIPE_VOLUME* vol);
bool stripe_volume_random_test(STRIPE_VOLUME* vol, uint32_t operations, uint32_t max_size_kb);
bool stripe_volume_normalize_ratios(uint32_t* speeds, uint32_t count, uint32_t* ratios, uint32_t* total_out);

// Worker thread lifecycle (internal to stripe_engine, called by volume_manager)
bool stripe_volume_workers_init(STRIPE_VOLUME* vol);
void stripe_volume_workers_stop(STRIPE_VOLUME* vol);