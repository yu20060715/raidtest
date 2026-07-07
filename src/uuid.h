#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#pragma pack(push, 1)
typedef struct {
    uint64_t high;
    uint64_t low;
} VOLUME_UUID;
#pragma pack(pop)

void uuid_generate(VOLUME_UUID* uuid);
void uuid_to_str(const VOLUME_UUID* uuid, char* out, size_t out_size);
bool uuid_is_zero(const VOLUME_UUID* uuid);
bool uuid_eq(const VOLUME_UUID* a, const VOLUME_UUID* b);
