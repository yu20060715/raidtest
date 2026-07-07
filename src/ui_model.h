#pragma once
#include "common.h"

typedef struct {
    uint32_t  count;
    uint32_t  selected_count;
    uint64_t  total_capacity_mb;
    uint32_t  write_speed_avg_mbs;
} UI_DISK_SUMMARY;

typedef struct {
    bool      exists;
    uint32_t  raid_level;
    uint32_t  disk_count;
    uint64_t  virtual_capacity_bytes;
    uint64_t  generation;
    char      uuid_str[64];
    bool      mounted;
    bool      cache_enabled;
    uint32_t  cache_mb;
    double    cache_dirty_pct;
    uint64_t  bytes_written;
    uint64_t  bytes_read;
    double    uptime_seconds;
    char      mount_point[4];
} UI_VOLUME_INFO;

typedef struct {
    uint32_t  healthy_count;
    uint32_t  total_count;
    bool      degraded;
} UI_HEALTH_SUMMARY;

void ui_get_disk_summary(UI_DISK_SUMMARY* out);
void ui_get_volume_info(UI_VOLUME_INFO* out);
void ui_get_health_summary(UI_HEALTH_SUMMARY* out);
const char* ui_get_state_str(void);
