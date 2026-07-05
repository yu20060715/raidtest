#pragma once
#include "common.h"
#include "stripe_engine.h"

#define JOURNAL_MAGIC     0x4A4F5552
#define JOURNAL_VERSION   2
#define JOURNAL_FILENAME  L"journal.dat"

typedef enum {
    JT_BEGIN  = 1,
    JT_DATA   = 2,
    JT_COMMIT = 3,
} JOURNAL_TYPE;

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t entry_type;
    uint64_t generation;
    uint64_t timestamp;
    uint64_t virtual_offset;
    uint32_t length;
    uint32_t data_crc;
    uint32_t checksum;
} JOURNAL_ENTRY;
#pragma pack(pop)

bool journal_begin(STRIPE_VOLUME* vol);
bool journal_data(STRIPE_VOLUME* vol, uint64_t virtual_offset, uint32_t length, const void* data);
bool journal_commit(STRIPE_VOLUME* vol);
bool journal_recover_all(STRIPE_VOLUME* vol);
