#include "profiler.h"
#include <math.h>

static IO_PROFILER g_profiler = {0};
static bool g_profiler_inited = false;

IO_PROFILER* profiler_get(void) {
    return g_profiler_inited ? &g_profiler : NULL;
}

void profiler_init(void) {
    if (g_profiler_inited) return;
    memset(&g_profiler, 0, sizeof(g_profiler));
    QueryPerformanceFrequency(&g_profiler.freq);
    g_profiler.read_latency_min_ms = 1e9;
    g_profiler.write_latency_min_ms = 1e9;
    g_profiler_inited = true;
}

static double now_ms(void) {
    LARGE_INTEGER cnt;
    QueryPerformanceCounter(&cnt);
    return (double)cnt.QuadPart * 1000.0 / (double)g_profiler.freq.QuadPart;
}

static int find_free_slot(void) {
    for (int i = 0; i < 8; i++)
        if (InterlockedCompareExchange(&g_profiler.samples[i].active, 1, 0) == 0) return i;
    return -1;
}

void profiler_read_begin(uint32_t* slot_out) {
    if (!slot_out) return;
    InterlockedIncrement(&g_profiler.pending_ios);
    int slot = find_free_slot();
    if (slot < 0) { *slot_out = 255; return; }
    g_profiler.samples[slot].start_time = now_ms();
    g_profiler.samples[slot].active = true;
    *slot_out = (uint32_t)slot;
}

void profiler_read_end(uint32_t slot, uint32_t bytes) {
    if (slot >= 8 || !g_profiler.samples[slot].active) return;
    double elapsed = now_ms() - g_profiler.samples[slot].start_time;
    g_profiler.samples[slot].active = false;
    InterlockedDecrement(&g_profiler.pending_ios);

    g_profiler.read_ops++;
    g_profiler.read_bytes += bytes;
    g_profiler.read_latency_sum_ms += elapsed;
    g_profiler.read_latency_count++;
    if (elapsed > g_profiler.read_latency_max_ms) g_profiler.read_latency_max_ms = elapsed;
    if (elapsed < g_profiler.read_latency_min_ms) g_profiler.read_latency_min_ms = elapsed;

    if (g_profiler.read_latency_count > 1000000) {
        g_profiler.read_latency_sum_ms /= 2;
        g_profiler.read_latency_count /= 2;
    }
}

void profiler_write_begin(uint32_t* slot_out) {
    if (!slot_out) return;
    InterlockedIncrement(&g_profiler.pending_ios);
    uint32_t qd = (uint32_t)g_profiler.pending_ios;
    if (qd > g_profiler.max_queue_depth) g_profiler.max_queue_depth = qd;
    g_profiler.current_queue_depth = qd;

    int slot = find_free_slot();
    if (slot < 0) { *slot_out = 255; return; }
    g_profiler.samples[slot].start_time = now_ms();
    g_profiler.samples[slot].active = true;
    *slot_out = (uint32_t)slot;
}

void profiler_write_end(uint32_t slot, uint32_t bytes) {
    if (slot >= 8 || !g_profiler.samples[slot].active) return;
    double elapsed = now_ms() - g_profiler.samples[slot].start_time;
    g_profiler.samples[slot].active = false;
    InterlockedDecrement(&g_profiler.pending_ios);

    g_profiler.write_ops++;
    g_profiler.write_bytes += bytes;
    g_profiler.write_latency_sum_ms += elapsed;
    g_profiler.write_latency_count++;
    if (elapsed > g_profiler.write_latency_max_ms) g_profiler.write_latency_max_ms = elapsed;
    if (elapsed < g_profiler.write_latency_min_ms) g_profiler.write_latency_min_ms = elapsed;

    if (g_profiler.write_latency_count > 1000000) {
        g_profiler.write_latency_sum_ms /= 2;
        g_profiler.write_latency_count /= 2;
    }
}

void profiler_update_rates(uint64_t total_read_bytes, uint64_t total_write_bytes) {
    double now_t = now_ms();
    double dt = (now_t - g_profiler.last_sample_time) / 1000.0;
    if (dt < 0.5) return;

    uint64_t dr = (total_read_bytes > g_profiler.last_read_bytes)
        ? total_read_bytes - g_profiler.last_read_bytes : 0;
    uint64_t dw = (total_write_bytes > g_profiler.last_write_bytes)
        ? total_write_bytes - g_profiler.last_write_bytes : 0;

    if (dt > 0) {
        g_profiler.avg_read_mbs = (double)dr / (1024.0 * 1024.0) / dt;
        g_profiler.avg_write_mbs = (double)dw / (1024.0 * 1024.0) / dt;
    }

    if (g_profiler.read_latency_count > 0)
        g_profiler.avg_read_latency_ms = g_profiler.read_latency_sum_ms / g_profiler.read_latency_count;
    if (g_profiler.write_latency_count > 0)
        g_profiler.avg_write_latency_ms = g_profiler.write_latency_sum_ms / g_profiler.write_latency_count;

    if (dt > 0) {
        uint64_t delta_read_ops = g_profiler.read_ops > g_profiler.last_read_ops
            ? g_profiler.read_ops - g_profiler.last_read_ops : 0;
        uint64_t delta_write_ops = g_profiler.write_ops > g_profiler.last_write_ops
            ? g_profiler.write_ops - g_profiler.last_write_ops : 0;
        g_profiler.avg_iops_read = (double)delta_read_ops / dt;
        g_profiler.avg_iops_write = (double)delta_write_ops / dt;
    }

    g_profiler.avg_queue_depth = g_profiler.current_queue_depth;

    g_profiler.last_read_bytes = total_read_bytes;
    g_profiler.last_write_bytes = total_write_bytes;
    g_profiler.last_read_ops = g_profiler.read_ops;
    g_profiler.last_write_ops = g_profiler.write_ops;
    g_profiler.last_sample_time = now_t;
}
