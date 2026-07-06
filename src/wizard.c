#include "wizard.h"
#include "disk_scanner.h"
#include "bench_io.h"
#include "fuse_bridge.h"
#include "config.h"
#include "volume_manager.h"

static int prompt_int(const char* question, int default_val, int min_val, int max_val) {
    printf("\n  %s [%d]: ", question, default_val);
    char input[64];
    if (!fgets(input, sizeof(input), stdin)) return default_val;
    uint32_t uv = 0;
    int val = safe_atou32(input, &uv) ? (int)uv : default_val;
    if (val < min_val) val = min_val;
    if (val > max_val) val = max_val;
    return val;
}

static char prompt_char(const char* question, char default_char) {
    printf("\n  %s [%c]: ", question, default_char);
    char input[64];
    if (!fgets(input, sizeof(input), stdin)) return default_char;
    if (input[0] >= 'A' && input[0] <= 'Z') return input[0];
    if (input[0] >= 'a' && input[0] <= 'z') return (char)(input[0] - 'a' + 'A');
    return default_char;
}

void wizard_run(APP_STATE* state) {
    if (!state) return;

    printf("\n");
    printf("  ==============================================\n");
    printf("    RAIDTEST Setup Wizard\n");
    printf("  ==============================================\n");

    // Step 1: Scan
    printf("\n  Step 1: Scanning physical disks...\n");
    if (state->disk.physical_disks) {
        disk_scan_free(state->disk.physical_disks, state->disk.physical_count);
        state->disk.physical_disks = NULL;
        state->disk.physical_count = 0;
    }
    if (!disk_scan_all(&state->disk.physical_disks, &state->disk.physical_count)) {
        LOG_ERROR("No disks found"); return;
    }
    disk_print_list(state->disk.physical_disks, state->disk.physical_count);

    // Step 2: Select disks
    printf("\n  Step 2: Select disks by ID (space-separated, e.g. '0 3 4')");
    printf("\n  Enter disk IDs: ");
    char input[128];
    if (!fgets(input, sizeof(input), stdin)) return;

    uint32_t indices[MAX_DISKS], sel_count = 0;
    char* ctx = NULL;
    char* tok = strtok_s(input, " \t\r\n", &ctx);
    while (tok && sel_count < MAX_DISKS) {
        uint32_t id = 0;
        if (!safe_atou32(tok, &id)) id = UINT32_MAX;
        if (id < state->disk.physical_count) indices[sel_count++] = id;
        tok = strtok_s(NULL, " \t\r\n", &ctx);
    }
    if (sel_count < MIN_DISKS) { LOG_ERROR("Need %d-%d disks", MIN_DISKS, MAX_DISKS); return; }

    disk_select(state->disk.physical_disks, state->disk.physical_count, indices, sel_count);

    // Step 3: Map drives + bench
    printf("\n  Step 3: Assigning drive letters and benchmarking...\n");
    char drive_letters[] = {'F', 'G', 'H', 'I'};
    for (uint32_t i = 0; i < sel_count; i++) {
        char dl = drive_letters[i % 4];
        printf("  Disk %u -> %c: ? [%c]: ", indices[i], dl, dl);
        char ans[64];
        if (fgets(ans, sizeof(ans), stdin) && ans[0] >= 'A' && ans[0] <= 'Z') dl = ans[0];
        disk_map_drive((char[]){dl, 0}, state->disk.physical_disks[indices[i]]);
    }

    printf("\n  Running benchmarks (512 MB test)...\n");
    for (uint32_t i = 0; i < sel_count; i++) {
        uint32_t di = indices[i];
        if (state->disk.physical_disks[di]->drive_letter[0]) {
            printf("  Testing disk %u...\n", di);
            bench_single_disk(state->disk.physical_disks[di], 512);
        }
    }
    disk_print_list(state->disk.physical_disks, state->disk.physical_count);

    // Step 4: Pool size
    printf("\n  Step 4: Pool size per disk (MB)");
    uint64_t total_pool = 0;
    for (uint32_t i = 0; i < sel_count; i++) {
        uint32_t di = indices[i];
        uint64_t max_mb = state->disk.physical_disks[di]->total_bytes / (1024 * 1024);
        uint64_t free_space = max_mb;  // rough estimate
        uint64_t default_pool = min_u64(51200, free_space / 2);
        int pool_mb = prompt_int("  Pool size for disk", (int)default_pool, 1024, (int)free_space);
        state->disk.pool_sizes_mb[i] = (uint64_t)pool_mb;
        total_pool += (uint64_t)pool_mb;
    }

    // Step 5: Create pool files
    printf("\n  Step 5: Creating pool files...\n");
    state->disk.disk_count = 0;
    for (uint32_t i = 0; i < sel_count; i++) {
        uint32_t di = indices[i];
        if (!volume_create_pool_file(state->disk.physical_disks[di], state->disk.pool_sizes_mb[i])) continue;
        state->disk.disks[state->disk.disk_count++] = state->disk.physical_disks[di];
    }
    if (state->disk.disk_count < MIN_DISKS) { LOG_ERROR("Failed to create pool files"); return; }

    // Step 6: Create volume
    printf("\n  Step 6: Creating stripe volume...\n");
    if (!volume_create(&state->vol.volume, state->disk.disks, state->disk.disk_count)) {
        LOG_ERROR("Failed to create volume"); return;
    }
    state->vol.volume_valid = true;

    // Step 7: RAM cache
    printf("\n  Step 7: Configure RAM write-back cache");
    uint64_t total_gb = total_pool / 1024;
    if (total_gb == 0) total_gb = 1;
    uint32_t max_cache = (uint32_t)min_u64(total_gb * 1024, 4096);
    int cache_mb = prompt_int("  Cache size (MB)", CACHE_DEFAULT_MB, 256, (int)max_cache);
    LOG_INFO("Initializing %u MB RAM write-back cache...", cache_mb);
    if (volume_cache_enable(&state->vol.volume, (uint64_t)cache_mb * 1024ULL * 1024ULL,
                            &state->cache.cache_on, &state->cache.cache_mb, &state->cache.flush_thread)) {
        if (state->cache.flush_thread) LOG_OK("Background flush thread started (1s interval)");
    }

    // Step 8: Mount
    printf("\n  Step 8: Mount drive letter");
    char mount_letter = prompt_char("  Mount as", 'G');
    if (fuse_mount_volume(&state->vol.volume, mount_letter)) state->rt.mounted = true;

    // Step 9: Save config
    state->cfg.config.disk_count = 0;
    for (uint32_t i = 0; i < sel_count && state->cfg.config.disk_count < MAX_DISKS; i++) {
        uint32_t di = indices[i];
        state->cfg.config.disks[state->cfg.config.disk_count].disk_id = di;
        state->cfg.config.disks[state->cfg.config.disk_count].drive_letter = (char)state->disk.physical_disks[di]->drive_letter[0];
        state->cfg.config.disks[state->cfg.config.disk_count].pool_mb = state->disk.pool_sizes_mb[i];
        state->cfg.config.disk_count++;
    }
    state->cfg.config.cache_mb = state->cache.cache_mb;
    state->cfg.config.mount_letter = mount_letter;
    state->cfg.config.auto_bench = true;
    config_save(&state->cfg.config);

    printf("\n");
    printf("  ==============================================\n");
    printf("    Wizard complete! Volume mounted at %c:\n", mount_letter);
    printf("    Type 'exit' to unmount and quit.\n");
    printf("  ==============================================\n");
    printf("\n");
}