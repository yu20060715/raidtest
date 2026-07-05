#include "config.h"

bool config_get_path(wchar_t* path, uint32_t max_len) {
    if (!path) return false;
    wchar_t appdata[MAX_DRIVE_PATH];
    if (GetEnvironmentVariableW(L"APPDATA", appdata, MAX_DRIVE_PATH) == 0) return false;
    StringCchPrintfW(path, max_len, L"%s\\RAIDTEST\\config.json", appdata);
    return true;
}

void config_defaults(APP_CONFIG* cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(APP_CONFIG));
    cfg->version = 1;
    cfg->cache_mb = CACHE_DEFAULT_MB;
    cfg->mount_letter = 'G';
    cfg->auto_bench = true;
}

bool config_save(APP_CONFIG* cfg) {
    if (!cfg) return false;
    wchar_t path[MAX_DRIVE_PATH];
    if (!config_get_path(path, MAX_DRIVE_PATH)) return false;

    wchar_t dir[MAX_DRIVE_PATH];
    StringCchPrintfW(dir, MAX_DRIVE_PATH, L"%s\\RAIDTEST", _wgetenv(L"APPDATA"));
    CreateDirectoryW(dir, NULL);

    FILE* f = _wfopen(path, L"w, ccs=UTF-8");
    if (!f) { LOG_ERROR("Cannot write config: %ls", path); return false; }

    fwprintf(f, L"{\n");
    fwprintf(f, L"  \"version\": %u,\n", cfg->version);
    fwprintf(f, L"  \"cache_mb\": %u,\n", cfg->cache_mb);
    fwprintf(f, L"  \"mount_letter\": \"%c\",\n", cfg->mount_letter);
    fwprintf(f, L"  \"auto_bench\": %ls,\n", cfg->auto_bench ? L"true" : L"false");
    fwprintf(f, L"  \"disks\": [\n");
    for (uint32_t i = 0; i < cfg->disk_count; i++) {
        fwprintf(f, L"    {\"id\": %u, \"drive\": \"%c\", \"pool_mb\": %llu}%s\n",
                 cfg->disks[i].disk_id, cfg->disks[i].drive_letter,
                 (unsigned long long)cfg->disks[i].pool_mb,
                 (i + 1 < cfg->disk_count) ? L"," : L"");
    }
    fwprintf(f, L"  ]\n}\n");
    fclose(f);
    LOG_OK("Config saved to %ls", path);
    return true;
}

bool config_load(APP_CONFIG* cfg) {
    if (!cfg) return false;
    config_defaults(cfg);
    wchar_t path[MAX_DRIVE_PATH];
    if (!config_get_path(path, MAX_DRIVE_PATH)) return false;

    FILE* f = _wfopen(path, L"r, ccs=UTF-8");
    if (!f) {
        LOG_INFO("No existing config found at %ls (first run?)", path);
        return true;
    }

    wchar_t line[512];
    int line_num = 0;
    while (fgetws(line, 512, f)) {
        line_num++;
        wchar_t* p = line;
        while (*p && *p <= L' ') p++;
        if (*p == L'{' || *p == L'}' || *p == L'[' || *p == L']') continue;
        if (wcsstr(p, L"\"disks\"") != NULL) continue;
        bool parsed = false;
        if (wcsstr(p, L"\"version\":")) { int v; if (swscanf(p, L" \"version\": %d,", &v) >= 1) { cfg->version = (uint32_t)v; parsed = true; } }
        else if (wcsstr(p, L"\"cache_mb\":")) { int v; if (swscanf(p, L" \"cache_mb\": %d,", &v) >= 1) { cfg->cache_mb = (uint32_t)v; parsed = true; } }
        else if (wcsstr(p, L"\"mount_letter\":")) { char c; if (swscanf(p, L" \"mount_letter\": \"%c\",", &c) >= 1) { cfg->mount_letter = c; parsed = true; } }
        else if (wcsstr(p, L"\"auto_bench\":")) { cfg->auto_bench = (wcsstr(p, L"true") != NULL); parsed = true; }
        else if (wcsstr(p, L"\"id\":")) {
            if (cfg->disk_count < MAX_DISKS) {
                int id, drive;
                unsigned long long pool;
                if (swscanf(p, L" {\"id\": %d, \"drive\": \"%c\", \"pool_mb\": %llu}",
                           &id, &drive, &pool) >= 3) {
                    cfg->disks[cfg->disk_count].disk_id = (uint32_t)id;
                    cfg->disks[cfg->disk_count].drive_letter = (char)drive;
                    cfg->disks[cfg->disk_count].pool_mb = pool;
                    cfg->disk_count++;
                    parsed = true;
                } else {
                    LOG_WARN("Config line %d: invalid disk entry: %ls", line_num, p);
                }
            }
        }
        if (!parsed && *p != L'\0') {
            LOG_WARN("Config line %d: unrecognized or malformed: %ls", line_num, p);
        }
    }
    fclose(f);
    LOG_OK("Config loaded: %u disks, cache=%u MB, mount=%c:",
           cfg->disk_count, cfg->cache_mb, cfg->mount_letter);
    return true;
}