#pragma once
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tchar.h>
#include <strsafe.h>
#include <process.h>

extern CRITICAL_SECTION g_state_cs;
static inline void gs_lock(void)   { EnterCriticalSection(&g_state_cs); }
static inline void gs_unlock(void) { LeaveCriticalSection(&g_state_cs); }

#include <errno.h>
static inline bool safe_atou32(const char* s, uint32_t* out) {
    if (!s || !out) return false;
    char* end = NULL;
    errno = 0;
    unsigned long val = strtoul(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || val > UINT32_MAX) return false;
    *out = (uint32_t)val;
    return true;
}
static inline bool safe_atou64(const char* s, uint64_t* out) {
    if (!s || !out) return false;
    char* end = NULL;
    errno = 0;
    unsigned long long val = strtoull(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') return false;
    *out = val;
    return true;
}

#define SECTOR_SIZE         512
#define DEFAULT_STRIPE_UNIT (1024 * 1024)
#define MAX_DISKS           4
#define MAX_IO_ENTRIES      (MAX_DISKS * 64)
#define MIN_DISKS           2
#define MIN_MIRROR_DISKS    2

#define RAID_LEVEL_STRIPE   0
#define RAID_LEVEL_MIRROR   1
#define MAX_DRIVE_PATH      260
#define MAX_MODEL_LEN       128
#define MAX_SERIAL_LEN      64
#define MAX_CUSTOM_DISKS    8

typedef enum {
    STATE_DISCONNECTED  = 0,
    STATE_DISCOVERED    = 1,
    STATE_INITIALIZED   = 2,
    STATE_MOUNTED       = 3,
    STATE_DEGRADED      = 4,
    STATE_RECOVERING    = 5,
    STATE_UNMOUNTED     = 6,
} RAID_STATE;

static inline const char* raid_state_str(RAID_STATE s) {
    switch (s) {
        case STATE_DISCONNECTED: return "DISCONNECTED";
        case STATE_DISCOVERED:   return "DISCOVERED";
        case STATE_INITIALIZED:  return "INITIALIZED";
        case STATE_MOUNTED:      return "MOUNTED";
        case STATE_DEGRADED:     return "DEGRADED";
        case STATE_RECOVERING:   return "RECOVERING";
        case STATE_UNMOUNTED:    return "UNMOUNTED";
        default:                 return "UNKNOWN";
    }
}

typedef enum {
    RC_OK                = 0,
    RC_ERR_GENERIC       = 1,
    RC_ERR_INVALID_STATE = 2,
    RC_ERR_INVALID_ARG   = 3,
    RC_ERR_INVALID_DISK  = 4,
    RC_ERR_IO            = 5,
    RC_ERR_METADATA      = 6,
    RC_ERR_CACHE         = 7,
    RC_ERR_MOUNT         = 8,
    RC_ERR_NOT_FOUND     = 9,
    RC_ERR_ALREADY       = 10,
    RC_ERR_NO_SPACE      = 11,
    RC_ERR_ROLLBACK      = 12,
} RC;
#define POOL_FILENAME       L"stripe_pool.dat"
#define CONFIG_DIR          L"RAIDTEST"
#define BENCH_SIZE_MB       512
#define BENCH_BLOCK_SIZE    (1024 * 1024)
#define CACHE_DEFAULT_MB    4096
#define CACHE_BLOCK_SIZE    (64 * 1024)
#define POOL_SIZE_DEFAULT_MB 51200

#define MAX_FLUSH_BATCH    256
#define MAX_FLUSH_SIZE     ((uint64_t)MAX_FLUSH_BATCH * CACHE_BLOCK_SIZE)

#include "logger.h"

#define LOG_DEBUG(str, ...) log_printf(LOG_LEVEL_DEBUG, str, ##__VA_ARGS__)
#define LOG_INFO(str, ...)  log_printf(LOG_LEVEL_INFO,  str, ##__VA_ARGS__)
#define LOG_OK(str, ...)    log_printf(LOG_LEVEL_OK,    str, ##__VA_ARGS__)
#define LOG_WARN(str, ...)  log_printf(LOG_LEVEL_WARN,  str, ##__VA_ARGS__)
#define LOG_ERROR(str, ...) log_printf(LOG_LEVEL_ERROR, str, ##__VA_ARGS__)

static inline uint64_t min_u64(uint64_t a, uint64_t b) { return a < b ? a : b; }
static inline uint32_t gcd_u32(uint32_t a, uint32_t b) {
    while (b) { uint32_t t = b; b = a % b; a = t; }
    return a;
}

static inline RC validate_pool_size(uint64_t size_bytes) {
    if (size_bytes == 0) return RC_ERR_INVALID_ARG;
    if (size_bytes < SECTOR_SIZE) return RC_ERR_INVALID_ARG;
    uint64_t sector_count = size_bytes / SECTOR_SIZE;
    if (sector_count == 0) return RC_ERR_INVALID_ARG;
    return RC_OK;
}

#define CHECK_HANDLE(h, label) do { if (!(h)) { LOG_ERROR("Handle allocation failed"); goto label; } } while(0)

#pragma pack(push, 1)
typedef struct {
    uint64_t high;
    uint64_t low;
} VOLUME_UUID;
#pragma pack(pop)

static inline void uuid_generate(VOLUME_UUID* uuid) {
    LARGE_INTEGER pc;
    QueryPerformanceCounter(&pc);
    uuid->high = ((uint64_t)GetCurrentProcessId() << 32) | ((uint32_t)(GetTickCount64() & 0xFFFFFFFF));
    uuid->low = (uint64_t)pc.QuadPart;
    if (uuid->high == 0) uuid->high = 1;
    if (uuid->low == 0) uuid->low = 1;
}

static inline void uuid_to_str(const VOLUME_UUID* uuid, char* out, size_t out_size) {
    snprintf(out, out_size, "%016llX-%016llX",
             (unsigned long long)uuid->high,
             (unsigned long long)uuid->low);
}

static inline bool uuid_is_zero(const VOLUME_UUID* uuid) {
    return uuid->high == 0 && uuid->low == 0;
}

static inline bool uuid_eq(const VOLUME_UUID* a, const VOLUME_UUID* b) {
    return a->high == b->high && a->low == b->low;
}

static inline uint32_t crc32(const uint8_t* data, size_t len) {
    uint32_t table[256];
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (uint32_t j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (crc & 1 ? 0xEDB88320 : 0);
        table[i] = crc;
    }
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++)
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFF;
}

