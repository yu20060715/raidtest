#pragma once
#include "common.h"

bool disk_scan_all(DISK_INFO*** out_disks, uint32_t* out_count);
void disk_scan_free(DISK_INFO** disks, uint32_t count);
bool disk_resolve_speed(DISK_INFO* disk);
bool disk_select(DISK_INFO** disks, uint32_t count, uint32_t* indices, uint32_t sel_count);
bool disk_map_drive(const char* drive_letter, DISK_INFO* disk);
void disk_print_list(DISK_INFO** disks, uint32_t count);