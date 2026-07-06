#pragma once
#include "common.h"

#define SUPERBLOCK_MAGIC      0x52414944
#define SUPERBLOCK_VERSION    4
#define SUPERBLOCK_FILENAME   L"superblock.dat"

#define SB_FEATURE_JOURNAL    (1 << 0)
#define SB_FEATURE_CACHE      (1 << 1)
#define SB_FEATURE_MIRROR     (1 << 2)

#pragma pack(push, 1)
/* v2 entry (24 bytes) — kept for backward compat reads */
typedef struct {
    wchar_t  drive_letter[4];
    uint32_t bench_write_mbs;
    uint32_t bench_read_mbs;
    uint64_t pool_bytes;
} SB_DISK_ENTRY_V2;

/* v3/v4 entry (88 bytes) — adds serial_number */
typedef struct {
    wchar_t  drive_letter[4];
    uint32_t bench_write_mbs;
    uint32_t bench_read_mbs;
    uint64_t pool_bytes;
    char     serial_number[MAX_SERIAL_LEN];
} SB_DISK_ENTRY;

/* v3 (796 bytes) */
typedef struct {
    uint32_t  magic;
    uint32_t  version;
    uint32_t  disk_count;
    uint32_t  stripe_unit;
    uint64_t  virtual_total_bytes;
    uint32_t  phase_count;
    uint64_t  generation;
    uint64_t  timestamp;
    uint32_t  feature_flags;
    uint64_t  journal_offset;
    SB_DISK_ENTRY disks[MAX_DISKS];
    MAPPING_PHASE  phases[MAX_DISKS];
    uint32_t  checksum;
} SUPERBLOCK_V3;

/* v4 (828 bytes) — adds volume_uuid, created_time, last_mount_time */
typedef struct {
    uint32_t  magic;
    uint32_t  version;
    uint32_t  disk_count;
    uint32_t  stripe_unit;
    uint64_t  virtual_total_bytes;
    uint32_t  phase_count;
    uint64_t  generation;
    uint64_t  timestamp;
    uint32_t  feature_flags;
    uint64_t  journal_offset;
    VOLUME_UUID volume_uuid;
    uint64_t  created_time;
    uint64_t  last_mount_time;
    SB_DISK_ENTRY disks[MAX_DISKS];
    MAPPING_PHASE  phases[MAX_DISKS];
    uint32_t  checksum;
} SUPERBLOCK;
#pragma pack(pop)

bool superblock_write(STRIPE_VOLUME* vol);
bool superblock_read(const wchar_t* drive_root, STRIPE_VOLUME* vol, DISK_INFO* disks_out, uint32_t* disk_count_out,
                     DISK_INFO** physical_disks, uint32_t physical_count);
bool superblock_read_raw(const wchar_t* drive_root, SUPERBLOCK* sb_out);
bool superblock_restore(const SUPERBLOCK* sb, DISK_INFO** physical_disks, uint32_t physical_count,
                         STRIPE_VOLUME* vol, DISK_INFO* disks_out, uint32_t* disk_count_out);
void superblock_format_str(const SUPERBLOCK* sb, char* out, size_t out_size);
