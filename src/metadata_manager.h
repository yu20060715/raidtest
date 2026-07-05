#pragma once
#include "common.h"
#include "superblock.h"

bool metadata_read(const wchar_t* drive_root, SUPERBLOCK* sb_out);
bool metadata_write(STRIPE_VOLUME* vol);
bool metadata_validate(const SUPERBLOCK* sb);
bool metadata_upgrade(SUPERBLOCK* sb);
void metadata_dump(const SUPERBLOCK* sb, char* out, size_t out_size);

bool metadata_restore(const SUPERBLOCK* sb, DISK_INFO** physical_disks, uint32_t physical_count,
                      STRIPE_VOLUME* vol, DISK_INFO* disks_out, uint32_t* disk_count_out);

bool metadata_load_volume(const wchar_t* drive_root, DISK_INFO** physical_disks, uint32_t physical_count,
                          STRIPE_VOLUME* vol, DISK_INFO* disks_out, uint32_t* disk_count_out);
