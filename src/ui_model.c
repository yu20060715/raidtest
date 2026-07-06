#include "ui_model.h"
#include "cmd_handler.h"

void ui_get_disk_summary(UI_DISK_SUMMARY* out) {
    memset(out, 0, sizeof(*out));
    if (!g_state.disk.physical_disks) return;
    out->count = g_state.disk.physical_count;
    uint64_t total_mb = 0;
    uint32_t sel = 0, speed_sum = 0, speed_n = 0;
    for (uint32_t i = 0; i < g_state.disk.physical_count; i++) {
        DISK_INFO* d = g_state.disk.physical_disks[i];
        if (d->selected) sel++;
        uint64_t mb = (d->pool_bytes > 0 ? d->pool_bytes : d->total_bytes) / (1024 * 1024);
        total_mb += mb;
        uint32_t spd = d->benchmarked ? d->bench_write_mbs : d->write_speed_mbs;
        if (spd > 0) { speed_sum += spd; speed_n++; }
    }
    out->selected_count = sel;
    out->total_capacity_mb = total_mb;
    out->write_speed_avg_mbs = speed_n > 0 ? speed_sum / speed_n : 0;
}

void ui_get_volume_info(UI_VOLUME_INFO* out) {
    memset(out, 0, sizeof(*out));
    if (!g_state.vol.volume_valid) return;
    STRIPE_VOLUME* vol = &g_state.vol.volume;
    out->exists = true;
    out->raid_level = vol->raid_level;
    out->disk_count = vol->disk_count;
    out->virtual_capacity_bytes = vol->virtual_total_bytes;
    out->generation = vol->generation;
    uuid_to_str(&vol->volume_uuid, out->uuid_str, sizeof(out->uuid_str));
    out->mounted = g_state.rt.mounted;
    out->cache_enabled = vol->cache_enabled;
    out->cache_mb = g_state.cache.cache_mb;
    if (vol->cache.block_count > 0) {
        uint32_t dirty = 0;
        EnterCriticalSection(&vol->cache.lock);
        for (uint32_t b = 0; b < vol->cache.block_count; b++)
            if (vol->cache.dirty_map[b / 8] & (1 << (b % 8))) dirty++;
        LeaveCriticalSection(&vol->cache.lock);
        out->cache_dirty_pct = (double)dirty / vol->cache.block_count * 100.0;
    }
    out->bytes_written = vol->bytes_written;
    out->bytes_read = vol->bytes_read;
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now); QueryPerformanceFrequency(&freq);
    out->uptime_seconds = (double)(now.QuadPart - vol->start_time.QuadPart) / freq.QuadPart;
}

void ui_get_health_summary(UI_HEALTH_SUMMARY* out) {
    memset(out, 0, sizeof(*out));
    if (!g_state.vol.volume_valid) return;
    STRIPE_VOLUME* vol = &g_state.vol.volume;
    out->total_count = vol->disk_count;
    out->healthy_count = (uint32_t)vol->healthy_count;
    out->degraded = (out->healthy_count < out->total_count);
}

uint32_t ui_get_state(void) {
    return (uint32_t)g_state.rt.state;
}

const char* ui_get_state_str(void) {
    return raid_state_str(g_state.rt.state);
}
