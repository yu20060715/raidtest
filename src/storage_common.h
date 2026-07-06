#pragma once
#include "common.h"

bool stripe_read_raw(DISK_INFO* disk, void* buffer, uint64_t offset, uint32_t length);
bool stripe_write_raw(DISK_INFO* disk, const void* buffer, uint64_t offset, uint32_t length);
