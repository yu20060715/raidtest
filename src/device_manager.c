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

bool device_bench(uint32_t disk_id, uint32_t size_mb) {
    if (disk_id >= g_count || !g_disks[disk_id]->drive_letter[0]) return false;
    bench_single_disk(g_disks[disk_id], size_mb);
    event_bus_publish(EVENT_DISK_BENCHED, g_disks[disk_id]->serial_number);
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

DISK_INFO* device_find_serial(const char* serial) {
    if (!serial || !serial[0]) return NULL;
    for (uint32_t i = 0; i < g_count; i++) {
        if (strcmp(g_disks[i]->serial_number, serial) == 0)
            return g_disks[i];
    }
    return NULL;
}

DISK_INFO* device_find_drive(wchar_t drive_letter) {
    for (uint32_t i = 0; i < g_count; i++) {
        if (g_disks[i]->drive_letter[0] == drive_letter)
            return g_disks[i];
    }
    return NULL;
}

DISK_INFO** device_get_all(void) { return g_disks; }

bool device_select(uint32_t* indices, uint32_t count) {
    return disk_select(g_disks, g_count, indices, count);
}

bool device_is_selected(uint32_t index) {
    if (index >= g_count) return false;
    return g_disks[index]->selected;
}

uint32_t device_selected_count(void) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < g_count; i++)
        if (g_disks[i]->selected) n++;
    return n;
}

bool device_map_drive(uint32_t disk_id, const char* drive_letter) {
    if (disk_id >= g_count) return false;
    return disk_map_drive(drive_letter, g_disks[disk_id]);
}

bool device_has_drive_letter(uint32_t disk_id) {
    if (disk_id >= g_count) return false;
    return g_disks[disk_id]->drive_letter[0] != 0;
}

uint64_t device_capacity(uint32_t disk_id) {
    if (disk_id >= g_count) return 0;
    DISK_INFO* d = g_disks[disk_id];
    return d->pool_bytes > 0 ? d->pool_bytes : d->total_bytes;
}

uint32_t device_speed(uint32_t disk_id) {
    if (disk_id >= g_count) return 0;
    DISK_INFO* d = g_disks[disk_id];
    return d->benchmarked ? d->bench_write_mbs : d->write_speed_mbs;
}

bool device_health(uint32_t disk_id) {
    if (disk_id >= g_count) return false;
    LONG h = InterlockedCompareExchange(&g_disks[disk_id]->healthy, 1, 1);
    return h && !g_disks[disk_id]->faulty;
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
