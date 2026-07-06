#pragma once
#include "common.h"

typedef struct {
    DISK_INFO*     disks[MAX_CUSTOM_DISKS];
    uint32_t       disk_count;
    uint64_t       pool_sizes_mb[MAX_DISKS];
    DISK_INFO**    physical_disks;
    uint32_t       physical_count;
    DISK_INFO      loaded_disks[MAX_DISKS];
} DiskState;

typedef struct {
    STRIPE_VOLUME  volume;
    bool           volume_valid;
} VolumeState;

typedef struct {
    bool           cache_on;
    uint32_t       cache_mb;
    HANDLE         flush_thread;
} CacheState;

typedef struct {
    RAID_STATE     state;
    bool           mounted;
    wchar_t        appdata_path[MAX_DRIVE_PATH];
} RuntimeState;

typedef struct {
    APP_CONFIG     config;
} ConfigState;

typedef struct {
    DiskState      disk;
    VolumeState    vol;
    CacheState     cache;
    RuntimeState   rt;
    ConfigState    cfg;
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