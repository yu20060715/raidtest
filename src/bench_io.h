#pragma once
#include "common.h"

#define BENCH_PASSES        10

bool bench_single_disk(DISK_INFO* disk, uint32_t size_mb);
bool bench_volume(STRIPE_VOLUME* vol, uint32_t size_mb, uint32_t block_kb);