typedef enum {
    DISK_TYPE_UNKNOWN = 0,
    DISK_TYPE_SATA_SSD,
    DISK_TYPE_NVME_SSD,
    DISK_TYPE_HDD,
    DISK_TYPE_RAMDISK,
    DISK_TYPE_FILEBACKED
} DISK_TYPE;

static inline const char* disk_type_str(DISK_TYPE t) {
    switch (t) {
        case DISK_TYPE_SATA_SSD:   return "SATA SSD";
        case DISK_TYPE_NVME_SSD:   return "NVMe SSD";
        case DISK_TYPE_HDD:        return "HDD";
        case DISK_TYPE_RAMDISK:    return "RAM Disk";
        case DISK_TYPE_FILEBACKED: return "File-backed";
        default:                   return "Unknown";
    }
}

typedef struct {
    wchar_t    device_path[MAX_DRIVE_PATH];
    wchar_t    drive_letter[MAX_DRIVE_PATH];
    wchar_t    model[MAX_MODEL_LEN];
    DISK_TYPE  type;
    uint64_t   total_bytes;
    uint64_t   pool_bytes;
    uint32_t   sector_size;
    uint32_t   read_speed_mbs;
    uint32_t   write_speed_mbs;
    uint32_t   bench_read_mbs;
    uint32_t   bench_write_mbs;
    bool       selected;
    bool       benchmarked;
    HANDLE     handle;
    void*      ram_buffer;
    wchar_t    file_path[MAX_DRIVE_PATH];
    volatile LONG faulty;
    volatile LONG error_count;
    volatile LONG healthy;
    char         serial_number[MAX_SERIAL_LEN];
} DISK_INFO;

typedef enum {
    PHASE_TYPE_MULTI = 0,
    PHASE_TYPE_SINGLE
} PHASE_TYPE;

typedef struct {
    uint32_t  disk_index;
    uint64_t  physical_offset;
    uint32_t  length_bytes;
} IO_MAPPING_ENTRY;

typedef struct {
    uint32_t  active_count;
    uint32_t  active_disk_indices[MAX_DISKS];
    uint32_t  ratios[MAX_DISKS];
    uint32_t  total_ratio;
    uint64_t  virtual_start_bytes;
    uint64_t  virtual_size_bytes;
    uint64_t  physical_starts_bytes[MAX_DISKS];
    uint64_t  cycle_size_bytes;
} MAPPING_PHASE;

typedef struct {
    uint8_t*  buffer;
    uint64_t  cache_size_bytes;
    uint32_t  block_size;
    uint32_t  block_count;
    uint8_t*  dirty_map;
    uint8_t*  valid_map;
    uint64_t  hit_count;
    uint64_t  miss_count;
    HANDLE    flush_thread;
    volatile LONG running;
    CRITICAL_SECTION lock;
    uint8_t*  flush_buffer;
    uint32_t  flush_buffer_size;
    volatile LONG write_through;
} RAM_CACHE;

typedef struct {
    wchar_t    label[64];
    DISK_INFO* disks[MAX_DISKS];
    uint32_t   disk_count;
    uint32_t   stripe_unit;
    uint32_t   phase_count;
    MAPPING_PHASE phases[MAX_DISKS];
    uint64_t   virtual_total_bytes;
    uint64_t   generation;
    VOLUME_UUID volume_uuid;
    uint64_t   created_time;
    uint64_t   last_mount_time;
    RAM_CACHE  cache;
    bool       cache_enabled;
    char       mount_point[4];
    volatile uint64_t bytes_written;
    volatile uint64_t bytes_read;
    LARGE_INTEGER start_time;
    volatile LONG cache_flush_in_progress;
    uint32_t   raid_level;
    volatile LONG healthy_count;
} STRIPE_VOLUME;

typedef struct {
    uint32_t disk_id;
    char     drive_letter;
    uint64_t pool_mb;
} DISK_CONFIG;

typedef struct {
    uint32_t   version;
    DISK_CONFIG disks[MAX_DISKS];
    uint32_t   disk_count;
    uint32_t   cache_mb;
    char       mount_letter;
    bool       auto_bench;
} APP_CONFIG;
