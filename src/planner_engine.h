#pragma once
#include "common.h"

typedef struct {
    uint32_t disk_index;
    uint64_t capacity_bytes;
    uint32_t speed_mbs;
    char     serial[MAX_SERIAL_LEN];
    bool     selected;
} PLANNER_DISK;

typedef struct {
    uint32_t  disk_count;
    uint64_t  total_raw_mb;
    uint64_t  raid0_capacity_mb;
    uint64_t  raid1_capacity_mb;
    uint64_t  raid10_capacity_mb;
    uint32_t  selected_count;
    double    efficiency_raid0;
    double    efficiency_raid1;
} PLANNER_RESULT;

void planner_calculate(PLANNER_DISK* disks, uint32_t count, PLANNER_RESULT* out);
void planner_print(const PLANNER_RESULT* result, PLANNER_DISK* disks, uint32_t count);
