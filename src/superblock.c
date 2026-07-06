#include "superblock.h"
#include "mirror_engine.h"
#include "pool_io.h"

/* ---- v1 struct for backward-compat reads ---- */
#pragma pack(push, 1)
typedef struct {
    uint32_t  magic;
    uint32_t  version;
    uint32_t  disk_count;
    uint32_t  stripe_unit;
    uint64_t  virtual_total_bytes;
    uint32_t  phase_count;
    SB_DISK_ENTRY disks[MAX_DISKS];
    MAPPING_PHASE  phases[MAX_DISKS];
    uint32_t  checksum;
} SUPERBLOCK_V1;

/* ---- v2 struct for backward-compat reads (SB_DISK_ENTRY_V2 = 24 bytes, no serial) ---- */
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
    SB_DISK_ENTRY_V2 disks[MAX_DISKS];
    MAPPING_PHASE  phases[MAX_DISKS];
    uint32_t  checksum;
} SUPERBLOCK_V2;

#pragma pack(pop)

static uint64_t get_filetime_now(void) {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

static int64_t filetime_to_unix_sec(uint64_t ft) {
    return (int64_t)((ft - 116444736000000000ULL) / 10000000);
}

/* ---- Write superblock (always v4) ---- */
bool superblock_write(STRIPE_VOLUME* vol) {
    if (!vol || vol->disk_count == 0 || vol->disk_count > MAX_DISKS) return false;

    SUPERBLOCK sb;
    memset(&sb, 0, sizeof(sb));
    sb.magic = SUPERBLOCK_MAGIC;
    sb.version = SUPERBLOCK_VERSION;
    sb.disk_count = vol->disk_count;
    sb.stripe_unit = vol->stripe_unit;
    sb.virtual_total_bytes = vol->virtual_total_bytes;
    sb.phase_count = vol->phase_count;

    vol->generation++;
    sb.generation = vol->generation;
    sb.timestamp = get_filetime_now();
    sb.feature_flags = 0;
    if (vol->cache_enabled) sb.feature_flags |= SB_FEATURE_CACHE;
    if (vol->raid_level == RAID_LEVEL_MIRROR) sb.feature_flags |= SB_FEATURE_MIRROR;
    sb.journal_offset = 0;
    sb.volume_uuid = vol->volume_uuid;
    sb.created_time = vol->created_time;
    if (sb.created_time == 0) sb.created_time = sb.timestamp;
    sb.last_mount_time = sb.timestamp;

    for (uint32_t i = 0; i < vol->disk_count; i++) {
        DISK_INFO* disk = vol->disks[i];
        wchar_t root[4] = { disk->drive_letter[0], L':', L'\\', 0 };
        wcscpy_s(sb.disks[i].drive_letter, 4, root);
        sb.disks[i].bench_write_mbs = disk->bench_write_mbs;
        sb.disks[i].bench_read_mbs = disk->bench_read_mbs;
        sb.disks[i].pool_bytes = disk->pool_bytes;
        strncpy_s(sb.disks[i].serial_number, MAX_SERIAL_LEN, disk->serial_number, _TRUNCATE);
    }

    for (uint32_t p = 0; p < vol->phase_count && p < MAX_DISKS; p++)
        memcpy(&sb.phases[p], &vol->phases[p], sizeof(MAPPING_PHASE));

    sb.checksum = crc32((const uint8_t*)&sb, offsetof(SUPERBLOCK, checksum));

    bool any_success = false;
    for (uint32_t di = 0; di < vol->disk_count; di++) {
        wchar_t dir_path[MAX_DRIVE_PATH], tmp_path[MAX_DRIVE_PATH], final_path[MAX_DRIVE_PATH];
        wchar_t root[4] = { vol->disks[di]->drive_letter[0], L':', L'\\', 0 };
        StringCchPrintfW(dir_path, MAX_DRIVE_PATH, L"%ls%s", root, CONFIG_DIR);
        StringCchPrintfW(tmp_path, MAX_DRIVE_PATH, L"%ls\\%ls.tmp", dir_path, SUPERBLOCK_FILENAME);
        StringCchPrintfW(final_path, MAX_DRIVE_PATH, L"%ls\\%ls", dir_path, SUPERBLOCK_FILENAME);

        CreateDirectoryW(dir_path, NULL);
        DeleteFileW(tmp_path);

        HANDLE h = CreateFileW(tmp_path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) continue;

        DWORD written = 0;
        BOOL ok = WriteFile(h, &sb, sizeof(sb), &written, NULL) && written == sizeof(sb);
        if (ok) ok = FlushFileBuffers(h);
        CloseHandle(h);

        if (!ok) { DeleteFileW(tmp_path); continue; }

        if (!MoveFileExW(tmp_path, final_path, MOVEFILE_REPLACE_EXISTING)) {
            DeleteFileW(tmp_path);
            continue;
        }
        any_success = true;
    }

    if (!any_success) {
        LOG_ERROR("Superblock write failed on all disks");
        return false;
    }
    LOG_OK("Superblock written to %u disk(s) (generation=%llu)", vol->disk_count,
           (unsigned long long)sb.generation);
    return true;
}

/* ---- Orphan .tmp recovery ---- */
static void try_recover_orphan_tmp(const wchar_t* drive_root) {
    wchar_t tmp_path[MAX_DRIVE_PATH], final_path[MAX_DRIVE_PATH];
    StringCchPrintfW(tmp_path, MAX_DRIVE_PATH, L"%ls%s\\%ls.tmp", drive_root, CONFIG_DIR, SUPERBLOCK_FILENAME);
    StringCchPrintfW(final_path, MAX_DRIVE_PATH, L"%ls%s\\%ls", drive_root, CONFIG_DIR, SUPERBLOCK_FILENAME);
    HANDLE h = CreateFileW(tmp_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
        LOG_WARN("Recovering orphan superblock.tmp on %ls", drive_root);
        MoveFileExW(tmp_path, final_path, MOVEFILE_REPLACE_EXISTING);
    }
}

/* ---- Read a single drive's superblock (v1 or v2) ---- */
static bool try_read_superblock_from_drive(const wchar_t* drive_root, SUPERBLOCK* sb_out) {
    try_recover_orphan_tmp(drive_root);
    wchar_t sb_path[MAX_DRIVE_PATH];
    StringCchPrintfW(sb_path, MAX_DRIVE_PATH, L"%ls%s\\%ls", drive_root, CONFIG_DIR, SUPERBLOCK_FILENAME);

    HANDLE h = CreateFileW(sb_path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;

    /* Read into a larger buffer to handle both v1 and v2 sizes */
    uint8_t raw[2048];
    DWORD read = 0;
    BOOL read_ok = ReadFile(h, raw, sizeof(raw), &read, NULL);
    CloseHandle(h);
    if (!read_ok) return false;

    /* Check magic at offset 0 */
    uint32_t magic;
    if (read < 4) return false;
    memcpy(&magic, raw, 4);
    if (magic != SUPERBLOCK_MAGIC) return false;

    /* Check version at offset 4 */
    uint32_t version;
    if (read < 8) return false;
    memcpy(&version, raw + 4, 4);

    memset(sb_out, 0, sizeof(SUPERBLOCK));
    sb_out->magic = magic;
    sb_out->version = version;

    if (version == 1) {
        /* v1 layout: header (28) + disks + phases + checksum(4) */
        if (read < sizeof(SUPERBLOCK_V1)) return false;
        SUPERBLOCK_V1 v1;
        memcpy(&v1, raw, sizeof(SUPERBLOCK_V1));

        uint32_t saved_checksum = v1.checksum;
        v1.checksum = 0;
        uint32_t actual = crc32((const uint8_t*)&v1, offsetof(SUPERBLOCK_V1, checksum));
        if (actual != saved_checksum) return false;
        v1.checksum = saved_checksum;

        /* Convert v1 → v2 (new fields already zeroed) */
        sb_out->version = 2; // upgrade on read
        sb_out->disk_count = v1.disk_count;
        sb_out->stripe_unit = v1.stripe_unit;
        sb_out->virtual_total_bytes = v1.virtual_total_bytes;
        sb_out->phase_count = v1.phase_count;
        sb_out->generation = 0;
        sb_out->timestamp = 0;
        sb_out->feature_flags = 0;
        sb_out->journal_offset = 0;
        memcpy(sb_out->disks, v1.disks, sizeof(v1.disks));
        memcpy(sb_out->phases, v1.phases, sizeof(v1.phases));
        sb_out->checksum = saved_checksum;

        LOG_WARN("Upgraded v1 superblock on %ls (migrate by re-creating volume)", drive_root);
        return true;
    } else if (version == 2) {
        if (read < sizeof(SUPERBLOCK_V2)) return false;
        SUPERBLOCK_V2 v2;
        memcpy(&v2, raw, sizeof(SUPERBLOCK_V2));

        uint32_t saved_checksum = v2.checksum;
        v2.checksum = 0;
        uint32_t actual = crc32((const uint8_t*)&v2, offsetof(SUPERBLOCK_V2, checksum));
        if (actual != saved_checksum) return false;
        v2.checksum = saved_checksum;

        /* Convert v2 → v3 (add blank serial) */
        sb_out->magic = v2.magic;
        sb_out->version = 3;
        sb_out->disk_count = v2.disk_count;
        sb_out->stripe_unit = v2.stripe_unit;
        sb_out->virtual_total_bytes = v2.virtual_total_bytes;
        sb_out->phase_count = v2.phase_count;
        sb_out->generation = v2.generation;
        sb_out->timestamp = v2.timestamp;
        sb_out->feature_flags = v2.feature_flags;
        sb_out->journal_offset = v2.journal_offset;
        for (uint32_t i = 0; i < MAX_DISKS; i++) {
            sb_out->disks[i].drive_letter[0] = v2.disks[i].drive_letter[0];
            sb_out->disks[i].drive_letter[1] = v2.disks[i].drive_letter[1];
            sb_out->disks[i].drive_letter[2] = v2.disks[i].drive_letter[2];
            sb_out->disks[i].drive_letter[3] = v2.disks[i].drive_letter[3];
            sb_out->disks[i].bench_write_mbs = v2.disks[i].bench_write_mbs;
            sb_out->disks[i].bench_read_mbs = v2.disks[i].bench_read_mbs;
            sb_out->disks[i].pool_bytes = v2.disks[i].pool_bytes;
            sb_out->disks[i].serial_number[0] = '\0';
        }
        memcpy(sb_out->phases, v2.phases, sizeof(v2.phases));
        sb_out->checksum = v2.checksum;
        return true;
    } else if (version == 3) {
        if (read < sizeof(SUPERBLOCK_V3)) return false;
        SUPERBLOCK_V3 v3;
        memcpy(&v3, raw, sizeof(SUPERBLOCK_V3));

        uint32_t saved_checksum = v3.checksum;
        v3.checksum = 0;
        uint32_t actual = crc32((const uint8_t*)&v3, offsetof(SUPERBLOCK_V3, checksum));
        if (actual != saved_checksum) return false;
        v3.checksum = saved_checksum;

        /* Upgrade v3 → v4 (zero UUID fields) */
        memset(sb_out, 0, sizeof(SUPERBLOCK));
        sb_out->magic = v3.magic;
        sb_out->version = 4;
        sb_out->disk_count = v3.disk_count;
        sb_out->stripe_unit = v3.stripe_unit;
        sb_out->virtual_total_bytes = v3.virtual_total_bytes;
        sb_out->phase_count = v3.phase_count;
        sb_out->generation = v3.generation;
        sb_out->timestamp = v3.timestamp;
        sb_out->feature_flags = v3.feature_flags;
        sb_out->journal_offset = v3.journal_offset;
        memset(&sb_out->volume_uuid, 0, sizeof(VOLUME_UUID));
        sb_out->created_time = 0;
        sb_out->last_mount_time = 0;
        memcpy(sb_out->disks, v3.disks, sizeof(v3.disks));
        memcpy(sb_out->phases, v3.phases, sizeof(v3.phases));
        sb_out->checksum = v3.checksum;
        return true;
    } else if (version == 4) {
        if (read < sizeof(SUPERBLOCK)) return false;
        SUPERBLOCK sb;
        memcpy(&sb, raw, sizeof(SUPERBLOCK));

        uint32_t saved_checksum = sb.checksum;
        sb.checksum = 0;
        uint32_t actual = crc32((const uint8_t*)&sb, offsetof(SUPERBLOCK, checksum));
        if (actual != saved_checksum) return false;
        sb.checksum = saved_checksum;

        *sb_out = sb;
        return true;
    }

    return false;
}

/* ---- Serial-based disk matching helper ---- */
static int match_disk_by_serial(const char* serial, DISK_INFO** physical_disks, uint32_t physical_count) {
    if (!serial || serial[0] == '\0') return -1;
    for (uint32_t i = 0; i < physical_count; i++) {
        if (physical_disks[i] && strcmp(serial, physical_disks[i]->serial_number) == 0)
            return (int)i;
    }
    return -1;
}

/* ---- Try to restore volume matching SB disks to physical disks by serial ---- */
bool superblock_restore(const SUPERBLOCK* sb, DISK_INFO** physical_disks, uint32_t physical_count,
                         STRIPE_VOLUME* vol, DISK_INFO* disks_out, uint32_t* disk_count_out) {
    if (!sb || !vol || !disks_out || !disk_count_out) return false;
    if (sb->disk_count < MIN_DISKS || sb->disk_count > MAX_DISKS) return false;

    memset(vol, 0, sizeof(STRIPE_VOLUME));
    vol->disk_count = sb->disk_count;
    vol->stripe_unit = sb->stripe_unit;
    vol->virtual_total_bytes = sb->virtual_total_bytes;
    vol->generation = sb->generation;
    vol->volume_uuid = sb->volume_uuid;
    vol->created_time = sb->created_time;
    vol->last_mount_time = sb->last_mount_time;

    bool is_mirror = (sb->feature_flags & SB_FEATURE_MIRROR) != 0;
    if (is_mirror) {
        vol->raid_level = RAID_LEVEL_MIRROR;
        vol->phase_count = 0;
        vol->healthy_count = 0;
    } else {
        vol->phase_count = sb->phase_count;
        for (uint32_t i = 0; i < sb->phase_count && i < MAX_DISKS; i++)
            memcpy(&vol->phases[i], &sb->phases[i], sizeof(MAPPING_PHASE));
    }

    uint32_t matched = 0;
    for (uint32_t i = 0; i < sb->disk_count; i++) {
        DISK_INFO* d = &disks_out[i];
        memset(d, 0, sizeof(DISK_INFO));

        int phys_idx = -1;
        if (sb->disks[i].serial_number[0] != '\0')
            phys_idx = match_disk_by_serial(sb->disks[i].serial_number, physical_disks, physical_count);

        /* Serial match: use physical disk's drive letter */
        if (phys_idx >= 0) {
            DISK_INFO* phys = physical_disks[phys_idx];
            wcscpy_s(d->drive_letter, MAX_DRIVE_PATH, phys->drive_letter);
            d->pool_bytes = sb->disks[i].pool_bytes;
            d->bench_write_mbs = sb->disks[i].bench_write_mbs;
            d->bench_read_mbs = sb->disks[i].bench_read_mbs;
            d->benchmarked = true;
            strncpy_s(d->serial_number, MAX_SERIAL_LEN, phys->serial_number, _TRUNCATE);
            matched++;
        } else {
            /* No serial match: fall back to SB drive_letter */
            wcscpy_s(d->drive_letter, MAX_DRIVE_PATH, sb->disks[i].drive_letter);
            d->pool_bytes = sb->disks[i].pool_bytes;
            d->bench_write_mbs = sb->disks[i].bench_write_mbs;
            d->bench_read_mbs = sb->disks[i].bench_read_mbs;
            d->benchmarked = true;
            strncpy_s(d->serial_number, MAX_SERIAL_LEN, sb->disks[i].serial_number, _TRUNCATE);
        }

        StringCchPrintfW(d->file_path, MAX_DRIVE_PATH, L"%ls%s\\%ls",
                         d->drive_letter, CONFIG_DIR, POOL_FILENAME);

        if (!pool_file_open(d)) {
            char path_a[MAX_DRIVE_PATH] = {0};
            wcstombs(path_a, d->file_path, MAX_DRIVE_PATH - 1);
            LOG_ERROR("Failed to open pool file: %s", path_a);
            for (uint32_t j = 0; j < i; j++) pool_file_close(&disks_out[j]);
            return false;
        }

        if (is_mirror) {
            InterlockedExchange(&d->healthy, 1);
            InterlockedIncrement(&vol->healthy_count);
        }
        vol->disks[i] = d;
        LOG_INFO("  Restored disk %u: %ls (serial=%s match=%s)", i, d->drive_letter,
                 d->serial_number, phys_idx >= 0 ? "yes" : "no");
    }

    QueryPerformanceCounter(&vol->start_time);
    *disk_count_out = sb->disk_count;

    LOG_OK("Restored %u/%u disks by serial from v%u superblock (generation=%llu)",
           matched, sb->disk_count, sb->version, (unsigned long long)sb->generation);
    return true;
}

/* ---- Public: read a single drive's raw superblock ---- */
bool superblock_read_raw(const wchar_t* drive_root, SUPERBLOCK* sb_out) {
    return try_read_superblock_from_drive(drive_root, sb_out);
}

/* ---- Public read: scan all drives, pick best superblock, restore ---- */
bool superblock_read(const wchar_t* drive_root, STRIPE_VOLUME* vol, DISK_INFO* disks_out, uint32_t* disk_count_out,
                     DISK_INFO** physical_disks, uint32_t physical_count) {
    if (!vol || !disks_out || !disk_count_out) return false;

    SUPERBLOCK candidates[MAX_DISKS];
    wchar_t candidate_roots[MAX_DISKS][4];
    uint32_t candidate_count = 0;

    /* Try specified drive first */
    if (drive_root && drive_root[0] >= L'A' && drive_root[0] <= L'Z') {
        if (try_read_superblock_from_drive(drive_root, &candidates[candidate_count])) {
            wcsncpy_s(candidate_roots[candidate_count], 4, drive_root, 3);
            candidate_count++;
        }
    }

    /* Scan ALL fixed drives */
    for (wchar_t letter = L'C'; letter <= L'Z'; letter++) {
        if (candidate_count >= MAX_DISKS) break;
        wchar_t root[4] = { letter, L':', L'\\', 0 };
        if (GetDriveTypeW(root) != DRIVE_FIXED) continue;
        if (letter == (drive_root && drive_root[0] >= L'A' ? drive_root[0] : 0)) continue;
        if (try_read_superblock_from_drive(root, &candidates[candidate_count])) {
            wcsncpy_s(candidate_roots[candidate_count], 4, root, 3);
            LOG_INFO("Superblock found on %ls (generation=%llu)", root,
                     (unsigned long long)candidates[candidate_count].generation);
            candidate_count++;
        }
    }

    if (candidate_count == 0) {
        LOG_ERROR("No valid superblock found on any drive");
        return false;
    }

    /* Pick best by generation (highest = latest), then timestamp, then virtual_total_bytes */
    uint32_t best_idx = 0;
    if (candidate_count > 1) {
        for (uint32_t i = 1; i < candidate_count; i++) {
            bool pick = false;
            if (candidates[i].generation > candidates[best_idx].generation) pick = true;
            else if (candidates[i].generation == candidates[best_idx].generation &&
                     candidates[i].timestamp > candidates[best_idx].timestamp) pick = true;
            else if (candidates[i].generation == candidates[best_idx].generation &&
                     candidates[i].timestamp == candidates[best_idx].timestamp &&
                     candidates[i].virtual_total_bytes > candidates[best_idx].virtual_total_bytes) pick = true;
            if (pick) best_idx = i;
        }
        LOG_WARN("%u superblocks found, picking generation=%llu (disk %u)",
                 candidate_count, (unsigned long long)candidates[best_idx].generation, best_idx);
    }

    SUPERBLOCK sb = candidates[best_idx];

    LOG_OK("Superblock v%u loaded from %ls (generation=%llu)", sb.version,
           candidate_roots[best_idx], (unsigned long long)sb.generation);

    /* Restore using serial matching */
    return superblock_restore(&sb, physical_disks, physical_count, vol, disks_out, disk_count_out);
}

/* ---- Format superblock metadata as human-readable string ---- */
void superblock_format_str(const SUPERBLOCK* sb, char* out, size_t out_size) {
    if (!sb || !out || out_size == 0) return;

    char uuid_str[64] = "0000000000000000-0000000000000000";
    if (sb->version >= 4) uuid_to_str(&sb->volume_uuid, uuid_str, sizeof(uuid_str));

    int64_t created_sec = sb->created_time ? filetime_to_unix_sec(sb->created_time) : 0;
    int64_t mount_sec = sb->last_mount_time ? filetime_to_unix_sec(sb->last_mount_time) : 0;
    int64_t timestamp_sec = sb->timestamp ? filetime_to_unix_sec(sb->timestamp) : 0;

    char buf[4096] = {0};
    size_t pos = 0;

    pos += snprintf(buf + pos, sizeof(buf) - pos,
        "Superblock v%u\n"
        "  UUID:          %s\n"
        "  Magic:         0x%08X\n"
        "  Version:       %u\n"
        "  Disk Count:    %u\n"
        "  Stripe Unit:   %u bytes\n"
        "  Virtual Size:  %llu bytes (%llu MB)\n"
        "  Phases:        %u\n"
        "  Generation:    %llu\n"
        "  Timestamp:     %lld\n"
        "  Created:       %lld\n"
        "  Last Mount:    %lld\n"
        "  Features:      0x%08X\n"
        "  Journal Off:   %llu\n"
        "  Checksum:      0x%08X\n",
        sb->version,
        uuid_str,
        (unsigned int)sb->magic,
        sb->version,
        sb->disk_count,
        sb->stripe_unit,
        (unsigned long long)sb->virtual_total_bytes,
        (unsigned long long)(sb->virtual_total_bytes / (1024 * 1024)),
        sb->phase_count,
        (unsigned long long)sb->generation,
        (long long)timestamp_sec,
        (long long)created_sec,
        (long long)mount_sec,
        (unsigned int)sb->feature_flags,
        (unsigned long long)sb->journal_offset,
        (unsigned int)sb->checksum);

    for (uint32_t i = 0; i < sb->disk_count && i < MAX_DISKS; i++) {
        char drive_a[8] = {0};
        wcstombs(drive_a, sb->disks[i].drive_letter, 7);
        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "  Disk %u: %s (W=%u R=%u pool=%llu serial=%s)\n",
            i, drive_a,
            sb->disks[i].bench_write_mbs,
            sb->disks[i].bench_read_mbs,
            (unsigned long long)sb->disks[i].pool_bytes,
            sb->disks[i].serial_number);
    }

    strncpy_s(out, out_size, buf, _TRUNCATE);
}
