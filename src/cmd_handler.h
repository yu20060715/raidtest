#pragma once
#include "common.h"
#include "stripe_engine.h"
#include "ram_cache.h"

typedef struct {
    DISK_INFO*     disks[MAX_CUSTOM_DISKS];
    uint32_t       disk_count;
    uint64_t       pool_sizes_mb[MAX_DISKS];
    STRIPE_VOLUME  volume;
    bool           volume_valid;
    DISK_INFO**    physical_disks;
    uint32_t       physical_count;
    DISK_INFO      loaded_disks[MAX_DISKS];
    bool           cache_on;
    uint32_t       cache_mb;
    HANDLE         flush_thread;
    bool           mounted;
    RAID_STATE     state;
    APP_CONFIG     config;
    wchar_t        appdata_path[MAX_DRIVE_PATH];
} APP_STATE;

extern APP_STATE g_state;

RC cmd_init_all(void);
void cmd_cleanup_all(void);
bool cmd_process(const char* input);
void cmd_print_banner(void);
void cmd_interactive(void);
void cmd_process_auto(const char* cmd_str);
RC cmd_cache(int argc, char* argv[]);
RC cmd_mount(int argc, char* argv[]);
RC cmd_destroy(int argc, char* argv[]);
RC cmd_metadata(int argc, char* argv[]);
RC cmd_check(int argc, char* argv[]);
RC cmd_simulate(int argc, char* argv[]);
RC cmd_planner(int argc, char* argv[]);
RC cmd_event_log(int argc, char* argv[]);
void cleanup_cs(void);