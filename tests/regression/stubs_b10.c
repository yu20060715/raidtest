/* Stubs for B10 regression test — only needed because ram_cache.o references these */
#include "../../src/common.h"
bool journal_begin(STRIPE_VOLUME* vol) { (void)vol; return true; }
bool journal_data(STRIPE_VOLUME* vol, uint64_t o, uint32_t l, const void* d) { (void)vol; (void)o; (void)l; (void)d; return true; }
bool journal_commit(STRIPE_VOLUME* vol) { (void)vol; return true; }
bool stripe_volume_map_lba(STRIPE_VOLUME* vol, uint64_t off, void* entries, uint32_t* cnt, uint32_t len) { (void)vol; (void)off; (void)entries; (void)cnt; (void)len; return true; }
