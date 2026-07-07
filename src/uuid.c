#include "uuid.h"
#include <stdio.h>
#include <windows.h>

void uuid_generate(VOLUME_UUID* uuid) {
    LARGE_INTEGER pc;
    QueryPerformanceCounter(&pc);
    uuid->high = ((uint64_t)GetCurrentProcessId() << 32) | ((uint32_t)(GetTickCount64() & 0xFFFFFFFF));
    uuid->low = (uint64_t)pc.QuadPart;
    if (uuid->high == 0) uuid->high = 1;
    if (uuid->low == 0) uuid->low = 1;
}

void uuid_to_str(const VOLUME_UUID* uuid, char* out, size_t out_size) {
    snprintf(out, out_size, "%016llX-%016llX",
             (unsigned long long)uuid->high,
             (unsigned long long)uuid->low);
}

bool uuid_is_zero(const VOLUME_UUID* uuid) {
    return uuid->high == 0 && uuid->low == 0;
}

bool uuid_eq(const VOLUME_UUID* a, const VOLUME_UUID* b) {
    return a->high == b->high && a->low == b->low;
}
