#include "journal.h"
#include "logger.h"

/* ---- CRC32 (same as superblock.c) ---- */
static uint32_t crc32(const uint8_t* data, size_t len) {
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

static uint64_t get_filetime_now(void) {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

static void make_je(JOURNAL_ENTRY* je, JOURNAL_TYPE type, uint64_t gen,
                    uint64_t virt_off, uint32_t len, const void* data) {
    memset(je, 0, sizeof(*je));
    je->magic = JOURNAL_MAGIC;
    je->version = JOURNAL_VERSION;
    je->entry_type = (uint32_t)type;
    je->generation = gen;
    je->timestamp = get_filetime_now();
    je->virtual_offset = virt_off;
    je->length = len;
    je->data_crc = 0;
    if (data && len > 0)
        je->data_crc = crc32((const uint8_t*)data, len);
    je->checksum = crc32((const uint8_t*)je, offsetof(JOURNAL_ENTRY, checksum));
}

/* ---- Write an entry to the journal file on all disks ---- */
static bool journal_write_entry(STRIPE_VOLUME* vol, JOURNAL_ENTRY* je) {
    bool any_ok = false;
    for (uint32_t di = 0; di < vol->disk_count; di++) {
        wchar_t path[MAX_DRIVE_PATH];
        wchar_t root[4] = { vol->disks[di]->drive_letter[0], L':', L'\\', 0 };
        StringCchPrintfW(path, MAX_DRIVE_PATH, L"%ls%s\\%ls", root, CONFIG_DIR, JOURNAL_FILENAME);

        HANDLE h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) continue;

        SetFilePointer(h, 0, NULL, FILE_END);
        DWORD written = 0;
        BOOL ok = WriteFile(h, je, sizeof(*je), &written, NULL) && written == sizeof(*je);
        if (ok) ok = FlushFileBuffers(h);
        CloseHandle(h);
        if (ok) any_ok = true;
    }
    return any_ok;
}

/* ---- Write BEGIN entry (start of flush cycle) ---- */
bool journal_begin(STRIPE_VOLUME* vol) {
    if (!vol) return false;
    JOURNAL_ENTRY je;
    make_je(&je, JT_BEGIN, vol->generation, 0, 0, NULL);
    return journal_write_entry(vol, &je);
}

/* ---- Write DATA entry + payload for a flushed block range ---- */
bool journal_data(STRIPE_VOLUME* vol, uint64_t virtual_offset, uint32_t length, const void* data) {
    if (!vol) return false;
    JOURNAL_ENTRY je;
    make_je(&je, JT_DATA, vol->generation, virtual_offset, length, data);
    bool any_ok = false;
    for (uint32_t di = 0; di < vol->disk_count; di++) {
        wchar_t path[MAX_DRIVE_PATH];
        wchar_t root[4] = { vol->disks[di]->drive_letter[0], L':', L'\\', 0 };
        StringCchPrintfW(path, MAX_DRIVE_PATH, L"%ls%s\\%ls", root, CONFIG_DIR, JOURNAL_FILENAME);
        HANDLE h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) continue;
        SetFilePointer(h, 0, NULL, FILE_END);
        DWORD written = 0;
        BOOL ok = WriteFile(h, &je, sizeof(je), &written, NULL) && written == sizeof(je);
        if (ok && data && length > 0)
            ok = WriteFile(h, data, length, &written, NULL) && written == length;
        if (ok) ok = FlushFileBuffers(h);
        CloseHandle(h);
        if (ok) any_ok = true;
    }
    return any_ok;
}

/* ---- Write COMMIT entry (flush cycle complete) ---- */
bool journal_commit(STRIPE_VOLUME* vol) {
    if (!vol) return false;
    JOURNAL_ENTRY je;
    make_je(&je, JT_COMMIT, vol->generation, 0, 0, NULL);
    return journal_write_entry(vol, &je);
}

/* ---- Recovery: scan journal files and replay incomplete flushes ---- */
bool journal_recover_all(STRIPE_VOLUME* vol) {
    if (!vol) return false;

    bool all_clean = true;
    for (uint32_t di = 0; di < vol->disk_count; di++) {
        wchar_t path[MAX_DRIVE_PATH];
        wchar_t root[4] = { vol->disks[di]->drive_letter[0], L':', L'\\', 0 };
        StringCchPrintfW(path, MAX_DRIVE_PATH, L"%ls%s\\%ls", root, CONFIG_DIR, JOURNAL_FILENAME);

        HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) continue;

        uint8_t raw[65536];
        DWORD read = 0;
        if (!ReadFile(h, raw, sizeof(raw), &read, NULL)) { CloseHandle(h); continue; }
        CloseHandle(h);

        if (read < sizeof(JOURNAL_ENTRY)) continue;

        uint32_t offset = 0;
        bool has_begin = false;
        bool has_commit = false;
        uint64_t begin_gen = 0;
        uint32_t range_count = 0;
        struct { uint64_t off; uint32_t len; uint32_t data_off; } ranges[256];

        while (offset + sizeof(JOURNAL_ENTRY) <= read) {
            JOURNAL_ENTRY je;
            memcpy(&je, raw + offset, sizeof(je));
            offset += sizeof(je);

            if (je.magic != JOURNAL_MAGIC) break;

            uint32_t saved_cs = je.checksum;
            je.checksum = 0;
            uint32_t actual = crc32((const uint8_t*)&je, offsetof(JOURNAL_ENTRY, checksum));
            je.checksum = saved_cs;
            if (actual != saved_cs) { offset = read; break; }

            uint32_t payload = 0;
            if (je.version >= 2 && je.entry_type == JT_DATA)
                payload = je.length;

            if (je.entry_type == JT_BEGIN) {
                has_begin = true; has_commit = false;
                begin_gen = je.generation; range_count = 0;
            } else if (je.entry_type == JT_DATA && has_begin && !has_commit) {
                if (je.version >= 2 && range_count < 256 && offset + payload <= read) {
                    ranges[range_count].off = je.virtual_offset;
                    ranges[range_count].len = je.length;
                    ranges[range_count].data_off = offset;
                    range_count++;
                }
            } else if (je.entry_type == JT_COMMIT && has_begin) {
                has_commit = true;
            }

            offset += payload;
            if (offset > read) offset = read;
        }

        bool clean = (!has_begin || has_commit);

        if (!clean) {
            all_clean = false;
            LOG_WARN("Incomplete journal on %ls (gen=%llu, %u ranges) — replaying...",
                     root, (unsigned long long)begin_gen, range_count);
            uint32_t replayed = 0, failed = 0;
            for (uint32_t i = 0; i < range_count; i++) {
                if (ranges[i].data_off == 0 || ranges[i].len == 0) { failed++; continue; }
                bool ok = stripe_volume_write(vol, raw + ranges[i].data_off,
                                              ranges[i].off, ranges[i].len);
                if (ok) replayed++; else failed++;
            }
            if (failed == 0)
                LOG_OK("Journal replay OK: %u ranges written", replayed);
            else
                LOG_ERROR("Journal replay: %u OK, %u FAILED", replayed, failed);
        } else {
            LOG_OK("Journal on %ls: clean", root);
        }

        h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }

    return all_clean;
}
