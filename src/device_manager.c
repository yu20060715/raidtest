#include "device_manager.h"
#include "disk_scanner.h"
#include "bench_io.h"
#include "pool_io.h"
#include "event_bus.h"

static DISK_INFO** g_disks = NULL;
static uint32_t g_count = 0;

bool device_refresh(void) {
    if (g_disks) {
        disk_scan_free(g_disks, g_count);
        g_disks = NULL;
        g_count = 0;
    }
    if (!disk_scan_all(&g_disks, &g_count)) {
        LOG_WARN("No physical disks detected");
        return false;
    }
    for (uint32_t i = 0; i < g_count; i++) {
        char sn[64] = {0};
        if (g_disks[i]->serial_number[0])
            strncpy_s(sn, sizeof(sn), g_disks[i]->serial_number, _TRUNCATE);
        event_bus_publish(EVENT_DISK_FOUND, sn);
    }
    return true;
}

bool device_bench_all_selected(uint32_t size_mb) {
    for (uint32_t i = 0; i < g_count; i++) {
        if (!g_disks[i]->selected || !g_disks[i]->drive_letter[0]) continue;
        LOG_INFO("  Testing %ls ...", g_disks[i]->drive_letter);
        bench_single_disk(g_disks[i], size_mb);
    }
    return true;
}

uint32_t device_get_count(void) { return g_count; }

DISK_INFO* device_get(uint32_t index) {
    if (index >= g_count) return NULL;
    return g_disks[index];
}

bool device_select(uint32_t* indices, uint32_t count) {
    return disk_select(g_disks, g_count, indices, count);
}

bool device_is_selected(uint32_t index) {
    if (index >= g_count) return false;
    return g_disks[index]->selected;
}

bool device_map_drive(uint32_t disk_id, const char* drive_letter) {
    if (disk_id >= g_count) return false;
    return disk_map_drive(drive_letter, g_disks[disk_id]);
}

void device_print_list(void) {
    disk_print_list(g_disks, g_count);
}

void device_cleanup(void) {
    if (g_disks) {
        disk_scan_free(g_disks, g_count);
        g_disks = NULL;
        g_count = 0;
    }
}
