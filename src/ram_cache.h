#pragma once
#include "common.h"

bool cache_init(RAM_CACHE* cache, uint64_t size_bytes);
void cache_destroy(RAM_CACHE* cache);
bool cache_write(RAM_CACHE* cache, const void* data, uint64_t offset, uint32_t length);
bool cache_read(RAM_CACHE* cache, void* data, uint64_t offset, uint32_t length);
void cache_invalidate(RAM_CACHE* cache, uint64_t offset, uint32_t length);
void cache_flush_all(RAM_CACHE* cache, STRIPE_VOLUME* vol);
unsigned int __stdcall cache_flush_thread(void* arg);