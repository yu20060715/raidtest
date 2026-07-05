#pragma once
#include "common.h"

#define PROFILER_SAMPLE_WINDOW 64

typedef struct {
    double start_time;
    bool   active;
} IO_SAMPLE;

typedef struct {
    /* Cumulative counters */
    uint64_t read_ops;
    uint64_t write_ops;
    uint64_t read_bytes;
    uint64_t write_bytes;

    /* Latency samples (sliding window) */
    double   read_latency_sum_ms;
    double   write_latency_sum_ms;
    uint32_t read_latency_count;
    uint32_t write_latency_count;
    double   read_latency_max_ms;
    double   write_latency_max_ms;
    double   read_latency_min_ms;
    double   write_latency_min_ms;

    /* Queue depth */
    volatile LONG pending_ios;
    uint32_t max_queue_depth;
    uint32_t current_queue_depth;

    /* Timing */
    LARGE_INTEGER freq;

    /* Per-thread samples (up to 8 concurrent) */
    IO_SAMPLE samples[8];

    /* Averaged values for GUI */
    double avg_read_mbs;
    double avg_write_mbs;
    double avg_read_latency_ms;
    double avg_write_latency_ms;
    double avg_iops_read;
    double avg_iops_write;
    uint32_t avg_queue_depth;

    /* Timestamp of last calc */
    uint64_t last_read_bytes;
    uint64_t last_write_bytes;
    double   last_sample_time;
} IO_PROFILER;

void profiler_init(void);
void profiler_read_begin(uint32_t* slot_out);
void profiler_read_end(uint32_t slot, uint32_t bytes);
void profiler_write_begin(uint32_t* slot_out);
void profiler_write_end(uint32_t slot, uint32_t bytes);
void profiler_update_rates(uint64_t total_read_bytes, uint64_t total_write_bytes);
IO_PROFILER* profiler_get(void);
