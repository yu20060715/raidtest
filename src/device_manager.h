#pragma once
#include "common.h"

bool device_refresh(void);
bool device_bench(uint32_t disk_id, uint32_t size_mb);
bool device_bench_all_selected(uint32_t size_mb);

uint32_t device_get_count(void);
DISK_INFO* device_get(uint32_t index);
DISK_INFO* device_find_serial(const char* serial);
DISK_INFO* device_find_drive(wchar_t drive_letter);
DISK_INFO** device_get_all(void);

bool device_select(uint32_t* indices, uint32_t count);
bool device_is_selected(uint32_t index);
uint32_t device_selected_count(void);

bool device_map_drive(uint32_t disk_id, const char* drive_letter);
bool device_has_drive_letter(uint32_t disk_id);

uint64_t device_capacity(uint32_t disk_id);
uint32_t device_speed(uint32_t disk_id);
bool device_health(uint32_t disk_id);

void device_print_list(void);
void device_cleanup(void);
