#pragma once
#include "common.h"

bool device_refresh(void);
bool device_bench_all_selected(uint32_t size_mb);

uint32_t device_get_count(void);
DISK_INFO* device_get(uint32_t index);

bool device_select(uint32_t* indices, uint32_t count);
bool device_is_selected(uint32_t index);

bool device_map_drive(uint32_t disk_id, const char* drive_letter);

void device_print_list(void);
void device_cleanup(void);
