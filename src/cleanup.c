#include "cleanup.h"
#include "disk_scanner.h"
#include "pool_io.h"
#include "fuse_bridge.h"
#include "config.h"
#include "stripe_engine.h"
#include "ram_cache.h"
#include "superblock.h"

void cleanup_volume_cache(STRIPE_VOLUME* vol) {
    if (!vol->cache_enabled) return;
    /* Signal flush thread to stop */
    InterlockedExchange(&vol->cache.running, 0);
    /* Wait for flush thread to finish its current cycle and exit.
       This eliminates the re-entrancy race where cleanup_volume_cache's
       cache_flush_all call would be skipped because the flush thread
       is already inside cache_flush_all. */
    if (vol->cache.flush_thread) {
        WaitForSingleObject(vol->cache.flush_thread, INFINITE);
        CloseHandle(vol->cache.flush_thread);
        vol->cache.flush_thread = NULL;
    }
    /* No re-entrancy race: flush thread is done, so cache_flush_all runs.
       Flush any remaining dirty data to disk. */
    cache_flush_all(&vol->cache, vol);
    /* Destroy cache resources — flush_thread handle is NULL so cache_destroy
       skips the WaitForSingleObject/CloseHandle internally. */
    cache_destroy(&vol->cache);
    vol->cache_enabled = false;
}

void cleanup_cache(APP_STATE* state) {
    if (!state->cache.cache_on) return;
    cleanup_volume_cache(&state->vol.volume);
    state->cache.flush_thread = NULL;
    state->cache.cache_on = false;
}

void cleanup_volume(APP_STATE* state) {
    if (state->rt.mounted) {
        fuse_unmount_volume(state->vol.volume.mount_point[0]);
        state->rt.mounted = false;
    }
    if (state->vol.volume_valid) {
        stripe_volume_destroy(&state->vol.volume);
        state->vol.volume_valid = false;
    }
}

void cleanup_pool_files(APP_STATE* state) {
    for (uint32_t i = 0; i < state->disk.disk_count; i++) {
        pool_file_close(state->disk.disks[i]);
        pool_file_delete(state->disk.disks[i]);
        pool_dir_delete(state->disk.disks[i]);
    }
    state->disk.disk_count = 0;
}

void cleanup_disks(APP_STATE* state) {
    if (state->disk.physical_disks) {
        disk_scan_free(state->disk.physical_disks, state->disk.physical_count);
        state->disk.physical_disks = NULL;
        state->disk.physical_count = 0;
    }
}

void cleanup_session(APP_STATE* state) {
    cleanup_cache(state);
    cleanup_volume(state);
    // Note: pool files are NOT deleted here ??keeps them for "load" persistence.
    // Call cleanup_pool_session() separately to delete pool files + superblock.
}

void cleanup_pool_session(APP_STATE* state) {
    // Save count before cleanup_pool_files resets it
    uint32_t saved_count = state->disk.disk_count;
    cleanup_pool_files(state);
    for (uint32_t i = 0; i < saved_count; i++) {
        wchar_t sb_path[MAX_DRIVE_PATH], dir_path[MAX_DRIVE_PATH];
        wchar_t root[4] = { state->disk.disks[i]->drive_letter[0], L':', L'\\', 0 };
        StringCchPrintfW(sb_path, MAX_DRIVE_PATH, L"%ls%s\\%ls", root, CONFIG_DIR, SUPERBLOCK_FILENAME);
        StringCchPrintfW(dir_path, MAX_DRIVE_PATH, L"%ls%s", root, CONFIG_DIR);
        DeleteFileW(sb_path);
        RemoveDirectoryW(dir_path);
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

    cleanup_pool_session(state);

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
