#include "cleanup.h"
#include "disk_scanner.h"
#include "pool_io.h"
#include "fuse_bridge.h"
#include "config.h"
#include "ram_cache.h"
#include "superblock.h"

void cleanup_cache(APP_STATE* state) {
    if (!state->cache_on) return;
    state->volume.cache.running = 0;
    cache_flush_all(&state->volume.cache, &state->volume);
    cache_destroy(&state->volume.cache);
    state->flush_thread = NULL;
    state->volume.cache_enabled = false;
    state->cache_on = false;
}

void cleanup_volume(APP_STATE* state) {
    if (state->mounted) {
        fuse_unmount_volume(state->volume.mount_point[0]);
        state->mounted = false;
    }
    if (state->volume_valid) {
        stripe_volume_destroy(&state->volume);
        state->volume_valid = false;
    }
}

void cleanup_pool_files(APP_STATE* state) {
    for (uint32_t i = 0; i < state->disk_count; i++) {
        pool_file_close(state->disks[i]);
        pool_file_delete(state->disks[i]);
        pool_dir_delete(state->disks[i]);
    }
    state->disk_count = 0;
}

void cleanup_disks(APP_STATE* state) {
    if (state->physical_disks) {
        disk_scan_free(state->physical_disks, state->physical_count);
        state->physical_disks = NULL;
        state->physical_count = 0;
    }
}

void cleanup_session(APP_STATE* state) {
    cleanup_cache(state);
    cleanup_volume(state);
    // Note: pool files are NOT deleted here — keeps them for "load" persistence.
    // Call cleanup_pool_session() separately to delete pool files + superblock.
}

void cleanup_pool_session(APP_STATE* state) {
    // Save count before cleanup_pool_files resets it
    uint32_t saved_count = state->disk_count;
    cleanup_pool_files(state);
    for (uint32_t i = 0; i < saved_count; i++) {
        wchar_t sb_path[MAX_DRIVE_PATH], dir_path[MAX_DRIVE_PATH];
        wchar_t root[4] = { state->disks[i]->drive_letter[0], L':', L'\\', 0 };
        StringCchPrintfW(sb_path, MAX_DRIVE_PATH, L"%ls%s\\%ls", root, CONFIG_DIR, SUPERBLOCK_FILENAME);
        StringCchPrintfW(dir_path, MAX_DRIVE_PATH, L"%ls%s", root, CONFIG_DIR);
        DeleteFileW(sb_path);
        RemoveDirectoryW(dir_path);
    }
}

static void cleanup_scan_all_drives(void) {
    for (wchar_t letter = L'A'; letter <= L'Z'; letter++) {
        wchar_t root[4] = { letter, L':', L'\\', 0 };
        if (GetDriveTypeW(root) != DRIVE_FIXED) continue;
        wchar_t pool_path[MAX_DRIVE_PATH];
        StringCchPrintfW(pool_path, MAX_DRIVE_PATH, L"%c:\\RAIDTEST\\stripe_pool.dat", letter);
        if (DeleteFileW(pool_path)) {
            char path_a[MAX_DRIVE_PATH] = {0};
            wcstombs(path_a, pool_path, MAX_DRIVE_PATH - 1);
            LOG_INFO("Deleted pool file: %s", path_a);
        }
        wchar_t dir_path[MAX_DRIVE_PATH];
        StringCchPrintfW(dir_path, MAX_DRIVE_PATH, L"%c:\\RAIDTEST", letter);
        if (RemoveDirectoryW(dir_path)) {
            char dir_a[MAX_DRIVE_PATH] = {0};
            wcstombs(dir_a, dir_path, MAX_DRIVE_PATH - 1);
            LOG_INFO("Removed directory: %s", dir_a);
        }
        StringCchPrintfW(pool_path, MAX_DRIVE_PATH, L"%c:\\RAIDTEST1-0\\stripe_pool.dat", letter);
        if (DeleteFileW(pool_path)) {
            char path_a[MAX_DRIVE_PATH] = {0};
            wcstombs(path_a, pool_path, MAX_DRIVE_PATH - 1);
            LOG_INFO("Deleted legacy pool file: %s", path_a);
        }
        wchar_t bench_path[MAX_DRIVE_PATH];
        StringCchPrintfW(bench_path, MAX_DRIVE_PATH, L"%c:\\RAIDTEST_BENCH\\raid_bench.tmp", letter);
        if (DeleteFileW(bench_path)) {
            char path_a[MAX_DRIVE_PATH] = {0};
            wcstombs(path_a, bench_path, MAX_DRIVE_PATH - 1);
            LOG_INFO("Deleted bench file: %s", path_a);
        }
        StringCchPrintfW(bench_path, MAX_DRIVE_PATH, L"%c:\\RAIDTEST_BENCH", letter);
        if (RemoveDirectoryW(bench_path)) {
            char dir_a[MAX_DRIVE_PATH] = {0};
            wcstombs(dir_a, bench_path, MAX_DRIVE_PATH - 1);
            LOG_INFO("Removed bench directory: %s", dir_a);
        }
    }
}

void cleanup_bench_dirs(void) {
    for (wchar_t letter = L'A'; letter <= L'Z'; letter++) {
        wchar_t root[4] = { letter, L':', L'\\', 0 };
        if (GetDriveTypeW(root) != DRIVE_FIXED) continue;
        wchar_t bench_path[MAX_DRIVE_PATH];
        StringCchPrintfW(bench_path, MAX_DRIVE_PATH, L"%c:\\RAIDTEST_BENCH\\raid_bench.tmp", letter);
        if (DeleteFileW(bench_path)) {
            char path_a[MAX_DRIVE_PATH] = {0};
            wcstombs(path_a, bench_path, MAX_DRIVE_PATH - 1);
            LOG_INFO("Deleted bench file: %s", path_a);
        }
        StringCchPrintfW(bench_path, MAX_DRIVE_PATH, L"%c:\\RAIDTEST_BENCH", letter);
        if (RemoveDirectoryW(bench_path)) {
            char dir_a[MAX_DRIVE_PATH] = {0};
            wcstombs(dir_a, bench_path, MAX_DRIVE_PATH - 1);
            LOG_INFO("Removed bench directory: %s", dir_a);
        }
    }
}

void cleanup_all(void) {
    APP_STATE* state = &g_state;
    cleanup_session(state);

    cleanup_scan_all_drives();

    wchar_t config_path[MAX_DRIVE_PATH];
    config_get_path(config_path, MAX_DRIVE_PATH);
    wchar_t* last_slash = wcsrchr(config_path, L'\\');
    if (last_slash) *last_slash = L'\0';
    if (RemoveDirectoryW(config_path)) {
        char dir_a[MAX_DRIVE_PATH] = {0};
        wcstombs(dir_a, config_path, MAX_DRIVE_PATH - 1);
        LOG_INFO("Removed config directory: %s", dir_a);
    }

    cleanup_disks(state);
    LOG_OK("Cleanup complete");
}
