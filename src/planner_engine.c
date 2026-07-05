#include "planner_engine.h"

void planner_calculate(PLANNER_DISK* disks, uint32_t count, PLANNER_RESULT* out) {
    memset(out, 0, sizeof(*out));
    if (!disks || count == 0) return;

    uint32_t sel = 0;
    uint64_t total_mb = 0, min_mb = UINT64_MAX;
    uint64_t capacities[MAX_DISKS];
    uint32_t cap_count = 0;

    for (uint32_t i = 0; i < count; i++) {
        if (!disks[i].selected) continue;
        sel++;
        uint64_t mb = disks[i].capacity_bytes / (1024 * 1024);
        total_mb += mb;
        if (mb < min_mb) min_mb = mb;
        if (cap_count < MAX_DISKS)
            capacities[cap_count++] = mb;
    }

    out->selected_count = sel;
    out->total_raw_mb = total_mb;

    if (sel < 2) return;

    out->disk_count = sel;
    out->raid0_capacity_mb = total_mb;
    out->efficiency_raid0 = 100.0;

    out->raid1_capacity_mb = min_mb;
    out->efficiency_raid1 = (double)min_mb / (double)total_mb * 100.0;

    if (sel >= 4) {
        for (uint32_t i = 0; i < cap_count; i++)
            for (uint32_t j = i + 1; j < cap_count; j++)
                if (capacities[j] > capacities[i]) {
                    uint64_t t = capacities[i]; capacities[i] = capacities[j]; capacities[j] = t;
                }
        uint64_t ra10 = 0;
        for (uint32_t p = 0; p + 1 < cap_count; p += 2)
            ra10 += (capacities[p] < capacities[p + 1]) ? capacities[p] : capacities[p + 1];
        out->raid10_capacity_mb = ra10;
    }
}

void planner_print(const PLANNER_RESULT* result, PLANNER_DISK* disks, uint32_t count) {
    printf("\n");
    printf("  ========== RAID CAPACITY PLANNER ==========\n");
    if (count == 0) {
        printf("  No physical disks scanned. Use 'scan' first.\n");
        printf("  ==============================================\n\n");
        return;
    }
    printf("  Available disks:\n");
    for (uint32_t i = 0; i < count; i++) {
        printf("    [%02u] size=%llu MB  speed=%u MB/s  %s%s\n",
               disks[i].disk_index,
               (unsigned long long)(disks[i].capacity_bytes / (1024 * 1024)),
               disks[i].speed_mbs,
               disks[i].serial[0] ? disks[i].serial : "",
               disks[i].selected ? " [SELECTED]" : "");
    }
    printf("\n");
    printf("  Selected: %u disks\n", result->selected_count);
    printf("  Total raw capacity: %llu MB (%.1f GB)\n",
           (unsigned long long)result->total_raw_mb,
           (double)result->total_raw_mb / 1024.0);
    if (result->selected_count >= 2) {
        printf("\n  RAID0 (stripe): ~%llu MB (%.1f GB)  (%.0f%% efficiency)\n",
               (unsigned long long)result->raid0_capacity_mb,
               (double)result->raid0_capacity_mb / 1024.0,
               result->efficiency_raid0);
        printf("  RAID1 (mirror): ~%llu MB (%.1f GB)  (%.0f%% efficiency)\n",
               (unsigned long long)result->raid1_capacity_mb,
               (double)result->raid1_capacity_mb / 1024.0,
               result->efficiency_raid1);
        if (result->selected_count >= 4 && result->raid10_capacity_mb > 0) {
            printf("  RAID10 (stripe of mirrors): ~%llu MB (%.1f GB)\n",
                   (unsigned long long)result->raid10_capacity_mb,
                   (double)result->raid10_capacity_mb / 1024.0);
        }
    } else {
        printf("  Need at least 2 selected disks for RAID.\n");
    }
    printf("\n");
    printf("  Sizing guidance:\n");
    printf("    - Pool files must fit on each disk's free space\n");
    printf("    - Min pool = 1 GB (1024 MB) per disk\n");
    printf("    - Max pool = available free space on disk\n");
    printf("  ==============================================\n\n");
}
